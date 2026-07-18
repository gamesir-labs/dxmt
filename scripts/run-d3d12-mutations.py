#!/usr/bin/env python3

from __future__ import annotations

import argparse
import fnmatch
import json
import re
import shlex
import subprocess
import sys
from pathlib import Path
from typing import Any


TEST_DECLARATION = re.compile(
    r"\b(?:TEST|TEST_F|TEST_P|DXMT_SERIAL_TEST_F)\s*\(\s*"
    r"([A-Za-z_][A-Za-z0-9_]*)\s*,\s*"
    r"([A-Za-z_][A-Za-z0-9_]*)\s*\)",
    re.MULTILINE,
)


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
    parser.add_argument(
        "--timeout",
        type=float,
        default=300.0,
        help="maximum seconds for the baseline and each mutation test command",
    )
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


def discover_declared_tests(repo_root: Path) -> set[str]:
    declared: set[str] = set()
    for path in repo_root.glob("tests/d3d12/**/*.cpp"):
        if not path.is_file():
            continue
        source = path.read_text(encoding="utf-8", errors="ignore")
        declared.update(
            f"{suite}.{test}"
            for suite, test in TEST_DECLARATION.findall(source)
        )
    if not declared:
        raise ValueError("no D3D12 test declarations were found")
    return declared


def filter_matches_declared_test(
    test_filter: str, declared_tests: set[str]
) -> bool:
    positive = test_filter.split("-", 1)[0]
    patterns = [pattern for pattern in positive.split(":") if pattern]
    for pattern in patterns:
        for test_name in declared_tests:
            # The second form covers parameterized gtest names such as
            # Prefix/Suite.Test/Parameter while still requiring an exact
            # checked-in Suite.Test declaration.
            if fnmatch.fnmatchcase(test_name, pattern) or test_name in pattern:
                return True
    return False


def validate_mutations(
    mutations: list[dict[str, Any]],
    originals: dict[Path, str],
    declared_tests: set[str],
) -> None:
    ids: set[str] = set()
    definitions: set[tuple[Path, str, int]] = set()
    for mutation in mutations:
        mutation_id = mutation.get("id")
        if not isinstance(mutation_id, str) or not mutation_id:
            raise ValueError("each mutation requires a non-empty id")
        if mutation_id in ids:
            raise ValueError(f"duplicate mutation id: {mutation_id}")
        ids.add(mutation_id)

        raw_path = mutation.get("file")
        if not isinstance(raw_path, str) or not raw_path:
            raise ValueError(f"{mutation_id}: file must be non-empty")
        path = Path(raw_path)
        if path.is_absolute() or ".." in path.parts:
            raise ValueError(f"{mutation_id}: file must stay inside the repository")
        if path not in originals:
            raise ValueError(f"{mutation_id}: mutation target was not loaded")

        find = mutation.get("find")
        occurrence = int(mutation.get("occurrence", 0))
        definition = (path, str(find), occurrence)
        if definition in definitions:
            raise ValueError(
                f"{mutation_id}: duplicate file/find/occurrence definition"
            )
        definitions.add(definition)
        mutated_text(originals[path], mutation)

        test_filter = mutation.get("test_filter")
        if not isinstance(test_filter, str) or not test_filter:
            raise ValueError(f"{mutation_id}: test_filter is required")
        if not filter_matches_declared_test(test_filter, declared_tests):
            raise ValueError(
                f"{mutation_id}: test_filter does not match a checked-in "
                f"D3D12 test declaration: {test_filter}"
            )
        print(
            f"[PASS] mutation/{mutation_id}: unique definition; "
            f"baseline filter exists"
        )


def require_clean_targets(repo_root: Path, paths: list[Path]) -> None:
    result = subprocess.run(
        ["git", "diff", "--quiet", "--", *(str(path) for path in paths)],
        cwd=repo_root,
        check=False,
    )
    if result.returncode != 0:
        raise ValueError("mutation target files have uncommitted changes")


def run_command(
    command_template: str,
    test_filter: str,
    repo_root: Path,
    timeout: float,
) -> tuple[int, str, bool]:
    command = shlex.split(command_template.format(filter=test_filter))
    try:
        process = subprocess.run(
            command,
            cwd=repo_root,
            check=False,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=timeout,
        )
        return process.returncode, process.stdout, False
    except subprocess.TimeoutExpired as error:
        output = error.stdout or ""
        if isinstance(output, bytes):
            output = output.decode(errors="replace")
        return 124, output, True


def output_is_build_failure(output: str) -> bool:
    build_failure_patterns = (
        r"(?m)^FAILED: .*\.(?:o|obj)(?:\s|$)",
        r"(?m)^ninja: build stopped:",
        r"(?m)^.*(?:compilation terminated|undefined reference to).*$",
        r"(?m)^meson\.build:.*ERROR:",
    )
    return any(re.search(pattern, output) for pattern in build_failure_patterns)


def output_is_infrastructure_failure(output: str) -> bool:
    infrastructure_failure_patterns = (
        r"dxmt-builder: no managed Wine development cache is available",
        r"No runnable Wine cache was found",
        r"Wine runtime is missing dependency",
        r"DXMT_TEST_RUNTIME_ROOT or WINEDLLPATH is required",
        r"invalid DXMT_TEST_RUNTIME_ROOT",
        r"wine: invalid directory .* in WINEPREFIX",
        r"Meson test encountered an error",
        r"ninja: error: loading 'build\.ninja'",
    )
    return any(
        re.search(pattern, output, re.IGNORECASE)
        for pattern in infrastructure_failure_patterns
    )


def main() -> int:
    args = parse_args()
    try:
        repo_root = args.repo_root.resolve()
        manifest_path = args.manifest
        if not manifest_path.is_absolute():
            manifest_path = repo_root / manifest_path
        mutations = load_manifest(manifest_path)
        target_paths = sorted({Path(mutation["file"]) for mutation in mutations})
        originals = {
            path: (repo_root / path).read_text(encoding="utf-8")
            for path in target_paths
        }
        validate_mutations(
            mutations, originals, discover_declared_tests(repo_root)
        )
        print(f"[PASS] validated {len(mutations)} exact mutation definitions")
        if args.validate_only:
            return 0

        require_clean_targets(repo_root, target_paths)
        baseline_filter = ":".join(
            sorted({str(mutation["test_filter"]) for mutation in mutations})
        )
        baseline_status, baseline_output, baseline_timed_out = run_command(
            args.test_command, baseline_filter, repo_root, args.timeout
        )
        if baseline_timed_out:
            print(baseline_output, file=sys.stderr)
            raise ValueError("baseline mutation-target tests timed out")
        if baseline_status != 0:
            print(baseline_output, file=sys.stderr)
            raise ValueError("baseline mutation-target tests failed")

        killed = 0
        for mutation in mutations:
            path = Path(mutation["file"])
            target = repo_root / path
            target.write_text(mutated_text(originals[path], mutation), encoding="utf-8")
            try:
                status, output, timed_out = run_command(
                    args.test_command,
                    str(mutation["test_filter"]),
                    repo_root,
                    args.timeout,
                )
            finally:
                target.write_text(originals[path], encoding="utf-8")
            mutation_id = str(mutation["id"])
            if status == 0:
                print(f"[SURVIVED] {mutation_id}")
                continue
            if timed_out:
                print(output, file=sys.stderr)
                raise ValueError(f"{mutation_id}: test command timed out")
            if output_is_build_failure(output):
                print(output, file=sys.stderr)
                raise ValueError(
                    f"{mutation_id}: mutated source did not compile"
                )
            if output_is_infrastructure_failure(output):
                print(output, file=sys.stderr)
                raise ValueError(
                    f"{mutation_id}: test infrastructure failed"
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
