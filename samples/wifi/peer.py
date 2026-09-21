#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

"""Run the desktop half of the ESP32-S3 Wi-Fi acceptance test."""

import argparse
import multiprocessing
import sys
import time

import rclpy
from rclpy.qos import DurabilityPolicy, HistoryPolicy, QoSProfile, ReliabilityPolicy
from std_msgs.msg import UInt32

EXPECTED_SAMPLES = 18
MAX_RECOVERY_SAMPLES = 10
DESKTOP_TO_DEVICE_VALUE = 314159265
DEVICE_TO_DESKTOP_VALUE = 271828182
RATE_CASES = ((1, 3), (10, 5), (100, 10))
TRANSIENT_HISTORY_LAST_VALUE = 5
TRANSIENT_LIVE_VALUE = 6
DEVICE_NODE = ("ros2_zephyr_esp32s3", "/")
GRAPH_LOCAL_TOPIC = "/ros2_zephyr/graph_local"
GRAPH_REMOTE_A = "/ros2_zephyr/graph_remote_a"
GRAPH_REMOTE_B = "/ros2_zephyr/graph_remote_b"
GRAPH_HOLD_SECONDS = 12.0
GRAPH_LOSS_HOLD_SECONDS = 20.0


def endpoint_qos(reliability: str, durability: str, depth: int) -> QoSProfile:
    return QoSProfile(
        depth=depth,
        durability=(
            DurabilityPolicy.TRANSIENT_LOCAL
            if durability == "transient_local"
            else DurabilityPolicy.VOLATILE
        ),
        history=HistoryPolicy.KEEP_LAST,
        reliability=(
            ReliabilityPolicy.RELIABLE
            if reliability == "reliable"
            else ReliabilityPolicy.BEST_EFFORT
        ),
    )


def wait_for_subscriber(node, publisher, timeout: float) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        rclpy.spin_once(node, timeout_sec=0.1)
        if publisher.get_subscription_count() > 0:
            return True
    return False


def wait_for_publisher(node, topic: str, timeout: float) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        rclpy.spin_once(node, timeout_sec=0.1)
        if node.get_publishers_info_by_topic(topic):
            return True
    return False


def wait_for_condition(node, condition, timeout: float) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        rclpy.spin_once(node, timeout_sec=0.1)
        if condition():
            return True
    return False


def device_node_visible(node) -> bool:
    return DEVICE_NODE in node.get_node_names_and_namespaces()


def device_topic_state(node) -> tuple[bool, bool] | None:
    if not device_node_visible(node):
        return None
    publishers = node.get_publisher_names_and_types_by_node(*DEVICE_NODE)
    subscribers = node.get_subscriber_names_and_types_by_node(*DEVICE_NODE)
    return (
        any(name == GRAPH_LOCAL_TOPIC for name, _ in publishers),
        any(name == GRAPH_LOCAL_TOPIC for name, _ in subscribers),
    )


def run_graph_outbound(node, timeout: float, cycle: int = 1) -> int:
    phases = (
        ("pubsub", (True, True)),
        ("subscription_only", (False, True)),
        ("node_only", (False, False)),
    )
    for phase, expected in phases:
        if not wait_for_condition(node, lambda: device_topic_state(node) == expected, timeout):
            print(
                f"ROS2_ZEPHYR_PEER_ERROR role=graph-outbound phase={phase} "
                f"observed={device_topic_state(node)}",
                file=sys.stderr,
            )
            return 1
        print(
            f"ROS2_ZEPHYR_PEER_GRAPH_PASS direction=outbound phase={phase} "
            f"cycle={cycle}"
        )

    if not wait_for_condition(node, lambda: not device_node_visible(node), timeout):
        print(
            "ROS2_ZEPHYR_PEER_ERROR role=graph-outbound phase=node_cleanup",
            file=sys.stderr,
        )
        return 1
    print(
        "ROS2_ZEPHYR_PEER_GRAPH_PASS direction=outbound phase=node_cleanup "
        f"cycle={cycle}"
    )
    return 0


def create_graph_node(name: str, namespace: str):
    return rclpy.create_node(
        name,
        namespace=namespace,
        enable_rosout=False,
        start_parameter_services=False,
    )


def graph_inbound_worker(connection, profile: str, timeout: float) -> None:
    rclpy.init()
    if profile == "beta":
        node = create_graph_node("graph_peer_beta", "/graph_acceptance_alt")
        publishers = [node.create_publisher(UInt32, GRAPH_REMOTE_B, 10)]
        subscriptions = []
    else:
        node = create_graph_node("graph_peer_alpha", "/graph_acceptance")
        publishers = []
        subscriptions = []
        if profile == "initial":
            publishers = [
                node.create_publisher(UInt32, GRAPH_REMOTE_A, 10),
                node.create_publisher(UInt32, GRAPH_REMOTE_A, 10),
            ]
            subscriptions = [
                node.create_subscription(UInt32, GRAPH_REMOTE_A, lambda _: None, 10),
                node.create_subscription(UInt32, GRAPH_REMOTE_A, lambda _: None, 10),
            ]
        else:
            subscriptions = [
                node.create_subscription(UInt32, GRAPH_REMOTE_A, lambda _: None, 10)
            ]
    connection.send("ready")
    visibility_deadline = time.monotonic() + timeout
    visibility_reported = False
    try:
        while True:
            rclpy.spin_once(node, timeout_sec=0.1)
            if not visibility_reported and device_node_visible(node):
                connection.send("visible")
                visibility_reported = True
            elif not visibility_reported and time.monotonic() >= visibility_deadline:
                connection.send("visibility_timeout")
                visibility_reported = True
            if not connection.poll():
                continue
            command = connection.recv()
            if command == "reduce":
                node.destroy_publisher(publishers.pop())
                for subscription in subscriptions:
                    node.destroy_subscription(subscription)
                subscriptions.clear()
                connection.send("reduced")
            elif command == "stop":
                return
    finally:
        node.destroy_node()
        rclpy.shutdown()


def wait_for_worker(connection, process, expected: str, timeout: float) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if connection.poll(0.1):
            message = connection.recv()
            if message == expected:
                return True
            if message == "visibility_timeout":
                return False
        if not process.is_alive():
            return False
    return False


def terminate_worker(process) -> None:
    if process.is_alive():
        process.kill()
        process.join(5.0)


def start_graph_worker(context, profile: str, timeout: float):
    parent_connection, child_connection = context.Pipe()
    process = context.Process(
        target=graph_inbound_worker,
        args=(child_connection, profile, timeout),
    )
    process.start()
    child_connection.close()
    return process, parent_connection


def run_graph_inbound(timeout: float) -> int:
    context = multiprocessing.get_context("spawn")
    alpha, alpha_connection = start_graph_worker(context, "initial", timeout)
    beta, beta_connection = start_graph_worker(context, "beta", timeout)
    restarted = None
    restarted_connection = None
    try:
        if not wait_for_worker(alpha_connection, alpha, "ready", timeout) or not wait_for_worker(
            beta_connection, beta, "ready", timeout
        ):
            print(
                "ROS2_ZEPHYR_PEER_ERROR role=graph-inbound reason=worker_start_timeout",
                file=sys.stderr,
            )
            return 1
        if not wait_for_worker(alpha_connection, alpha, "visible", timeout):
            print(
                "ROS2_ZEPHYR_PEER_ERROR role=graph-inbound reason=device_discovery_timeout",
                file=sys.stderr,
            )
            return 1
        print("ROS2_ZEPHYR_PEER_GRAPH_PHASE direction=inbound phase=initial", flush=True)
        time.sleep(GRAPH_HOLD_SECONDS)

        alpha_connection.send("reduce")
        if not wait_for_worker(alpha_connection, alpha, "reduced", timeout):
            print(
                "ROS2_ZEPHYR_PEER_ERROR role=graph-inbound reason=reduce_timeout",
                file=sys.stderr,
            )
            return 1
        terminate_worker(beta)
        print("ROS2_ZEPHYR_PEER_GRAPH_PHASE direction=inbound phase=reduced", flush=True)
        time.sleep(GRAPH_LOSS_HOLD_SECONDS)

        terminate_worker(alpha)
        print(
            "ROS2_ZEPHYR_PEER_GRAPH_PHASE direction=inbound phase=participant_lost",
            flush=True,
        )
        time.sleep(GRAPH_LOSS_HOLD_SECONDS)

        restarted, restarted_connection = start_graph_worker(context, "restart", timeout)
        if not wait_for_worker(
            restarted_connection, restarted, "ready", timeout
        ) or not wait_for_worker(restarted_connection, restarted, "visible", timeout):
            print(
                "ROS2_ZEPHYR_PEER_ERROR role=graph-inbound reason=restart_timeout",
                file=sys.stderr,
            )
            return 1
        print("ROS2_ZEPHYR_PEER_GRAPH_PHASE direction=inbound phase=restart", flush=True)
        time.sleep(GRAPH_HOLD_SECONDS)

        terminate_worker(restarted)
        print(
            "ROS2_ZEPHYR_PEER_GRAPH_PHASE direction=inbound phase=restart_lost",
            flush=True,
        )
        time.sleep(GRAPH_LOSS_HOLD_SECONDS)
        print("ROS2_ZEPHYR_PEER_PASS role=graph-inbound")
        return 0
    finally:
        terminate_worker(alpha)
        terminate_worker(beta)
        if restarted is not None:
            terminate_worker(restarted)
        alpha_connection.close()
        beta_connection.close()
        if restarted_connection is not None:
            restarted_connection.close()


def run_publisher(
    node,
    timeout: float,
    reliability: str,
    durability: str,
    depth: int,
    skip_match: bool,
) -> int:
    publisher = node.create_publisher(
        UInt32,
        "/ros2_zephyr/desktop_to_device",
        endpoint_qos(reliability, durability, depth),
    )
    if durability == "transient_local":
        for value in range(1, TRANSIENT_HISTORY_LAST_VALUE + 1):
            publisher.publish(UInt32(data=value))
            rclpy.spin_once(node, timeout_sec=0.02)
            print(f"ROS2_ZEPHYR_PEER_SENT phase=history value={value}")
        print(
            f"ROS2_ZEPHYR_PEER_HISTORY_READY role=pub depth={depth} "
            f"history_last={TRANSIENT_HISTORY_LAST_VALUE}",
            flush=True,
        )
    started = time.monotonic()
    if not skip_match and not wait_for_subscriber(node, publisher, timeout):
        print("ROS2_ZEPHYR_PEER_ERROR role=pub reason=match_timeout", file=sys.stderr)
        return 1

    if skip_match:
        print("ROS2_ZEPHYR_PEER_MATCH role=pub discovery_ms=unavailable")
    else:
        print(
            f"ROS2_ZEPHYR_PEER_MATCH role=pub discovery_ms="
            f"{(time.monotonic() - started) * 1000:.3f}"
        )
    sent = 0
    recovery_message = UInt32(data=DESKTOP_TO_DEVICE_VALUE)
    if durability == "transient_local":
        history_deadline = time.monotonic() + 0.5
        while time.monotonic() < history_deadline:
            rclpy.spin_once(node, timeout_sec=0.05)
        publisher.publish(UInt32(data=TRANSIENT_LIVE_VALUE))
        sent = TRANSIENT_HISTORY_LAST_VALUE + 1
        print(f"ROS2_ZEPHYR_PEER_SENT phase=live value={TRANSIENT_LIVE_VALUE}")
    else:
        for rate_hz, sample_count in RATE_CASES:
            interval = 1.0 / rate_hz
            for sequence in range(1, sample_count + 1):
                publisher.publish(recovery_message)
                sent += 1
                print(
                    "ROS2_ZEPHYR_PEER_SENT "
                    f"value={recovery_message.data} rate_hz={rate_hz} sequence={sequence}"
                )
                rclpy.spin_once(node, timeout_sec=interval)

    recovery_samples = 0
    if skip_match:
        drain_deadline = time.monotonic() + 2.0
        while time.monotonic() < drain_deadline:
            rclpy.spin_once(node, timeout_sec=0.1)
    else:
        completion_deadline = time.monotonic() + 10.0
        while (
            publisher.get_subscription_count() > 0
            and time.monotonic() < completion_deadline
        ):
            if (
                durability == "volatile"
                and reliability == "best_effort"
                and recovery_samples < MAX_RECOVERY_SAMPLES
            ):
                publisher.publish(recovery_message)
                recovery_samples += 1
            rclpy.spin_once(node, timeout_sec=0.1)

        if publisher.get_subscription_count() > 0:
            print(
                "ROS2_ZEPHYR_PEER_ERROR role=pub reason=device_did_not_complete "
                f"scheduled={sent} recovery={recovery_samples}",
                file=sys.stderr,
            )
            return 1

    print(
        f"ROS2_ZEPHYR_PEER_PASS role=pub reliability={reliability} "
        f"durability={durability} depth={depth} scheduled={sent} recovery={recovery_samples}"
    )
    return 0


def run_subscriber(
    node, timeout: float, reliability: str, durability: str, depth: int
) -> int:
    received_at: list[float] = []
    received_values: list[int] = []
    topic = "/ros2_zephyr/device_to_desktop"

    def receive(message: UInt32) -> None:
        received_at.append(time.monotonic())
        received_values.append(message.data)
        print(
            "ROS2_ZEPHYR_PEER_RECEIVED "
            f"value={message.data} sequence={len(received_at)}"
        )

    if durability == "transient_local":
        if not wait_for_publisher(node, topic, timeout):
            print(
                "ROS2_ZEPHYR_PEER_ERROR "
                "role=sub reason=publisher_discovery_timeout",
                file=sys.stderr,
            )
            return 1
        print("ROS2_ZEPHYR_PEER_PUBLISHER_READY role=sub", flush=True)
        history_deadline = time.monotonic() + 0.5
        while time.monotonic() < history_deadline:
            rclpy.spin_once(node, timeout_sec=0.05)

    subscription = node.create_subscription(
        UInt32,
        topic,
        receive,
        endpoint_qos(reliability, durability, depth),
    )
    if durability == "transient_local":
        retained = min(depth, TRANSIENT_HISTORY_LAST_VALUE)
        expected_values = list(
            range(TRANSIENT_LIVE_VALUE - retained, TRANSIENT_LIVE_VALUE + 1)
        )
    else:
        expected_values = [DEVICE_TO_DESKTOP_VALUE] * EXPECTED_SAMPLES
    deadline = time.monotonic() + timeout
    while len(received_at) < len(expected_values) and time.monotonic() < deadline:
        rclpy.spin_once(node, timeout_sec=0.1)

    node.destroy_subscription(subscription)
    if received_values != expected_values:
        print(
            "ROS2_ZEPHYR_PEER_ERROR "
            f"role=sub expected={expected_values} received={received_values}",
            file=sys.stderr,
        )
        return 1

    intervals_ms = [
        (current - previous) * 1000
        for previous, current in zip(received_at, received_at[1:])
    ]
    intervals = ",".join(f"{interval:.3f}" for interval in intervals_ms)
    print(
        f"ROS2_ZEPHYR_PEER_PASS role=sub reliability={reliability} "
        f"durability={durability} depth={depth} samples={len(received_at)} "
        f"values={received_values} arrival_intervals_ms={intervals}"
    )
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "role",
        choices=("pub", "sub", "graph-outbound", "graph-inbound"),
        help="desktop peer role",
    )
    parser.add_argument("--timeout", type=float, default=90.0)
    parser.add_argument(
        "--reliability", choices=("best_effort", "reliable"), default="best_effort"
    )
    parser.add_argument(
        "--durability", choices=("volatile", "transient_local"), default="volatile"
    )
    parser.add_argument("--depth", type=int, default=32)
    parser.add_argument(
        "--cycles",
        type=int,
        default=1,
        help="complete outbound graph lifecycles to observe (graph-outbound only)",
    )
    parser.add_argument(
        "--skip-match",
        action="store_true",
        help="publish without graph-based endpoint matching",
    )
    args = parser.parse_args()
    if args.depth <= 0:
        parser.error("--depth must be a positive integer")
    if args.cycles <= 0:
        parser.error("--cycles must be a positive integer")
    if args.cycles != 1 and args.role != "graph-outbound":
        parser.error("--cycles is only valid for graph-outbound")

    if args.role == "graph-inbound":
        return run_graph_inbound(args.timeout)

    rclpy.init()
    node = rclpy.create_node(
        f"ros2_zephyr_wifi_peer_{args.role.replace('-', '_')}",
        enable_rosout=False,
        start_parameter_services=False,
    )
    try:
        if args.role == "pub":
            return run_publisher(
                node,
                args.timeout,
                args.reliability,
                args.durability,
                args.depth,
                args.skip_match,
            )
        if args.role == "sub":
            return run_subscriber(
                node, args.timeout, args.reliability, args.durability, args.depth
            )
        for cycle in range(1, args.cycles + 1):
            if run_graph_outbound(node, args.timeout, cycle) != 0:
                return 1
        return 0
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == "__main__":
    raise SystemExit(main())
