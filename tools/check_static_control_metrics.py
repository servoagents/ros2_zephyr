#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Check static-control steady-state allocation metrics from a runtime log."""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path
from typing import NoReturn


class MetricsError(Exception):
    pass


def reject(message: str) -> NoReturn:
    raise MetricsError(message)


def parse_metrics(text: str) -> list[dict[str, int]]:
    records: list[dict[str, int]] = []
    for line in text.splitlines():
        if "STATIC_CONTROL_METRICS" not in line:
            continue
        values = {
            key: int(value)
            for key, value in re.findall(r"([A-Za-z_][A-Za-z0-9_]*)=([0-9]+)", line)
        }
        for required in ("ros_steady_alloc_calls", "middleware_steady_alloc_calls"):
            if required not in values:
                reject(f"metrics marker is missing {required}")
        records.append(values)
    return records


def check_metrics(
    records: list[dict[str, int]], minimum_samples: int, maximum_middleware_delta: int
) -> dict[str, int]:
    if len(records) < minimum_samples:
        reject(f"expected at least {minimum_samples} metrics samples, found {len(records)}")
    if any(record["ros_steady_alloc_calls"] != 0 for record in records):
        reject("ROS steady-state allocation attempts must remain zero")

    middleware = [record["middleware_steady_alloc_calls"] for record in records]
    deltas = [later - earlier for earlier, later in zip(middleware, middleware[1:])]
    if any(delta < 0 for delta in deltas):
        reject("middleware allocation counter decreased between samples")
    maximum_observed = max(deltas, default=middleware[0])
    if maximum_observed > maximum_middleware_delta:
        reject(
            "middleware allocation delta exceeds bound: "
            f"observed={maximum_observed} allowed={maximum_middleware_delta}"
        )
    return {
        "samples": len(records),
        "maximum_middleware_delta": maximum_observed,
        "final_middleware_calls": middleware[-1],
    }


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    parser.add_argument("--minimum-samples", type=int, default=2)
    parser.add_argument("--maximum-middleware-delta", type=int, default=32)
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    try:
        if args.minimum_samples < 1 or args.maximum_middleware_delta < 0:
            reject("metric bounds must be non-negative and require at least one sample")
        try:
            text = args.log.read_text(encoding="utf-8", errors="replace")
        except OSError as error:
            reject(f"cannot read {args.log}: {error}")
        result = check_metrics(
            parse_metrics(text), args.minimum_samples, args.maximum_middleware_delta
        )
    except MetricsError as error:
        print(f"E_STATIC_CONTROL_METRICS: {error}", file=sys.stderr)
        return 2
    print(
        "ROS2_ZEPHYR_STATIC_CONTROL_METRICS_PASS "
        f"samples={result['samples']} "
        f"max_middleware_delta={result['maximum_middleware_delta']} "
        f"final_middleware_calls={result['final_middleware_calls']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
