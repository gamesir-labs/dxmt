from __future__ import annotations

import fnmatch
import re
from pathlib import Path
from typing import Any


def percentage(covered: int, total: int) -> float:
    return 100.0 * covered / total if total else 100.0


def read_globs(repo_root: Path, globs: list[str]) -> str:
    sources: list[str] = []
    for source_glob in globs:
        sources.extend(
            path.read_text(encoding="utf-8", errors="ignore")
            for path in repo_root.glob(source_glob)
            if path.is_file()
        )
    return "\n".join(sources)


def scan_public_api(
    config: dict[str, Any], repo_root: Path
) -> tuple[list[dict[str, Any]], list[str]]:
    public_api = config.get("public_api", {})
    source_globs = public_api.get("test_globs", [])
    categories = public_api.get("categories", {})
    if not source_globs or not categories:
        raise ValueError("coverage config must define public_api globs and categories")

    combined = read_globs(repo_root, source_globs)
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
        results.append(
            {
                "category": category_name,
                "covered": len(covered),
                "total": len(api_names),
                "percent": actual,
                "minimum_percent": minimum,
                "missing": missing,
            }
        )
        if actual < minimum:
            errors.append(
                f"public-api/{category_name}: {actual:.2f}% below "
                f"{minimum:.2f}%; missing: {', '.join(missing)}"
            )
    return results, errors


def compiler_counts(
    file_entry: dict[str, Any],
) -> tuple[int, int, int, int, int, int]:
    lines = file_entry.get("lines", [])
    line_total = len(lines)
    line_covered = sum(int(line.get("count", 0)) > 0 for line in lines)
    functions = [
        function
        for function in file_entry.get("functions", [])
        if not function.get("excluded", False)
    ]
    function_total = len(functions)
    function_covered = sum(
        int(function.get("execution_count", 0)) > 0 for function in functions
    )
    branches = [
        branch
        for line in lines
        for branch in line.get("branches", [])
        if not branch.get("excluded", False)
    ]
    branch_total = len(branches)
    branch_covered = sum(int(branch.get("count", 0)) > 0 for branch in branches)
    return (
        line_covered,
        line_total,
        function_covered,
        function_total,
        branch_covered,
        branch_total,
    )


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

        counts = [compiler_counts(entry) for entry in selected]
        line_covered = sum(count[0] for count in counts)
        line_total = sum(count[1] for count in counts)
        function_covered = sum(count[2] for count in counts)
        function_total = sum(count[3] for count in counts)
        branch_covered = sum(count[4] for count in counts)
        branch_total = sum(count[5] for count in counts)
        line_percent = percentage(line_covered, line_total)
        function_percent = percentage(function_covered, function_total)
        branch_percent = percentage(branch_covered, branch_total)
        minimum_line = float(module["minimum_line_percent"])
        minimum_function = float(module["minimum_function_percent"])
        minimum_branch = float(module["minimum_branch_percent"])
        passed = (
            line_percent >= minimum_line
            and function_percent >= minimum_function
            and branch_percent >= minimum_branch
        )
        results.append(
            {
                "module": module_name,
                "files": len(selected),
                "line_covered": line_covered,
                "line_total": line_total,
                "line_percent": line_percent,
                "function_covered": function_covered,
                "function_total": function_total,
                "function_percent": function_percent,
                "branch_covered": branch_covered,
                "branch_total": branch_total,
                "branch_percent": branch_percent,
                "minimum_line_percent": minimum_line,
                "minimum_function_percent": minimum_function,
                "minimum_branch_percent": minimum_branch,
            }
        )
        if not passed:
            errors.append(
                f"compiler/{module_name}: line/function/branch "
                f"{line_percent:.2f}%/{function_percent:.2f}%/"
                f"{branch_percent:.2f}% do not meet {minimum_line:.2f}%/"
                f"{minimum_function:.2f}%/{minimum_branch:.2f}%"
            )
    return results, errors


def normalize_error_evidence(
    mapping_name: str, mapping: dict[str, Any]
) -> tuple[list[dict[str, Any]], list[str]]:
    raw_evidence = mapping.get("evidence")
    if raw_evidence is None:
        test_globs = mapping.get("test_globs", [])
        test_regex = mapping.get("test_regex", "")
        if not test_globs or not test_regex:
            raise ValueError(
                f"error-path/{mapping_name}: legacy test_globs and "
                "test_regex are required"
            )
        return (
            [
                {
                    "category": "legacy",
                    "test_globs": test_globs,
                    "test_regex": test_regex,
                }
            ],
            [],
        )

    if not isinstance(raw_evidence, list) or not raw_evidence:
        raise ValueError(
            f"error-path/{mapping_name}: evidence must be a non-empty list"
        )

    evidence: list[dict[str, Any]] = []
    categories: set[str] = set()
    for index, entry in enumerate(raw_evidence):
        if not isinstance(entry, dict):
            raise ValueError(
                f"error-path/{mapping_name}: evidence[{index}] must be an object"
            )
        category = entry.get("category")
        if not isinstance(category, str) or not re.fullmatch(
            r"[a-z][a-z0-9-]*", category
        ):
            raise ValueError(
                f"error-path/{mapping_name}: evidence[{index}] has an invalid "
                "category"
            )
        test_globs = entry.get("test_globs", mapping.get("test_globs", []))
        test_regex = entry.get("test_regex", "")
        if not isinstance(test_globs, list) or not test_globs or not test_regex:
            raise ValueError(
                f"error-path/{mapping_name}: evidence[{index}] requires "
                "test_globs and test_regex"
            )
        evidence.append(
            {
                "category": category,
                "test_globs": test_globs,
                "test_regex": str(test_regex),
            }
        )
        categories.add(category)

    required = mapping.get("required_evidence_categories", [])
    if not isinstance(required, list) or any(
        not isinstance(category, str) for category in required
    ):
        raise ValueError(
            f"error-path/{mapping_name}: required_evidence_categories must be "
            "a string list"
        )
    missing = sorted(set(required) - categories)
    if missing:
        raise ValueError(
            f"error-path/{mapping_name}: missing required evidence categories: "
            f"{', '.join(missing)}"
        )
    return evidence, sorted(set(required))


def scan_error_paths(
    config: dict[str, Any], repo_root: Path
) -> tuple[list[dict[str, Any]], list[str]]:
    error_path = config.get("error_path", {})
    mappings = error_path.get("mappings", {})
    minimum = float(error_path.get("minimum_percent", 100.0))
    if not mappings:
        raise ValueError("coverage config must define error-path mappings")

    results: list[dict[str, Any]] = []
    errors: list[str] = []
    covered = 0
    for mapping_name, mapping in mappings.items():
        source_text = read_globs(repo_root, mapping.get("source_globs", []))
        source_pattern = str(mapping.get("source_regex", ""))
        if not source_text or not source_pattern:
            raise ValueError(
                f"error-path/{mapping_name}: source globs and regex are required"
            )
        source_matched = re.search(source_pattern, source_text) is not None
        evidence, required_categories = normalize_error_evidence(
            mapping_name, mapping
        )
        evidence_results: list[dict[str, Any]] = []
        for item in evidence:
            test_text = read_globs(repo_root, item["test_globs"])
            matched = bool(test_text) and re.search(
                item["test_regex"], test_text
            ) is not None
            evidence_results.append(
                {"category": item["category"], "matched": matched}
            )
        test_matched = all(item["matched"] for item in evidence_results)
        mapped = source_matched and test_matched
        covered += mapped
        results.append(
            {
                "path": mapping_name,
                "source_matched": source_matched,
                "test_matched": test_matched,
                "required_evidence_categories": required_categories,
                "evidence": evidence_results,
                "mapped": mapped,
            }
        )

    actual = percentage(covered, len(mappings))
    if actual < minimum:
        errors.append(f"error-path/total: {actual:.2f}% below {minimum:.2f}%")
    return results, errors
