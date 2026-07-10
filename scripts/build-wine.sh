#!/bin/sh
set -eu

if [ "$#" -lt 13 ]; then
  echo "usage: build-wine.sh <source-dir> <build-dir> <runtime-root> <runtime-preparer> <windows-arch> <unix-arch> <libwinecrt0> <libntdll> <dbghelp> <winebuild> <winemac> <ntdll-so> <runtime-manifest> [configure-arg...]" >&2
  exit 2
fi

src=$1
build=$2
runtime_root=$3
runtime_preparer=$4
windows_arch=$5
unix_arch=$6
shift 6
out_winecrt0=$1
out_ntdll=$2
out_dbghelp=$3
out_winebuild=$4
out_winemac=$5
out_ntdll_so=$6
out_runtime_manifest=$7
shift 7

if [ ! -d "$src" ]; then
  echo "Wine source directory not found: $src" >&2
  exit 1
fi

mkdir -p "$build"

if [ ! -x "$src/configure" ]; then
  (cd "$src" && ./autogen.sh)
fi

config_stamp=$build/.dxmt-wine-cache-config
build_stamp=$runtime_root/.dxmt-wine-cache
next_config=$build/.dxmt-wine-cache-config.$$
next_build=$build/.dxmt-wine-cache-state.$$

cleanup() {
  rm -f "$next_config" "$next_build"
}
trap cleanup EXIT HUP INT TERM

{
  printf 'schema=1\n'
  printf 'source=%s\n' "$src"
  printf 'runtime_root=%s\n' "$runtime_root"
  printf 'windows_arch=%s\n' "$windows_arch"
  printf 'unix_arch=%s\n' "$unix_arch"
  for arg do
    printf 'configure_arg=%s\n' "$arg"
  done
} > "$next_config"

if [ -f "$build/Makefile" ] && ! cmp -s "$next_config" "$config_stamp"; then
  make -C "$build" distclean
fi

if [ ! -f "$build/Makefile" ]; then
  (cd "$build" && "$src/configure" "$@" --prefix="$runtime_root")
fi
cp "$next_config" "$config_stamp"

source_revision=unknown
source_diff=unknown
if command -v git >/dev/null 2>&1 && git -C "$src" rev-parse HEAD >/dev/null 2>&1; then
  source_revision=$(git -C "$src" rev-parse HEAD)
  source_diff=$(git -C "$src" diff --no-ext-diff --binary HEAD -- | shasum -a 256 | awk '{print $1}')
fi

{
  cat "$next_config"
  printf 'source_revision=%s\n' "$source_revision"
  printf 'source_diff=%s\n' "$source_diff"
} > "$next_build"

make_jobs=${WINE_BUILD_JOBS:-}
if [ -z "$make_jobs" ]; then
  make_jobs=$(getconf _NPROCESSORS_ONLN 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
fi

build_cache_ready() {
  [ -f "$build/dlls/winecrt0/$windows_arch/libwinecrt0.a" ] &&
    [ -f "$build/dlls/ntdll/$windows_arch/libntdll.a" ] &&
    [ -f "$build/dlls/dbghelp/$windows_arch/dbghelp.dll" ] &&
    [ -x "$build/tools/winebuild/winebuild" ]
}

runtime_install_ready() {
  { [ -x "$runtime_root/bin/wine" ] || [ -x "$runtime_root/bin/wine64" ]; } &&
    [ -x "$runtime_root/bin/wineserver" ] &&
    [ -x "$runtime_root/bin/winebuild" ] &&
    [ -f "$runtime_root/lib/wine/$windows_arch/libwinecrt0.a" ] &&
    [ -f "$runtime_root/lib/wine/$windows_arch/libntdll.a" ] &&
    [ -f "$runtime_root/lib/wine/$windows_arch/dbghelp.dll" ]
}

runtime_dependency_cache_ready() {
  if [ -z "$runtime_preparer" ]; then
    return 0
  fi
  [ -f "$runtime_root/.wine-development-cache" ] &&
    grep -qx 'schema=2' "$runtime_root/.wine-development-cache" &&
    [ -f "$runtime_root/lib/libfreetype.6.dylib" ] &&
    [ -f "$runtime_root/lib/libgcrypt.20.dylib" ] &&
    [ -f "$runtime_root/lib/libgmp.10.dylib" ] &&
    [ -f "$runtime_root/lib/libgnutls.30.dylib" ] &&
    [ -f "$runtime_root/lib/libSDL2-2.0.0.dylib" ] &&
    [ -f "$runtime_root/lib/libMoltenVK.dylib" ]
}

if [ "$unix_arch" = "x86_64-unix" ] || [ "$unix_arch" = "aarch64-unix" ]; then
  build_cache_ready() {
    [ -f "$build/dlls/winecrt0/$windows_arch/libwinecrt0.a" ] &&
      [ -f "$build/dlls/ntdll/$windows_arch/libntdll.a" ] &&
      [ -f "$build/dlls/dbghelp/$windows_arch/dbghelp.dll" ] &&
      [ -x "$build/tools/winebuild/winebuild" ] &&
      [ -f "$build/dlls/winemac.drv/winemac.so" ] &&
      [ -f "$build/dlls/ntdll/ntdll.so" ]
  }
  runtime_install_ready() {
    { [ -x "$runtime_root/bin/wine" ] || [ -x "$runtime_root/bin/wine64" ]; } &&
      [ -x "$runtime_root/bin/wineserver" ] &&
      [ -x "$runtime_root/bin/winebuild" ] &&
      [ -f "$runtime_root/lib/wine/$windows_arch/libwinecrt0.a" ] &&
      [ -f "$runtime_root/lib/wine/$windows_arch/libntdll.a" ] &&
      [ -f "$runtime_root/lib/wine/$windows_arch/dbghelp.dll" ] &&
      [ -f "$runtime_root/lib/wine/$unix_arch/winemac.so" ] &&
      [ -f "$runtime_root/lib/wine/$unix_arch/ntdll.so" ]
  }
fi

runtime_cache_ready() {
  runtime_install_ready && runtime_dependency_cache_ready
}

if ! build_cache_ready || ! runtime_install_ready || ! cmp -s "$next_build" "$build_stamp"; then
  make -C "$build" -j "$make_jobs"
  rm -rf "$runtime_root"
  make -C "$build" install
  cp "$next_build" "$build_stamp"
fi

if [ -n "$runtime_preparer" ]; then
  if [ ! -x "$runtime_preparer" ]; then
    echo "Wine runtime preparer is not executable: $runtime_preparer" >&2
    exit 1
  fi
  "$runtime_preparer" \
    --install-root "$runtime_root" \
    --cache-dir "$build/dxmt-runtime-deps"
fi
runtime_cache_ready || {
  echo "Wine install did not produce a complete runtime cache: $runtime_root" >&2
  exit 1
}

copy_artifact() {
  src_file=$1
  dst_file=$2

  mkdir -p "$(dirname "$dst_file")"
  if [ -e "$src_file" ]; then
    if [ ! -e "$dst_file" ] || ! cmp -s "$src_file" "$dst_file"; then
      cp "$src_file" "$dst_file"
    fi
  else
    echo "Wine build did not produce expected artifact: $src_file" >&2
    exit 1
  fi
}

copy_artifact "$build/dlls/winecrt0/$windows_arch/libwinecrt0.a" "$out_winecrt0"
copy_artifact "$build/dlls/ntdll/$windows_arch/libntdll.a" "$out_ntdll"
copy_artifact "$build/dlls/dbghelp/$windows_arch/dbghelp.dll" "$out_dbghelp"
copy_artifact "$build/tools/winebuild/winebuild" "$out_winebuild"
chmod +x "$out_winebuild"

if [ "$unix_arch" = "x86_64-unix" ] || [ "$unix_arch" = "aarch64-unix" ]; then
  copy_artifact "$build/dlls/winemac.drv/winemac.so" "$out_winemac"
  copy_artifact "$build/dlls/ntdll/ntdll.so" "$out_ntdll_so"
else
  : >"$out_winemac"
  : >"$out_ntdll_so"
fi

mkdir -p "$(dirname "$out_runtime_manifest")"
copy_artifact "$build_stamp" "$out_runtime_manifest"
