#!/bin/sh
set -eu

airconv=$1
fixture_hex=$2
work_dir=$(mktemp -d "${TMPDIR:-/tmp}/dxmt-airconv-invalid.XXXXXX")
trap 'rm -rf "$work_dir"' EXIT HUP INT TERM

valid="$work_dir/valid.dxbc"
xxd -r -p "$fixture_hex" "$valid"

expect_rejected() {
  name=$1
  input=$2
  output="$work_dir/$name.ll"
  if "$airconv" -S --O0 "$input" -o "$output" >/dev/null 2>&1; then
    echo "airconv accepted malformed container: $name" >&2
    exit 1
  fi
}

empty="$work_dir/empty.dxbc"
: >"$empty"
expect_rejected empty "$empty"

truncated_header="$work_dir/truncated-header.dxbc"
head -c 31 "$valid" >"$truncated_header"
expect_rejected truncated-header "$truncated_header"

bad_magic="$work_dir/bad-magic.dxbc"
cp "$valid" "$bad_magic"
printf '\000\000\000\000' | dd of="$bad_magic" bs=1 seek=0 conv=notrunc >/dev/null 2>&1
expect_rejected bad-magic "$bad_magic"

truncated_payload="$work_dir/truncated-payload.dxbc"
size=$(wc -c <"$valid")
head -c $((size - 1)) "$valid" >"$truncated_payload"
expect_rejected truncated-payload "$truncated_payload"

oversized_part_table="$work_dir/oversized-part-table.dxbc"
cp "$valid" "$oversized_part_table"
printf '\377\377\377\177' | dd of="$oversized_part_table" bs=1 seek=28 conv=notrunc >/dev/null 2>&1
expect_rejected oversized-part-table "$oversized_part_table"
