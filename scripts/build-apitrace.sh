#!/bin/sh
set -eu

if [ "$#" -ne 3 ]; then
  echo "usage: build-apitrace.sh <source-dir> <build-dir> <stamp-file>" >&2
  exit 2
fi

src=$1
build=$2
stamp=$3

if [ ! -f "$src/CMakeLists.txt" ]; then
  echo "apitrace source directory not found: $src" >&2
  exit 1
fi

cmake -S "$src" -B "$build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DAPITRACE_BUILD_TOOLS=OFF \
  -DAPITRACE_BUILD_WINDOWS_DLLS=OFF \
  -DAPITRACE_BUILD_METAL_BACKEND=ON \
  -DAPITRACE_BUILD_D3D_NATIVE_RETRACE=OFF

cmake --build "$build" --target apitrace_core apitrace_platform_apple_metal

mkdir -p "$(dirname "$stamp")"
: > "$stamp"
