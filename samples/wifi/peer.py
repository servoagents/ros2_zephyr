#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

"""Run the desktop half of the ESP32-S3 Wi-Fi acceptance test."""

import argparse
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


def run_graph_outbound(node, timeout: float) -> int:
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
        print(f"ROS2_ZEPHYR_PEER_GRAPH_PASS direction=outbound phase={phase}")

    if not wait_for_condition(node, lambda: not device_node_visible(node), timeout):
        print(
            "ROS2_ZEPHYR_PEER_ERROR role=graph-outbound phase=node_cleanup",
            file=sys.stderr,
        )
        return 1
    print("ROS2_ZEPHYR_PEER_GRAPH_PASS direction=outbound phase=node_cleanup")
    return 0


def create_graph_node(name: str, namespace: str):
    return rclpy.create_node(
        name,
        namespace=namespace,
        enable_rosout=False,
        start_parameter_services=False,
    )


def hold_graph_phase(node, phase: str) -> None:
    print(f"ROS2_ZEPHYR_PEER_GRAPH_PHASE direction=inbound phase={phase}", flush=True)
    deadline = time.monotonic() + GRAPH_HOLD_SECONDS
    while time.monotonic() < deadline:
        rclpy.spin_once(node, timeout_sec=0.1)


def run_graph_inbound(timeout: float) -> int:
    rclpy.init()
    alpha = create_graph_node("graph_peer_alpha", "/graph_acceptance")
    beta = create_graph_node("graph_peer_beta", "/graph_acceptance_alt")
    alpha_publishers = [
        alpha.create_publisher(UInt32, GRAPH_REMOTE_A, 10),
        alpha.create_publisher(UInt32, GRAPH_REMOTE_A, 10),
    ]
    alpha_subscriptions = [
        alpha.create_subscription(UInt32, GRAPH_REMOTE_A, lambda _: None, 10),
        alpha.create_subscription(UInt32, GRAPH_REMOTE_A, lambda _: None, 10),
    ]
    beta_publisher = beta.create_publisher(UInt32, GRAPH_REMOTE_B, 10)
    beta_destroyed = False
    try:
        if not wait_for_condition(alpha, lambda: device_node_visible(alpha), timeout):
            print(
                "ROS2_ZEPHYR_PEER_ERROR role=graph-inbound reason=device_discovery_timeout",
                file=sys.stderr,
            )
            return 1
        hold_graph_phase(alpha, "initial")

        alpha.destroy_publisher(alpha_publishers.pop())
        for subscription in alpha_subscriptions:
            alpha.destroy_subscription(subscription)
        alpha_subscriptions.clear()
        beta.destroy_publisher(beta_publisher)
        beta.destroy_node()
        beta_destroyed = True
        hold_graph_phase(alpha, "reduced")
    finally:
        if not beta_destroyed:
            beta.destroy_node()
        alpha.destroy_node()
        if rclpy.ok():
            rclpy.shutdown()

    print(
        "ROS2_ZEPHYR_PEER_GRAPH_PHASE direction=inbound phase=participant_lost",
        flush=True,
    )
    time.sleep(GRAPH_HOLD_SECONDS)

    rclpy.init()
    restarted = create_graph_node("graph_peer_alpha", "/graph_acceptance")
    subscription = restarted.create_subscription(UInt32, GRAPH_REMOTE_A, lambda _: None, 10)
    try:
        hold_graph_phase(restarted, "restart")
        restarted.destroy_subscription(subscription)
    finally:
        restarted.destroy_node()
        if rclpy.ok():
            rclpy.shutdown()

    print(
        "ROS2_ZEPHYR_PEER_GRAPH_PHASE direction=inbound phase=restart_lost",
        flush=True,
    )
    time.sleep(GRAPH_HOLD_SECONDS)
    print("ROS2_ZEPHYR_PEER_PASS role=graph-inbound")
    return 0


def run_publisher(
    node, timeout: float, reliability: str, durability: str, depth: int
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
    if not wait_for_subscriber(node, publisher, timeout):
        print("ROS2_ZEPHYR_PEER_ERROR role=pub reason=match_timeout", file=sys.stderr)
        return 1

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
    args = parser.parse_args()
    if args.depth <= 0:
        parser.error("--depth must be a positive integer")

    if args.role == "graph-inbound":
        return run_graph_inbound(args.timeout)

    rclpy.init()
    node = rclpy.create_node(f"ros2_zephyr_wifi_peer_{args.role}")
    try:
        if args.role == "pub":
            return run_publisher(
                node, args.timeout, args.reliability, args.durability, args.depth
            )
        if args.role == "sub":
            return run_subscriber(
                node, args.timeout, args.reliability, args.durability, args.depth
            )
        return run_graph_outbound(node, args.timeout)
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == "__main__":
    raise SystemExit(main())
