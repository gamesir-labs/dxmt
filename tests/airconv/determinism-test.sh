#!/bin/sh
set -eu

airconv=$1
fixture_hex=$2
work_dir=$(mktemp -d "${TMPDIR:-/tmp}/dxmt-airconv-determinism.XXXXXX")
trap 'rm -rf "$work_dir"' EXIT HUP INT TERM

fixture="$work_dir/shader.dxbc"
first="$work_dir/first.ll"
second="$work_dir/second.ll"
xxd -r -p "$fixture_hex" "$fixture"
"$airconv" -S --O0 "$fixture" -o "$first"
"$airconv" -S --O0 "$fixture" -o "$second"

if ! cmp -s "$first" "$second"; then
  echo 'airconv emitted nondeterministic IR for identical input' >&2
  diff -u "$first" "$second" >&2 || true
  exit 1
fi

if [ ! -s "$first" ]; then
  echo 'airconv emitted an empty IR module' >&2
  exit 1
fi
