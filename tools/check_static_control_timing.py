#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Compare matched optional static-control timing windows on physical hardware."""

import argparse
import re
from pathlib import Path


FIELDS = (
    "samples",
    "release_p50_us",
    "release_p95_us",
    "release_p99_us",
    "release_max_us",
    "completion_p50_us",
    "completion_p95_us",
    "completion_p99_us",
    "completion_max_us",
)
PATTERN = re.compile(
    r"STATIC_CONTROL_TIMING " + r" ".join(rf"{field}=(\d+)" for field in FIELDS)
)
TIMING_GATES = (
    "release_p99_us",
    "release_max_us",
    "completion_p99_us",
    "completion_max_us",
)


def final_record(path: Path, minimum_records: int) -> dict[str, int]:
    matches = PATTERN.findall(path.read_text(errors="replace"))
    if len(matches) < minimum_records:
        raise ValueError(f"{path}: only {len(matches)} timing records; need {minimum_records}")
    records = [dict(zip(FIELDS, map(int, values))) for values in matches]
    if any(next_record["samples"] <= record["samples"]
           for record, next_record in zip(records, records[1:])):
        raise ValueError(f"{path}: sample count did not increase")
    record = records[minimum_records - 1]
    if record["samples"] < 50 * minimum_records or record["release_max_us"] == 0:
        raise ValueError(f"{path}: cycle counter did not produce useful release measurements")
    for name in ("release", "completion"):
        if not (record[f"{name}_p50_us"] <= record[f"{name}_p95_us"]
                <= record[f"{name}_p99_us"]):
            raise ValueError(f"{path}: invalid {name} percentile order")
    return record


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--posix", type=Path, nargs="+", required=True)
    parser.add_argument("--native", type=Path, nargs="+", required=True)
    parser.add_argument("--minimum-records", type=int, default=12)
    parser.add_argument("--maximum-regression-us", type=int, default=1000)
    args = parser.parse_args()
    if len(args.posix) != len(args.native) or args.minimum_records < 1:
        parser.error("provide equal, nonzero counts of matched runs")

    try:
        for index, (posix_path, native_path) in enumerate(zip(args.posix, args.native), 1):
            posix = final_record(posix_path, args.minimum_records)
            native = final_record(native_path, args.minimum_records)
            for field in TIMING_GATES:
                if native[field] > posix[field] + args.maximum_regression_us:
                    raise ValueError(
                        f"run {index}: {field} grew from {posix[field]} to {native[field]} us"
                    )
            print(
                f"STATIC_CONTROL_TIMING_PASS run={index} "
                f"posix_samples={posix['samples']} native_samples={native['samples']} "
                + " ".join(
                    f"{field}={posix[field]}/{native[field]}" for field in TIMING_GATES
                )
            )
    except (OSError, ValueError) as error:
        parser.exit(1, f"STATIC_CONTROL_TIMING_ERROR {error}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
