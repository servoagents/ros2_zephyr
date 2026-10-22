# SPDX-License-Identifier: Apache-2.0
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from admit_esp32c6 import stack_budget
from verify_resources import ElfSection, region_usage


class C6StackAdmissionTest(unittest.TestCase):
    def test_main_error_path_requires_the_extra_backing(self):
        old = stack_budget(11136, 8704, 2576, 8, 99)
        current = stack_budget(11648, 8704, 2576, 8, 99)
        self.assertEqual(old["status"], "FAIL")
        self.assertEqual(current["status"], "PASS")
        self.assertEqual(current["remaining_after_budget_bytes"], 6)

    def test_tls_alignment_and_isr_are_distinct(self):
        task = stack_budget(2048, 1536, 2576, 8, 99)
        interrupt = stack_budget(2048, 1536, 2576, 8, 99, isr=True)
        self.assertEqual(task["status"], "FAIL")
        self.assertEqual(interrupt["status"], "PASS")
        self.assertEqual(interrupt["tls_upper_bytes"], 0)

    def test_invalid_alignment_is_rejected(self):
        with self.assertRaises(ValueError):
            stack_budget(4096, 1024, 100, 0, 99)


class C6PhysicalRamTest(unittest.TestCase):
    def test_physical_span_counts_code_gaps_and_backing_once(self):
        sections = [
            ElfSection(".text", 0x1000, 0x100, 1, 2),
            ElfSection(".data", 0x1200, 0x80, 1, 3),
            ElfSection(".bss", 0x1280, 0x80, 8, 3),
            ElfSection(".alias", 0x1280, 0x80, 8, 3),
            ElfSection(".debug", 0x1400, 0x100, 1, 0),
        ]
        self.assertEqual(
            region_usage(sections, {"origin": 0x1000, "length": 0x1000}), 0x300
        )
