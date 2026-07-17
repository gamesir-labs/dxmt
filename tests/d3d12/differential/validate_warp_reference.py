#!/usr/bin/env python3
"""Strictly validate a captured Windows WARP reference snapshot."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any


REQUIRED_CASES = {
    "barrier_copy_chain",
    "binary_occlusion_nonzero",
    "blend_additive_rgba8",
    "buffer_copy",
    "buffer_copy_offset",
    "clear_rect_rgba8",
    "clear_rgba8",
    "compute_u32",
    "cross_queue_fence_copy",
    "descriptor_overwrite_before_submit",
    "descriptor_table_compute",
    "depth_reject_rgba8",
    "draw_fullscreen_rgba8",
    "execute_indirect_dispatch",
    "invalid_descriptor_heap",
    "invalid_resource",
    "invalid_root_signature",
    "msaa_resolve_rgba8",
    "predicated_compute_equal_zero_execute",
    "predicated_compute_equal_zero_skip",
    "predicated_compute_false",
    "predicated_compute_true",
    "predication_disabled_compute",
    "root_constants_uav",
    "render_pass_flattened_equivalence",
    "texture_copy_rgba8",
    "timestamp_query_ordering",
    "unsupported_iid",
}
MICROSOFT_VENDOR_ID = 0x1414
INPUT_VALUES = [
    (0x10203040 + index * 0x01020408) & 0xFFFFFFFF for index in range(16)
]
COMPUTE_VALUES = [
    ((value ^ 0xA5A5A5A5) + index * 17) & 0xFFFFFFFF
    for index, value in enumerate(INPUT_VALUES)
]
PREDICATION_SENTINEL_VALUES = [
    0xC001D00D ^ (index * 0x01010101) for index in range(16)
]
RENDER_PASS_CLEAR_PIXEL = 0xFFFFFF00
EXACT_CASE_VALUES = {
    "binary_occlusion_nonzero": [1, 0],
    "cross_queue_fence_copy": INPUT_VALUES,
    "predicated_compute_equal_zero_execute": COMPUTE_VALUES,
    "predicated_compute_equal_zero_skip": PREDICATION_SENTINEL_VALUES,
    "predicated_compute_false": PREDICATION_SENTINEL_VALUES,
    "predication_disabled_compute": COMPUTE_VALUES,
    "render_pass_flattened_equivalence": [RENDER_PASS_CLEAR_PIXEL] * 32,
    "root_constants_uav": [0x01234567, 0x98BADCFE, 0x3175B9FD, 0x175B9FD3],
    "timestamp_query_ordering": [3, 1, 1, 1],
}
CASE_VALUE_COUNTS = {
    "predicated_compute_equal_zero_execute": 16,
    "predicated_compute_equal_zero_skip": 16,
    "render_pass_flattened_equivalence": 32,
    "timestamp_query_ordering": 4,
}


def fnv1a64(values: list[int]) -> str:
    value_hash = 1469598103934665603
    for value in values:
        for shift in range(0, 32, 8):
            value_hash ^= (value >> shift) & 0xFF
            value_hash = (value_hash * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return f"0x{value_hash:016x}"


def validate(snapshot: Any) -> list[str]:
    errors: list[str] = []
    if not isinstance(snapshot, dict):
        return ["snapshot root must be an object"]
    if snapshot.get("schema_version") != 1:
        errors.append("schema_version must be 1")
    if snapshot.get("suite") != "d3d12-core-differential":
        errors.append("suite must be d3d12-core-differential")

    adapter = snapshot.get("adapter")
    if not isinstance(adapter, dict):
        errors.append("adapter must be an object")
    else:
        if adapter.get("mode") != "warp":
            errors.append("adapter.mode must be warp")
        if adapter.get("vendor_id") != MICROSOFT_VENDOR_ID:
            errors.append(
                f"adapter.vendor_id must be Microsoft 0x{MICROSOFT_VENDOR_ID:04x}"
            )
        device_id = adapter.get("device_id")
        if not isinstance(device_id, int) or device_id < 0:
            errors.append("adapter.device_id must be a non-negative integer")

    cases = snapshot.get("cases")
    if not isinstance(cases, dict):
        errors.append("cases must be an object")
        return errors
    missing = sorted(REQUIRED_CASES - set(cases))
    unexpected = sorted(set(cases) - REQUIRED_CASES)
    if missing:
        errors.append(f"missing cases: {', '.join(missing)}")
    if unexpected:
        errors.append(f"unexpected cases: {', '.join(unexpected)}")

    for name, case in sorted(cases.items()):
        if not isinstance(case, dict):
            errors.append(f"{name}: case must be an object")
            continue
        if case.get("kind") != "u32-array":
            errors.append(f"{name}: kind must be u32-array")
            continue
        values = case.get("values")
        if not isinstance(values, list) or not values:
            errors.append(f"{name}: values must be a non-empty array")
            continue
        if any(
            not isinstance(value, int) or isinstance(value, bool)
            or value < 0 or value > 0xFFFFFFFF
            for value in values
        ):
            errors.append(f"{name}: values must contain only uint32 integers")
            continue
        expected_count = CASE_VALUE_COUNTS.get(name)
        if expected_count is not None and len(values) != expected_count:
            errors.append(
                f"{name}: values must contain exactly {expected_count} entries"
            )
        expected_hash = fnv1a64(values)
        if case.get("hash_fnv1a64") != expected_hash:
            errors.append(
                f"{name}: hash_fnv1a64 {case.get('hash_fnv1a64')!r} "
                f"!= {expected_hash}"
            )
        exact_values = EXACT_CASE_VALUES.get(name)
        if exact_values is not None and values != exact_values:
            errors.append(f"{name}: values do not match the required oracle contract")
        if name == "timestamp_query_ordering" and len(values) == 4:
            if values[0] != 3 or values[1:] != [1, 1, 1]:
                errors.append(
                    f"{name}: resolved timestamps must be transitively nondecreasing"
                )
        if name == "render_pass_flattened_equivalence" and len(values) == 32:
            if values[:16] != values[16:]:
                errors.append(
                    f"{name}: render-pass and flattened pixels must match"
                )
            if any(value != RENDER_PASS_CLEAR_PIXEL for value in values):
                errors.append(
                    f"{name}: both paths must produce the required clear color"
                )
    return errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("snapshot", type=Path)
    args = parser.parse_args()
    try:
        snapshot = json.loads(args.snapshot.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        print(f"WARP reference validation failed: {error}", file=sys.stderr)
        return 2
    errors = validate(snapshot)
    if errors:
        for error in errors:
            print(f"WARP reference validation failed: {error}", file=sys.stderr)
        return 1
    print(f"[PASS] validated WARP reference with {len(REQUIRED_CASES)} cases")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
