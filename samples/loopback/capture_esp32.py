#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Reset an ESP32, stream its UART, and stop after an expected marker."""

from __future__ import annotations

import argparse
import sys
import time

import serial
from esp_pylib.serial_reset import hard_reset


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("device")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=30.0)
    parser.add_argument("--until", required=True)
    parser.add_argument("--minimum-matches", type=int, default=1)
    parser.add_argument(
        "--no-reset",
        action="store_true",
        help="open with DTR deasserted without pulsing EN",
    )
    parser.add_argument(
        "--reconnect",
        action="store_true",
        help="reopen the device after native USB disconnect/re-enumeration",
    )
    args = parser.parse_args()
    if args.minimum_matches < 1:
        parser.error("--minimum-matches must be positive")

    marker = args.until.encode()
    recent = bytearray()
    matches = 0
    deadline = time.monotonic() + args.timeout

    port = None
    reset_pending = not args.no_reset
    while time.monotonic() < deadline:
        try:
            if port is None:
                port = serial.Serial(baudrate=args.baud, timeout=0.2, exclusive=True)
                # Configure DTR before opening the port. This avoids an assertion
                # pulse when observing a run started by a power cycle.
                port.dtr = False
                port.port = args.device
                port.open()
                if reset_pending:
                    hard_reset(port)
                    reset_pending = False

            chunk = port.read(port.in_waiting or 1)
        except (OSError, serial.SerialException) as error:
            if port is not None:
                port.close()
                port = None
            if not args.reconnect:
                print(f"serial error on {args.device}: {error}", file=sys.stderr)
                return 1
            time.sleep(0.2)
            continue

        if not chunk:
            continue
        sys.stdout.buffer.write(chunk)
        sys.stdout.buffer.flush()
        recent.extend(chunk)
        while True:
            marker_index = recent.find(marker)
            if marker_index < 0:
                break
            line_end = recent.find(b"\n", marker_index + len(marker))
            if line_end < 0:
                break
            matches += 1
            del recent[: line_end + 1]
            if matches >= args.minimum_matches:
                if port is not None:
                    port.close()
                return 0
        if len(recent) > 4096:
            del recent[:-2048]

    if port is not None:
        port.close()

    print(
        f"serial timeout: observed {matches}/{args.minimum_matches} complete "
        f"{args.until!r} markers on {args.device}",
        file=sys.stderr,
    )
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
