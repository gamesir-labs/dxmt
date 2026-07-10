#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any


TIME_UNIT_TO_NS = {
    "ns": 1.0,
    "us": 1_000.0,
    "ms": 1_000_000.0,
    "s": 1_000_000_000.0,
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run one DXMT benchmark suite and enforce its performance budget."
    )
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--budget", required=True, type=Path)
    parser.add_argument("--repetitions", type=int, default=5)
    parser.add_argument("--min-time", default="0.05s")
    parser.add_argument("--warmup-time", type=float, default=0.01)
    return parser.parse_args()


def load_budget(path: Path) -> dict[str, dict[str, Any]]:
    with path.open(encoding="utf-8") as budget_file:
        document = json.load(budget_file)

    if document.get("schema_version") != 1:
        raise ValueError(f"unsupported budget schema in {path}")

    budgets = document.get("benchmarks")
    if not isinstance(budgets, dict) or not budgets:
        raise ValueError(f"budget file has no benchmark entries: {path}")

    for name, budget in budgets.items():
        if not isinstance(budget, dict):
            raise ValueError(f"budget for {name} must be an object")
        if budget.get("metric", "cpu_time") not in {"cpu_time", "real_time"}:
            raise ValueError(f"budget for {name} has an unsupported metric")
        if not isinstance(budget.get("max_median_ns"), (int, float)):
            raise ValueError(f"budget for {name} must define max_median_ns")
        if budget["max_median_ns"] <= 0:
            raise ValueError(f"budget for {name} must be positive")

    return budgets


def canonical_name(result: dict[str, Any]) -> str:
    name = str(result.get("run_name", result["name"]))
    return re.sub(r"/repeats:\d+$", "", name)


def run_benchmark(args: argparse.Namespace, output_path: Path) -> int:
    command = [
        str(args.executable),
        f"--benchmark_out={output_path}",
        "--benchmark_out_format=json",
        "--benchmark_color=false",
        f"--benchmark_repetitions={args.repetitions}",
        "--benchmark_report_aggregates_only=true",
        "--benchmark_display_aggregates_only=true",
        f"--benchmark_min_time={args.min_time}",
        f"--benchmark_min_warmup_time={args.warmup_time}",
    ]
    return subprocess.run(command, check=False).returncode


def validate_results(
    result_path: Path, budgets: dict[str, dict[str, Any]]
) -> list[str]:
    with result_path.open(encoding="utf-8") as result_file:
        document = json.load(result_file)

    results = document.get("benchmarks")
    if not isinstance(results, list) or not results:
        return ["benchmark produced no result rows"]

    errors: list[str] = []
    medians: dict[str, dict[str, Any]] = {}

    for result in results:
        name = canonical_name(result)
        if result.get("error_occurred"):
            errors.append(f"{name}: {result.get('error_message', 'benchmark error')}")
        if int(result.get("threads", 1)) != 1:
            errors.append(f"{name}: multithreaded benchmarks are not allowed")
        if result.get("aggregate_name") == "median":
            medians[name] = result

    missing_budgets = sorted(set(medians) - set(budgets))
    stale_budgets = sorted(set(budgets) - set(medians))
    for name in missing_budgets:
        errors.append(f"{name}: no acceptance budget is defined")
    for name in stale_budgets:
        errors.append(f"{name}: budget has no matching benchmark result")

    for name in sorted(set(medians) & set(budgets)):
        result = medians[name]
        budget = budgets[name]
        metric = budget.get("metric", "cpu_time")
        unit = result.get("time_unit", "ns")
        multiplier = TIME_UNIT_TO_NS.get(unit)
        if multiplier is None:
            errors.append(f"{name}: unsupported time unit {unit}")
            continue

        measured_ns = float(result[metric]) * multiplier
        limit_ns = float(budget["max_median_ns"])
        status = "PASS" if measured_ns <= limit_ns else "FAIL"
        print(
            f"[{status}] {name}: median {metric}={measured_ns:.3f} ns "
            f"(limit {limit_ns:.3f} ns)"
        )
        if measured_ns > limit_ns:
            errors.append(
                f"{name}: median {metric} {measured_ns:.3f} ns exceeds "
                f"{limit_ns:.3f} ns"
            )

    return errors


def main() -> int:
    args = parse_args()
    try:
        budgets = load_budget(args.budget)
        with tempfile.TemporaryDirectory(prefix="dxmt-benchmark-") as temp_dir:
            result_path = Path(temp_dir) / "results.json"
            return_code = run_benchmark(args, result_path)
            if return_code != 0:
                print(
                    f"benchmark executable exited with status {return_code}",
                    file=sys.stderr,
                )
                return return_code

            errors = validate_results(result_path, budgets)
    except (OSError, ValueError, KeyError, TypeError, json.JSONDecodeError) as error:
        print(f"benchmark acceptance failed: {error}", file=sys.stderr)
        return 2

    if errors:
        for error in errors:
            print(f"benchmark acceptance failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
