# SPDX-License-Identifier: Apache-2.0

import importlib.util
import tempfile
import unittest
from pathlib import Path


SPEC = importlib.util.spec_from_file_location(
    "check_static_control_timing",
    Path(__file__).resolve().parents[1] / "tools" / "check_static_control_timing.py",
)
assert SPEC is not None and SPEC.loader is not None
timing = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(timing)


def record(samples: int, release_max: int = 40) -> str:
    return (
        f"STATIC_CONTROL_TIMING samples={samples} "
        "release_p50_us=100 release_p95_us=100 release_p99_us=100 "
        f"release_max_us={release_max} "
        "completion_p50_us=100 completion_p95_us=100 "
        "completion_p99_us=100 completion_max_us=0\n"
    )


class StaticControlTimingTest(unittest.TestCase):
    def test_twelve_increasing_hardware_records(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "run.log"
            path.write_text("".join(record(100 * index) for index in range(1, 13)))
            result = timing.final_record(path, 12)
            self.assertEqual(result["samples"], 1200)
            self.assertEqual(result["release_max_us"], 40)

    def test_rejects_virtual_counter_with_zero_release_maximum(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "run.log"
            path.write_text("".join(record(100 * index, 0) for index in range(1, 13)))
            with self.assertRaisesRegex(ValueError, "cycle counter"):
                timing.final_record(path, 12)

    def test_rejects_missing_records(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "run.log"
            path.write_text(record(100))
            with self.assertRaisesRegex(ValueError, "only 1 timing records"):
                timing.final_record(path, 12)


if __name__ == "__main__":
    unittest.main()
