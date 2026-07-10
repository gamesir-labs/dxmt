#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
project_root=$(CDPATH= cd -- "$script_dir/.." && pwd)

export WINEPREFIX="${DXMT_TEST_WINEPREFIX:-$project_root/.wine-x86_64-test}"
export WINEARCH="${WINEARCH:-win64}"
export WINEDEBUG="${WINEDEBUG:--all}"

exec "${DXMT_TEST_WINE:-$project_root/external/wine/build/wine}" "$@"
