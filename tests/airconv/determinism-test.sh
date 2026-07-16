#!/bin/sh
set -eu

airconv=$1
shift
work_dir=$(mktemp -d "${TMPDIR:-/tmp}/dxmt-airconv-determinism.XXXXXX")
trap 'rm -rf "$work_dir"' EXIT HUP INT TERM

if [ "$#" -eq 0 ]; then
  echo 'determinism-test.sh requires at least one DXBC hex fixture' >&2
  exit 2
fi

fixture_index=0
for fixture_hex in "$@"; do
  fixture="$work_dir/shader-$fixture_index.dxbc"
  xxd -r -p "$fixture_hex" "$fixture"

  for optimization in o0 optimized; do
    first="$work_dir/$fixture_index-$optimization-first.ll"
    second="$work_dir/$fixture_index-$optimization-second.ll"
    if [ "$optimization" = o0 ]; then
      "$airconv" -S --O0 "$fixture" -o "$first"
      "$airconv" -S --O0 "$fixture" -o "$second"
    else
      "$airconv" -S "$fixture" -o "$first"
      "$airconv" -S "$fixture" -o "$second"
    fi

    if ! cmp -s "$first" "$second"; then
      echo "airconv emitted nondeterministic $optimization IR for fixture $fixture_hex" >&2
      diff -u "$first" "$second" >&2 || true
      exit 1
    fi

    if [ ! -s "$first" ]; then
      echo "airconv emitted an empty $optimization IR module for fixture $fixture_hex" >&2
      exit 1
    fi
  done

  fixture_index=$((fixture_index + 1))
done
