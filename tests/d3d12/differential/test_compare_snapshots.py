from __future__ import annotations

import importlib.util
import unittest
from pathlib import Path


MODULE_PATH = Path(__file__).with_name("compare_snapshots.py")
SPEC = importlib.util.spec_from_file_location("compare_snapshots", MODULE_PATH)
assert SPEC and SPEC.loader
COMPARE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(COMPARE)


def snapshot(cases: dict) -> dict:
    return {
        "schema_version": 1,
        "suite": "d3d12-core-differential",
        "adapter": {},
        "cases": cases,
    }


class SnapshotComparisonTest(unittest.TestCase):
    def test_ignores_adapter_metadata_and_hash(self) -> None:
        reference = snapshot(
            {"copy": {"kind": "u32-array", "values": [1, 2], "hash": "a"}}
        )
        candidate = snapshot(
            {"copy": {"kind": "u32-array", "values": [1, 2], "hash": "b"}}
        )
        self.assertEqual(COMPARE.compare_snapshots(reference, candidate), [])

    def test_reports_missing_and_changed_cases(self) -> None:
        reference = snapshot(
            {
                "copy": {"kind": "u32-array", "values": [1, 2]},
                "clear": {"kind": "scalar", "value": 7},
            }
        )
        candidate = snapshot(
            {"copy": {"kind": "u32-array", "values": [1, 3]}}
        )
        errors = COMPARE.compare_snapshots(reference, candidate)
        self.assertIn("missing cases: clear", errors)
        self.assertTrue(any(error.startswith("copy:") for error in errors))

    def test_applies_reference_float_tolerance(self) -> None:
        reference = snapshot(
            {
                "float": {
                    "kind": "float-array",
                    "values": [0.25, 0.5],
                    "abs_tolerance": 0.001,
                }
            }
        )
        candidate = snapshot(
            {"float": {"kind": "float-array", "values": [0.2505, 0.4995]}}
        )
        self.assertEqual(COMPARE.compare_snapshots(reference, candidate), [])


if __name__ == "__main__":
    unittest.main()
