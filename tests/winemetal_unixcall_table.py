#!/usr/bin/env python3
from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def fail(message: str) -> None:
    print(message, file=sys.stderr)
    sys.exit(1)


def normalize(name: str) -> str:
    name = name.removeprefix("_").removeprefix("thunk")
    name = name.removeprefix("32")
    return re.sub(r"[^a-z0-9]", "", name.lower())


def parse_windows_thunks() -> dict[int, str]:
    source = read("src/winemetal/winemetal_thunks.c")
    calls: dict[int, str] = {}

    for match in re.finditer(
        r"WINEMETAL_API\s+.*?\n([A-Za-z0-9_]+)\s*\([^)]*\)\s*\{(?P<body>.*?)\n\}",
        source,
        re.S,
    ):
        name = match.group(1)
        body = match.group("body")
        call = re.search(r"(?:UNIX_CALL|WINE_UNIX_CALL)\((\d+),", body)
        if not call:
            continue
        index = int(call.group(1))
        if index in calls:
            fail(f"duplicate unixcall index {index}: {calls[index]} and {name}")
        calls[index] = name

    return calls


def parse_unix_table(table_name: str) -> list[str]:
    source = read("src/winemetal/unix/winemetal_unix.c")
    match = re.search(
        rf"const void \*{re.escape(table_name)}\[\]\s*=\s*\{{(?P<body>.*?)\n\}};",
        source,
        re.S,
    )
    if not match:
        fail(f"missing {table_name}")
    return re.findall(r"&([A-Za-z0-9_]+)", match.group("body"))


def require_table_matches(calls: dict[int, str], table_name: str) -> None:
    table = parse_unix_table(table_name)
    for index, win_name in sorted(calls.items()):
        if index >= len(table):
            fail(f"{win_name} uses unixcall {index}, beyond {table_name} size {len(table)}")

        unix_name = table[index]
        win_key = normalize(win_name)
        unix_key = normalize(unix_name)

        if win_name == "WMTCopyAllDevices":
            expected = normalize("MTLCopyAllDevices")
        else:
            expected = win_key

        if expected not in unix_key and unix_key not in expected:
            fail(f"unixcall {index} mismatch in {table_name}: {win_name} -> {unix_name}")


def main() -> None:
    calls = parse_windows_thunks()
    if not calls:
        fail("no Windows winemetal thunks found")

    require_table_matches(calls, "__wine_unix_call_funcs")
    require_table_matches(calls, "__wine_unix_call_wow64_funcs")


if __name__ == "__main__":
    main()
