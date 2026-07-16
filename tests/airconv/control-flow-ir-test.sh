#!/bin/sh
set -eu

airconv=$1
fixture_hex=$2
work_dir=$(mktemp -d "${TMPDIR:-/tmp}/dxmt-airconv-control-flow.XXXXXX")
trap 'rm -rf "$work_dir"' EXIT HUP INT TERM

fixture="$work_dir/control-flow.dxbc"
shader_ir="$work_dir/control-flow.ll"
xxd -r -p "$fixture_hex" "$fixture"
"$airconv" -S --O0 "$fixture" -o "$shader_ir"

require_ir() {
  description=$1
  pattern=$2
  if ! grep -Eq "$pattern" "$shader_ir"; then
    echo "airconv IR is missing $description" >&2
    exit 1
  fi
}

require_ir 'the fragment entry point' '^define .* @shader_main\('
require_ir 'a conditional branch' 'br i1 .*label %if_true'
require_ir 'the loop header' '^loop_entrance:'
require_ir 'the loop exit' '^end_loop:'
require_ir 'the loop back edge' 'br label %loop_entrance'
require_ir 'the switch terminator' 'switch i32 .*label %case_default'
require_ir 'the switch merge block' '^end_switch:'

case_count=$(grep -Ec 'i32 [012], label %switch_case' "$shader_ir")
if [ "$case_count" -ne 3 ]; then
  echo "airconv lowered $case_count explicit switch cases, expected 3" >&2
  exit 1
fi

if grep -Eq 'unexpected undefined basicblock|UnsupportedFeature' "$shader_ir"; then
  echo 'airconv left an invalid control-flow block in the IR' >&2
  exit 1
fi
