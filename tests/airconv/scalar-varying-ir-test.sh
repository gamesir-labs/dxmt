#!/bin/sh
set -eu

airconv=$1
fixture_hex=$2
work_dir=$(mktemp -d "${TMPDIR:-/tmp}/dxmt-airconv-scalar-varying.XXXXXX")
trap 'rm -rf "$work_dir"' EXIT HUP INT TERM

fixture_dxbc="$work_dir/scalar-varying-vs.dxbc"
shader_ir="$work_dir/scalar-varying-vs.ll"

xxd -r -p "$fixture_hex" "$fixture_dxbc"
"$airconv" -S --O0 "$fixture_dxbc" -o "$shader_ir"

if ! grep -q 'user(texcoord1)' "$shader_ir"; then
  echo 'scalar varying was not emitted with its cross-stage semantic name' >&2
  exit 1
fi

if grep -n 'undef' "$shader_ir"; then
  echo 'scalar varying output contains undefined inactive lanes' >&2
  exit 1
fi
