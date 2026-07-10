#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"

BUILD_DIR="${DXMT_BUILD_DIR:-${REPO_ROOT}/.cache/build/gamehub-runtime}"
NATIVE_LLVM_PATH="${DXMT_NATIVE_LLVM_PATH:-/usr/local/opt/llvm@15}"
GAMEHUB_BUNDLE_ID="${GAMEHUB_BUNDLE_ID:-com.gamemac.test}"
GAMEHUB_DXMT_PACKAGE="${GAMEHUB_DXMT_PACKAGE:-dxmt-v0.80}"
GAMEHUB_DXMT_ROOT="${GAMEHUB_DXMT_ROOT:-${HOME}/Library/Application Support/${GAMEHUB_BUNDLE_ID}/wine-engine/downloads/${GAMEHUB_DXMT_PACKAGE}}"
GAMEHUB_VERSION_MODE="${DXMT_GAMEHUB_VERSION_MODE:-auto}"
GAMEHUB_VERSION_OVERRIDE="${DXMT_GAMEHUB_VERSION:-}"
BACKUP_ROOT="${DXMT_GAMEHUB_BACKUP_ROOT:-${HOME}/Library/Caches/dxmt-runtime-smoke}"
STAGE_DIR="${DXMT_GAMEHUB_STAGE_DIR:-${REPO_ROOT}/.cache/stage/gamehub-test}"
BACKUP_TAG="${DXMT_GAMEHUB_BACKUP_TAG:-$(date -u +%Y%m%dT%H%M%SZ)}"
BACKUP_DIR="${BACKUP_ROOT}/gamehub-${GAMEHUB_DXMT_PACKAGE}-backup-${BACKUP_TAG}"
ENABLE_APITRACE="${DXMT_GAMEHUB_ENABLE_APITRACE:-0}"
APITRACE_OUTPUT_DIR="${DXMT_GAMEHUB_APITRACE_OUTPUT_DIR:-${REPO_ROOT}/tmp/gamehub-apitrace}"
APITRACE_VERBOSE="${DXMT_GAMEHUB_APITRACE_VERBOSE:-0}"
GAMEHUB_WINEDEBUG="${DXMT_GAMEHUB_WINEDEBUG:--all}"

runtime_files=(
  "x86_64-windows/winemetal.dll"
  "x86_64-windows/winemetal4.dll"
  "x86_64-windows/dxgi.dll"
  "x86_64-windows/d3d12.dll"
  "x86_64-windows/d3d12core.dll"
  "x86_64-windows/d3d11.dll"
  "x86_64-windows/d3d11_dxmt.dll"
  "x86_64-windows/d3d10core.dll"
  "x86_64-windows/nvapi64.dll"
  "x86_64-windows/nvngx.dll"
  "x86_64-unix/winemetal.so"
  "x86_64-unix/winemetal4.so"
)

windows_runtime_files=(
  "winemetal.dll"
  "winemetal4.dll"
  "dxgi.dll"
  "d3d12.dll"
  "d3d12core.dll"
  "d3d11.dll"
  "d3d11_dxmt.dll"
  "d3d10core.dll"
  "nvapi64.dll"
  "nvngx.dll"
)

log() {
  printf '[gamehub-dxmt] %s\n' "$*"
}

die() {
  printf '[gamehub-dxmt] error: %s\n' "$*" >&2
  exit 1
}

resolve_version_mode() {
  case "${GAMEHUB_VERSION_MODE}" in
    release|nightly)
      printf '%s\n' "${GAMEHUB_VERSION_MODE}"
      ;;
    auto)
      if [[ "${GAMEHUB_DXMT_PACKAGE}" == *nightly* ]]; then
        printf 'nightly\n'
      elif [[ "${GAMEHUB_DXMT_PACKAGE}" =~ ^dxmt-v[0-9] ]]; then
        printf 'release\n'
      else
        printf 'nightly\n'
      fi
      ;;
    *)
      die "unsupported DXMT_GAMEHUB_VERSION_MODE: ${GAMEHUB_VERSION_MODE}"
      ;;
  esac
}

resolve_release_version() {
  if [[ -n "${GAMEHUB_VERSION_OVERRIDE}" ]]; then
    printf '%s\n' "${GAMEHUB_VERSION_OVERRIDE}"
    return
  fi
  if [[ "${GAMEHUB_DXMT_PACKAGE}" =~ ^dxmt-(v[0-9][0-9A-Za-z._-]*)$ ]]; then
    printf '%s\n' "${BASH_REMATCH[1]}"
    return
  fi
  die "release mode requires DXMT_GAMEHUB_VERSION or a package id like dxmt-v0.80"
}

resolve_nightly_version() {
  if [[ -n "${GAMEHUB_VERSION_OVERRIDE}" ]]; then
    printf '%s\n' "${GAMEHUB_VERSION_OVERRIDE}"
    return
  fi
  git -C "${REPO_ROOT}" rev-parse --short=12 HEAD
}

resolve_runtime_version() {
  local mode="$1"
  case "${mode}" in
    release)
      resolve_release_version
      ;;
    nightly)
      resolve_nightly_version
      ;;
    *)
      die "internal error: invalid resolved version mode: ${mode}"
      ;;
  esac
}

prepend_tool_path() {
  local dir="$1"
  if [[ -d "${dir}" ]]; then
    PATH="${dir}:${PATH}"
  fi
}

setup_tool_path() {
  local bison_prefix
  bison_prefix="$(brew --prefix bison 2>/dev/null || true)"
  if [[ -n "${bison_prefix}" ]]; then
    prepend_tool_path "${bison_prefix}/bin"
  fi
  prepend_tool_path "/usr/local/opt/bison/bin"
  prepend_tool_path "/opt/homebrew/opt/bison/bin"
  export PATH
}

build_dir_source_root() {
  local ninja_file="${BUILD_DIR}/build.ninja"
  [[ -f "${ninja_file}" ]] || return 0

  python3 - "${ninja_file}" <<'PY'
import shlex
import sys

ninja_file = sys.argv[1]
with open(ninja_file, "r", encoding="utf-8") as f:
    for line in f:
        if "meson --internal regenerate" not in line:
            continue
        command = line.split("=", 1)[1].strip() if "=" in line else line.strip()
        parts = shlex.split(command)
        try:
            index = parts.index("regenerate")
        except ValueError:
            continue
        if index + 1 < len(parts):
            print(parts[index + 1])
            sys.exit(0)
sys.exit(0)
PY
}

setup_build_dir() {
  if [[ -f "${BUILD_DIR}/build.ninja" ]]; then
    local source_root
    source_root="$(build_dir_source_root)"
    if [[ -z "${source_root}" || "${source_root}" == "${REPO_ROOT}" ]]; then
      return
    fi

    log "removing stale build directory: ${BUILD_DIR}"
    log "stale source root: ${source_root}"
    rm -rf "${BUILD_DIR}"
  fi

  log "configuring builtin build: ${BUILD_DIR}"
  meson setup \
    --cross-file "${REPO_ROOT}/build-win64.txt" \
    -Dnative_llvm_path="${NATIVE_LLVM_PATH}" \
    --buildtype release \
    "${BUILD_DIR}"
}

configure_build_version() {
  log "configuring runtime version (${RUNTIME_VERSION_MODE}): ${RUNTIME_VERSION}"
  meson configure "${BUILD_DIR}" -Ddxmt_version="${RUNTIME_VERSION}"
}

install_to_stage() {
  rm -rf "${STAGE_DIR}"
  mkdir -p "${STAGE_DIR}"

  log "compiling builtin build"
  meson compile -C "${BUILD_DIR}"

  log "installing runtime to staging root: ${STAGE_DIR}"
  DESTDIR="${STAGE_DIR}" meson install -C "${BUILD_DIR}" --tags runtime,nvext
}

verify_runtime_version() {
  local dll="$1"
  [[ -f "${dll}" ]] || die "missing d3d12.dll for version check: ${dll}"
  if ! grep -aFq "${RUNTIME_VERSION}" "${dll}"; then
    die "d3d12.dll does not contain expected version ${RUNTIME_VERSION}: ${dll}"
  fi
}

resolve_stage_prefix() {
  local prefix
  prefix="$(meson introspect "${BUILD_DIR}" --buildoptions | python3 -c 'import json, sys
data = json.load(sys.stdin)
for item in data:
    if item.get("name") == "prefix":
        print(item.get("value"))
        break
')"
  [[ -n "${prefix}" ]] || die "failed to read Meson install prefix"
  printf '%s%s\n' "${STAGE_DIR}" "${prefix}"
}

copy_runtime() {
  local stage_prefix="$1"
  local missing=0

  for rel in "${runtime_files[@]}"; do
    if [[ ! -f "${stage_prefix}/${rel}" ]]; then
      printf '[gamehub-dxmt] missing staged runtime file: %s\n' "${stage_prefix}/${rel}" >&2
      missing=1
    fi
  done
  [[ "${missing}" == 0 ]] || die "staged runtime is incomplete"

  mkdir -p "${BACKUP_DIR}/wine" "${GAMEHUB_DXMT_ROOT}/wine"

  log "backing up current GameHub DXMT files to: ${BACKUP_DIR}"
  for rel in "${runtime_files[@]}"; do
    if [[ -e "${GAMEHUB_DXMT_ROOT}/wine/${rel}" ]]; then
      mkdir -p "${BACKUP_DIR}/wine/$(dirname "${rel}")"
      cp -p "${GAMEHUB_DXMT_ROOT}/wine/${rel}" "${BACKUP_DIR}/wine/${rel}"
    fi
  done
  if [[ -f "${GAMEHUB_DXMT_ROOT}/manifest.json" ]]; then
    cp -p "${GAMEHUB_DXMT_ROOT}/manifest.json" "${BACKUP_DIR}/manifest.json"
  fi

  log "replacing GameHub test DXMT package: ${GAMEHUB_DXMT_ROOT}"
  for rel in "${runtime_files[@]}"; do
    mkdir -p "${GAMEHUB_DXMT_ROOT}/wine/$(dirname "${rel}")"
    cp -p "${stage_prefix}/${rel}" "${GAMEHUB_DXMT_ROOT}/wine/${rel}"
  done

  if [[ -d "${GAMEHUB_DXMT_ROOT}/wine/system32" ]]; then
    log "refreshing legacy package system32 mirror"
    mkdir -p "${BACKUP_DIR}/wine/system32"
    for file in "${windows_runtime_files[@]}"; do
      if [[ -f "${stage_prefix}/x86_64-windows/${file}" &&
            -e "${GAMEHUB_DXMT_ROOT}/wine/system32/${file}" ]]; then
        cp -p "${GAMEHUB_DXMT_ROOT}/wine/system32/${file}" \
          "${BACKUP_DIR}/wine/system32/${file}"
        cp -p "${stage_prefix}/x86_64-windows/${file}" \
          "${GAMEHUB_DXMT_ROOT}/wine/system32/${file}"
      fi
    done
  fi
}

write_manifest() {
  mkdir -p "${GAMEHUB_DXMT_ROOT}"
  if [[ "${ENABLE_APITRACE}" == "1" ]]; then
    mkdir -p "${APITRACE_OUTPUT_DIR}"
  fi

  python3 - "$GAMEHUB_DXMT_ROOT/manifest.json" "$GAMEHUB_DXMT_PACKAGE" \
    "$RUNTIME_VERSION" "$ENABLE_APITRACE" "$APITRACE_OUTPUT_DIR" \
    "$GAMEHUB_WINEDEBUG" <<'PY'
import json
import sys

manifest_path = sys.argv[1]
package_name = sys.argv[2]
runtime_version = sys.argv[3]
enable_apitrace = sys.argv[4] == "1"
apitrace_output_dir = sys.argv[5]
winedebug = sys.argv[6]
environment_template = {
    "DXMT_EXPERIMENT_DX12_SUPPORT": "1",
    "WINEDLLOVERRIDES": "d3d10core,d3d11,d3d11_dxmt,d3d12,dxgi,winemetal,winemetal4,nvapi64,nvngx=n,b",
    "WINEDLLPATH": "${COMPONENT_PATH}/wine",
    "DYLD_FALLBACK_LIBRARY_PATH": "${COMPONENT_PATH}/wine/x86_64-unix:${WINE_INSTALL_PATH}/lib:${WINE_INSTALL_PATH}/lib/wine/x86_64-unix",
}
if winedebug:
    environment_template["WINEDEBUG"] = winedebug
if enable_apitrace:
    environment_template["DXMT_APITRACE_ENABLED"] = "1"
    environment_template["APITRACE_TRACE_BUNDLE"] = apitrace_output_dir

manifest = {
    "id": package_name,
    "name": "DXMT GameHub Test",
    "version": runtime_version,
    "link_unix_libs": [
        "${COMPONENT_PATH}/wine/x86_64-unix",
    ],
    "link_windows_dlls": [
        "${COMPONENT_PATH}/wine/x86_64-windows",
    ],
    "environment_template": environment_template,
}
with open(manifest_path, "w", encoding="utf-8") as f:
    json.dump(manifest, f, indent=4)
    f.write("\n")
PY
}

main() {
  cd "${REPO_ROOT}"
  RUNTIME_VERSION_MODE="$(resolve_version_mode)"
  RUNTIME_VERSION="$(resolve_runtime_version "${RUNTIME_VERSION_MODE}")"
  export RUNTIME_VERSION_MODE RUNTIME_VERSION

  setup_tool_path
  setup_build_dir
  configure_build_version
  install_to_stage

  local stage_prefix
  stage_prefix="$(resolve_stage_prefix)"
  verify_runtime_version "${stage_prefix}/x86_64-windows/d3d12.dll"
  copy_runtime "${stage_prefix}"
  write_manifest
  verify_runtime_version "${GAMEHUB_DXMT_ROOT}/wine/x86_64-windows/d3d12.dll"

  log "done"
  log "package: ${GAMEHUB_DXMT_ROOT}"
  log "version: ${RUNTIME_VERSION} (${RUNTIME_VERSION_MODE})"
  log "backup:  ${BACKUP_DIR}"
  if [[ "${ENABLE_APITRACE}" == "1" ]]; then
    log "apitrace output: ${APITRACE_OUTPUT_DIR}"
  fi
}

main "$@"
