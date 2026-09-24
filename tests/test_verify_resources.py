# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations

import copy
import importlib.util
import json
import sys
import unittest
from pathlib import Path


REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "verify_resources", REPOSITORY_ROOT / "tools" / "verify_resources.py"
)
assert SPEC is not None and SPEC.loader is not None
resources = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = resources
SPEC.loader.exec_module(resources)


class ResourceVerificationTest(unittest.TestCase):
    def setUp(self) -> None:
        deployment = json.loads(
            (REPOSITORY_ROOT / "samples" / "static_control" / "deployment.json").read_text(
                encoding="utf-8"
            )
        )
        capability = json.loads(
            (REPOSITORY_ROOT / "capabilities" / "rmw_cyclonedds_c.json").read_text(
                encoding="utf-8"
            )
        )
        self.deployment_plan = {
            "schema_version": 2,
            "backend": "rmw_cyclonedds_c",
            "deployment": deployment,
            "capability": capability,
        }
        self.config = {
            "CONFIG_MAIN_STACK_SIZE": "4096",
            "CONFIG_IDLE_STACK_SIZE": "1024",
            "CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE": "2048",
            "CONFIG_HEAP_MEM_POOL_SIZE": "56000",
            "CONFIG_COMMON_LIBC_MALLOC_ARENA_SIZE": "-1",
            "CONFIG_ROS2_ZEPHYR_CYCLONEDDS_THREAD_COUNT": "5",
            "CONFIG_ROS2_ZEPHYR_CYCLONEDDS_THREAD_STACK_SIZE": "8192",
            "CONFIG_ROS2_ZEPHYR_GRAPH_MAX_LOCAL_NODES": "1",
            "CONFIG_ROS2_ZEPHYR_GRAPH_MAX_ENDPOINTS_PER_NODE": "2",
            "CONFIG_ROS2_ZEPHYR_GRAPH_CACHE_MAX_PARTICIPANTS": "3",
            "CONFIG_ROS2_ZEPHYR_GRAPH_CACHE_MAX_NODES": "6",
            "CONFIG_ROS2_ZEPHYR_GRAPH_CACHE_MAX_ENDPOINTS": "24",
        }
        self.target = "esp32s3_devkitc/esp32s3/procpu"

    def test_prelink_plan_separates_known_reservations(self) -> None:
        plan = resources.build_prelink_plan(self.deployment_plan, self.config, self.target)
        self.assertEqual(plan["status"], "PASS")
        self.assertEqual(plan["planned"]["known_ram_slices_bytes"], 120512)
        self.assertIsNone(plan["planned"]["heap_backing"][-1]["bytes"])

    def test_worker_contract_rejects_configuration_above_maximum(self) -> None:
        plan_input = copy.deepcopy(self.deployment_plan)
        plan_input["deployment"]["resources"]["workers"]["maximum"] = 4
        plan = resources.build_prelink_plan(plan_input, self.config, self.target)
        self.assertEqual(plan["status"], "FAIL")
        failed = resources.first_failed(plan)
        self.assertEqual(failed["code"], "E_RESOURCE_WORKERS")

    def test_postlink_does_not_add_runtime_high_water_to_linked_ram(self) -> None:
        plan = resources.build_prelink_plan(self.deployment_plan, self.config, self.target)
        linked = {
            "flash_bytes": 1015956,
            "flash_source": "zephyr.bin",
            "static_ram_bytes": 262120,
            "ram_source": "map region dram0_0_seg",
            "memory_regions": {
                "FLASH": {"origin": 0, "length": 33554176},
                "dram0_0_seg": {"origin": 0x3FC88000, "length": 399108},
            },
            "heap_backing": [],
            "thread_stacks": [],
            "elf_sections": {},
        }
        runtime = {
            "status": "measured",
            "records": [{"marker": "STATIC_CONTROL_METRICS", "values": {"middleware_high_water": 66702}}],
        }
        report = resources.build_postlink_report(plan, linked, runtime)
        self.assertEqual(report["status"], "PASS")
        ram_check = next(
            item for item in report["checks"] if item["id"] == "linked_ram_budget_with_reserve"
        )
        self.assertEqual(ram_check["required"], 278504)

    def test_postlink_rejects_flash_over_budget(self) -> None:
        plan = resources.build_prelink_plan(self.deployment_plan, self.config, self.target)
        linked = {
            "flash_bytes": 1100001,
            "flash_source": "zephyr.bin",
            "static_ram_bytes": 200000,
            "ram_source": "ELF",
            "memory_regions": {},
            "heap_backing": [],
            "thread_stacks": [],
            "elf_sections": {},
        }
        report = resources.build_postlink_report(plan, linked, {"status": "not_provided"})
        self.assertEqual(report["status"], "FAIL")
        failed = resources.first_failed(report)
        self.assertEqual(failed["code"], "E_RESOURCE_FLASH_BUDGET")

    def test_memory_regions_and_linked_stacks_are_parsed_from_map(self) -> None:
        map_text = """Memory Configuration

Name             Origin             Length             Attributes
FLASH            0x00000000         0x00100000         r
dram0_0_seg      0x3fc80000         0x00060000         rw

Linker script and memory map
.dram0.noinit
 .noinit.app.0
                0x3fc81000       0x800 app/libapp.a(control.c.obj)
                0x3fc81000                control_stack
"""
        regions = resources.parse_memory_regions(map_text)
        stacks = resources.parse_linked_stacks(map_text)
        self.assertEqual(regions["dram0_0_seg"]["length"], 0x60000)
        self.assertEqual(stacks[0]["name"], "control_stack")
        self.assertEqual(stacks[0]["bytes"], 0x800)

    def test_zero_address_input_sections_keep_stack_attribution_local(self) -> None:
        map_text = """.noinit.app.0
                0x00000000       0x800
 .noinit.app.0
                0x00000000       0x800 app/libapp.a(control.c.obj)
                0x00000000                control_stack

.noinit.kernel.0
                0x00000000      0x1000
 .noinit.kernel.0
                0x00000000      0x1000 zephyr/kernel/libkernel.a(init.c.obj)
                0x00000000                z_main_stack
"""
        stacks = resources.parse_linked_stacks(map_text)
        self.assertEqual(
            stacks,
            [
                {
                    "name": "control_stack",
                    "bytes": 0x800,
                    "source": "app/libapp.a(control.c.obj)",
                },
                {
                    "name": "z_main_stack",
                    "bytes": 0x1000,
                    "source": "zephyr/kernel/libkernel.a(init.c.obj)",
                },
            ],
        )


if __name__ == "__main__":
    unittest.main()
