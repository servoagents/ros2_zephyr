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


def endpoint_qos(reliability: str) -> QoSProfile:
    return QoSProfile(
        depth=32,
        durability=DurabilityPolicy.VOLATILE,
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


def run_publisher(node, timeout: float, reliability: str) -> int:
    publisher = node.create_publisher(
        UInt32, "/ros2_zephyr/desktop_to_device", endpoint_qos(reliability)
    )
    started = time.monotonic()
    if not wait_for_subscriber(node, publisher, timeout):
        print("ROS2_ZEPHYR_PEER_ERROR role=pub reason=match_timeout", file=sys.stderr)
        return 1

    print(
        f"ROS2_ZEPHYR_PEER_MATCH role=pub discovery_ms="
        f"{(time.monotonic() - started) * 1000:.3f}"
    )
    message = UInt32(data=DESKTOP_TO_DEVICE_VALUE)
    sent = 0
    for rate_hz, sample_count in RATE_CASES:
        interval = 1.0 / rate_hz
        for sequence in range(1, sample_count + 1):
            publisher.publish(message)
            sent += 1
            print(
                "ROS2_ZEPHYR_PEER_SENT "
                f"value={message.data} rate_hz={rate_hz} sequence={sequence}"
            )
            rclpy.spin_once(node, timeout_sec=interval)

    recovery_samples = 0
    completion_deadline = time.monotonic() + 10.0
    while (
        publisher.get_subscription_count() > 0
        and time.monotonic() < completion_deadline
    ):
        if reliability == "best_effort" and recovery_samples < MAX_RECOVERY_SAMPLES:
            publisher.publish(message)
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
        f"ROS2_ZEPHYR_PEER_PASS role=pub reliability={reliability} scheduled={sent} "
        f"recovery={recovery_samples}"
    )
    return 0


def run_subscriber(node, timeout: float, reliability: str) -> int:
    received_at: list[float] = []
    invalid_value = False

    def receive(message: UInt32) -> None:
        nonlocal invalid_value
        received_at.append(time.monotonic())
        invalid_value |= message.data != DEVICE_TO_DESKTOP_VALUE
        print(
            "ROS2_ZEPHYR_PEER_RECEIVED "
            f"value={message.data} sequence={len(received_at)}"
        )

    subscription = node.create_subscription(
        UInt32, "/ros2_zephyr/device_to_desktop", receive, endpoint_qos(reliability)
    )
    deadline = time.monotonic() + timeout
    while len(received_at) < EXPECTED_SAMPLES and time.monotonic() < deadline:
        rclpy.spin_once(node, timeout_sec=0.1)

    node.destroy_subscription(subscription)
    if len(received_at) != EXPECTED_SAMPLES or invalid_value:
        print(
            "ROS2_ZEPHYR_PEER_ERROR "
            f"role=sub samples={len(received_at)} invalid={int(invalid_value)}",
            file=sys.stderr,
        )
        return 1

    intervals_ms = [
        (current - previous) * 1000
        for previous, current in zip(received_at, received_at[1:])
    ]
    intervals = ",".join(f"{interval:.3f}" for interval in intervals_ms)
    print(
        f"ROS2_ZEPHYR_PEER_PASS role=sub reliability={reliability} samples={len(received_at)} "
        f"arrival_intervals_ms={intervals}"
    )
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("role", choices=("pub", "sub"), help="desktop peer role")
    parser.add_argument("--timeout", type=float, default=90.0)
    parser.add_argument(
        "--reliability", choices=("best_effort", "reliable"), default="best_effort"
    )
    args = parser.parse_args()

    rclpy.init()
    node = rclpy.create_node(f"ros2_zephyr_wifi_peer_{args.role}")
    try:
        if args.role == "pub":
            return run_publisher(node, args.timeout, args.reliability)
        return run_subscriber(node, args.timeout, args.reliability)
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == "__main__":
    raise SystemExit(main())
