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
    args = parser.parse_args()

    marker = args.until.encode()
    recent = bytearray()
    deadline = time.monotonic() + args.timeout

    with serial.Serial(args.device, args.baud, timeout=0.2, exclusive=True) as port:
        # Keep GPIO0 high while pulsing EN; pyserial otherwise opens with DTR
        # asserted and leaves a classic ESP32 in the ROM bootloader.
        port.dtr = False
        hard_reset(port)
        while time.monotonic() < deadline:
            chunk = port.read(port.in_waiting or 1)
            if not chunk:
                continue
            sys.stdout.buffer.write(chunk)
            sys.stdout.buffer.flush()
            recent.extend(chunk)
            if marker in recent:
                return 0
            if len(recent) > 4096:
                del recent[:-2048]

    print(
        f"serial timeout: did not observe {args.until!r} on {args.device}",
        file=sys.stderr,
    )
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
