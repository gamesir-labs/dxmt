#!/usr/bin/env python3
"""Ensure malformed DXBC diagnostics are non-empty and deterministic."""

from __future__ import annotations

import struct
import subprocess
import sys
import tempfile
from pathlib import Path


def replace(data: bytes, offset: int, value: bytes) -> bytes:
    result = bytearray(data)
    result[offset : offset + len(value)] = value
    return bytes(result)


def run_failure(airconv: Path, input_path: Path, output_path: Path) -> bytes:
    output_path.unlink(missing_ok=True)
    result = subprocess.run(
        [str(airconv), "-S", "--O0", str(input_path), "-o", str(output_path)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=10,
        check=False,
    )
    if result.returncode == 0:
        raise RuntimeError(f"airconv accepted {input_path.name}")
    if result.returncode < 0:
        raise RuntimeError(
            f"airconv terminated from signal {-result.returncode} for {input_path.name}"
        )
    if output_path.exists() and output_path.stat().st_size:
        raise RuntimeError(f"airconv left output for {input_path.name}")
    diagnostic = result.stdout + result.stderr
    if not diagnostic.strip():
        raise RuntimeError(f"airconv emitted no diagnostic for {input_path.name}")
    return diagnostic


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: diagnostic-determinism-test.py AIRCONV FIXTURE_HEX", file=sys.stderr)
        return 2

    airconv = Path(sys.argv[1])
    valid = bytes.fromhex(Path(sys.argv[2]).read_text(encoding="ascii"))
    if len(valid) < 36 or valid[:4] != b"DXBC":
        print("fixture is not a DXBC container", file=sys.stderr)
        return 2

    chunk_count = struct.unpack_from("<I", valid, 28)[0]
    shader_offset = None
    for index in range(chunk_count):
        offset = struct.unpack_from("<I", valid, 32 + index * 4)[0]
        if valid[offset : offset + 4] in (b"SHDR", b"SHEX"):
            shader_offset = offset
            break
    if shader_offset is None:
        print("fixture has no shader chunk", file=sys.stderr)
        return 2

    malformed = {
        "bad-magic": replace(valid, 0, b"BAD!"),
        "missing-shader": replace(valid, shader_offset, b"BAD!"),
        "truncated-payload": valid[:-1],
    }

    with tempfile.TemporaryDirectory(prefix="dxmt-airconv-diagnostics-") as temp:
        work = Path(temp)
        try:
            for name, contents in malformed.items():
                input_path = work / f"{name}.dxbc"
                output_path = work / f"{name}.ll"
                input_path.write_bytes(contents)
                first = run_failure(airconv, input_path, output_path)
                second = run_failure(airconv, input_path, output_path)
                if first != second:
                    raise RuntimeError(f"diagnostic changed between runs for {name}")
        except (RuntimeError, subprocess.TimeoutExpired) as error:
            print(error, file=sys.stderr)
            return 1

    print(f"verified deterministic diagnostics for {len(malformed)} failures")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
