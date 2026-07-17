from __future__ import annotations

import importlib.util
import unittest
from pathlib import Path


MODULE_PATH = Path(__file__).with_name("validate_warp_reference.py")
SPEC = importlib.util.spec_from_file_location("validate_warp_reference", MODULE_PATH)
assert SPEC and SPEC.loader
VALIDATOR = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(VALIDATOR)


def valid_snapshot() -> dict:
    cases = {}
    for index, name in enumerate(sorted(VALIDATOR.REQUIRED_CASES)):
        values = [index, index + 1]
        cases[name] = {
            "kind": "u32-array",
            "hash_fnv1a64": VALIDATOR.fnv1a64(values),
            "values": values,
        }
    return {
        "schema_version": 1,
        "suite": "d3d12-core-differential",
        "adapter": {
            "mode": "warp",
            "vendor_id": VALIDATOR.MICROSOFT_VENDOR_ID,
            "device_id": 1,
        },
        "cases": cases,
    }


class WarpReferenceValidationTest(unittest.TestCase):
    def test_requires_gpu_state_and_indirect_cases(self) -> None:
        self.assertTrue(
            {
                "blend_additive_rgba8",
                "depth_reject_rgba8",
                "execute_indirect_dispatch",
                "msaa_resolve_rgba8",
            }.issubset(VALIDATOR.REQUIRED_CASES)
        )

    def test_accepts_complete_warp_snapshot(self) -> None:
        self.assertEqual(VALIDATOR.validate(valid_snapshot()), [])

    def test_rejects_non_warp_adapter_and_case_drift(self) -> None:
        snapshot = valid_snapshot()
        snapshot["adapter"]["mode"] = "default"
        snapshot["cases"].pop("buffer_copy")
        errors = VALIDATOR.validate(snapshot)
        self.assertIn("adapter.mode must be warp", errors)
        self.assertIn("missing cases: buffer_copy", errors)

    def test_rejects_stale_hash_and_out_of_range_value(self) -> None:
        snapshot = valid_snapshot()
        snapshot["cases"]["buffer_copy"]["hash_fnv1a64"] = "0x0"
        snapshot["cases"]["compute_u32"]["values"] = [0x100000000]
        errors = VALIDATOR.validate(snapshot)
        self.assertTrue(any("buffer_copy: hash_fnv1a64" in error for error in errors))
        self.assertIn(
            "compute_u32: values must contain only uint32 integers", errors
        )


if __name__ == "__main__":
    unittest.main()
