#!/usr/bin/env python3

import importlib.util
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock


REPO_ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "run_d3d12_mutations", REPO_ROOT / "scripts/run-d3d12-mutations.py"
)
assert SPEC and SPEC.loader
MUTATIONS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MUTATIONS)


class MutationValidationTest(unittest.TestCase):
    def make_repo(self, root: Path) -> tuple[Path, dict[Path, str], set[str]]:
        (root / "src").mkdir()
        (root / "tests/d3d12").mkdir(parents=True)
        target = Path("src/target.cpp")
        source = "return value;\n"
        (root / target).write_text(source, encoding="utf-8")
        (root / "tests/d3d12/sample.cpp").write_text(
            "TEST_F(SampleSpec, DetectsMutation) {}\n"
            "TEST_P(MatrixSpec, DetectsBoundaryMutation) {}\n",
            encoding="utf-8",
        )
        return target, {target: source}, MUTATIONS.discover_declared_tests(root)

    def test_accepts_unique_definition_and_parameterized_filter(self):
        with tempfile.TemporaryDirectory() as temporary:
            target, originals, declared = self.make_repo(Path(temporary))
            mutation = {
                "id": "sample",
                "file": str(target),
                "find": "value",
                "replace": "other",
                "test_filter": "Cases/MatrixSpec.DetectsBoundaryMutation/AtLimit",
            }
            MUTATIONS.validate_mutations([mutation], originals, declared)

    def test_rejects_duplicate_ids_and_unknown_filter(self):
        with tempfile.TemporaryDirectory() as temporary:
            target, originals, declared = self.make_repo(Path(temporary))
            base = {
                "id": "duplicate",
                "file": str(target),
                "find": "value",
                "replace": "other",
                "test_filter": "SampleSpec.DetectsMutation",
            }
            with self.assertRaisesRegex(ValueError, "duplicate mutation id"):
                MUTATIONS.validate_mutations(
                    [base, {**base, "find": "return", "replace": "yield"}],
                    originals,
                    declared,
                )
            with self.assertRaisesRegex(ValueError, "does not match"):
                MUTATIONS.validate_mutations(
                    [{**base, "id": "unknown", "test_filter": "Missing.Nope"}],
                    originals,
                    declared,
                )

    def test_distinguishes_compilation_failure_from_test_failure(self):
        self.assertTrue(
            MUTATIONS.output_is_build_failure(
                "FAILED: obj/source.cpp.obj\nninja: build stopped: subcommand failed."
            )
        )
        self.assertFalse(
            MUTATIONS.output_is_build_failure(
                "[  FAILED  ] SampleSpec.DetectsMutation\nFail: 1"
            )
        )

    def test_distinguishes_infrastructure_failure_from_test_failure(self):
        self.assertTrue(
            MUTATIONS.output_is_infrastructure_failure(
                "dxmt-builder: no managed Wine development cache is available"
            )
        )
        self.assertTrue(
            MUTATIONS.output_is_infrastructure_failure(
                "wine: invalid directory relative/prefix in WINEPREFIX"
            )
        )
        self.assertFalse(
            MUTATIONS.output_is_infrastructure_failure(
                "[  FAILED  ] SampleSpec.DetectsMutation\nFail: 1"
            )
        )

    def test_run_command_reports_timeout_without_counting_a_kill(self):
        timeout = subprocess.TimeoutExpired(
            cmd=["fake-test"], timeout=1.0, output=b"partial output"
        )
        with mock.patch.object(MUTATIONS.subprocess, "run", side_effect=timeout):
            status, output, timed_out = MUTATIONS.run_command(
                "fake-test --filter={filter}",
                "SampleSpec.DetectsMutation",
                REPO_ROOT,
                1.0,
            )
        self.assertEqual(status, 124)
        self.assertEqual(output, "partial output")
        self.assertTrue(timed_out)


if __name__ == "__main__":
    unittest.main()
