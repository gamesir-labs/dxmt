#!/usr/bin/env python3

import importlib.util
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "check_d3d12_coverage", REPO_ROOT / "scripts/check-d3d12-coverage.py"
)
assert SPEC and SPEC.loader
COVERAGE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(COVERAGE)


class ErrorEvidenceTest(unittest.TestCase):
    def test_checks_every_declared_evidence_category(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "src").mkdir()
            (root / "tests").mkdir()
            (root / "src/path.cpp").write_text("FAULT_POINT", encoding="utf-8")
            (root / "tests/path.cpp").write_text(
                "NegativeCase LifecycleCase NoOpCase", encoding="utf-8"
            )
            config = {
                "error_path": {
                    "minimum_percent": 100,
                    "mappings": {
                        "sample": {
                            "source_globs": ["src/path.cpp"],
                            "source_regex": "FAULT_POINT",
                            "test_globs": ["tests/path.cpp"],
                            "required_evidence_categories": [
                                "negative",
                                "lifecycle",
                                "no-op",
                            ],
                            "evidence": [
                                {"category": "negative", "test_regex": "NegativeCase"},
                                {"category": "lifecycle", "test_regex": "LifecycleCase"},
                                {"category": "no-op", "test_regex": "NoOpCase"},
                            ],
                        }
                    },
                }
            }
            results, errors = COVERAGE.scan_error_paths(config, root)
            self.assertFalse(errors)
            self.assertTrue(results[0]["mapped"])
            self.assertEqual(
                {entry["category"] for entry in results[0]["evidence"]},
                {"negative", "lifecycle", "no-op"},
            )

    def test_rejects_missing_required_category(self):
        mapping = {
            "test_globs": ["tests/path.cpp"],
            "required_evidence_categories": ["negative", "lifecycle"],
            "evidence": [
                {"category": "negative", "test_regex": "NegativeCase"}
            ],
        }
        with self.assertRaisesRegex(ValueError, "missing required evidence"):
            COVERAGE.normalize_error_evidence("sample", mapping)


class CompilerModuleTest(unittest.TestCase):
    def test_independent_module_threshold_is_enforced(self):
        config = {
            "compiler_coverage": {
                "modules": {
                    "parser-ir": {
                        "globs": ["src/parser.cpp"],
                        "minimum_line_percent": 50,
                        "minimum_function_percent": 50,
                        "minimum_branch_percent": 50,
                    }
                }
            }
        }
        report = {
            "files": [
                {
                    "file": "src/parser.cpp",
                    "lines": [
                        {"count": 1, "branches": [{"count": 1}]},
                        {"count": 0, "branches": [{"count": 0}]},
                    ],
                    "functions": [
                        {"execution_count": 1},
                        {"execution_count": 0},
                    ],
                }
            ]
        }
        results, errors = COVERAGE.scan_compiler_coverage(
            config, report, REPO_ROOT
        )
        self.assertFalse(errors)
        self.assertEqual(results[0]["module"], "parser-ir")
        self.assertEqual(results[0]["line_percent"], 50.0)


if __name__ == "__main__":
    unittest.main()
