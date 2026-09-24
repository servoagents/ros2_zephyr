# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations

import copy
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path


REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
COMPILER_PATH = REPOSITORY_ROOT / "tools" / "compile_deployment.py"
SPEC = importlib.util.spec_from_file_location("compile_deployment", COMPILER_PATH)
assert SPEC is not None and SPEC.loader is not None
compiler = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(compiler)


class DeploymentCompilerTest(unittest.TestCase):
    def setUp(self) -> None:
        self.deployment_path = REPOSITORY_ROOT / "samples" / "loopback" / "deployment.json"
        self.capabilities = REPOSITORY_ROOT / "capabilities"
        self.deployment = json.loads(self.deployment_path.read_text(encoding="utf-8"))

    def assert_rejected(self, deployment: dict[str, object], backend: str, code: str) -> None:
        validated = compiler.validate_deployment(deployment)
        capability = compiler.load_json(self.capabilities / f"{backend}.json")
        with self.assertRaises(compiler.DeploymentError) as caught:
            compiler.validate_capabilities(validated, capability, backend)
        self.assertEqual(caught.exception.code, code)

    def test_outputs_are_deterministic_for_both_backends(self) -> None:
        expected_names = {
            "generated_ros_init.c",
            "generated_ros_init.h",
            "generated_ros2_zephyr.conf",
            "generated_features.cmake",
            "deployment-plan.json",
            "deployment-report.md",
        }
        with tempfile.TemporaryDirectory() as first_root, tempfile.TemporaryDirectory() as second_root:
            for backend in ("rmw_cyclonedds_c", "rmw_zenoh_pico"):
                first = Path(first_root) / backend
                second = Path(second_root) / backend
                compiler.compile_deployment(self.deployment_path, backend, self.capabilities, first)
                compiler.compile_deployment(self.deployment_path, backend, self.capabilities, second)
                self.assertEqual({path.name for path in first.iterdir()}, expected_names)
                for name in expected_names:
                    self.assertEqual((first / name).read_bytes(), (second / name).read_bytes())
                generated_features = (first / "generated_features.cmake").read_text(encoding="utf-8")
                self.assertIn("set(ROS2_ZEPHYR_RCLC_ENABLE_ACTIONS OFF)", generated_features)
                self.assertIn("ros2_zephyr_test_msgs", generated_features)
                generated_config = (first / "generated_ros2_zephyr.conf").read_text(encoding="utf-8")
                if backend == "rmw_zenoh_pico":
                    self.assertIn("CONFIG_NET_TCP=y", generated_config)

    def test_generated_setup_uses_standard_ros_apis(self) -> None:
        source = compiler.generate_source(compiler.validate_deployment(self.deployment))
        self.assertIn("rclc_node_init_default", source)
        self.assertIn("rclc_publisher_init", source)
        self.assertIn("rclc_subscription_init", source)
        self.assertIn("rclc_executor_prepare", source)
        self.assertNotIn("#ifdef", source)
        self.assertNotIn("cyclonedds", source.lower())
        self.assertNotIn("zenoh", source.lower())

    def test_zenoh_rejects_transient_local(self) -> None:
        deployment = copy.deepcopy(self.deployment)
        deployment["endpoints"][0]["qos"]["durability"] = "transient_local"
        self.assert_rejected(deployment, "rmw_zenoh_pico", "E_QOS_UNSUPPORTED")

    def test_zenoh_rejects_graph_requirement(self) -> None:
        deployment = copy.deepcopy(self.deployment)
        deployment["graph"]["outbound"] = True
        self.assert_rejected(deployment, "rmw_zenoh_pico", "E_GRAPH_UNSUPPORTED")

    def test_unknown_field_is_rejected(self) -> None:
        deployment = copy.deepcopy(self.deployment)
        deployment["unexpected"] = True
        with self.assertRaises(compiler.DeploymentError) as caught:
            compiler.validate_deployment(deployment)
        self.assertEqual(caught.exception.code, "E_SCHEMA")

    def test_duplicate_endpoint_id_is_rejected(self) -> None:
        deployment = copy.deepcopy(self.deployment)
        deployment["endpoints"][1]["id"] = deployment["endpoints"][0]["id"]
        with self.assertRaises(compiler.DeploymentError) as caught:
            compiler.validate_deployment(deployment)
        self.assertEqual(caught.exception.code, "E_DUPLICATE_ENDPOINT")

    def test_unknown_backend_is_rejected(self) -> None:
        deployment = copy.deepcopy(self.deployment)
        deployment["rmw"] = ["rmw_unknown"]
        with self.assertRaises(compiler.DeploymentError) as caught:
            compiler.validate_deployment(deployment)
        self.assertEqual(caught.exception.code, "E_BACKEND_UNKNOWN")


if __name__ == "__main__":
    unittest.main()
