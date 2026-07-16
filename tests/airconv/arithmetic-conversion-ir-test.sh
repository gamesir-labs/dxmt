#!/bin/sh
set -eu

airconv=$1
fixture_hex=$2
work_dir=$(mktemp -d "${TMPDIR:-/tmp}/dxmt-airconv-arithmetic.XXXXXX")
trap 'rm -rf "$work_dir"' EXIT HUP INT TERM

fixture="$work_dir/arithmetic.dxbc"
shader_ir="$work_dir/arithmetic.ll"
xxd -r -p "$fixture_hex" "$fixture"
"$airconv" -S --O0 "$fixture" -o "$shader_ir"

require_ir() {
  description=$1
  pattern=$2
  if ! grep -Eq "$pattern" "$shader_ir"; then
    echo "airconv arithmetic IR is missing $description" >&2
    exit 1
  fi
}

require_ir 'a masked left shift' 'shl i32 '
require_ir 'a logical right shift' 'lshr i32 '
require_ir 'an arithmetic right shift' 'ashr i32 '
require_ir 'float-to-unsigned conversion' '@air\.convert\.u\.i32\.f\.f32'
require_ir 'float-to-signed conversion' '@air\.convert\.s\.i32\.f\.f32'
require_ir 'unsigned-to-float conversion' '@air\.convert\.f\.f32\.u\.i32'
require_ir 'signed-to-float conversion' '@air\.convert\.f\.f32\.s\.i32'
require_ir 'float-to-half conversion' '@air\.convert\.f\.f16\.f\.f32'
require_ir 'half-to-float conversion' '@air\.convert\.f\.f32\.f\.f16'
require_ir 'integer population count' '@air\.popcount\.i32'
require_ir 'an unsigned render-target signature' '"air\.arg_type_name", !"uint4"'
