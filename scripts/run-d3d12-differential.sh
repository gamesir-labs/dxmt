#!/bin/sh
set -eu

script_dir=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
project_root=$(CDPATH='' cd -- "$script_dir/.." && pwd)

if [ "$#" -lt 2 ] || [ "$#" -gt 3 ]; then
  printf 'usage: %s <meson-build-dir> <warp-reference.json> [candidate.json]\n' \
    "$0" >&2
  exit 2
fi

case $1 in
  /*) build_dir=$1 ;;
  *) build_dir=$project_root/$1 ;;
esac
case $2 in
  /*) reference=$2 ;;
  *) reference=$project_root/$2 ;;
esac
if [ "$#" -eq 3 ]; then
  case $3 in
    /*) candidate=$3 ;;
    *) candidate=$project_root/$3 ;;
  esac
else
  candidate=$build_dir/d3d12-differential-dxmt.json
fi

if [ ! -f "$reference" ]; then
  printf 'WARP reference snapshot not found: %s\n' "$reference" >&2
  exit 2
fi

runtime_root=$("$script_dir/stage-wine-test-runtime.sh" \
  "$build_dir" "" d3d12 unit)
export DXMT_TEST_RUNTIME_ROOT=$runtime_root
export DXMT_TEST_REQUIRE_RUNTIME=1
profile_root=$(dirname -- "$build_dir")
export DXMT_TEST_WINEPREFIX="${DXMT_TEST_WINEPREFIX:-$profile_root/prefix}"

if [ -z "${DXMT_TEST_WINE_ROOT:-${DXMT_WINE_ROOT:-}}" ]; then
  DXMT_TEST_WINE_ROOT=$(meson introspect "$build_dir" --buildoptions | \
    python3 -c '
import json
import os
import sys

source_root = sys.argv[1]
options = {item["name"]: item["value"] for item in json.load(sys.stdin)}
wine_root = options.get("wine_install_path", "")
if not wine_root:
    build_path = options.get("wine_build_path", "")
    if build_path:
        wine_root = os.path.join(build_path, "dxmt-install")
if not wine_root:
    raise SystemExit("Meson build has no Wine cache root")
if not os.path.isabs(wine_root):
    wine_root = os.path.join(source_root, wine_root)
print(os.path.realpath(wine_root))
' "$project_root")
  export DXMT_TEST_WINE_ROOT
fi
wine_root=${DXMT_TEST_WINE_ROOT:-$DXMT_WINE_ROOT}

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
if ! prefix_ready || [ "$prefix_identity" != "$wine_identity" ]; then
  DXMT_TEST_REQUIRE_RUNTIME=0 "$script_dir/wine-test-wrapper.sh" wineboot -u \
    >&2 &
  wineboot_pid=$!
  while ! prefix_ready; do
    if ! kill -0 "$wineboot_pid" 2>/dev/null; then
      wait "$wineboot_pid"
      printf '%s\n' \
        'Wine prefix initialization exited before the prefix was ready' >&2
      exit 1
    fi
    sleep 1
  done
  WINEPREFIX="$DXMT_TEST_WINEPREFIX" "$wine_root/bin/wineserver" -k \
    >/dev/null 2>&1 || true
  wait "$wineboot_pid" 2>/dev/null || true
  printf '%s\n' "$wine_identity" > "$prefix_marker"
else
  WINEPREFIX="$DXMT_TEST_WINEPREFIX" "$wine_root/bin/wineserver" -k \
    >/dev/null 2>&1 || true
fi
WINEPREFIX="$DXMT_TEST_WINEPREFIX" "$wine_root/bin/wineserver" -w

oracle=$build_dir/tests/d3d12/dxmt-d3d12-differential-oracle.exe
if [ ! -f "$oracle" ]; then
  printf 'D3D12 differential oracle was not built: %s\n' "$oracle" >&2
  exit 2
fi

mkdir -p "$(dirname -- "$candidate")"
temporary=$candidate.tmp.$$
trap 'rm -f "$temporary"' EXIT HUP INT TERM
windows_temporary=$(WINEPREFIX="$DXMT_TEST_WINEPREFIX" \
  "$wine_root/bin/winepath" -w "$temporary")
"$script_dir/wine-test-wrapper.sh" "$oracle" --adapter=default \
  "--output=$windows_temporary"
python3 -m json.tool "$temporary" >/dev/null
mv "$temporary" "$candidate"
trap - EXIT HUP INT TERM

python3 "$project_root/tests/d3d12/differential/compare_snapshots.py" \
  "$reference" "$candidate"
printf 'DXMT differential snapshot: %s\n' "$candidate"
