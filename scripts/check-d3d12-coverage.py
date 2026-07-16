#!/usr/bin/env python3

from __future__ import annotations

import argparse
import fnmatch
import json
import re
import sys
from pathlib import Path
from typing import Any


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Enforce D3D12 line, branch, and public API coverage gates."
    )
    parser.add_argument(
        "--config",
        type=Path,
        default=Path("tests/coverage/d3d12_coverage.json"),
    )
    parser.add_argument("--gcovr", type=Path)
    parser.add_argument("--repo-root", type=Path, default=Path.cwd())
    parser.add_argument("--output", type=Path)
    return parser.parse_args()


def percentage(covered: int, total: int) -> float:
    return 100.0 * covered / total if total else 100.0


def load_json(path: Path) -> dict[str, Any]:
    with path.open(encoding="utf-8") as input_file:
        value = json.load(input_file)
    if not isinstance(value, dict):
        raise ValueError(f"{path}: root must be an object")
    return value


def scan_public_api(
    config: dict[str, Any], repo_root: Path
) -> tuple[list[dict[str, Any]], list[str]]:
    public_api = config.get("public_api", {})
    source_globs = public_api.get("test_globs", [])
    categories = public_api.get("categories", {})
    if not source_globs or not categories:
        raise ValueError("coverage config must define public_api globs and categories")
    sources: list[str] = []
    for source_glob in source_globs:
        sources.extend(
            path.read_text(encoding="utf-8", errors="ignore")
            for path in repo_root.glob(source_glob)
            if path.is_file()
        )
    combined = "\n".join(sources)
    results: list[dict[str, Any]] = []
    errors: list[str] = []
    for category_name, category in categories.items():
        api_names = category.get("apis", [])
        minimum = float(category.get("minimum_percent", 100.0))
        covered = [
            name
            for name in api_names
            if re.search(rf"\b{re.escape(name)}\s*\(", combined)
        ]
        missing = sorted(set(api_names) - set(covered))
        actual = percentage(len(covered), len(api_names))
        result = {
            "category": category_name,
            "covered": len(covered),
            "total": len(api_names),
            "percent": actual,
            "minimum_percent": minimum,
            "missing": missing,
        }
        results.append(result)
        status = "PASS" if actual >= minimum else "FAIL"
        print(
            f"[{status}] public-api/{category_name}: {len(covered)}/{len(api_names)} "
            f"({actual:.2f}%, minimum {minimum:.2f}%)"
        )
        if actual < minimum:
            errors.append(
                f"public-api/{category_name}: {actual:.2f}% below {minimum:.2f}%; "
                f"missing: {', '.join(missing)}"
            )
    return results, errors


def line_and_branch_counts(file_entry: dict[str, Any]) -> tuple[int, int, int, int]:
    lines = file_entry.get("lines", [])
    line_total = len(lines)
    line_covered = sum(int(line.get("count", 0)) > 0 for line in lines)
    branches = [
        branch
        for line in lines
        for branch in line.get("branches", [])
        if not branch.get("excluded", False)
    ]
    branch_total = len(branches)
    branch_covered = sum(int(branch.get("count", 0)) > 0 for branch in branches)
    return line_covered, line_total, branch_covered, branch_total


def scan_compiler_coverage(
    config: dict[str, Any], report: dict[str, Any], repo_root: Path
) -> tuple[list[dict[str, Any]], list[str]]:
    files = report.get("files")
    modules = config.get("compiler_coverage", {}).get("modules", {})
    if not isinstance(files, list) or not files:
        raise ValueError("gcovr report has no files")
    if not modules:
        raise ValueError("coverage config has no compiler coverage modules")
    normalized: list[tuple[str, dict[str, Any]]] = []
    for entry in files:
        path = Path(str(entry.get("file", "")))
        if path.is_absolute():
            try:
                path = path.relative_to(repo_root)
            except ValueError:
                continue
        normalized.append((path.as_posix(), entry))

    results: list[dict[str, Any]] = []
    errors: list[str] = []
    for module_name, module in modules.items():
        patterns = module.get("globs", [])
        selected = [
            entry
            for path, entry in normalized
            if any(fnmatch.fnmatch(path, pattern) for pattern in patterns)
        ]
        if not selected:
            errors.append(f"compiler/{module_name}: no matching files in gcovr report")
            continue
        counts = [line_and_branch_counts(entry) for entry in selected]
        line_covered = sum(count[0] for count in counts)
        line_total = sum(count[1] for count in counts)
        branch_covered = sum(count[2] for count in counts)
        branch_total = sum(count[3] for count in counts)
        line_percent = percentage(line_covered, line_total)
        branch_percent = percentage(branch_covered, branch_total)
        minimum_line = float(module["minimum_line_percent"])
        minimum_branch = float(module["minimum_branch_percent"])
        passed = line_percent >= minimum_line and branch_percent >= minimum_branch
        status = "PASS" if passed else "FAIL"
        print(
            f"[{status}] compiler/{module_name}: line {line_percent:.2f}% "
            f"(minimum {minimum_line:.2f}%), branch {branch_percent:.2f}% "
            f"(minimum {minimum_branch:.2f}%)"
        )
        results.append(
            {
                "module": module_name,
                "files": len(selected),
                "line_covered": line_covered,
                "line_total": line_total,
                "line_percent": line_percent,
                "branch_covered": branch_covered,
                "branch_total": branch_total,
                "branch_percent": branch_percent,
                "minimum_line_percent": minimum_line,
                "minimum_branch_percent": minimum_branch,
            }
        )
        if not passed:
            errors.append(
                f"compiler/{module_name}: line {line_percent:.2f}% and branch "
                f"{branch_percent:.2f}% do not meet {minimum_line:.2f}%/"
                f"{minimum_branch:.2f}%"
            )
    return results, errors


def main() -> int:
    args = parse_args()
    try:
        repo_root = args.repo_root.resolve()
        config_path = args.config
        if not config_path.is_absolute():
            config_path = repo_root / config_path
        config = load_json(config_path)
        if config.get("schema_version") != 1:
            raise ValueError("unsupported coverage config schema")
        public_results, errors = scan_public_api(config, repo_root)
        compiler_results: list[dict[str, Any]] = []
        if args.gcovr:
            compiler_results, compiler_errors = scan_compiler_coverage(
                config, load_json(args.gcovr), repo_root
            )
            errors.extend(compiler_errors)
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(
                json.dumps(
                    {
                        "schema_version": 1,
                        "public_api": public_results,
                        "compiler_coverage": compiler_results,
                        "passed": not errors,
                    },
                    indent=2,
                )
                + "\n",
                encoding="utf-8",
            )
    except (OSError, ValueError, KeyError, TypeError, json.JSONDecodeError) as error:
        print(f"coverage validation failed: {error}", file=sys.stderr)
        return 2
    if errors:
        for error in errors:
            print(f"coverage gate failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
