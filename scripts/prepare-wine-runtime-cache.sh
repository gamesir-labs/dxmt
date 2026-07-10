#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

WINE_SOURCE=""
INSTALLROOT=""
CACHE_DIR="${PROJECT_ROOT}/.cache/wine-runtime/x86_64"

usage() {
  cat <<USAGE
Usage: $(basename "$0") --wine-source PATH --install-root PATH

Build the DXMT-owned runtime dependency cache around a Wine development
installation. Wine supplies only its generic dylib packaging tools; all cache
state, validation, and lifecycle policy remain owned by DXMT.
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --wine-source)
      WINE_SOURCE="$2"
      shift 2
      ;;
    --install-root)
      INSTALLROOT="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

[[ -n "${WINE_SOURCE}" && -n "${INSTALLROOT}" ]] || {
  usage >&2
  exit 2
}

WINE_SOURCE="$(cd "${WINE_SOURCE}" && pwd)"
INSTALLROOT="$(cd "${INSTALLROOT}" && pwd)"
mkdir -p "${CACHE_DIR}"
CACHE_DIR="$(cd "${CACHE_DIR}" && pwd)"

PACK_SCRIPT="${WINE_SOURCE}/scripts/pack_runtime_deps.sh"
RELOCATE_SCRIPT="${WINE_SOURCE}/scripts/relocate_wine_runtime.sh"
RUNTIME_DEPS_ARCHIVE="${CACHE_DIR}/runtime-deps.zip"
RUNTIME_DEPS_STAGE="${CACHE_DIR}/runtime-deps-stage"
RUNTIME_DEPS_STAMP="${CACHE_DIR}/runtime-deps.state"
CACHE_STAMP="${INSTALLROOT}/.dxmt-wine-runtime-cache"

for packaging_script in "${PACK_SCRIPT}" "${RELOCATE_SCRIPT}"; do
  [[ -f "${packaging_script}" ]] || {
    echo "Wine dependency is missing packaging tool: ${packaging_script}" >&2
    exit 1
  }
done

required_paths=(
  "bin/wine"
  "bin/wineserver"
  "bin/winebuild"
  "lib/wine/x86_64-windows/libwinecrt0.a"
  "lib/wine/x86_64-windows/libntdll.a"
  "lib/wine/x86_64-unix/ntdll.so"
  "lib/wine/x86_64-unix/win32u.so"
)
required_dylibs=(
  "libfreetype.6.dylib"
  "libgcrypt.20.dylib"
  "libgmp.10.dylib"
  "libgnutls.30.dylib"
  "libSDL2-2.0.0.dylib"
  "libMoltenVK.dylib"
)

for relative_path in "${required_paths[@]}"; do
  [[ -e "${INSTALLROOT}/${relative_path}" ]] || {
    echo "Wine development install is missing ${relative_path}: ${INSTALLROOT}" >&2
    exit 1
  }
done

next_state="${CACHE_DIR}/runtime-cache-state.$$"
next_dependency_state="${CACHE_DIR}/runtime-deps-state.$$"
trap 'rm -f "${next_state}" "${next_dependency_state}"' EXIT HUP INT TERM

write_dependency_state() {
  local output="$1"
  local dependency
  local input
  local module
  local module_name
  local wine_dependency_contract_sha256

  wine_dependency_contract_sha256="$({
    while IFS= read -r -d '' module; do
      module_name="$(basename "${module}")"
      printf 'module=%s\n' "${module_name}"
      while IFS= read -r dependency; do
        printf 'dependency=%s:%s\n' \
          "${module_name}" "$(basename "${dependency}")"
      done < <(otool -L "${module}" | awk 'NR > 1 {print $1}')
    done < <(find "${INSTALLROOT}/lib/wine/x86_64-unix" \
      -type f -name '*.so' -print0 | sort -z)
  } | shasum -a 256 | awk '{print $1}')"

  {
    echo "schema=1"
    echo "wine_dependency_contract_sha256=${wine_dependency_contract_sha256}"
    echo "input_sha256=pack_runtime_deps.sh:$(shasum -a 256 "${PACK_SCRIPT}" | awk '{print $1}')"
    while IFS= read -r -d '' input; do
      echo "input_sha256=$(basename "${input}"):$(shasum -a 256 "${input}" | awk '{print $1}')"
    done < <(find "${WINE_SOURCE}/scripts/deps" -type f -name '*.txt' -print0 | sort -z)
  } > "${output}"
}

write_runtime_state() {
  local output="$1"
  local dylib
  local relative_path
  local runtime_dylibs_sha256

  runtime_dylibs_sha256="$({
    while IFS= read -r -d '' dylib; do
      relative_path="${dylib#${INSTALLROOT}/lib/}"
      if [[ -L "${dylib}" ]]; then
        printf 'link=%s:%s\n' "${relative_path}" "$(readlink "${dylib}")"
      else
        printf 'file=%s:%s\n' \
          "${relative_path}" "$(shasum -a 256 "${dylib}" | awk '{print $1}')"
      fi
    done < <(find "${INSTALLROOT}/lib" -maxdepth 2 \
      \( -type f -o -type l \) -name '*.dylib' -print0 2>/dev/null | sort -z)
  } | shasum -a 256 | awk '{print $1}')"

  {
    echo "schema=1"
    sed '/^schema=/d' "${next_dependency_state}"
    echo "runtime_dylibs_sha256=${runtime_dylibs_sha256}"
    echo "input_sha256=relocate_wine_runtime.sh:$(shasum -a 256 "${RELOCATE_SCRIPT}" | awk '{print $1}')"
    echo "input_sha256=prepare-wine-runtime-cache.sh:$(shasum -a 256 "${SCRIPT_DIR}/prepare-wine-runtime-cache.sh" | awk '{print $1}')"
  } > "${output}"
}

runtime_dependency_files_ready() {
  local dylib
  for dylib in "${required_dylibs[@]}"; do
    [[ -f "${INSTALLROOT}/lib/${dylib}" ]] || return 1
  done
}

audit_runtime_dylibs() {
  local dylib
  local leaked_dependency
  local leaked_rpath

  while IFS= read -r -d '' dylib; do
    codesign --verify "${dylib}" >/dev/null 2>&1 || {
      echo "Runtime dylib is not validly signed: ${dylib}" >&2
      return 1
    }
    leaked_dependency="$(otool -L "${dylib}" | awk 'NR > 1 {print $1}' \
      | grep -E '^(/usr/local|/opt/homebrew)/' | head -n 1 || true)"
    if [[ -n "${leaked_dependency}" ]]; then
      echo "Runtime dylib contains a host-only dependency: ${dylib} -> ${leaked_dependency}" >&2
      return 1
    fi
    leaked_rpath="$(otool -l "${dylib}" \
      | awk '/LC_RPATH/{getline; getline; print $2}' \
      | grep -E '^(/usr/local|/opt/homebrew)/' | head -n 1 || true)"
    if [[ -n "${leaked_rpath}" ]]; then
      echo "Runtime dylib contains a host-only RPATH: ${dylib} -> ${leaked_rpath}" >&2
      return 1
    fi
  done < <(find "${INSTALLROOT}/lib" -maxdepth 2 \
    -type f -name '*.dylib' -print0)
}

runtime_relocation_ready=0
if runtime_dependency_files_ready && \
  bash "${RELOCATE_SCRIPT}" --install-root "${INSTALLROOT}"; then
  runtime_relocation_ready=1
fi

write_dependency_state "${next_dependency_state}"
write_runtime_state "${next_state}"

runtime_cache_ready() {
  [[ "${runtime_relocation_ready}" -eq 1 ]] || return 1
  [[ -f "${CACHE_STAMP}" ]] && cmp -s "${next_state}" "${CACHE_STAMP}" || return 1
  runtime_dependency_files_ready
}

runtime_dependency_archive_ready() {
  [[ -f "${RUNTIME_DEPS_ARCHIVE}" ]] || return 1
  [[ -f "${RUNTIME_DEPS_STAMP}" ]] || return 1
  cmp -s "${next_dependency_state}" "${RUNTIME_DEPS_STAMP}" || return 1
  unzip -tq "${RUNTIME_DEPS_ARCHIVE}" >/dev/null
}

if runtime_cache_ready; then
  audit_runtime_dylibs
  echo "DXMT Wine runtime cache is ready: ${INSTALLROOT}"
  exit 0
fi

if runtime_dependency_archive_ready; then
  echo "Reusing Wine runtime dependency archive: ${RUNTIME_DEPS_ARCHIVE}"
else
  rm -rf "${RUNTIME_DEPS_STAGE}"
  rm -f "${RUNTIME_DEPS_ARCHIVE}"
  KEEP_STAGE=0 bash "${PACK_SCRIPT}" \
    --install-root "${INSTALLROOT}" \
    --out-zip "${RUNTIME_DEPS_ARCHIVE}" \
    --stage-dir "${RUNTIME_DEPS_STAGE}"
  cp "${next_dependency_state}" "${RUNTIME_DEPS_STAMP}"
fi

mkdir -p "${INSTALLROOT}/lib"
find "${INSTALLROOT}/lib" -maxdepth 1 \
  \( -type f -o -type l \) -name '*.dylib' -delete
rm -rf "${INSTALLROOT}/lib/gstreamer-1.0"
unzip -oq "${RUNTIME_DEPS_ARCHIVE}" -d "${INSTALLROOT}/lib"
bash "${RELOCATE_SCRIPT}" --install-root "${INSTALLROOT}"

runtime_dependency_files_ready || {
  echo "Wine runtime dependency archive is incomplete" >&2
  exit 1
}

for relative_path in \
  "lib/wine/x86_64-windows/libwinecrt0.a" \
  "lib/wine/x86_64-windows/libntdll.a"
do
  [[ -f "${INSTALLROOT}/${relative_path}" ]] || {
    echo "Runtime preparation removed required development artifact: ${relative_path}" >&2
    exit 1
  }
done

audit_runtime_dylibs
write_runtime_state "${next_state}"
cp "${next_state}" "${CACHE_STAMP}"
echo "DXMT Wine runtime cache prepared: ${INSTALLROOT}"
