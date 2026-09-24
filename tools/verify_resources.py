#!/usr/bin/env python3
"""Plan and verify resources for a compiled ROS 2 Zephyr deployment."""

from __future__ import annotations

import argparse
import dataclasses
import json
import re
import struct
import sys
from pathlib import Path
from typing import NoReturn


REMOTE_CONFIG_KEYS = {
    "participants": "CONFIG_ROS2_ZEPHYR_GRAPH_CACHE_MAX_PARTICIPANTS",
    "nodes": "CONFIG_ROS2_ZEPHYR_GRAPH_CACHE_MAX_NODES",
    "endpoints": "CONFIG_ROS2_ZEPHYR_GRAPH_CACHE_MAX_ENDPOINTS",
}
STATIC_BUFFER_KEYS = (
    "CONFIG_NET_PKT_RX_COUNT",
    "CONFIG_NET_PKT_TX_COUNT",
    "CONFIG_NET_BUF_RX_COUNT",
    "CONFIG_NET_BUF_TX_COUNT",
    "CONFIG_NET_BUF_DATA_SIZE",
    "CONFIG_NET_MAX_CONTEXTS",
    "CONFIG_MAX_PTHREAD_MUTEX_COUNT",
    "CONFIG_MAX_PTHREAD_COND_COUNT",
)


class ResourceError(Exception):
    def __init__(self, code: str, message: str):
        super().__init__(message)
        self.code = code


def reject(code: str, message: str) -> NoReturn:
    raise ResourceError(code, message)


def load_json(path: Path) -> dict[str, object]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        reject("E_RESOURCE_INPUT", f"cannot read {path}: {error}")
    if not isinstance(value, dict):
        reject("E_RESOURCE_INPUT", f"{path} must contain a JSON object")
    return value


def parse_config(path: Path) -> dict[str, str]:
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except OSError as error:
        reject("E_RESOURCE_INPUT", f"cannot read {path}: {error}")
    values: dict[str, str] = {}
    for line in lines:
        if line.startswith("CONFIG_") and "=" in line:
            key, value = line.split("=", 1)
            values[key] = value.strip('"')
        elif line.startswith("# CONFIG_") and line.endswith(" is not set"):
            values[line[2:-11]] = "n"
    return values


def integer_config(config: dict[str, str], key: str) -> int | None:
    value = config.get(key)
    if value is None or value in ("n", "y"):
        return None
    try:
        return int(value, 0)
    except ValueError:
        return None


def make_check(
    check_id: str,
    code: str,
    required: int,
    available: int | None,
    detail: str,
) -> dict[str, object]:
    passed = available is not None and required <= available
    return {
        "id": check_id,
        "code": code,
        "required": required,
        "available": available,
        "status": "PASS" if passed else "FAIL",
        "detail": detail,
    }


def configured_reservations(
    config: dict[str, str], backend: str, worker_count: int
) -> tuple[list[dict[str, object]], list[dict[str, object]], int]:
    stacks: list[dict[str, object]] = []
    known_total = 0
    for name, key in (
        ("main stack", "CONFIG_MAIN_STACK_SIZE"),
        ("idle stack", "CONFIG_IDLE_STACK_SIZE"),
        ("system workqueue stack", "CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE"),
    ):
        value = integer_config(config, key)
        if value is not None:
            stacks.append({"name": name, "bytes": value, "source": key})
            known_total += value
    if backend == "rmw_cyclonedds_c":
        worker_stack = integer_config(config, "CONFIG_ROS2_ZEPHYR_CYCLONEDDS_THREAD_STACK_SIZE")
        if worker_stack is not None:
            value = worker_count * worker_stack
            stacks.append(
                {
                    "name": "Cyclone DDS worker stacks",
                    "bytes": value,
                    "source": f"{worker_count} x {worker_stack}",
                }
            )
            known_total += value

    heaps: list[dict[str, object]] = []
    zephyr_heap = integer_config(config, "CONFIG_HEAP_MEM_POOL_SIZE")
    if zephyr_heap is not None:
        heaps.append(
            {
                "name": "Zephyr heap backing",
                "bytes": zephyr_heap,
                "source": "CONFIG_HEAP_MEM_POOL_SIZE",
            }
        )
        known_total += zephyr_heap
    libc_heap = integer_config(config, "CONFIG_COMMON_LIBC_MALLOC_ARENA_SIZE")
    if libc_heap is not None and libc_heap >= 0:
        heaps.append(
            {
                "name": "libc heap backing",
                "bytes": libc_heap,
                "source": "CONFIG_COMMON_LIBC_MALLOC_ARENA_SIZE",
            }
        )
        known_total += libc_heap
    else:
        heaps.append(
            {
                "name": "libc heap backing",
                "bytes": None,
                "source": "target-resolved linker remainder",
            }
        )
    return stacks, heaps, known_total


def build_prelink_plan(
    deployment_plan: dict[str, object],
    config: dict[str, str],
    target: str,
) -> dict[str, object]:
    if deployment_plan.get("schema_version") != 2:
        reject("E_RESOURCE_INPUT", "deployment plan schema_version must be 2")
    deployment = deployment_plan.get("deployment")
    capability = deployment_plan.get("capability")
    backend = deployment_plan.get("backend")
    if not isinstance(deployment, dict) or not isinstance(capability, dict) or not isinstance(backend, str):
        reject("E_RESOURCE_INPUT", "deployment plan is missing deployment, capability, or backend")
    resources = deployment.get("resources")
    endpoints = deployment.get("endpoints")
    if not isinstance(resources, dict) or not isinstance(endpoints, list):
        reject("E_RESOURCE_INPUT", "deployment plan has no validated resource contract")
    targets = resources.get("targets")
    configured_target = config.get("CONFIG_BOARD_TARGET")
    if configured_target and isinstance(targets, dict) and configured_target in targets:
        target = configured_target
    if not isinstance(targets, dict) or target not in targets:
        reject("E_RESOURCE_TARGET", f"deployment has no resource budget for target {target}")
    budget = targets[target]
    if not isinstance(budget, dict):
        reject("E_RESOURCE_INPUT", f"invalid resource budget for target {target}")

    checks: list[dict[str, object]] = []
    endpoint_count = len(endpoints)
    checks.append(
        make_check(
            "declared_local_endpoint_capacity",
            "E_RESOURCE_ENDPOINT_CAPACITY",
            endpoint_count,
            resources.get("local_endpoints") if isinstance(resources.get("local_endpoints"), int) else None,
            "declared fixed endpoints must fit the deployment capacity",
        )
    )
    checks.append(
        make_check(
            "configured_local_node_capacity",
            "E_RESOURCE_GRAPH_CAPACITY",
            1,
            integer_config(config, "CONFIG_ROS2_ZEPHYR_GRAPH_MAX_LOCAL_NODES"),
            "one generated node must fit the configured outbound graph capacity",
        )
    )
    checks.append(
        make_check(
            "configured_local_endpoint_capacity",
            "E_RESOURCE_GRAPH_CAPACITY",
            endpoint_count,
            integer_config(config, "CONFIG_ROS2_ZEPHYR_GRAPH_MAX_ENDPOINTS_PER_NODE"),
            "generated endpoints must fit the configured per-node graph capacity",
        )
    )

    remote = resources.get("remote")
    if not isinstance(remote, dict):
        reject("E_RESOURCE_INPUT", "deployment resource contract has no remote bounds")
    for name, config_key in REMOTE_CONFIG_KEYS.items():
        required = remote.get(name)
        if not isinstance(required, int):
            reject("E_RESOURCE_INPUT", f"invalid remote {name} bound")
        if required == 0:
            checks.append(
                {
                    "id": f"configured_remote_{name}_capacity",
                    "code": "E_RESOURCE_GRAPH_CAPACITY",
                    "required": 0,
                    "available": integer_config(config, config_key),
                    "status": "PASS",
                    "detail": "deployment does not require inbound graph capacity",
                }
            )
        else:
            checks.append(
                make_check(
                    f"configured_remote_{name}_capacity",
                    "E_RESOURCE_GRAPH_CAPACITY",
                    required,
                    integer_config(config, config_key),
                    f"declared remote {name} must fit {config_key}",
                )
            )

    workers = capability.get("workers")
    worker_contract = resources.get("workers")
    if not isinstance(workers, dict) or not isinstance(worker_contract, dict):
        reject("E_RESOURCE_INPUT", "worker capability or resource contract is invalid")
    minimum_workers = workers.get("minimum")
    maximum_workers = worker_contract.get("maximum")
    if not isinstance(minimum_workers, int) or not isinstance(maximum_workers, int):
        reject("E_RESOURCE_INPUT", "worker bounds must be integers")
    if backend == "rmw_cyclonedds_c":
        configured_workers = integer_config(config, "CONFIG_ROS2_ZEPHYR_CYCLONEDDS_THREAD_COUNT")
    else:
        configured_workers = minimum_workers
    checks.append(
        make_check(
            "backend_minimum_workers",
            "E_RESOURCE_WORKERS",
            minimum_workers,
            configured_workers,
            "configured middleware worker count must meet the backend minimum",
        )
    )
    checks.append(
        make_check(
            "deployment_maximum_workers",
            "E_RESOURCE_WORKERS",
            configured_workers if configured_workers is not None else maximum_workers + 1,
            maximum_workers,
            "configured middleware worker count must not exceed the deployment contract",
        )
    )

    stacks, heaps, known_ram = configured_reservations(
        config, backend, configured_workers or minimum_workers
    )
    application = resources.get("application")
    if not isinstance(application, dict) or not isinstance(application.get("static_reserve_bytes"), int):
        reject("E_RESOURCE_INPUT", "application static reserve is invalid")
    application_reserve = application["static_reserve_bytes"]
    known_ram += application_reserve
    ram_budget = budget.get("ram_bytes")
    flash_budget = budget.get("flash_bytes")
    if not isinstance(ram_budget, int) or not isinstance(flash_budget, int):
        reject("E_RESOURCE_INPUT", f"invalid target budget for {target}")
    checks.append(
        make_check(
            "known_prelink_ram_slices",
            "E_RESOURCE_RAM_BUDGET",
            known_ram,
            ram_budget,
            "known stack, heap, and application reservations must fit the RAM budget",
        )
    )

    static_buffers = {
        key: value
        for key in STATIC_BUFFER_KEYS
        if (value := integer_config(config, key)) is not None
    }
    failed = [item for item in checks if item["status"] == "FAIL"]
    return {
        "schema_version": 1,
        "status": "FAIL" if failed else "PASS",
        "target": target,
        "backend": backend,
        "deployment": deployment.get("name"),
        "budgets": {
            "flash_bytes": flash_budget,
            "ram_bytes": ram_budget,
        },
        "planned": {
            "local_endpoint_resources": {
                "required": endpoint_count,
                "capacity": resources["local_endpoints"],
            },
            "remote_discovery_bounds": remote,
            "thread_stacks": stacks,
            "static_buffers": static_buffers,
            "heap_backing": heaps,
            "middleware_reservations": {
                "workers": configured_workers,
                "minimum_workers": minimum_workers,
                "maximum_workers": maximum_workers,
            },
            "application_reservations": {
                "static_reserve_bytes": application_reserve,
            },
            "known_ram_slices_bytes": known_ram,
        },
        "checks": checks,
    }


@dataclasses.dataclass(frozen=True)
class ElfSection:
    name: str
    address: int
    size: int
    section_type: int
    flags: int

    @property
    def alloc(self) -> bool:
        return bool(self.flags & 0x2)

    @property
    def writable(self) -> bool:
        return bool(self.flags & 0x1)

    @property
    def executable(self) -> bool:
        return bool(self.flags & 0x4)

    @property
    def occupies_flash(self) -> bool:
        return self.alloc and self.section_type != 8 and "dummy" not in self.name

    @property
    def occupies_ram(self) -> bool:
        return self.alloc and self.writable and not self.executable and "dummy" not in self.name


def parse_elf_sections(path: Path) -> list[ElfSection]:
    try:
        data = path.read_bytes()
    except OSError as error:
        reject("E_RESOURCE_ARTIFACT", f"cannot read {path}: {error}")
    if data[:4] != b"\x7fELF":
        reject("E_RESOURCE_ARTIFACT", f"{path} is not an ELF file")
    elf_class, byte_order = data[4], data[5]
    endian = "<" if byte_order == 1 else ">" if byte_order == 2 else None
    if endian is None or elf_class not in (1, 2):
        reject("E_RESOURCE_ARTIFACT", f"{path} has an unsupported ELF format")
    if elf_class == 1:
        header = struct.unpack_from(endian + "16sHHIIIIIHHHHHH", data, 0)
        section_offset, entry_size, count, names_index = header[6], header[11], header[12], header[13]
        section_format = endian + "IIIIIIIIII"
    else:
        header = struct.unpack_from(endian + "16sHHIQQQIHHHHHH", data, 0)
        section_offset, entry_size, count, names_index = header[6], header[11], header[12], header[13]
        section_format = endian + "IIQQQQIIQQ"
    if count == 0 or names_index >= count:
        reject("E_RESOURCE_ARTIFACT", f"{path} has no supported section table")
    raw = [
        struct.unpack_from(section_format, data, section_offset + index * entry_size)
        for index in range(count)
    ]
    names_header = raw[names_index]
    names = data[names_header[4]:names_header[4] + names_header[5]]

    def section_name(offset: int) -> str:
        end = names.find(b"\0", offset)
        if end < 0:
            end = len(names)
        return names[offset:end].decode("utf-8", errors="replace")

    return [
        ElfSection(section_name(item[0]), item[3], item[5], item[1], item[2])
        for item in raw
    ]


def parse_memory_regions(map_text: str) -> dict[str, dict[str, int]]:
    marker = "Memory Configuration"
    end_marker = "Linker script and memory map"
    if marker not in map_text or end_marker not in map_text:
        return {}
    block = map_text.split(marker, 1)[1].split(end_marker, 1)[0]
    regions: dict[str, dict[str, int]] = {}
    for line in block.splitlines():
        match = re.match(
            r"^([^\s*][^\s]*)\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)(?:\s+\S+)?$",
            line.strip(),
        )
        if match:
            name, origin, length = match.groups()
            regions[name] = {"origin": int(origin, 16), "length": int(length, 16)}
    return regions


def region_usage(sections: list[ElfSection], region: dict[str, int]) -> int:
    origin = region["origin"]
    end = origin + region["length"]
    ends = [
        section.address + section.size
        for section in sections
        if section.alloc and origin <= section.address < end and section.address + section.size <= end
    ]
    return max(ends, default=origin) - origin


def parse_linked_stacks(map_text: str) -> list[dict[str, object]]:
    pending = ""
    current: dict[str, object] | None = None
    rows: list[dict[str, object]] = []
    one_line = re.compile(
        r"^\s(\.noinit\S*)\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)\s+(.+)$"
    )
    name_line = re.compile(r"^\s(\.noinit\S*)\s*$")
    detail_line = re.compile(r"^\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)\s+(.+)$")
    stack_symbol = re.compile(
        r"^\s+0x[0-9a-fA-F]+\s+([A-Za-z_][A-Za-z0-9_]*stack(?:s)?)\s*$"
    )

    def finish_current() -> None:
        nonlocal current
        if current is not None and current["names"]:
            rows.append(
                {
                    "name": ", ".join(sorted(current["names"])),
                    "bytes": current["bytes"],
                    "source": current["source"],
                }
            )
        current = None

    for line in map_text.splitlines():
        match = one_line.match(line)
        if match:
            finish_current()
            _, address, size, source = match.groups()
            current = {"bytes": int(size, 16), "source": source, "names": []}
            pending = ""
            continue
        match = name_line.match(line)
        if match:
            finish_current()
            pending = match.group(1)
            continue
        match = detail_line.match(line) if pending else None
        if match:
            finish_current()
            address, size, source = match.groups()
            current = {"bytes": int(size, 16), "source": source, "names": []}
            pending = ""
            continue
        match = stack_symbol.match(line) if current is not None else None
        if match:
            current["names"].append(match.group(1))
            continue
        if current is not None:
            finish_current()
    finish_current()
    return rows


def parse_runtime_log(path: Path | None) -> dict[str, object]:
    if path is None:
        return {"status": "not_provided"}
    try:
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    except OSError as error:
        reject("E_RESOURCE_INPUT", f"cannot read runtime log {path}: {error}")
    records = []
    for line in lines:
        if "_METRICS" not in line and "_RESOURCES" not in line:
            continue
        fields: dict[str, object] = {}
        for key, value in re.findall(r"([A-Za-z_][A-Za-z0-9_]*)=([^\s]+)", line):
            try:
                fields[key] = int(value, 0)
            except ValueError:
                fields[key] = value
        records.append({"marker": line.split()[0], "values": fields})
    return {
        "status": "measured" if records else "no_resource_markers",
        "source": str(path),
        "records": records,
    }


def measure_linked(build_dir: Path) -> dict[str, object]:
    zephyr_dir = build_dir / "zephyr" if (build_dir / "zephyr").is_dir() else build_dir
    elf_path = zephyr_dir / "zephyr.elf"
    map_path = zephyr_dir / "zephyr.map"
    config_path = zephyr_dir / ".config"
    if not map_path.is_file():
        map_path = zephyr_dir / "zephyr_final.map"
    missing = [path for path in (elf_path, map_path, config_path) if not path.is_file()]
    if missing:
        reject(
            "E_RESOURCE_ARTIFACT",
            "missing required post-link artifacts: " + ", ".join(map(str, missing)),
        )
    sections = parse_elf_sections(elf_path)
    map_text = map_path.read_text(encoding="utf-8", errors="replace")
    config = parse_config(config_path)
    regions = parse_memory_regions(map_text)
    binary_path = zephyr_dir / "zephyr.bin"
    if binary_path.is_file():
        flash_bytes = binary_path.stat().st_size
        flash_source = str(binary_path)
    else:
        flash_bytes = sum(section.size for section in sections if section.occupies_flash)
        flash_source = "ELF allocated file-backed sections"
    if "dram0_0_seg" in regions:
        ram_bytes = region_usage(sections, regions["dram0_0_seg"])
        ram_source = "map region dram0_0_seg"
    else:
        ram_bytes = sum(section.size for section in sections if section.occupies_ram)
        ram_source = "ELF allocated writable sections"

    libc_heap = None
    libc_heap_source = "linker symbol"
    match = re.search(r"0x([0-9a-fA-F]+)\s+_libc_heap_size\s*=", map_text)
    if match:
        libc_heap = int(match.group(1), 16)
    else:
        configured_libc_heap = integer_config(
            config, "CONFIG_COMMON_LIBC_MALLOC_ARENA_SIZE"
        )
        if configured_libc_heap is not None and configured_libc_heap >= 0:
            libc_heap = configured_libc_heap
            libc_heap_source = "CONFIG_COMMON_LIBC_MALLOC_ARENA_SIZE"
    heap_rows = []
    zephyr_heap = integer_config(config, "CONFIG_HEAP_MEM_POOL_SIZE")
    if zephyr_heap is not None:
        heap_rows.append(
            {"name": "Zephyr heap backing", "bytes": zephyr_heap, "source": "CONFIG_HEAP_MEM_POOL_SIZE"}
        )
    if libc_heap is not None:
        heap_rows.append(
            {
                "name": "libc heap backing",
                "bytes": libc_heap,
                "source": libc_heap_source,
            }
        )
    return {
        "flash_bytes": flash_bytes,
        "flash_source": flash_source,
        "static_ram_bytes": ram_bytes,
        "ram_source": ram_source,
        "memory_regions": regions,
        "heap_backing": heap_rows,
        "thread_stacks": parse_linked_stacks(map_text),
        "elf_sections": {
            "allocated_file_bytes": sum(section.size for section in sections if section.occupies_flash),
            "allocated_ram_bytes": sum(section.size for section in sections if section.occupies_ram),
        },
    }


def build_postlink_report(
    resource_plan: dict[str, object],
    linked: dict[str, object],
    runtime: dict[str, object],
) -> dict[str, object]:
    if resource_plan.get("status") != "PASS":
        reject("E_RESOURCE_PLAN", "cannot verify a failed pre-link resource plan")
    budgets = resource_plan["budgets"]
    application = resource_plan["planned"]["application_reservations"]
    reserve = application["static_reserve_bytes"]
    checks = list(resource_plan["checks"])
    checks.extend(
        [
            make_check(
                "linked_flash_budget",
                "E_RESOURCE_FLASH_BUDGET",
                linked["flash_bytes"],
                budgets["flash_bytes"],
                "linked flash must not exceed the target budget",
            ),
            make_check(
                "linked_ram_budget_with_reserve",
                "E_RESOURCE_RAM_BUDGET",
                linked["static_ram_bytes"] + reserve,
                budgets["ram_bytes"],
                "linked static RAM plus declared application reserve must fit the target budget",
            ),
        ]
    )
    regions = linked.get("memory_regions", {})
    if "FLASH" in regions:
        checks.append(
            make_check(
                "flash_budget_within_target_region",
                "E_RESOURCE_TARGET_CAPACITY",
                budgets["flash_bytes"],
                regions["FLASH"]["length"],
                "declared flash budget must fit the linked target region",
            )
        )
    if "dram0_0_seg" in regions:
        checks.append(
            make_check(
                "ram_budget_within_target_region",
                "E_RESOURCE_TARGET_CAPACITY",
                budgets["ram_bytes"],
                regions["dram0_0_seg"]["length"],
                "declared RAM budget must fit the linked target region",
            )
        )
    failed = [item for item in checks if item["status"] == "FAIL"]
    return {
        "schema_version": 1,
        "status": "FAIL" if failed else "PASS",
        "target": resource_plan["target"],
        "backend": resource_plan["backend"],
        "deployment": resource_plan["deployment"],
        "planned": resource_plan["planned"],
        "budgets": budgets,
        "linked": linked,
        "runtime_measured": runtime,
        "checks": checks,
        "accounting_note": (
            "Thread stacks and heap backing are explanatory slices of linked static RAM. "
            "Runtime allocator high-water values are observations and are never added to linked RAM."
        ),
    }


def format_bytes(value: int | None) -> str:
    return "target-resolved" if value is None else f"{value:,} B"


def plan_markdown(plan: dict[str, object]) -> str:
    planned = plan["planned"]
    lines = [
        "# Pre-build resource plan",
        "",
        f"Status: **{plan['status']}**",
        f"Deployment: {plan['deployment']}",
        f"Target: {plan['target']}",
        f"Backend: {plan['backend']}",
        "",
        "## Contract",
        "",
        f"- Local endpoints: {planned['local_endpoint_resources']['required']} required / "
        f"{planned['local_endpoint_resources']['capacity']} allowed.",
        f"- Remote discovery bounds: {planned['remote_discovery_bounds']}.",
        f"- Middleware workers: {planned['middleware_reservations']['workers']} configured / "
        f"{planned['middleware_reservations']['maximum_workers']} allowed.",
        f"- Flash budget: {format_bytes(plan['budgets']['flash_bytes'])}.",
        f"- RAM budget: {format_bytes(plan['budgets']['ram_bytes'])}.",
        f"- Application reserve: "
        f"{format_bytes(planned['application_reservations']['static_reserve_bytes'])}.",
        "",
        "## Planned RAM slices",
        "",
        "| Kind | Reservation | Bytes | Evidence |",
        "|---|---|---:|---|",
    ]
    for kind, rows in (
        ("stack", planned["thread_stacks"]),
        ("heap", planned["heap_backing"]),
    ):
        for row in rows:
            lines.append(
                f"| {kind} | {row['name']} | {format_bytes(row['bytes'])} | {row['source']} |"
            )
    lines.extend(["", "## Admission checks", "", "| Check | Required | Available | Status |", "|---|---:|---:|---|"])
    for item in plan["checks"]:
        lines.append(
            f"| {item['id']} | {item['required']} | "
            f"{item['available'] if item['available'] is not None else 'missing'} | {item['status']} |"
        )
    lines.append("")
    return "\n".join(lines)


def report_markdown(report: dict[str, object]) -> str:
    linked = report["linked"]
    reserve = report["planned"]["application_reservations"]["static_reserve_bytes"]
    lines = [
        "# Post-link resource verification",
        "",
        f"Status: **{report['status']}**",
        f"Deployment: {report['deployment']}",
        f"Target: {report['target']}",
        f"Backend: {report['backend']}",
        "",
        "## Planned versus linked",
        "",
        "| Resource | Planned limit | Linked | Headroom |",
        "|---|---:|---:|---:|",
        f"| Flash | {format_bytes(report['budgets']['flash_bytes'])} | "
        f"{format_bytes(linked['flash_bytes'])} | "
        f"{format_bytes(report['budgets']['flash_bytes'] - linked['flash_bytes'])} |",
        f"| Static RAM | {format_bytes(report['budgets']['ram_bytes'])} | "
        f"{format_bytes(linked['static_ram_bytes'])} | "
        f"{format_bytes(report['budgets']['ram_bytes'] - linked['static_ram_bytes'])} |",
        f"| Static RAM plus application reserve | {format_bytes(report['budgets']['ram_bytes'])} | "
        f"{format_bytes(linked['static_ram_bytes'] + reserve)} | "
        f"{format_bytes(report['budgets']['ram_bytes'] - linked['static_ram_bytes'] - reserve)} |",
        "",
        "Thread stacks and heap backing below are slices of linked RAM, not additional totals.",
        "",
        "## Linked reservations",
        "",
        "| Kind | Reservation | Bytes | Evidence |",
        "|---|---|---:|---|",
    ]
    for kind, rows in (("stack", linked["thread_stacks"]), ("heap", linked["heap_backing"])):
        for row in rows:
            lines.append(f"| {kind} | {row['name']} | {format_bytes(row['bytes'])} | {row['source']} |")
    lines.extend(["", "## Runtime measured", ""])
    runtime = report["runtime_measured"]
    if runtime["status"] == "measured":
        lines.append(f"Runtime markers were parsed from {runtime['source']}.")
        for record in runtime["records"]:
            lines.append(f"- {record['marker']}: {record['values']}")
    else:
        lines.append(f"Runtime measurements: {runtime['status']}.")
    lines.extend(["", "## Verification checks", "", "| Check | Required | Available | Status |", "|---|---:|---:|---|"])
    for item in report["checks"]:
        lines.append(
            f"| {item['id']} | {item['required']} | "
            f"{item['available'] if item['available'] is not None else 'missing'} | {item['status']} |"
        )
    lines.extend(["", report["accounting_note"], ""])
    return "\n".join(lines)


def write_report(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")


def first_failed(report: dict[str, object]) -> dict[str, object] | None:
    return next((item for item in report["checks"] if item["status"] == "FAIL"), None)


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    plan = subparsers.add_parser("plan", help="derive and admit the pre-build resource plan")
    plan.add_argument("--deployment-plan", type=Path, required=True)
    plan.add_argument("--config", type=Path, required=True)
    plan.add_argument("--target", required=True)
    plan.add_argument("--json", type=Path, required=True)
    plan.add_argument("--markdown", type=Path, required=True)
    verify = subparsers.add_parser("verify", help="verify linked artifacts against a resource plan")
    verify.add_argument("--resource-plan", type=Path, required=True)
    verify.add_argument("--build-dir", type=Path, required=True)
    verify.add_argument("--runtime-log", type=Path)
    verify.add_argument("--json", type=Path, required=True)
    verify.add_argument("--markdown", type=Path, required=True)
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    try:
        if args.command == "plan":
            report = build_prelink_plan(
                load_json(args.deployment_plan),
                parse_config(args.config),
                args.target,
            )
            markdown = plan_markdown(report)
            marker = "ROS2_ZEPHYR_RESOURCE_PLAN"
        else:
            report = build_postlink_report(
                load_json(args.resource_plan),
                measure_linked(args.build_dir),
                parse_runtime_log(args.runtime_log),
            )
            markdown = report_markdown(report)
            marker = "ROS2_ZEPHYR_RESOURCE_VERIFY"
        write_report(args.json, json.dumps(report, indent=2, sort_keys=True) + "\n")
        write_report(args.markdown, markdown)
        failed = first_failed(report)
        if failed is not None:
            print(f"{failed['code']}: {failed['detail']}: required={failed['required']} available={failed['available']}", file=sys.stderr)
            return 2
        print(
            f"{marker}_PASS deployment={report['deployment']} target={report['target']} backend={report['backend']}"
        )
        return 0
    except ResourceError as error:
        print(f"{error.code}: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
