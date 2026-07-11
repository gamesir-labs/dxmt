#!/bin/sh
set -eu

script_dir=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
project_root=$(CDPATH='' cd -- "$script_dir/.." && pwd)

if [ "$#" -lt 1 ]; then
  printf 'usage: %s <meson-build-dir> [all|unit|integration] [meson-test-option...]\n' "$0" >&2
  exit 2
fi

case $1 in
  /*) build_dir=$1 ;;
  *) build_dir=$project_root/$1 ;;
esac
shift

mode=${1:-all}
case $mode in
  all|unit|integration)
    if [ "$#" -gt 0 ]; then
      shift
    fi
    ;;
  *) mode=all ;;
esac

suite=all
forwarded_args=
test_args=
while [ "$#" -gt 0 ]; do
  argument=$1
  shift
  case "$argument" in
    --suite=*) suite=${argument#--suite=} ;;
    --suite)
      if [ "$#" -eq 0 ]; then
        printf '%s\n' '--suite requires a value' >&2
        exit 2
      fi
      suite=$1
      shift
      ;;
    --test-args=*)
      value=${argument#--test-args=}
      test_args="${test_args:+$test_args }$value"
      ;;
    --test-args)
      if [ "$#" -eq 0 ]; then
        printf '%s\n' '--test-args requires a value' >&2
        exit 2
      fi
      test_args="${test_args:+$test_args }$1"
      shift
      ;;
    *)
      if [ -z "$forwarded_args" ]; then
        forwarded_args=$argument
      else
        forwarded_args="$forwarded_args
$argument"
      fi
      continue
      ;;
  esac
done

case $suite in
  all|framework|d3d10|d3d11|d3d12) ;;
  *) printf 'unsupported Wine test suite: %s\n' "$suite" >&2; exit 2 ;;
esac

runtime_root=$("$script_dir/stage-wine-test-runtime.sh" "$build_dir" "" "$suite" "$mode")
export DXMT_TEST_RUNTIME_ROOT="$runtime_root"

wine_root=$(meson introspect "$build_dir" --buildoptions | python3 -c '
import json
import os
import sys

source_root = sys.argv[1]
options = {item["name"]: item["value"] for item in json.load(sys.stdin)}
wine_root = options.get("wine_install_path", "")
if not wine_root:
    wine_build_path = options.get("wine_build_path", "")
    if wine_build_path:
        wine_root = os.path.join(wine_build_path, "dxmt-install")
if not wine_root:
    raise SystemExit("Meson build has no Wine cache root")
if not os.path.isabs(wine_root):
    wine_root = os.path.join(source_root, wine_root)
print(os.path.realpath(wine_root))
' "$project_root")
export DXMT_TEST_WINE_ROOT="$wine_root"
export DXMT_TEST_WINEPREFIX="${DXMT_TEST_WINEPREFIX:-$build_dir/wine-prefix}"

prefix_ready() {
  [ -s "$DXMT_TEST_WINEPREFIX/system.reg" ] &&
    [ -s "$DXMT_TEST_WINEPREFIX/user.reg" ] &&
    [ -s "$DXMT_TEST_WINEPREFIX/userdef.reg" ] &&
    [ -f "$DXMT_TEST_WINEPREFIX/.update-timestamp" ] &&
    [ -e "$DXMT_TEST_WINEPREFIX/dosdevices/c:" ]
}

wine_identity=$("$wine_root/bin/wine" --version)
prefix_marker=$DXMT_TEST_WINEPREFIX/.dxmt-test-ready
prefix_identity=
if [ -f "$prefix_marker" ]; then
  prefix_identity=$(cat "$prefix_marker")
fi
if prefix_ready && [ -z "$prefix_identity" ]; then
  printf '%s\n' "$wine_identity" > "$prefix_marker"
  prefix_identity=$wine_identity
fi

if ! prefix_ready || [ "$prefix_identity" != "$wine_identity" ]; then
  DXMT_TEST_REQUIRE_RUNTIME=0 "$script_dir/wine-test-wrapper.sh" wineboot -u >&2 &
  wineboot_pid=$!
  while ! prefix_ready; do
    if ! kill -0 "$wineboot_pid" 2>/dev/null; then
      wait "$wineboot_pid"
      printf 'Wine prefix initialization exited before the prefix was ready\n' >&2
      exit 1
    fi
    sleep 1
  done
  WINEPREFIX="$DXMT_TEST_WINEPREFIX" "$wine_root/bin/wineserver" -k >/dev/null 2>&1 || true
  wait "$wineboot_pid" 2>/dev/null || true
  printf '%s\n' "$wine_identity" > "$prefix_marker"
else
  WINEPREFIX="$DXMT_TEST_WINEPREFIX" "$wine_root/bin/wineserver" -k >/dev/null 2>&1 || true
fi
WINEPREFIX="$DXMT_TEST_WINEPREFIX" "$wine_root/bin/wineserver" -w

case $mode in
  all|unit)
    set --
    if [ -n "$forwarded_args" ]; then
      old_ifs=$IFS
      IFS='
'
      for argument in $forwarded_args; do set -- "$@" "$argument"; done
      IFS=$old_ifs
    fi
    scheduler_args="--dxmt-test-suite=$suite"
    if [ -n "$test_args" ]; then
      scheduler_args="$scheduler_args $test_args"
    fi
    meson test -C "$build_dir" --no-rebuild --suite wine --print-errorlogs \
      --test-args="$scheduler_args" "$@"
    ;;
esac

case $mode in
  all|integration)
    benchmark_suite=wine
    if [ "$suite" != all ]; then benchmark_suite=$suite; fi
    set --
    if [ -n "$forwarded_args" ]; then
      old_ifs=$IFS
      IFS='
'
      for argument in $forwarded_args; do set -- "$@" "$argument"; done
      IFS=$old_ifs
    fi
    if [ -n "$test_args" ]; then
      set -- "$@" "--test-args=$test_args"
    fi
    meson test -C "$build_dir" --no-rebuild --benchmark \
      --suite "$benchmark_suite" --print-errorlogs "$@"
    ;;
esac
