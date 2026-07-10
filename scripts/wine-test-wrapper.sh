#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
project_root=$(CDPATH= cd -- "$script_dir/.." && pwd)

export WINEPREFIX="${DXMT_TEST_WINEPREFIX:-$project_root/.wine-x86_64-test}"
export WINEARCH="${WINEARCH:-win64}"
export WINEDEBUG="${WINEDEBUG:--all}"
export DXMT_EXPERIMENT_DX12_SUPPORT="${DXMT_EXPERIMENT_DX12_SUPPORT:-1}"
export WINEDLLOVERRIDES="${WINEDLLOVERRIDES:-d3d10core,d3d11,d3d11_dxmt,d3d12,dxgi,winemetal,winemetal4=n,b}"

wine=${DXMT_TEST_WINE:-}
wine_explicit=0
if [ -n "$wine" ]; then
  wine_explicit=1
  wine_root=
else
  wine_root=${DXMT_TEST_WINE_ROOT:-${DXMT_WINE_ROOT:-}}
fi
runtime_root=${DXMT_TEST_RUNTIME_ROOT:-}

find_wine_launcher() {
  candidate_root=$1
  if [ -x "$candidate_root/bin/wine" ]; then
    printf '%s\n' "$candidate_root/bin/wine"
    return 0
  fi
  if [ -x "$candidate_root/bin/wine64" ]; then
    printf '%s\n' "$candidate_root/bin/wine64"
    return 0
  fi
  return 1
}

if [ -z "$wine" ]; then
  if [ -n "$wine_root" ]; then
    wine=$(find_wine_launcher "$wine_root") || true
  else
    for candidate_root in \
      "$project_root/toolchains/wine" \
      "$project_root/external/wine/build/dxmt-install"
    do
      if wine=$(find_wine_launcher "$candidate_root"); then
        wine_root=$candidate_root
        break
      fi
    done
  fi
elif [ ! -x "$wine" ]; then
  resolved_wine=$(command -v "$wine" 2>/dev/null || true)
  if [ -n "$resolved_wine" ]; then
    wine=$resolved_wine
  fi
fi

if [ -z "$wine" ] || [ ! -x "$wine" ]; then
  printf '%s\n' \
    'No runnable Wine cache was found. Configure wine_install_path or build the managed Wine cache.' >&2
  exit 2
fi

if [ -z "$wine_root" ]; then
  wine_root=$(CDPATH= cd -- "$(dirname -- "$wine")/.." && pwd)
fi
if [ ! -x "$wine_root/bin/wineserver" ] ||
   { [ "$wine_explicit" -eq 0 ] && [ ! -x "$wine_root/bin/winebuild" ]; }; then
  printf 'incomplete Wine cache root: %s\n' "$wine_root" >&2
  exit 2
fi
export DXMT_TEST_WINE_ROOT="$wine_root"

if [ "${DXMT_TEST_REQUIRE_RUNTIME:-0}" = "1" ]; then
  if [ ! -f "$wine_root/.wine-development-cache" ] ||
     ! grep -qx 'schema=2' "$wine_root/.wine-development-cache"; then
    printf 'Wine cache is missing a schema-2 runtime marker: %s\n' "$wine_root" >&2
    exit 2
  fi
  for runtime_dylib in \
    libfreetype.6.dylib \
    libgcrypt.20.dylib \
    libgmp.10.dylib \
    libgnutls.30.dylib \
    libSDL2-2.0.0.dylib \
    libMoltenVK.dylib
  do
    if [ ! -f "$wine_root/lib/$runtime_dylib" ]; then
      printf 'Wine cache is missing runtime dependency %s: %s\n' \
        "$runtime_dylib" "$wine_root" >&2
      exit 2
    fi
  done
fi

if [ -n "$runtime_root" ]; then
  if [ ! -d "$runtime_root/x86_64-windows" ] ||
     [ ! -d "$runtime_root/x86_64-unix" ]; then
    printf 'invalid DXMT_TEST_RUNTIME_ROOT: %s\n' "$runtime_root" >&2
    exit 2
  fi
  export WINEDLLPATH="$runtime_root${WINEDLLPATH:+:$WINEDLLPATH}"

elif [ "${DXMT_TEST_REQUIRE_RUNTIME:-0}" = "1" ] && [ -z "${WINEDLLPATH:-}" ]; then
  printf '%s\n' \
    'DXMT_TEST_RUNTIME_ROOT or WINEDLLPATH is required for Wine tests.' >&2
  exit 2
fi

runtime_library_path="$wine_root/lib:$wine_root/lib/wine/x86_64-unix"
if [ -n "$runtime_root" ]; then
  runtime_library_path="$runtime_root/x86_64-unix:$runtime_library_path"
fi
export DYLD_FALLBACK_LIBRARY_PATH="$runtime_library_path${DYLD_FALLBACK_LIBRARY_PATH:+:$DYLD_FALLBACK_LIBRARY_PATH}"
if [ -f "$wine_root/lib/libMoltenVK.dylib" ]; then
  export WINE_SONAME_LIBVULKAN="$wine_root/lib/libMoltenVK.dylib"
fi

wine_pid=
stop_test_wine() {
  if [ -n "$wine_pid" ]; then
    kill -TERM "$wine_pid" >/dev/null 2>&1 || true
  fi
  WINEPREFIX="$WINEPREFIX" "$wine_root/bin/wineserver" -k >/dev/null 2>&1 || true
  WINEPREFIX="$WINEPREFIX" "$wine_root/bin/wineserver" -w >/dev/null 2>&1 || true
}

handle_signal() {
  stop_test_wine
  exit 143
}
trap handle_signal HUP INT TERM

"$wine" "$@" &
wine_pid=$!
set +e
wait "$wine_pid"
status=$?
set -e
wine_pid=
stop_test_wine
exit "$status"
