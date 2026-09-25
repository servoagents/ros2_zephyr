# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations

import importlib.util
import sys
import unittest
from pathlib import Path


REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "check_static_control_metrics",
    REPOSITORY_ROOT / "tools" / "check_static_control_metrics.py",
)
assert SPEC is not None and SPEC.loader is not None
metrics = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = metrics
SPEC.loader.exec_module(metrics)


class StaticControlMetricsTest(unittest.TestCase):
    def test_optimized_allocation_rate_passes(self) -> None:
        records = metrics.parse_metrics(
            """STATIC_CONTROL_METRICS ros_steady_alloc_calls=0 middleware_steady_alloc_calls=34
STATIC_CONTROL_METRICS ros_steady_alloc_calls=0 middleware_steady_alloc_calls=50
STATIC_CONTROL_METRICS ros_steady_alloc_calls=0 middleware_steady_alloc_calls=68
"""
        )
        result = metrics.check_metrics(records, 2, 32)
        self.assertEqual(result["maximum_middleware_delta"], 18)

    def test_preoptimization_allocation_rate_fails(self) -> None:
        records = metrics.parse_metrics(
            """STATIC_CONTROL_METRICS ros_steady_alloc_calls=0 middleware_steady_alloc_calls=100
STATIC_CONTROL_METRICS ros_steady_alloc_calls=0 middleware_steady_alloc_calls=184
"""
        )
        with self.assertRaises(metrics.MetricsError):
            metrics.check_metrics(records, 2, 32)

    def test_ros_steady_state_allocation_is_rejected(self) -> None:
        records = metrics.parse_metrics(
            """STATIC_CONTROL_METRICS ros_steady_alloc_calls=1 middleware_steady_alloc_calls=20
STATIC_CONTROL_METRICS ros_steady_alloc_calls=1 middleware_steady_alloc_calls=40
"""
        )
        with self.assertRaises(metrics.MetricsError):
            metrics.check_metrics(records, 2, 32)


if __name__ == "__main__":
    unittest.main()
