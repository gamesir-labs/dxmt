#!/usr/bin/env python3
"""Run each D3D12 fault scenario in a fresh Wine process."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--profile", default="gcc-x64-release-full")
    parser.add_argument(
        "--manifest", type=Path,
        default=Path("tests/fault_injection/d3d12_faults.json"),
    )
    parser.add_argument("--timeout", type=float, default=120.0)
    parser.add_argument("--scenario", action="append", default=[])
    return parser.parse_args()


def load_manifest(path: Path) -> list[dict[str, object]]:
    data = json.loads(path.read_text(encoding="utf-8"))
    if data.get("schema_version") != 1 or not isinstance(data.get("scenarios"), list):
        raise ValueError("unsupported D3D12 fault-injection manifest")
    seen: set[str] = set()
    for scenario in data["scenarios"]:
        if not isinstance(scenario, dict):
            raise ValueError("each fault scenario must be an object")
        scenario_id = scenario.get("id")
        test_filter = scenario.get("filter")
        environment = scenario.get("environment")
        if not isinstance(scenario_id, str) or not scenario_id or scenario_id in seen:
            raise ValueError(f"invalid or duplicate scenario id: {scenario_id!r}")
        if not isinstance(test_filter, str) or not test_filter:
            raise ValueError(f"scenario {scenario_id} has no test filter")
        if not isinstance(environment, dict) or not all(
            isinstance(key, str) and isinstance(value, str)
            for key, value in environment.items()
        ):
            raise ValueError(f"scenario {scenario_id} has an invalid environment")
        seen.add(scenario_id)
    return data["scenarios"]


def meson_options(build_dir: Path) -> dict[str, object]:
    completed = subprocess.run(
        ["meson", "introspect", str(build_dir), "--buildoptions"],
        check=True, text=True, capture_output=True,
    )
    return {item["name"]: item["value"] for item in json.loads(completed.stdout)}


def main() -> int:
    args = parse_args()
    repo = Path(__file__).resolve().parent.parent
    manifest_path = args.manifest
    if not manifest_path.is_absolute():
        manifest_path = repo / manifest_path
    scenarios = load_manifest(manifest_path)
    requested = set(args.scenario)
    known = {str(item["id"]) for item in scenarios}
    unknown = requested - known
    if unknown:
        raise ValueError(f"unknown fault scenarios: {', '.join(sorted(unknown))}")
    if requested:
        scenarios = [item for item in scenarios if item["id"] in requested]

    profile_root = repo / ".cache" / "managed" / "profiles" / args.profile
    build_dir = profile_root / "build"
    if not (build_dir / "meson-private" / "coredata.dat").is_file():
        raise FileNotFoundError(f"profile is not configured: {args.profile}")

    stage = subprocess.run(
        [str(repo / "scripts" / "stage-wine-test-runtime.sh"),
         str(build_dir), "", "d3d12", "unit"],
        check=True, text=True, capture_output=True,
    )
    if stage.stderr:
        sys.stderr.write(stage.stderr)
    runtime_root = Path(stage.stdout.strip())
    options = meson_options(build_dir)
    wine_root = Path(str(options.get("wine_install_path", "")))
    if not wine_root.is_absolute():
        wine_root = (repo / wine_root).resolve()
    executable = build_dir / "tests" / "dxmt-wine-d3d12-tests.exe"

    failures: list[str] = []
    for scenario in scenarios:
        scenario_id = str(scenario["id"])
        print(f"[ RUN      ] fault/{scenario_id}", flush=True)
        environment = os.environ.copy()
        environment.update({
            "DXMT_TEST_RUNTIME_ROOT": str(runtime_root),
            "DXMT_TEST_WINEPREFIX": str(profile_root / "prefix"),
            "DXMT_TEST_WINE_ROOT": str(wine_root),
            "DXMT_TEST_REQUIRE_RUNTIME": "1",
        })
        environment.update(scenario["environment"])
        try:
            completed = subprocess.run(
                [str(repo / "scripts" / "wine-test-wrapper.sh"),
                 str(executable), "--dxmt-test-worker", "--gtest_brief=1",
                 f"--gtest_filter={scenario['filter']}"],
                cwd=repo, env=environment, text=True, capture_output=True,
                timeout=args.timeout,
            )
        except subprocess.TimeoutExpired as error:
            failures.append(scenario_id)
            print(f"[  TIMEOUT ] fault/{scenario_id} ({args.timeout:.0f}s)")
            if error.stdout:
                print(error.stdout)
            if error.stderr:
                print(error.stderr, file=sys.stderr)
            continue
        if completed.returncode:
            failures.append(scenario_id)
            print(f"[  FAILED  ] fault/{scenario_id} (exit {completed.returncode})")
            sys.stdout.write(completed.stdout)
            sys.stderr.write(completed.stderr)
        else:
            print(f"[       OK ] fault/{scenario_id}")

    if failures:
        print(f"D3D12 fault injection failed: {', '.join(failures)}", file=sys.stderr)
        return 1
    print(f"D3D12 fault injection passed: {len(scenarios)} scenarios")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (FileNotFoundError, ValueError, json.JSONDecodeError) as error:
        print(f"fault-injection configuration error: {error}", file=sys.stderr)
        raise SystemExit(2)
