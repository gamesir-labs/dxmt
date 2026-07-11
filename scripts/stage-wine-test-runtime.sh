#!/bin/sh
set -eu

script_dir=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
project_root=$(CDPATH='' cd -- "$script_dir/.." && pwd)

if [ "$#" -lt 1 ] || [ "$#" -gt 4 ]; then
  printf 'usage: %s <meson-build-dir> [stage-dir] [all|framework|d3d10|d3d11|d3d12] [all|unit|integration]\n' "$0" >&2
  exit 2
fi

case $1 in
  /*) build_dir=$1 ;;
  *) build_dir=$project_root/$1 ;;
esac
stage_dir=${2:-$build_dir/wine-test-runtime-stage}
if [ -z "$stage_dir" ]; then stage_dir=$build_dir/wine-test-runtime-stage; fi
suite=${3:-all}
mode=${4:-all}
case $stage_dir in
  /*) ;;
  *) stage_dir=$project_root/$stage_dir ;;
esac

if [ ! -f "$build_dir/meson-private/coredata.dat" ]; then
  printf 'not a Meson build directory: %s\n' "$build_dir" >&2
  exit 2
fi

rm -rf "$stage_dir"
mkdir -p "$stage_dir"

case $suite in
  all)
    build_targets="dxmt-runtime dxmt-wine-tests"
    install_tags="runtime-common,runtime-metal3,runtime-metal4,nvext"
    ;;
  framework)
    build_targets="dxmt-wine-tests-framework"
    install_tags=
    ;;
  d3d10)
    build_targets="dxmt-d3d10 dxmt-wine-tests-d3d10"
    install_tags="runtime-common,runtime-metal3"
    ;;
  d3d11)
    build_targets="dxmt-d3d11 dxmt-wine-tests-d3d11"
    install_tags="runtime-common,runtime-metal3"
    ;;
  d3d12)
    build_targets="dxmt-d3d12 dxmt-wine-tests-d3d12"
    install_tags="runtime-common,runtime-metal4"
    ;;
  *) printf 'unsupported Wine test suite: %s\n' "$suite" >&2; exit 2 ;;
esac
if [ "$mode" = all ] || [ "$mode" = integration ]; then
  build_targets="$build_targets dxmt-benchmarks"
fi

# The target names are fixed above and contain no user-provided shell text.
# shellcheck disable=SC2086
meson compile -C "$build_dir" $build_targets >&2
if [ -n "$install_tags" ]; then
  DESTDIR="$stage_dir" meson install -C "$build_dir" --no-rebuild \
    --tags "$install_tags" >&2
fi

prefix=$(meson introspect "$build_dir" --buildoptions | python3 -c '
import json
import sys

for option in json.load(sys.stdin):
    if option.get("name") == "prefix":
        print(option["value"])
        break
')
if [ -z "$prefix" ]; then
  printf 'failed to resolve Meson install prefix\n' >&2
  exit 2
fi

runtime_root=$stage_dir$prefix
mkdir -p "$runtime_root/x86_64-windows" "$runtime_root/x86_64-unix"
required_paths=
case $suite in
  all) required_paths='x86_64-windows/d3d11.dll
x86_64-windows/d3d12.dll
x86_64-windows/dxgi.dll
x86_64-windows/winemetal.dll
x86_64-windows/winemetal4.dll
x86_64-unix/winemetal.so
x86_64-unix/winemetal4.so' ;;
  d3d10) required_paths='x86_64-windows/d3d10core.dll
x86_64-windows/d3d11.dll
x86_64-windows/dxgi.dll
x86_64-unix/winemetal.so' ;;
  d3d11) required_paths='x86_64-windows/d3d11.dll
x86_64-windows/dxgi.dll
x86_64-unix/winemetal.so' ;;
  d3d12) required_paths='x86_64-windows/d3d12.dll
x86_64-windows/dxgi.dll
x86_64-windows/winemetal4.dll
x86_64-unix/winemetal4.so' ;;
esac
old_ifs=$IFS
IFS='
'
for relative_path in $required_paths; do
  if [ ! -f "$runtime_root/$relative_path" ]; then
    printf 'staged runtime is missing %s\n' "$relative_path" >&2
    exit 2
  fi
done
IFS=$old_ifs

printf '%s\n' "$runtime_root"
