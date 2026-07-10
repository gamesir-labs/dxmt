#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
project_root=$(CDPATH= cd -- "$script_dir/.." && pwd)

if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
  printf 'usage: %s <meson-build-dir> [stage-dir]\n' "$0" >&2
  exit 2
fi

case $1 in
  /*) build_dir=$1 ;;
  *) build_dir=$project_root/$1 ;;
esac
stage_dir=${2:-$build_dir/wine-test-runtime-stage}
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

meson compile -C "$build_dir" >&2
DESTDIR="$stage_dir" meson install -C "$build_dir" --tags runtime >&2

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
for relative_path in \
  x86_64-windows/d3d12.dll \
  x86_64-windows/dxgi.dll \
  x86_64-windows/winemetal4.dll \
  x86_64-unix/winemetal4.so
do
  if [ ! -f "$runtime_root/$relative_path" ]; then
    printf 'staged runtime is missing %s\n' "$relative_path" >&2
    exit 2
  fi
done

printf '%s\n' "$runtime_root"
