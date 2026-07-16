#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import re
import shlex
import subprocess
import sys
from pathlib import Path
from typing import Any


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run exact, reversible D3D12 source mutations."
    )
    parser.add_argument(
        "--manifest",
        type=Path,
        default=Path("tests/mutation/d3d12_mutations.json"),
    )
    parser.add_argument("--repo-root", type=Path, default=Path.cwd())
    parser.add_argument(
        "--test-command",
        default=(
            "scripts/dxmt-builder test --profile gcc-x64-release-full unit "
            "--suite d3d12 --test-args=--gtest_filter={filter}"
        ),
        help="command template containing {filter}",
    )
    parser.add_argument("--minimum-score", type=float, default=100.0)
    parser.add_argument("--validate-only", action="store_true")
    return parser.parse_args()


def load_manifest(path: Path) -> list[dict[str, Any]]:
    with path.open(encoding="utf-8") as manifest_file:
        document = json.load(manifest_file)
    if document.get("schema_version") != 1:
        raise ValueError("unsupported mutation manifest schema")
    mutations = document.get("mutations")
    if not isinstance(mutations, list) or not mutations:
        raise ValueError("mutation manifest must contain mutations")
    return mutations


def mutated_text(original: str, mutation: dict[str, Any]) -> str:
    find = mutation.get("find")
    replacement = mutation.get("replace")
    occurrence = int(mutation.get("occurrence", 0))
    if not isinstance(find, str) or not find:
        raise ValueError(f"{mutation.get('id')}: find must be non-empty")
    if not isinstance(replacement, str) or replacement == find:
        raise ValueError(f"{mutation.get('id')}: replacement must differ")
    offsets = [match.start() for match in re.finditer(re.escape(find), original)]
    if occurrence < 0 or occurrence >= len(offsets):
        raise ValueError(
            f"{mutation.get('id')}: occurrence {occurrence} is unavailable "
            f"({len(offsets)} matches)"
        )
    offset = offsets[occurrence]
    return original[:offset] + replacement + original[offset + len(find) :]


def require_clean_targets(repo_root: Path, paths: list[Path]) -> None:
    result = subprocess.run(
        ["git", "diff", "--quiet", "--", *(str(path) for path in paths)],
        cwd=repo_root,
        check=False,
    )
    if result.returncode != 0:
        raise ValueError("mutation target files have uncommitted changes")


def run_command(command_template: str, test_filter: str, repo_root: Path) -> tuple[int, str]:
    command = shlex.split(command_template.format(filter=test_filter))
    process = subprocess.run(
        command,
        cwd=repo_root,
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    return process.returncode, process.stdout


def main() -> int:
    args = parse_args()
    try:
        repo_root = args.repo_root.resolve()
        manifest_path = args.manifest
        if not manifest_path.is_absolute():
            manifest_path = repo_root / manifest_path
        mutations = load_manifest(manifest_path)
        target_paths = sorted({Path(mutation["file"]) for mutation in mutations})
        require_clean_targets(repo_root, target_paths)
        originals = {
            path: (repo_root / path).read_text(encoding="utf-8")
            for path in target_paths
        }
        for mutation in mutations:
            path = Path(mutation["file"])
            mutated_text(originals[path], mutation)
            if not mutation.get("test_filter"):
                raise ValueError(f"{mutation.get('id')}: test_filter is required")
        print(f"[PASS] validated {len(mutations)} exact mutation definitions")
        if args.validate_only:
            return 0

        baseline_filter = ":".join(
            sorted({str(mutation["test_filter"]) for mutation in mutations})
        )
        baseline_status, baseline_output = run_command(
            args.test_command, baseline_filter, repo_root
        )
        if baseline_status != 0:
            print(baseline_output, file=sys.stderr)
            raise ValueError("baseline mutation-target tests failed")

        killed = 0
        failure_pattern = re.compile(r"Fail:\s+[1-9][0-9]*")
        for mutation in mutations:
            path = Path(mutation["file"])
            target = repo_root / path
            target.write_text(mutated_text(originals[path], mutation), encoding="utf-8")
            try:
                status, output = run_command(
                    args.test_command, str(mutation["test_filter"]), repo_root
                )
            finally:
                target.write_text(originals[path], encoding="utf-8")
            mutation_id = str(mutation["id"])
            if status == 0:
                print(f"[SURVIVED] {mutation_id}")
                continue
            if not failure_pattern.search(output):
                print(output, file=sys.stderr)
                raise ValueError(
                    f"{mutation_id}: command failed without a test failure; "
                    "mutation may not compile"
                )
            killed += 1
            print(f"[KILLED] {mutation_id}")
        score = 100.0 * killed / len(mutations)
        print(
            f"mutation score: {killed}/{len(mutations)} ({score:.2f}%, "
            f"minimum {args.minimum_score:.2f}%)"
        )
        return 0 if score >= args.minimum_score else 1
    except (OSError, ValueError, KeyError, TypeError, json.JSONDecodeError) as error:
        print(f"mutation runner failed: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
