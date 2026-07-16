#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path
from typing import Any


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Compare a DXMT D3D12 snapshot with a Windows/WARP reference."
    )
    parser.add_argument("reference", type=Path)
    parser.add_argument("candidate", type=Path)
    return parser.parse_args()


def load_snapshot(path: Path) -> dict[str, Any]:
    with path.open(encoding="utf-8") as snapshot_file:
        snapshot = json.load(snapshot_file)
    if snapshot.get("schema_version") != 1:
        raise ValueError(f"{path}: unsupported schema_version")
    if snapshot.get("suite") != "d3d12-core-differential":
        raise ValueError(f"{path}: unexpected suite")
    cases = snapshot.get("cases")
    if not isinstance(cases, dict) or not cases:
        raise ValueError(f"{path}: cases must be a non-empty object")
    return snapshot


def compare_float_arrays(
    name: str, reference: dict[str, Any], candidate: dict[str, Any]
) -> list[str]:
    expected = reference.get("values")
    actual = candidate.get("values")
    if not isinstance(expected, list) or not isinstance(actual, list):
        return [f"{name}: float-array values must be arrays"]
    if len(expected) != len(actual):
        return [f"{name}: length {len(actual)} != {len(expected)}"]
    tolerance = float(reference.get("abs_tolerance", 0.0))
    errors: list[str] = []
    for index, (expected_value, actual_value) in enumerate(zip(expected, actual)):
        if not isinstance(expected_value, (int, float)) or not isinstance(
            actual_value, (int, float)
        ):
            errors.append(f"{name}[{index}]: values must be numeric")
        elif not math.isclose(
            float(expected_value), float(actual_value), rel_tol=0.0, abs_tol=tolerance
        ):
            errors.append(
                f"{name}[{index}]: {actual_value} != {expected_value} "
                f"(abs tolerance {tolerance})"
            )
    return errors


def compare_case(
    name: str, reference: dict[str, Any], candidate: dict[str, Any]
) -> list[str]:
    kind = reference.get("kind")
    if candidate.get("kind") != kind:
        return [f"{name}: kind {candidate.get('kind')} != {kind}"]
    if kind == "float-array":
        return compare_float_arrays(name, reference, candidate)
    if kind in {"u32-array", "scalar"}:
        expected = reference.get("values", reference.get("value"))
        actual = candidate.get("values", candidate.get("value"))
        return [] if actual == expected else [f"{name}: {actual!r} != {expected!r}"]
    return [f"{name}: unsupported case kind {kind!r}"]


def compare_snapshots(
    reference: dict[str, Any], candidate: dict[str, Any]
) -> list[str]:
    reference_cases = reference["cases"]
    candidate_cases = candidate["cases"]
    errors: list[str] = []
    missing = sorted(set(reference_cases) - set(candidate_cases))
    unexpected = sorted(set(candidate_cases) - set(reference_cases))
    if missing:
        errors.append(f"missing cases: {', '.join(missing)}")
    if unexpected:
        errors.append(f"unexpected cases: {', '.join(unexpected)}")
    for name in sorted(set(reference_cases) & set(candidate_cases)):
        errors.extend(compare_case(name, reference_cases[name], candidate_cases[name]))
    return errors


def main() -> int:
    args = parse_args()
    try:
        reference = load_snapshot(args.reference)
        candidate = load_snapshot(args.candidate)
        errors = compare_snapshots(reference, candidate)
    except (OSError, ValueError, TypeError, json.JSONDecodeError) as error:
        print(f"differential snapshot validation failed: {error}", file=sys.stderr)
        return 2
    if errors:
        for error in errors:
            print(f"differential mismatch: {error}", file=sys.stderr)
        return 1
    print(f"[PASS] {len(reference['cases'])} D3D12 differential cases match WARP")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
