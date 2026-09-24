#!/usr/bin/env python3
"""Validate and compile a fixed ROS boundary into ordinary rclc/rcl setup."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import NoReturn


IDENTIFIER = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
LOWER_IDENTIFIER = re.compile(r"^[a-z][a-z0-9_]*$")
MESSAGE_TYPE = re.compile(r"^([A-Za-z][A-Za-z0-9_]*)/msg/([A-Za-z][A-Za-z0-9_]*)$")
ROOT_KEYS = {"schema_version", "name", "domain_id", "node", "rmw", "message_profile", "graph", "endpoints"}
NODE_KEYS = {"name", "namespace"}
GRAPH_KEYS = {"outbound", "inbound", "api"}
ENDPOINT_KEYS = {"id", "kind", "topic", "type", "callback", "qos", "lifetime"}
QOS_KEYS = {"reliability", "durability", "history", "depth"}
SUPPORTED_BACKENDS = {"rmw_cyclonedds_c", "rmw_zenoh_pico"}


class DeploymentError(Exception):
    def __init__(self, code: str, message: str):
        super().__init__(message)
        self.code = code


def reject(code: str, message: str) -> NoReturn:
    raise DeploymentError(code, message)


def require_object(value: object, path: str) -> dict[str, object]:
    if not isinstance(value, dict):
        reject("E_SCHEMA", f"{path} must be an object")
    return value


def exact_keys(value: dict[str, object], allowed: set[str], required: set[str], path: str) -> None:
    unknown = sorted(set(value) - allowed)
    missing = sorted(required - set(value))
    if unknown:
        reject("E_SCHEMA", f"{path} contains unknown field {unknown[0]!r}")
    if missing:
        reject("E_SCHEMA", f"{path} is missing required field {missing[0]!r}")


def require_string(value: object, path: str, pattern: re.Pattern[str] | None = None) -> str:
    if not isinstance(value, str) or (pattern and not pattern.fullmatch(value)):
        reject("E_SCHEMA", f"{path} has an invalid string value")
    return value


def load_json(path: Path) -> dict[str, object]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        reject("E_INPUT", f"cannot read {path}: {error}")
    return require_object(value, str(path))


def validate_deployment(value: dict[str, object]) -> dict[str, object]:
    exact_keys(value, ROOT_KEYS, ROOT_KEYS, "deployment")
    if value["schema_version"] != 1:
        reject("E_SCHEMA_VERSION", "schema_version must be 1")
    require_string(value["name"], "name", LOWER_IDENTIFIER)
    domain = value["domain_id"]
    if isinstance(domain, bool) or not isinstance(domain, int) or not 0 <= domain <= 232:
        reject("E_SCHEMA", "domain_id must be an integer from 0 through 232")
    if value["message_profile"] != "fixed_size":
        reject("E_MESSAGE_PROFILE_UNSUPPORTED", "only fixed_size messages are accepted")

    node = require_object(value["node"], "node")
    exact_keys(node, NODE_KEYS, NODE_KEYS, "node")
    require_string(node["name"], "node.name", IDENTIFIER)
    namespace = require_string(node["namespace"], "node.namespace")
    if namespace and (not namespace.startswith("/") or namespace.endswith("/")):
        reject("E_SCHEMA", "node.namespace must be empty or an absolute namespace without a trailing slash")

    rmw = value["rmw"]
    if not isinstance(rmw, list) or not rmw or any(not isinstance(item, str) for item in rmw):
        reject("E_SCHEMA", "rmw must be a non-empty string array")
    if len(set(rmw)) != len(rmw):
        reject("E_SCHEMA", "rmw entries must be unique")
    unknown_backends = sorted(set(rmw) - SUPPORTED_BACKENDS)
    if unknown_backends:
        reject("E_BACKEND_UNKNOWN", f"unknown backend {unknown_backends[0]}")

    graph = require_object(value["graph"], "graph")
    exact_keys(graph, GRAPH_KEYS, GRAPH_KEYS, "graph")
    if not isinstance(graph["outbound"], bool) or not isinstance(graph["inbound"], bool):
        reject("E_SCHEMA", "graph outbound/inbound requirements must be boolean")
    if not isinstance(graph["api"], list) or any(not isinstance(item, str) for item in graph["api"]):
        reject("E_SCHEMA", "graph.api must be a string array")
    if len(set(graph["api"])) != len(graph["api"]):
        reject("E_SCHEMA", "graph.api entries must be unique")

    endpoints = value["endpoints"]
    if not isinstance(endpoints, list):
        reject("E_SCHEMA", "endpoints must be an array")
    ids: set[str] = set()
    for index, raw_endpoint in enumerate(endpoints):
        path = f"endpoints[{index}]"
        endpoint = require_object(raw_endpoint, path)
        exact_keys(endpoint, ENDPOINT_KEYS, ENDPOINT_KEYS - {"callback"}, path)
        endpoint_id = require_string(endpoint["id"], f"{path}.id", LOWER_IDENTIFIER)
        if endpoint_id in ids:
            reject("E_DUPLICATE_ENDPOINT", f"duplicate endpoint id {endpoint_id!r}")
        ids.add(endpoint_id)
        if endpoint["kind"] not in ("publisher", "subscription"):
            reject("E_SCHEMA", f"{path}.kind must be publisher or subscription")
        topic = require_string(endpoint["topic"], f"{path}.topic")
        if not topic or topic.endswith("/") or "//" in topic:
            reject("E_SCHEMA", f"{path}.topic is invalid")
        require_string(endpoint["type"], f"{path}.type", MESSAGE_TYPE)
        if endpoint["lifetime"] != "fixed_startup":
            reject("E_LIFETIME_UNSUPPORTED", f"{path} must use fixed_startup lifetime")
        if endpoint["kind"] == "subscription":
            require_string(endpoint.get("callback"), f"{path}.callback", IDENTIFIER)
        elif "callback" in endpoint:
            reject("E_SCHEMA", f"{path}.callback is only valid for a subscription")
        qos = require_object(endpoint["qos"], f"{path}.qos")
        exact_keys(qos, QOS_KEYS, QOS_KEYS, f"{path}.qos")
        if qos["reliability"] not in ("best_effort", "reliable"):
            reject("E_SCHEMA", f"{path}.qos.reliability is invalid")
        if qos["durability"] not in ("volatile", "transient_local"):
            reject("E_SCHEMA", f"{path}.qos.durability is invalid")
        if qos["history"] != "keep_last":
            reject("E_SCHEMA", f"{path}.qos.history must be keep_last")
        depth = qos["depth"]
        if isinstance(depth, bool) or not isinstance(depth, int) or depth < 1:
            reject("E_SCHEMA", f"{path}.qos.depth must be a positive integer")
    return value


def validate_capabilities(deployment: dict[str, object], capability: dict[str, object], backend: str) -> None:
    if backend not in deployment["rmw"]:
        reject("E_BACKEND_NOT_SELECTED", f"backend {backend} is not allowed by this deployment")
    if capability.get("schema_version") != 1 or capability.get("backend") != backend:
        reject("E_CAPABILITY_SCHEMA", f"invalid capability document for {backend}")
    if deployment["message_profile"] not in capability.get("message_profiles", []):
        reject("E_MESSAGE_PROFILE_UNSUPPORTED", f"backend {backend} does not support {deployment['message_profile']}")
    qos_cap = require_object(capability.get("qos"), f"capability {backend}.qos")
    depth_cap = require_object(qos_cap.get("depth"), f"capability {backend}.qos.depth")
    minimum_depth = depth_cap.get("minimum")
    maximum_depth = depth_cap.get("maximum")
    if (
        isinstance(minimum_depth, bool)
        or not isinstance(minimum_depth, int)
        or isinstance(maximum_depth, bool)
        or not isinstance(maximum_depth, int)
        or minimum_depth < 1
        or maximum_depth < minimum_depth
    ):
        reject("E_CAPABILITY_SCHEMA", f"invalid QoS depth range for {backend}")
    for endpoint in deployment["endpoints"]:
        qos = endpoint["qos"]
        for policy in ("reliability", "durability", "history"):
            if qos[policy] not in qos_cap.get(policy, []):
                reject(
                    "E_QOS_UNSUPPORTED",
                    f"endpoint {endpoint['topic']} requests {policy}={qos[policy]}; backend {backend} does not support it",
                )
        if not minimum_depth <= qos["depth"] <= maximum_depth:
            reject(
                "E_QOS_UNSUPPORTED",
                f"endpoint {endpoint['topic']} requests depth={qos['depth']}; backend {backend} accepts "
                f"{minimum_depth}..{maximum_depth}",
            )
    graph_cap = require_object(capability.get("graph"), f"capability {backend}.graph")
    graph = deployment["graph"]
    for direction in ("outbound", "inbound"):
        if graph[direction] and not graph_cap.get(direction):
            reject("E_GRAPH_UNSUPPORTED", f"deployment requires {direction} graph behavior; backend {backend} lacks it")
    missing_api = sorted(set(graph["api"]) - set(graph_cap.get("api", [])))
    if missing_api:
        reject("E_GRAPH_UNSUPPORTED", f"deployment requires graph API {missing_api[0]}; backend {backend} lacks it")


def split_type(type_name: str) -> tuple[str, str]:
    match = MESSAGE_TYPE.fullmatch(type_name)
    assert match
    return match.group(1), match.group(2)


def snake_case(name: str) -> str:
    return re.sub(r"(?<!^)(?=[A-Z])", "_", name).lower()


def qos_initializer(endpoint: dict[str, object]) -> list[str]:
    qos = endpoint["qos"]
    variable = f"qos_{endpoint['id']}"
    reliability = "RMW_QOS_POLICY_RELIABILITY_" + qos["reliability"].upper()
    durability = "RMW_QOS_POLICY_DURABILITY_" + qos["durability"].upper()
    history = "RMW_QOS_POLICY_HISTORY_" + qos["history"].upper()
    return [
        f"  rmw_qos_profile_t {variable} = rmw_qos_profile_default;",
        f"  {variable}.reliability = {reliability};",
        f"  {variable}.durability = {durability};",
        f"  {variable}.history = {history};",
        f"  {variable}.depth = {qos['depth']}U;",
    ]


def generate_header(deployment: dict[str, object]) -> str:
    includes = sorted({split_type(endpoint["type"]) for endpoint in deployment["endpoints"]})
    subscriptions = [item for item in deployment["endpoints"] if item["kind"] == "subscription"]
    lines = [
        "/* Generated by compile_deployment.py; do not edit. */",
        "#ifndef ROS2_ZEPHYR_GENERATED_ROS_INIT_H",
        "#define ROS2_ZEPHYR_GENERATED_ROS_INIT_H",
        "",
        "#include <stdbool.h>",
        "#include <rcl/rcl.h>",
        "#include <rclc/executor.h>",
        "#include <rclc/rclc.h>",
    ]
    lines.extend(f"#include <{package}/msg/{snake_case(message)}.h>" for package, message in includes)
    lines.extend(["", "typedef struct ros2z_deployment_context {"])
    lines.append("  rcl_node_t node;")
    for endpoint in deployment["endpoints"]:
        lines.append(f"  rcl_{endpoint['kind']}_t {endpoint['kind']}_{endpoint['id']};")
        if endpoint["kind"] == "subscription":
            package, message = split_type(endpoint["type"])
            lines.append(f"  {package}__msg__{message} subscription_{endpoint['id']}_message;")
    lines.append("  rclc_executor_t executor;")
    lines.append("  bool node_initialized;")
    for endpoint in deployment["endpoints"]:
        lines.append(f"  bool {endpoint['kind']}_{endpoint['id']}_initialized;")
    lines.append("  bool executor_initialized;")
    lines.extend([
        "} ros2z_deployment_context_t;",
        "",
        "rcl_ret_t ros2z_deployment_init(ros2z_deployment_context_t *context,",
        "                                   rclc_support_t *support,",
        "                                   rcl_allocator_t *allocator);",
        "rcl_ret_t ros2z_deployment_fini(ros2z_deployment_context_t *context);",
        "",
    ])
    for endpoint in subscriptions:
        lines.append(f"void {endpoint['callback']}(const void *message);")
    lines.extend(["", "#endif", ""])
    return "\n".join(lines)


def generate_source(deployment: dict[str, object]) -> str:
    subscriptions = [item for item in deployment["endpoints"] if item["kind"] == "subscription"]
    lines = [
        "/* Generated by compile_deployment.py; do not edit. */",
        '#include "generated_ros_init.h"',
        "",
        "#include <string.h>",
        "",
        "rcl_ret_t ros2z_deployment_fini(ros2z_deployment_context_t *context)",
        "{",
        "  rcl_ret_t result = RCL_RET_OK;",
        "  if (context == NULL) { return RCL_RET_INVALID_ARGUMENT; }",
        "  if (context->executor_initialized && rclc_executor_fini(&context->executor) != RCL_RET_OK) { result = RCL_RET_ERROR; }",
    ]
    for endpoint in reversed(deployment["endpoints"]):
        kind = endpoint["kind"]
        lines.append(
            f"  if (context->{kind}_{endpoint['id']}_initialized && "
            f"rcl_{kind}_fini(&context->{kind}_{endpoint['id']}, &context->node) != RCL_RET_OK) "
            "{ result = RCL_RET_ERROR; }"
        )
    lines.extend([
        "  if (context->node_initialized && rcl_node_fini(&context->node) != RCL_RET_OK) { result = RCL_RET_ERROR; }",
        "  return result;",
        "}",
        "",
        "rcl_ret_t ros2z_deployment_init(ros2z_deployment_context_t *context,",
        "                                   rclc_support_t *support,",
        "                                   rcl_allocator_t *allocator)",
        "{",
        "  if (context == NULL || support == NULL || allocator == NULL) { return RCL_RET_INVALID_ARGUMENT; }",
        "  memset(context, 0, sizeof(*context));",
        "  context->node = rcl_get_zero_initialized_node();",
        "  context->executor = rclc_executor_get_zero_initialized_executor();",
    ])
    for endpoint in deployment["endpoints"]:
        lines.append(f"  context->{endpoint['kind']}_{endpoint['id']} = rcl_get_zero_initialized_{endpoint['kind']}();")
    node = deployment["node"]
    lines.extend([
        f'  rcl_ret_t result = rclc_node_init_default(&context->node, "{node["name"]}", "{node["namespace"]}", support);',
        "  if (result != RCL_RET_OK) { return result; }",
        "  context->node_initialized = true;",
    ])
    for endpoint in deployment["endpoints"]:
        package, message = split_type(endpoint["type"])
        kind = endpoint["kind"]
        lines.extend(qos_initializer(endpoint))
        lines.extend([
            f"  result = rclc_{kind}_init(&context->{kind}_{endpoint['id']}, &context->node,",
            f"      ROSIDL_GET_MSG_TYPE_SUPPORT({package}, msg, {message}), \"{endpoint['topic']}\", &qos_{endpoint['id']});",
            "  if (result != RCL_RET_OK) { (void)ros2z_deployment_fini(context); return result; }",
            f"  context->{kind}_{endpoint['id']}_initialized = true;",
        ])
    if subscriptions:
        lines.extend([
            f"  result = rclc_executor_init(&context->executor, &support->context, {len(subscriptions)}U, allocator);",
            "  if (result != RCL_RET_OK) { (void)ros2z_deployment_fini(context); return result; }",
            "  context->executor_initialized = true;",
        ])
        for endpoint in subscriptions:
            lines.extend([
                f"  result = rclc_executor_add_subscription(&context->executor, &context->subscription_{endpoint['id']},",
                f"      &context->subscription_{endpoint['id']}_message, {endpoint['callback']}, ON_NEW_DATA);",
                "  if (result != RCL_RET_OK) { (void)ros2z_deployment_fini(context); return result; }",
            ])
        lines.extend([
            "  result = rclc_executor_prepare(&context->executor);",
            "  if (result != RCL_RET_OK) { (void)ros2z_deployment_fini(context); return result; }",
        ])
    lines.extend(["  return RCL_RET_OK;", "}", ""])
    return "\n".join(lines)


def generate_config(deployment: dict[str, object], backend: str) -> str:
    symbol = {
        "rmw_cyclonedds_c": "CONFIG_ROS2_ZEPHYR_RMW_CYCLONEDDS_C",
        "rmw_zenoh_pico": "CONFIG_ROS2_ZEPHYR_RMW_ZENOH_PICO",
    }.get(backend)
    if symbol is None:
        reject("E_BACKEND_UNKNOWN", f"unknown backend {backend}")
    lines = [
        "# Generated by compile_deployment.py; do not edit.",
        "CONFIG_ROS2_ZEPHYR=y",
        f"{symbol}=y",
        f"CONFIG_ROS2_ZEPHYR_DOMAIN_ID={deployment['domain_id']}",
    ]
    if backend == "rmw_zenoh_pico":
        lines.append("CONFIG_NET_TCP=y")
    lines.append("")
    return "\n".join(lines)


def generate_features(deployment: dict[str, object]) -> str:
    message_packages = sorted({split_type(endpoint["type"])[0] for endpoint in deployment["endpoints"]})
    return "\n".join([
        "# Generated by compile_deployment.py; do not edit.",
        "set(ROS2_ZEPHYR_RCLC_ENABLE_ACTIONS OFF)",
        f"set(ROS2_ZEPHYR_MESSAGE_PACKAGES \"{';'.join(message_packages)}\")",
        "",
    ])


def generate_report(deployment: dict[str, object], capability: dict[str, object], backend: str) -> str:
    namespace = deployment["node"]["namespace"]
    node_path = f"{namespace}/{deployment['node']['name']}" if namespace else deployment["node"]["name"]
    lines = [
        "# Deployment report", "", f"Deployment: `{deployment['name']}`", "", f"Backend: `{backend}`", "",
        f"Node: `{node_path}`", "", "## Endpoints", "",
        "| ID | Kind | Topic | Type | QoS |", "|---|---|---|---|---|",
    ]
    for endpoint in deployment["endpoints"]:
        qos = endpoint["qos"]
        lines.append(
            f"| {endpoint['id']} | {endpoint['kind']} | `{endpoint['topic']}` | `{endpoint['type']}` | "
            f"{qos['reliability']}, {qos['durability']}, {qos['history']}({qos['depth']}) |"
        )
    lines.extend([
        "", "## Accepted contract", "", "- Endpoint lifetime is fixed at startup.",
        f"- Message profile: `{deployment['message_profile']}`.",
        f"- Graph outbound/inbound required: `{deployment['graph']['outbound']}` / `{deployment['graph']['inbound']}`.",
        f"- ROS allocation after executor preparation promised by backend: `{not capability['runtime_allocation']['ros_after_executor_prepare']}`.",
        "",
    ])
    return "\n".join(lines)


def compile_deployment(deployment_path: Path, backend: str, capability_dir: Path, output_dir: Path) -> None:
    deployment = validate_deployment(load_json(deployment_path))
    capability = load_json(capability_dir / f"{backend}.json")
    validate_capabilities(deployment, capability, backend)
    output_dir.mkdir(parents=True, exist_ok=True)
    plan = {"schema_version": 1, "backend": backend, "deployment": deployment, "capability": capability}
    outputs = {
        "generated_ros_init.h": generate_header(deployment),
        "generated_ros_init.c": generate_source(deployment),
        "generated_ros2_zephyr.conf": generate_config(deployment, backend),
        "generated_features.cmake": generate_features(deployment),
        "deployment-plan.json": json.dumps(plan, indent=2, sort_keys=True) + "\n",
        "deployment-report.md": generate_report(deployment, capability, backend),
    }
    for name, content in outputs.items():
        (output_dir / name).write_text(content, encoding="utf-8")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("deployment", type=Path)
    parser.add_argument("--backend", required=True)
    parser.add_argument("--capabilities-dir", type=Path, default=Path(__file__).resolve().parents[1] / "capabilities")
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        compile_deployment(args.deployment, args.backend, args.capabilities_dir, args.output_dir)
    except DeploymentError as error:
        print(f"{error.code}: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
