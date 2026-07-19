#!/usr/bin/env python3

import importlib.util
from pathlib import Path
import re
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
COMMIT_TYPES_PATH = REPOSITORY_ROOT / ".github/scripts/commit-types.py"
COMMIT_TYPES_SPEC = importlib.util.spec_from_file_location(
    "dxmt_ci_commit_types", COMMIT_TYPES_PATH
)
assert COMMIT_TYPES_SPEC is not None and COMMIT_TYPES_SPEC.loader is not None
COMMIT_TYPES = importlib.util.module_from_spec(COMMIT_TYPES_SPEC)
COMMIT_TYPES_SPEC.loader.exec_module(COMMIT_TYPES)


def read_repository_file(relative_path: str) -> str:
    return (REPOSITORY_ROOT / relative_path).read_text(encoding="utf-8")


class CiRoutingPolicyTest(unittest.TestCase):
    def test_all_pushed_commit_messages_are_classified(self) -> None:
        event = {
            "commits": [
                {"message": "chore(ci): aggregate CI notifications\n\nBody"},
                {"message": "test(d3d11): cover deferred contexts"},
                {"message": "fix(d3d12): preserve resource state"},
            ]
        }
        subjects = COMMIT_TYPES.subjects_from_event(event)
        self.assertEqual(
            subjects,
            [
                "chore(ci): aggregate CI notifications",
                "test(d3d11): cover deferred contexts",
                "fix(d3d12): preserve resource state",
            ],
        )
        self.assertEqual(
            COMMIT_TYPES.classify_subjects(subjects),
            (["chore", "fix", "test"], True, True),
        )
        markdown = COMMIT_TYPES.format_commit_markdown(
            COMMIT_TYPES.commits_from_event(event),
            "https://github.com",
            "gamesir-labs/dxmt",
        )
        self.assertEqual(markdown.count("\n"), 2)
        self.assertIn("test(d3d11): cover deferred contexts", markdown)

    def test_non_runtime_changes_skip_nightly(self) -> None:
        self.assertEqual(
            COMMIT_TYPES.classify_subjects(
                [
                    "test(d3d12): cover resource behavior",
                    "docs(repo): document the workflow",
                    "perf(ci): reduce notification latency",
                    "fix(git): tighten commit hooks",
                ]
            ),
            (["docs", "fix", "perf", "test"], True, False),
        )
        self.assertEqual(
            COMMIT_TYPES.classify_subjects(
                [
                    "docs(d3d12): document resource behavior",
                    "style(dxgi): format the adapter code",
                    "chore(deps): refresh test dependencies",
                ]
            ),
            (["chore", "docs", "style"], False, False),
        )

    def test_runtime_change_routes_to_nightly(self) -> None:
        self.assertEqual(
            COMMIT_TYPES.classify_subjects(
                [
                    "test(d3d11): cover deferred contexts",
                    "fix(d3d12): preserve resource state",
                ]
            ),
            (["fix", "test"], True, True),
        )

    def test_all_subjects_must_pass_repository_commit_policy(self) -> None:
        self.assertEqual(
            COMMIT_TYPES.validate_subjects(
                [
                    "test(d3d12): cover resource behavior",
                    "fix(ci): route the push workflow",
                ]
            ),
            [],
        )
        errors = COMMIT_TYPES.validate_subjects(
            [
                "ci(d3d12): bypass the reviewed type vocabulary",
                "fix(unknown): bypass the reviewed scope vocabulary",
            ]
        )
        self.assertEqual(len(errors), 2)
        self.assertIn("invalid commit type", errors[0])
        self.assertIn("invalid commit scope", errors[1])

    def test_push_notifications_are_aggregated(self) -> None:
        collaboration = read_repository_file(
            ".github/workflows/feishu-notify.yml"
        )
        event_block = collaboration.split("permissions:", 1)[0]
        self.assertNotRegex(event_block, re.compile(r"^  push:", re.MULTILINE))

        nightly = read_repository_file(".github/workflows/nightly.yml")
        self.assertIn("Notify Feishu with CI summary", nightly)
        self.assertIn("Send aggregated CI result to Feishu", nightly)
        self.assertIn("steps.commit-types.outputs.commit_markdown", nightly)
        self.assertIn("steps.commit-types.outputs.conventional_valid", nightly)
        self.assertIn("Validate and classify all pushed commits", nightly)
        self.assertIn("cleanup-runner-artifacts, windows-d3d-conformance", nightly)
        self.assertIn("needs.classify-commits.outputs.run_nightly == 'true'", nightly)
        self.assertIn("needs.classify-commits.result == 'success'", nightly)
        notify_job = nightly.split("  notify:\n", 1)[1]
        self.assertIn("if: ${{ always() }}", notify_job)
        self.assertIn("${COMMIT_MARKDOWN}", notify_job)
        self.assertIn("label ci_runs", notify_job)
        self.assertIn("linked_status", notify_job)
        self.assertIn('if [[ "${RUN_NIGHTLY}" == \'true\' ]]', notify_job)
        self.assertIn('if [[ "${RUN_WINDOWS_D3D}" == \'true\' ]]', notify_job)
        self.assertIn("needs.package.outputs.package_name", notify_job)

    def test_test_type_routes_to_reusable_windows_conformance(self) -> None:
        nightly = read_repository_file(".github/workflows/nightly.yml")
        self.assertIn(".github/scripts/commit-types.py", nightly)
        self.assertIn("needs.classify-commits.outputs.has_test == 'true'", nightly)
        self.assertIn(
            "uses: ./.github/workflows/windows-d3d-conformance.yml", nightly
        )

        windows = read_repository_file(
            ".github/workflows/windows-d3d-conformance.yml"
        )
        event_block = windows.split("permissions:", 1)[0]
        self.assertRegex(event_block, re.compile(r"^  workflow_call:", re.MULTILINE))
        self.assertNotRegex(event_block, re.compile(r"^  push:", re.MULTILINE))
        self.assertNotRegex(
            event_block, re.compile(r"^  pull_request:", re.MULTILINE)
        )
        self.assertIn("Notify Feishu for manual conformance run", windows)
        self.assertIn("github.event_name == 'workflow_dispatch'", windows)


if __name__ == "__main__":
    unittest.main()
