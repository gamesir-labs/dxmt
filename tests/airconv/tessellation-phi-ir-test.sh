#!/bin/sh
set -eu

airconv=$1
hull_hex=$2
domain_hex=$3
work_dir=$(mktemp -d "${TMPDIR:-/tmp}/dxmt-airconv-tess-phi.XXXXXX")
trap 'rm -rf "$work_dir"' EXIT HUP INT TERM

hull="$work_dir/phi-hull.dxbc"
domain="$work_dir/phi-domain.dxbc"
shader_ir="$work_dir/phi-domain.ll"
xxd -r -p "$hull_hex" "$hull"
xxd -r -p "$domain_hex" "$domain"

"$airconv" -S --O0 --hull-before-domain="$hull" "$domain" -o "$shader_ir"

require_ir() {
  description=$1
  pattern=$2
  if ! grep -Eq "$pattern" "$shader_ir"; then
    echo "airconv tessellation IR is missing $description" >&2
    exit 1
  fi
}

require_ir 'the mesh entry point' '^define void @shader_main\('
require_ir 'the domain iteration loop' '^vertex_start:'
require_ir 'the loop-carried phi node' 'phi i32 \[ 0, %entry \], \[ .*%vertex_end \]'
require_ir 'the phi back-edge block' '^vertex_end:'
require_ir 'the loop back edge' 'br i1 .*label %vertex_start'
require_ir 'mesh-stage metadata' '^!air\.mesh = '
