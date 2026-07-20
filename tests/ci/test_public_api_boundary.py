#!/usr/bin/env python3

import re
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
TEST_ROOT = REPO_ROOT / "tests"
TEST_SOURCE_ROOTS = (TEST_ROOT,)
SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hpp"}

INCLUDE_PATTERN = re.compile(r'^\s*#\s*include\s*"([^"]+)"', re.MULTILINE)
FORBIDDEN_INCLUDE_PATTERN = re.compile(
    r"(^|/)(?:src|airconv|winemetal|com|rc|config|log|sha1)/"
    r"|^(?:DXBCParser|DXILParser)/"
    r"|^(?:dxmt_(?!test)|util_|wsi_platform|adt\.hpp$|ftl\.hpp$|"
    r"objc_pointer\.hpp$|thread\.hpp$|threadpool\.hpp$)"
)
PRIVATE_CONTROL_PATTERN = re.compile(
    r"DXMT_TEST_(?:COMMAND_BUFFER|D3D12_BARRIER|FAIL_|FAULT_MARKER|"
    r"FENCE_CREATION_MARKER|FORCE_|METAL4_)"
)
PRIVATE_NAMESPACE_PATTERN = re.compile(r"\bdxmt::(?!test\b)")
PRIVATE_SYMBOL_PATTERN = re.compile(
    r"\b(?:MTLD3D(?:10|11|12)|I?D3D(?:10|11|12)DXMT)\w*"
    r"|\bWMT(?:Device|Command|Resource|Sparse|Metal)\w*"
    r"|\bMTL(?:Device|CommandQueue|Buffer|Texture)_\w+"
    r"|\b(?:WMT|MTL)::"
    r"|\b(?:SM50|DXMT12(?:SM50|DXIL))\w*"
)


def source_files(root: Path):
    return sorted(path for path in root.rglob("*") if path.suffix in SOURCE_SUFFIXES)


class PublicApiBoundaryPolicyTest(unittest.TestCase):
    def test_product_tests_do_not_include_dxmt_implementation_headers(self):
        violations = []
        for root in TEST_SOURCE_ROOTS:
            for path in source_files(root):
                text = path.read_text(encoding="utf-8")
                for include in INCLUDE_PATTERN.findall(text):
                    if FORBIDDEN_INCLUDE_PATTERN.search(include):
                        violations.append(f"{path.relative_to(REPO_ROOT)}: {include}")
        self.assertEqual([], violations, "forbidden implementation includes:\n" + "\n".join(violations))

    def test_product_tests_do_not_use_private_runtime_controls(self):
        violations = []
        for root in TEST_SOURCE_ROOTS:
            for path in source_files(root):
                text = path.read_text(encoding="utf-8")
                if PRIVATE_CONTROL_PATTERN.search(text):
                    violations.append(str(path.relative_to(REPO_ROOT)))
        self.assertEqual([], violations, "private runtime controls:\n" + "\n".join(violations))

    def test_product_tests_do_not_reference_private_dxmt_symbols(self):
        violations = []
        for root in TEST_SOURCE_ROOTS:
            for path in source_files(root):
                text = path.read_text(encoding="utf-8")
                if (
                    PRIVATE_NAMESPACE_PATTERN.search(text)
                    or PRIVATE_SYMBOL_PATTERN.search(text)
                ):
                    violations.append(str(path.relative_to(REPO_ROOT)))
        self.assertEqual([], violations, "private DXMT symbols:\n" + "\n".join(violations))

    def test_manifest_does_not_compile_dxmt_implementation_into_tests(self):
        manifest = (TEST_ROOT / "meson.build").read_text(encoding="utf-8")
        forbidden = ("../src/", "src/airconv", "src/winemetal", "airconv_cli")
        found = [token for token in forbidden if token in manifest]
        self.assertEqual([], found, f"private implementation references in tests/meson.build: {found}")

    def test_private_implementation_test_lanes_are_absent(self):
        internal_units = sorted((TEST_ROOT / "wine").glob("*_test.cpp"))
        private_assets = [TEST_ROOT / "airconv", TEST_ROOT / "fault_injection"]
        violations = [str(path.relative_to(REPO_ROOT)) for path in internal_units]
        violations.extend(
            str(path.relative_to(REPO_ROOT)) for path in private_assets if path.exists()
        )
        self.assertEqual([], violations, "private test lanes:\n" + "\n".join(violations))


if __name__ == "__main__":
    unittest.main()
