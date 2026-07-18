#!/usr/bin/env python3

import importlib.util
import json
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "d3d12_coverage_support",
    Path(__file__).with_name("d3d12_coverage_support.py"),
)
assert SPEC and SPEC.loader
COVERAGE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(COVERAGE)


class PublicApiEvidenceTest(unittest.TestCase):
    def test_reports_declared_api_without_test_evidence(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "tests").mkdir()
            (root / "tests/path.cpp").write_text(
                "CoveredMethod();", encoding="utf-8"
            )
            config = {
                "public_api": {
                    "test_globs": ["tests/*.cpp"],
                    "categories": {
                        "sample": {
                            "minimum_percent": 100,
                            "apis": ["CoveredMethod", "MissingMethod"],
                        }
                    },
                }
            }
            results, errors = COVERAGE.scan_public_api(config, root)
            self.assertEqual(results[0]["covered"], 1)
            self.assertEqual(results[0]["missing"], ["MissingMethod"])
            self.assertEqual(len(errors), 1)

    def test_reports_implemented_public_method_missing_from_manifest(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "include").mkdir()
            (root / "src").mkdir()
            (root / "include/api.hpp").write_text(
                "virtual void STDMETHODCALLTYPE MissingMethod() = 0;",
                encoding="utf-8",
            )
            (root / "src/api.cpp").write_text(
                "void STDMETHODCALLTYPE MissingMethod() override {}",
                encoding="utf-8",
            )
            config = {
                "public_api": {
                    "interface_globs": ["include/*.hpp"],
                    "implementation_globs": ["src/*.cpp"],
                    "categories": {
                        "sample": {
                            "apis": ["OtherMethod"],
                        }
                    },
                }
            }
            result, errors = COVERAGE.scan_public_api_surface(config, root)
            self.assertEqual(result["missing"], ["MissingMethod"])
            self.assertEqual(len(errors), 1)

    def test_discovers_implemented_method_declared_in_idl(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "include").mkdir()
            (root / "src").mkdir()
            (root / "include/api.idl").write_text(
                "interface ID3D12Sample : IUnknown\n"
                "{\n"
                "    HRESULT IdlMethod(UINT value);\n"
                "}\n",
                encoding="utf-8",
            )
            (root / "src/api.cpp").write_text(
                "HRESULT STDMETHODCALLTYPE IdlMethod(UINT value) override {}",
                encoding="utf-8",
            )
            config = {
                "public_api": {
                    "interface_globs": ["include/*.idl"],
                    "implementation_globs": ["src/*.cpp"],
                    "categories": {"sample": {"apis": []}},
                }
            }
            result, errors = COVERAGE.scan_public_api_surface(config, root)
            self.assertEqual(result["missing"], ["IdlMethod"])
            self.assertEqual(len(errors), 1)

    def test_accepts_promotion_gate_with_negative_test_evidence(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "include").mkdir()
            (root / "src").mkdir()
            (root / "tests").mkdir()
            (root / "include/api.idl").write_text(
                "interface ID3D12Sample : IUnknown\n"
                "{\n"
                "    HRESULT GatedMethod();\n"
                "}\n",
                encoding="utf-8",
            )
            (root / "src/api.cpp").write_text(
                "HRESULT STDMETHODCALLTYPE GatedMethod() override {}",
                encoding="utf-8",
            )
            (root / "tests/gate.cpp").write_text(
                "TEST(PromotionGate, RemainsUnavailable)", encoding="utf-8"
            )
            config = {
                "public_api": {
                    "interface_globs": ["include/*.idl"],
                    "implementation_globs": ["src/*.cpp"],
                    "categories": {"sample": {"apis": []}},
                    "promotion_gates": {
                        "sample-gate": {
                            "apis": ["GatedMethod"],
                            "test_globs": ["tests/*.cpp"],
                            "test_regex": "RemainsUnavailable",
                        }
                    },
                }
            }
            result, errors = COVERAGE.scan_public_api_surface(config, root)
            self.assertFalse(errors)
            self.assertFalse(result["missing"])
            self.assertEqual(result["promotion_gated"], 1)

    def test_rejects_promotion_gate_without_test_evidence(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "include").mkdir()
            (root / "src").mkdir()
            (root / "tests").mkdir()
            (root / "include/api.idl").write_text(
                "interface ID3D12Sample : IUnknown\n"
                "{\n"
                "    HRESULT GatedMethod();\n"
                "}\n",
                encoding="utf-8",
            )
            (root / "src/api.cpp").write_text(
                "HRESULT STDMETHODCALLTYPE GatedMethod() override {}",
                encoding="utf-8",
            )
            (root / "tests/gate.cpp").write_text(
                "TEST(PromotionGate, OtherCase)", encoding="utf-8"
            )
            config = {
                "public_api": {
                    "interface_globs": ["include/*.idl"],
                    "implementation_globs": ["src/*.cpp"],
                    "categories": {"sample": {"apis": []}},
                    "promotion_gates": {
                        "sample-gate": {
                            "apis": ["GatedMethod"],
                            "test_globs": ["tests/*.cpp"],
                            "test_regex": "RemainsUnavailable",
                        }
                    },
                }
            }
            result, errors = COVERAGE.scan_public_api_surface(config, root)
            self.assertFalse(result["missing"])
            self.assertEqual(len(errors), 1)


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

    def test_legacy_mapping_requires_both_source_and_test_evidence(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "src").mkdir()
            (root / "tests").mkdir()
            (root / "src/path.cpp").write_text("FAULT_POINT", encoding="utf-8")
            (root / "tests/path.cpp").write_text("OtherCase", encoding="utf-8")
            config = {
                "error_path": {
                    "minimum_percent": 100,
                    "mappings": {
                        "sample": {
                            "source_globs": ["src/path.cpp"],
                            "source_regex": "FAULT_POINT",
                            "test_globs": ["tests/path.cpp"],
                            "test_regex": "ExpectedCase",
                        }
                    },
                }
            }
            results, errors = COVERAGE.scan_error_paths(config, root)
            self.assertFalse(results[0]["mapped"])
            self.assertEqual(len(errors), 1)


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

    def test_function_threshold_fails_independently(self):
        config = {
            "compiler_coverage": {
                "modules": {
                    "parser-ir": {
                        "globs": ["src/parser.cpp"],
                        "minimum_line_percent": 100,
                        "minimum_function_percent": 100,
                        "minimum_branch_percent": 100,
                    }
                }
            }
        }
        report = {
            "files": [
                {
                    "file": "src/parser.cpp",
                    "lines": [{"count": 1, "branches": [{"count": 1}]}],
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
        self.assertEqual(results[0]["line_percent"], 100.0)
        self.assertEqual(results[0]["branch_percent"], 100.0)
        self.assertEqual(results[0]["function_percent"], 50.0)
        self.assertEqual(len(errors), 1)


class RepositoryManifestTest(unittest.TestCase):
    def test_current_manifest_evidence_is_consistent(self):
        config_path = REPO_ROOT / "tests/coverage/d3d12_coverage.json"
        config = json.loads(config_path.read_text(encoding="utf-8"))
        public_results, public_errors = COVERAGE.scan_public_api(
            config, REPO_ROOT
        )
        surface_result, surface_errors = COVERAGE.scan_public_api_surface(
            config, REPO_ROOT
        )
        error_results, error_errors = COVERAGE.scan_error_paths(
            config, REPO_ROOT
        )
        self.assertFalse(public_errors)
        self.assertFalse(surface_errors)
        self.assertFalse(error_errors)
        self.assertFalse(surface_result["missing"])
        self.assertTrue(all(not result["missing"] for result in public_results))
        self.assertTrue(all(result["mapped"] for result in error_results))


if __name__ == "__main__":
    unittest.main()
