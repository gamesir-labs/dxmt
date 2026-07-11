#!/bin/sh
set -eu

if [ "$#" -lt 3 ] || [ "$#" -gt 5 ]; then
  echo "usage: build-apitrace.sh <source-dir> <build-dir> <stamp-file> [target=native|mingw] [toolchain-file]" >&2
  exit 2
fi

src=$1
build=$2
stamp=$3
target=${4:-native}
toolchain=${5:-}

if [ ! -f "$src/CMakeLists.txt" ]; then
  echo "apitrace source directory not found: $src" >&2
  exit 1
fi

source_revision=$(git -C "$src" rev-parse HEAD 2>/dev/null || printf 'unversioned')
source_diff=$(git -C "$src" diff --binary HEAD 2>/dev/null | shasum -a 256 | awk '{print $1}')
script_hash=$(shasum -a 256 "$0" | awk '{print $1}')
cmake_identity=$(cmake --version | sed -n '1p')
toolchain_hash=none
if [ -n "$toolchain" ] && [ -f "$toolchain" ]; then
  toolchain_hash=$(shasum -a 256 "$toolchain" | awk '{print $1}')
fi
compiler_identity=$(cc --version 2>/dev/null | sed -n '1p' || printf 'unknown')
cache_key=$(printf '%s\n' \
  'schema=1' \
  "target=$target" \
  "source_revision=$source_revision" \
  "source_diff=$source_diff" \
  "script_hash=$script_hash" \
  "cmake=$cmake_identity" \
  "toolchain=$toolchain_hash" \
  "compiler=$compiler_identity" | shasum -a 256 | awk '{print $1}')

cas_root=${DXMT_APITRACE_CAS:-}
cache_archive=
if [ -n "$cas_root" ]; then
  cache_archive=$cas_root/$target/$cache_key.tar.gz
fi

if [ "${DXMT_APITRACE_LOCKED:-0}" != 1 ] && \
   [ -n "${DXMT_BUILDER_BINARY:-}" ] && [ -x "${DXMT_BUILDER_BINARY}" ]; then
  exec "${DXMT_BUILDER_BINARY}" internal lock-command \
    --name "apitrace-$cache_key" -- \
    env DXMT_APITRACE_LOCKED=1 "$0" "$@"
fi

case "$target" in
  native) archives='libapitrace_core.a libapitrace_platform_apple_metal.a' ;;
  mingw) archives='libapitrace_core.a libapitrace_d3d11_capture.a libapitrace_d3d12_capture.a' ;;
  *) echo "build-apitrace.sh: unknown target '$target' (expected native or mingw)" >&2; exit 2 ;;
esac

if [ -n "$cache_archive" ] && [ -f "$cache_archive" ]; then
  rm -rf "$build"
  mkdir -p "$build"
  tar -xzf "$cache_archive" -C "$build"
  mkdir -p "$(dirname "$stamp")"
  : > "$stamp"
  exit 0
fi

ccache_options=
if command -v ccache >/dev/null 2>&1; then
  ccache_options='-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache'
fi

case "$target" in
  native)
    # The values in ccache_options are fixed CMake arguments without spaces.
    # shellcheck disable=SC2086
    cmake -S "$src" -B "$build" -G Ninja \
      $ccache_options \
      -DCMAKE_BUILD_TYPE=Debug \
      -DAPITRACE_BUILD_TOOLS=OFF \
      -DAPITRACE_BUILD_WINDOWS_DLLS=OFF \
      -DAPITRACE_BUILD_METAL_BACKEND=ON \
      -DAPITRACE_BUILD_D3D_NATIVE_RETRACE=OFF
    cmake --build "$build" --target apitrace_core apitrace_platform_apple_metal
    ;;
  mingw)
    if [ -z "$toolchain" ]; then
      echo "build-apitrace.sh: target=mingw requires a toolchain file as 5th arg" >&2
      exit 2
    fi
    if [ ! -f "$toolchain" ]; then
      echo "build-apitrace.sh: toolchain file not found: $toolchain" >&2
      exit 1
    fi
    # The values in ccache_options are fixed CMake arguments without spaces.
    # shellcheck disable=SC2086
    cmake -S "$src" -B "$build" -G Ninja \
      $ccache_options \
      -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
      -DCMAKE_BUILD_TYPE=Release \
      -DAPITRACE_BUILD_TOOLS=OFF \
      -DAPITRACE_BUILD_WINDOWS_DLLS=OFF \
      -DAPITRACE_BUILD_METAL_BACKEND=OFF \
      -DAPITRACE_BUILD_D3D_NATIVE_RETRACE=OFF \
      -DAPITRACE_BUILD_PE_CAPTURE_STATIC=ON
    cmake --build "$build" --target \
      apitrace_core \
      apitrace_platform_windows_d3d11 \
      apitrace_platform_windows_d3d12
    ;;
esac

for archive in $archives; do
  if [ ! -f "$build/$archive" ]; then
    echo "build-apitrace.sh: expected archive was not produced: $build/$archive" >&2
    exit 1
  fi
done

if [ -n "$cache_archive" ]; then
  mkdir -p "$(dirname "$cache_archive")"
  incomplete=$cache_archive.incomplete-$$
  trap 'rm -f "$incomplete"' EXIT HUP INT TERM
  # The archive list is fixed above and contains no user input.
  # shellcheck disable=SC2086
  tar -czf "$incomplete" -C "$build" $archives
  mv "$incomplete" "$cache_archive"
  trap - EXIT HUP INT TERM
fi

mkdir -p "$(dirname "$stamp")"
: > "$stamp"
