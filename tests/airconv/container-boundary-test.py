#!/usr/bin/env python3
"""Exercise DXBC container and chunk-table boundary rejection paths."""

from __future__ import annotations

import struct
import subprocess
import sys
import tempfile
from pathlib import Path


def u32(data: bytes | bytearray, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def set_u32(data: bytes, offset: int, value: int) -> bytes:
    result = bytearray(data)
    struct.pack_into("<I", result, offset, value)
    return bytes(result)


def replace(data: bytes, offset: int, value: bytes) -> bytes:
    result = bytearray(data)
    result[offset : offset + len(value)] = value
    return bytes(result)


def mutations(valid: bytes) -> list[tuple[str, bytes]]:
    if len(valid) < 36 or valid[:4] != b"DXBC":
        raise ValueError("fixture is not a DXBC container")

    chunk_count = u32(valid, 28)
    table_end = 32 + 4 * chunk_count
    if table_end > len(valid):
        raise ValueError("fixture has a truncated chunk table")
    chunk_offsets = [u32(valid, 32 + 4 * index) for index in range(chunk_count)]

    cases: list[tuple[str, bytes]] = [
        ("empty", b""),
        ("one-byte", valid[:1]),
        ("magic-only", valid[:4]),
        ("truncated-hash", valid[:19]),
        ("truncated-version", valid[:23]),
        ("truncated-size", valid[:27]),
        ("truncated-count", valid[:31]),
        ("truncated-table", valid[: table_end - 1]),
        ("bad-magic", replace(valid, 0, b"BAD!")),
        ("zero-total-size", set_u32(valid, 24, 0)),
        ("short-total-size", set_u32(valid, 24, table_end - 1)),
        ("undersized-container", set_u32(valid, 24, len(valid) - 1)),
        ("oversized-container", set_u32(valid, 24, len(valid) + 1)),
        ("overflowing-container", set_u32(valid, 24, 0xFFFFFFFF)),
        ("zero-chunks", set_u32(valid, 28, 0)),
        ("oversized-chunk-count", set_u32(valid, 28, 0x7FFFFFFF)),
        ("offset-in-header", set_u32(valid, 32, 4)),
        ("offset-in-table", set_u32(valid, 32, table_end - 1)),
        ("offset-at-eof", set_u32(valid, 32, len(valid))),
        ("offset-after-eof", set_u32(valid, 32, len(valid) + 4)),
        ("overflowing-offset", set_u32(valid, 32, 0xFFFFFFFC)),
    ]

    first_chunk = chunk_offsets[0]
    cases.extend(
        [
            ("truncated-chunk-header", valid[: first_chunk + 7]),
            ("zero-first-chunk-size", set_u32(valid, first_chunk + 4, 0)),
            (
                "oversized-first-chunk",
                set_u32(valid, first_chunk + 4, len(valid)),
            ),
            (
                "overflowing-first-chunk",
                set_u32(valid, first_chunk + 4, 0xFFFFFFFF),
            ),
        ]
    )

    required_tags = {
        b"ISGN": "missing-input-signature",
        b"OSGN": "missing-output-signature",
        b"SHDR": "missing-shader-bytecode",
        b"SHEX": "missing-shader-bytecode",
    }
    for chunk_offset in chunk_offsets:
        tag = valid[chunk_offset : chunk_offset + 4]
        if tag in required_tags:
            cases.append(
                (required_tags[tag], replace(valid, chunk_offset, b"BAD!"))
            )

    return cases


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: container-boundary-test.py AIRCONV FIXTURE_HEX", file=sys.stderr)
        return 2

    airconv = Path(sys.argv[1])
    valid = bytes.fromhex(Path(sys.argv[2]).read_text(encoding="ascii"))
    cases = mutations(valid)

    with tempfile.TemporaryDirectory(prefix="dxmt-airconv-boundary-") as temp:
        work = Path(temp)
        failures: list[str] = []
        for name, contents in cases:
            input_path = work / f"{name}.dxbc"
            output_path = work / f"{name}.ll"
            input_path.write_bytes(contents)
            try:
                result = subprocess.run(
                    [str(airconv), "-S", "--O0", str(input_path), "-o", str(output_path)],
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    timeout=10,
                    check=False,
                )
            except subprocess.TimeoutExpired:
                failures.append(f"{name}: conversion timed out")
                continue
            if result.returncode == 0:
                failures.append(f"{name}: malformed container was accepted")
            elif result.returncode < 0:
                failures.append(
                    f"{name}: converter terminated from signal {-result.returncode}"
                )
            if output_path.exists() and output_path.stat().st_size:
                failures.append(f"{name}: failure left a non-empty IR output")

        if failures:
            print("\n".join(failures), file=sys.stderr)
            return 1

    print(f"rejected {len(cases)} malformed DXBC boundary cases")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
