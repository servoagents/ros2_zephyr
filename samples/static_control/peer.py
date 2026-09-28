#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Exercise the static-control command/state contract with stock ROS 2."""

import argparse
import time

import rclpy
from rclpy.qos import DurabilityPolicy, QoSProfile, ReliabilityPolicy
from ros2_zephyr_test_msgs.msg import ControlCommand, ControlState


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--timeout", type=float, default=60.0)
    parser.add_argument("--sequence", type=int, default=1)
    args = parser.parse_args()

    rclpy.init()
    node = rclpy.create_node("static_control_stock_peer")
    qos = QoSProfile(
        depth=10,
        reliability=ReliabilityPolicy.BEST_EFFORT,
        durability=DurabilityPolicy.VOLATILE,
    )
    states = []
    subscription = node.create_subscription(
        ControlState, "/static_control/state", states.append, qos
    )
    publisher = node.create_publisher(ControlCommand, "/static_control/command", qos)

    def wait_for(predicate, label: str) -> bool:
        deadline = time.monotonic() + args.timeout
        while time.monotonic() < deadline:
            rclpy.spin_once(node, timeout_sec=0.1)
            if predicate():
                return True
        print(f"STATIC_CONTROL_PEER_ERROR waiting_for={label}", flush=True)
        return False

    try:
        if not wait_for(
            lambda: publisher.get_subscription_count() > 0
            and any(state.status == ControlState.STATUS_DISABLED for state in states),
            "discovery_and_disabled_state",
        ):
            return 1
        print("STATIC_CONTROL_PEER_DISABLED", flush=True)

        publisher.publish(ControlCommand(target=0.5, sequence=args.sequence))
        if not wait_for(
            lambda: any(
                state.status == ControlState.STATUS_ACTIVE
                and state.command_sequence == args.sequence
                for state in states
            ),
            "active_state",
        ):
            return 1
        print(f"STATIC_CONTROL_PEER_ACTIVE sequence={args.sequence}", flush=True)

        if not wait_for(
            lambda: any(
                state.status == ControlState.STATUS_STALE
                and state.command_sequence == args.sequence
                for state in states
            ),
            "stale_state",
        ):
            return 1
        print(f"STATIC_CONTROL_PEER_STALE sequence={args.sequence}", flush=True)
        if any(state.status == ControlState.STATUS_FAULT for state in states):
            print("STATIC_CONTROL_PEER_ERROR fault_state", flush=True)
            return 1
        print(f"STATIC_CONTROL_PEER_PASS state_samples={len(states)}", flush=True)
        return 0
    finally:
        node.destroy_publisher(publisher)
        node.destroy_subscription(subscription)
        node.destroy_node()
        rclpy.shutdown()


if __name__ == "__main__":
    raise SystemExit(main())
