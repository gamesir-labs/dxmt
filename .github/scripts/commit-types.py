#!/usr/bin/env python3

import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import uuid


TYPE_PATTERN = re.compile(r"^([a-z][a-z0-9-]*)\([a-z0-9][a-z0-9._/-]*\)!?: ")
RUNTIME_TYPES = {"feat", "fix", "refactor", "perf", "build", "revert", "merge"}
NON_RUNTIME_SCOPES = {"ci", "feishu", "git", "repo"}
REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
COMMIT_MESSAGE_CHECKER = REPOSITORY_ROOT / "scripts/check-commit-message.sh"


def commits_from_event(event: dict) -> list[dict[str, str]]:
    commits = []
    for commit in event.get("commits", []):
        message = str(commit.get("message", ""))
        if message:
            author = commit.get("author") or {}
            commits.append(
                {
                    "sha": str(commit.get("id", "")),
                    "subject": message.splitlines()[0],
                    "author": str(author.get("name", "unknown")),
                }
            )
    return commits


def subjects_from_event(event: dict) -> list[str]:
    return [commit["subject"] for commit in commits_from_event(event)]


def classify_subjects(subjects: list[str]) -> tuple[list[str], bool, bool]:
    commit_types = set()
    run_nightly = False
    for subject in subjects:
        match = TYPE_PATTERN.match(subject)
        if match:
            commit_type = match.group(1)
            scope_start = subject.index("(") + 1
            scope_end = subject.index(")", scope_start)
            scope = subject[scope_start:scope_end]
            commit_types.add(commit_type)
            if commit_type in RUNTIME_TYPES and scope not in NON_RUNTIME_SCOPES:
                run_nightly = True
    ordered_types = sorted(commit_types)
    return ordered_types, "test" in commit_types, run_nightly


def validate_subjects(subjects: list[str]) -> list[str]:
    errors = []
    for subject in subjects:
        result = subprocess.run(
            [str(COMMIT_MESSAGE_CHECKER), "--subject", subject],
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            detail = result.stderr.strip() or f"checker exited with {result.returncode}"
            errors.append(f"{subject}: {detail}")
    return errors


def fallback_commit_entry(commit: str) -> dict[str, str]:
    fields = subprocess.run(
        ["git", "show", "-s", "--format=%H%x00%an%x00%s", commit],
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip().split("\0", 2)
    return {"sha": fields[0], "author": fields[1], "subject": fields[2]}


def format_commit_markdown(
    commits: list[dict[str, str]], server_url: str, repository: str
) -> str:
    lines = []
    for commit in commits:
        sha = commit["sha"]
        short_sha = sha[:7] if sha else "unknown"
        subject = commit["subject"].replace("\r", " ").replace("\t", " ")
        author = commit["author"].replace("\r", " ").replace("\n", " ").replace("\t", " ")
        if len(subject) > 180:
            subject = subject[:177] + "..."
        commit_url = f"{server_url}/{repository}/commit/{sha}"
        lines.append(f"[{short_sha}]({commit_url})  {subject} - {author}")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--event", required=True, type=Path)
    parser.add_argument("--fallback-commit", required=True)
    args = parser.parse_args()

    with args.event.open(encoding="utf-8") as event_file:
        event = json.load(event_file)
    commits = commits_from_event(event)
    if not commits:
        commits = [fallback_commit_entry(args.fallback_commit)]
    subjects = [commit["subject"] for commit in commits]

    commit_types, has_test, run_nightly = classify_subjects(subjects)
    validation_errors = validate_subjects(subjects)
    conventional_valid = not validation_errors
    type_summary = ",".join(commit_types) if commit_types else "unknown"
    for subject in subjects:
        print(f"Commit subject: {subject}")
    print(f"Commit types: {type_summary}")
    print(f"Windows D3D required: {str(has_test).lower()}")
    print(f"Nightly required: {str(run_nightly).lower()}")
    print(f"Conventional Commit validation: {str(conventional_valid).lower()}")
    for error in validation_errors:
        print(error, file=sys.stderr)

    commit_markdown = format_commit_markdown(
        commits,
        os.environ.get("GITHUB_SERVER_URL", "https://github.com"),
        os.environ.get("GITHUB_REPOSITORY", "unknown/unknown"),
    )

    output_path = os.environ.get("GITHUB_OUTPUT")
    if not output_path:
        raise RuntimeError("GITHUB_OUTPUT is not set")
    with Path(output_path).open("a", encoding="utf-8") as output_file:
        output_file.write(f"types={type_summary}\n")
        output_file.write(f"has_test={str(has_test).lower()}\n")
        output_file.write(f"run_nightly={str(run_nightly).lower()}\n")
        output_file.write(f"conventional_valid={str(conventional_valid).lower()}\n")
        delimiter = f"DXMT_COMMITS_{uuid.uuid4().hex}"
        output_file.write(f"commit_markdown<<{delimiter}\n")
        output_file.write(f"{commit_markdown}\n")
        output_file.write(f"{delimiter}\n")
    return 0 if conventional_valid else 1


if __name__ == "__main__":
    raise SystemExit(main())
