#!/usr/bin/env bash
set -euo pipefail

# Surface silent set -e aborts (common on bash 3.2 + glob/&& patterns).
trap 'printf "[dxmt-ci] error: command failed at line %s: %s (exit %s)\n" "$LINENO" "$BASH_COMMAND" "$?" >&2' ERR

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
CONFIG_PATH=""
case "${1:-}" in
  --config)
    [[ $# -ge 2 ]] || { echo "ci-self-hosted.sh: --config requires a value" >&2; exit 2; }
    CONFIG_PATH="$2"
    shift 2
    ;;
  --config=*)
    CONFIG_PATH="${1#--config=}"
    shift
    ;;
esac
if [[ -z "${CONFIG_PATH}" ]]; then
  search_dir="${REPO_ROOT}"
  while :; do
    candidate="${search_dir}/.dxmt-builder/config.json"
    if [[ -f "${candidate}" ]]; then
      CONFIG_PATH="${candidate}"
      break
    fi
    parent_dir="$(dirname "${search_dir}")"
    [[ "${parent_dir}" != "${search_dir}" ]] || break
    search_dir="${parent_dir}"
  done
fi
if [[ -z "${CONFIG_PATH}" && -f "${REPO_ROOT}/tools/dxmt-builder/config.json" ]]; then
  CONFIG_PATH="${REPO_ROOT}/tools/dxmt-builder/config.json"
fi

CACHE_ROOT="${DXMT_CI_ROOT:-${DXMT_MANAGED_CACHE_ROOT:-${REPO_ROOT}/.cache/managed}}"
if [[ -n "${CONFIG_PATH}" ]]; then
  if [[ "${CONFIG_PATH}" != /* ]]; then
    CONFIG_PATH="${PWD}/${CONFIG_PATH}"
  fi
  [[ -f "${CONFIG_PATH}" ]] || {
    echo "ci-self-hosted.sh: config does not exist: ${CONFIG_PATH}" >&2
    exit 2
  }
  configured_root="$(python3 - "${CONFIG_PATH}" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as config_file:
    value = json.load(config_file).get("cache_root")
if not isinstance(value, str) or not value:
    raise SystemExit(2)
print(value)
PY
  )" || {
    echo "ci-self-hosted.sh: config must contain a string cache_root: ${CONFIG_PATH}" >&2
    exit 2
  }
  if [[ "${configured_root}" == /* ]]; then
    CACHE_ROOT="${configured_root}"
  else
    CACHE_ROOT="${REPO_ROOT}/${configured_root}"
  fi
fi
TOOLCHAINS_DIR="${CACHE_ROOT}/deps"
DOWNLOADS_DIR="${CACHE_ROOT}/downloads"
ARTIFACTS_DIR="${CACHE_ROOT}/artifacts"
SOURCES_DIR="${CACHE_ROOT}/sources"
RUN_KEY="${GITHUB_RUN_ID:-local}-${GITHUB_RUN_ATTEMPT:-1}"

LLVM_VERSION="${LLVM_VERSION:-llvmorg-15.0.7}"
WINE_VERSION="${WINE_VERSION:-proton-11.0-macos}"
WINE_GIT_URL="${WINE_GIT_URL:-https://github.com/gamesir123/wine-proton-macos.git}"
WINE_GIT_BRANCH="${WINE_GIT_BRANCH:-proton-11.0-macos}"
LLVM_MINGW_VERSION="${LLVM_MINGW_VERSION:-20251216}"
LLVM_MINGW_DIR="${LLVM_MINGW_DIR:-llvm-mingw-20251216-ucrt-macos-universal}"
MESON_VERSION="${MESON_VERSION:-1.10.0}"

log() {
  printf '[dxmt-ci] %s\n' "$*" >&2
}

die() {
  printf '[dxmt-ci] error: %s\n' "$*" >&2
  exit 1
}

sudo_run() {
  if [[ "${EUID}" == 0 ]]; then
    "$@"
  elif sudo -n true 2>/dev/null; then
    sudo "$@"
  elif [[ -n "${MAC_RUNNER_SUDO_PASS:-}" ]]; then
    printf '%s\n' "${MAC_RUNNER_SUDO_PASS}" | sudo -S "$@"
  else
    die "sudo is required for '$*'; set MAC_RUNNER_SUDO_PASS"
  fi
}

append_path() {
  local dir="$1"
  [[ -d "${dir}" ]] || return 0
  case ":${PATH}:" in
    *":${dir}:"*) ;;
    *) export PATH="${dir}:${PATH}" ;;
  esac
  if [[ -n "${GITHUB_PATH:-}" ]]; then
    printf '%s\n' "${dir}" >> "${GITHUB_PATH}" ||
      die "failed to append ${dir} to GITHUB_PATH=${GITHUB_PATH}"
  fi
}

setup_paths() {
  append_path /opt/homebrew/bin
  append_path /usr/local/bin
  append_path /opt/homebrew/opt/bison/bin
  append_path /usr/local/opt/bison/bin
  local pybin llvm_mingw
  shopt -s nullglob
  for pybin in "${HOME}"/Library/Python/*/bin; do
    append_path "${pybin}"
  done
  for llvm_mingw in "${TOOLCHAINS_DIR}"/llvm-mingw-*; do
    if [[ -d "${llvm_mingw}/bin" ]]; then
      append_path "${llvm_mingw}/bin"
    fi
  done
  shopt -u nullglob
}

ensure_root() {
  log "ensuring managed cache dirs under ${CACHE_ROOT}"
  mkdir -p "${TOOLCHAINS_DIR}" "${DOWNLOADS_DIR}" \
    "${ARTIFACTS_DIR}" "${SOURCES_DIR}" ||
    die "failed to create managed cache dirs under ${CACHE_ROOT} (pwd=$(pwd) user=$(id -un 2>/dev/null || true))"
}

ensure_brew() {
  setup_paths
  command -v brew >/dev/null || die "Homebrew is required on the self-hosted runner"
  export HOMEBREW_NO_AUTO_UPDATE="${HOMEBREW_NO_AUTO_UPDATE:-1}"
  export HOMEBREW_NO_INSTALL_CLEANUP="${HOMEBREW_NO_INSTALL_CLEANUP:-1}"
  export HOMEBREW_NO_ENV_HINTS="${HOMEBREW_NO_ENV_HINTS:-1}"
}

ensure_brew_packages() {
  ensure_brew
  for package in "$@"; do
    if ! brew list --formula "${package}" >/dev/null 2>&1; then
      log "installing Homebrew formula: ${package}"
      brew install --formula "${package}"
    fi
  done
}

ensure_command_or_formula() {
  local command_name="$1"
  local formula="$2"
  if command -v "${command_name}" >/dev/null 2>&1; then
    return
  fi
  ensure_brew_packages "${formula}"
  command -v "${command_name}" >/dev/null 2>&1 ||
    die "${command_name} is missing after installing ${formula}"
}

ensure_homebrew_build_tools() {
  ensure_command_or_formula cmake cmake
  ensure_command_or_formula ninja ninja
  ensure_command_or_formula bison bison
  ensure_command_or_formula x86_64-w64-mingw32-gcc mingw-w64
  ensure_command_or_formula i686-w64-mingw32-gcc mingw-w64
  ensure_command_or_formula ccache ccache
  ensure_command_or_formula gh gh
}

ensure_wine_rosetta_build_tools() {
  if [[ "$(uname -m)" != "arm64" ]]; then
    return
  fi

  arch -x86_64 /usr/bin/true >/dev/null 2>&1 ||
    die "Rosetta 2 is required for Wine x86_64 builds; install it on the runner"

  [[ -x /usr/local/bin/brew ]] ||
    die "x86_64 Homebrew is required at /usr/local/bin/brew; install it on the runner"

  local brew_x86=(arch -x86_64 /usr/local/bin/brew)
  local brew_updated=0
  local packages=(
    zlib
    pkg-config
    freetype
    gnutls
    libgcrypt
    sdl2
    ffmpeg
    gstreamer
    gst-plugins-base
    libffi
    vulkan-loader
    vulkan-headers
    bison
    autoconf
    automake
    libtool
    gnu-tar
    dylibbundler
    gh
    mingw-w64
  )

  for package in "${packages[@]}"; do
    if ! "${brew_x86[@]}" list --formula "${package}" >/dev/null 2>&1; then
      log "installing x86_64 Homebrew formula for Wine: ${package}"
      if ! "${brew_x86[@]}" install --formula "${package}"; then
        if ! "${brew_x86[@]}" list --formula "${package}" >/dev/null 2>&1; then
          if [[ "${brew_updated}" -eq 0 ]]; then
            log "updating x86_64 Homebrew after formula install failure"
            "${brew_x86[@]}" update --force --quiet ||
              die "failed to update x86_64 Homebrew"
            brew_updated=1
          fi
          "${brew_x86[@]}" install --formula "${package}" ||
            die "failed to install x86_64 Homebrew formula for Wine: ${package}"
        fi
        log "x86_64 Homebrew formula ${package} is installed despite brew returning non-zero"
      fi
    fi
  done
}

vulkan_x86_64_link_ready() {
  local probe_src probe_bin clang_runner=()
  if [[ "$(uname -m)" == "arm64" ]]; then
    clang_runner=(arch -x86_64)
  fi

  probe_bin="$(mktemp "${TMPDIR:-/tmp}/dxmt-vulkan-probe.XXXXXX")"
  probe_src="${probe_bin}.c"
  cat > "${probe_src}" <<'EOF'
extern void *vkGetInstanceProcAddr(void *, const char *);
int main(void) { return vkGetInstanceProcAddr(0, "vkCreateInstance") != 0; }
EOF

  if "${clang_runner[@]}" /usr/bin/clang -arch x86_64 "${probe_src}" -L/usr/local/lib -lvulkan -Wl,-undefined,error -o "${probe_bin}" >/dev/null 2>&1; then
    rm -f "${probe_src}" "${probe_bin}"
    return 0
  fi
  if "${clang_runner[@]}" /usr/bin/clang -arch x86_64 "${probe_src}" -L/usr/local/lib -lMoltenVK -Wl,-undefined,error -o "${probe_bin}" >/dev/null 2>&1; then
    rm -f "${probe_src}" "${probe_bin}"
    return 0
  fi

  rm -f "${probe_src}" "${probe_bin}"
  return 1
}

vulkan_system_files_ready() {
  [[ -f /usr/local/lib/libMoltenVK.dylib ]] &&
    compgen -G '/usr/local/lib/libvulkan*.dylib' >/dev/null &&
    vulkan_x86_64_link_ready
}

ensure_lunarg_vulkan_sdk() {
  if vulkan_system_files_ready; then
    return
  fi

  ensure_root
  local version archive extract_dir install_root installer_bin
  version="$(curl -fsSL https://vulkan.lunarg.com/sdk/latest/mac.txt)"
  [[ -n "${version}" ]] || die "failed to query latest Vulkan SDK version"
  archive="${DOWNLOADS_DIR}/vulkansdk-macos-${version}.zip"
  extract_dir="${DOWNLOADS_DIR}/vulkansdk-macos-${version}"
  install_root="${TOOLCHAINS_DIR}/vulkan-sdk-${version}"

  download_file "https://sdk.lunarg.com/sdk/download/latest/mac/vulkan-sdk.zip" "${archive}"
  if [[ ! -d "${extract_dir}" ]]; then
    log "extracting Vulkan SDK ${version}"
    mkdir -p "${extract_dir}"
    unzip -q "${archive}" -d "${extract_dir}"
  fi

  if [[ ! -f "${install_root}/macOS/lib/libMoltenVK.dylib" ]]; then
    installer_bin="$(find "${extract_dir}" -type f -path "*/Contents/MacOS/vulkansdk-macOS-*" -print -quit)"
    [[ -n "${installer_bin}" ]] || die "Vulkan SDK installer executable was not found in ${archive}"
    log "installing Vulkan SDK ${version} into ${install_root}"
    "${installer_bin}" \
      --root "${install_root}" \
      --accept-licenses \
      --default-answer \
      --confirm-command install
  fi

  [[ -f "${install_root}/macOS/lib/libMoltenVK.dylib" ]] ||
    die "libMoltenVK.dylib was not installed by Vulkan SDK"
  [[ -f "${install_root}/macOS/include/vulkan/vulkan.h" ]] ||
    die "Vulkan headers were not installed by Vulkan SDK"

  log "staging Vulkan SDK ${version} development files into /usr/local"
  sudo_run mkdir -p /usr/local/lib /usr/local/include
  sudo_run rm -f /usr/local/lib/libMoltenVK.dylib
  sudo_run cp -f "${install_root}/macOS/lib/libMoltenVK.dylib" /usr/local/lib/
  while IFS= read -r dylib; do
    sudo_run rm -f "/usr/local/lib/$(basename "${dylib}")"
    sudo_run cp -f "${dylib}" /usr/local/lib/
  done < <(find "${install_root}/macOS/lib" -type f -name "libvulkan*.dylib" -print)
  if [[ ! -e /usr/local/lib/libvulkan.dylib ]]; then
    local vulkan_runtime
    vulkan_runtime="$(find /usr/local/lib -maxdepth 1 -type f -name 'libvulkan.1*.dylib' -print | sort | head -n 1)"
    [[ -n "${vulkan_runtime}" ]] || die "Vulkan SDK did not stage a linkable libvulkan runtime"
    sudo_run ln -sfn "$(basename "${vulkan_runtime}")" /usr/local/lib/libvulkan.dylib
  fi
  sudo_run rsync -a "${install_root}/macOS/include/" /usr/local/include/

  if [[ -d "${install_root}/macOS/lib/pkgconfig" ]]; then
    sudo_run mkdir -p /usr/local/lib/pkgconfig
    sudo_run cp -f "${install_root}/macOS/lib/pkgconfig/"*.pc /usr/local/lib/pkgconfig/
  fi

  vulkan_system_files_ready ||
    die "Vulkan SDK install completed but /usr/local libvulkan/libMoltenVK files are missing"
}

ensure_meson() {
  local meson_path
  setup_paths
  if command -v meson >/dev/null 2>&1 &&
     [[ "$(meson --version)" == "${MESON_VERSION}" ]]; then
    meson_path="$(command -v meson)"
    if [[ "${meson_path}" != "/usr/local/bin/meson" ]]; then
      sudo_run ln -sfn "${meson_path}" /usr/local/bin/meson
    fi
    return
  fi
  log "installing Meson ${MESON_VERSION}"
  python3 -m pip install --user "meson==${MESON_VERSION}" --break-system-packages \
    || python3 -m pip install --user "meson==${MESON_VERSION}"
  setup_paths
  meson_path="$(command -v meson)" || die "meson is missing after installation"
  sudo_run ln -sfn "${meson_path}" /usr/local/bin/meson
}

check_apple_tools() {
  command -v xcrun >/dev/null || die "xcrun is missing; install Apple Command Line Tools"
  xcrun --sdk macosx --show-sdk-path >/dev/null ||
    die "macOS SDK is unavailable via xcrun --sdk macosx --show-sdk-path"
  ensure_metal_toolchain
}

metal_tools_available() {
  xcrun --find metal >/dev/null 2>&1 &&
    xcrun --find metallib >/dev/null 2>&1
}

ensure_metal_toolchain() {
  if metal_tools_available; then
    return
  fi

  command -v xcodebuild >/dev/null ||
    die "Metal tools are missing and xcodebuild is unavailable; install Xcode or the Metal Toolchain manually"

  log "installing optional Metal Toolchain"
  xcodebuild -downloadComponent metalToolchain ||
    xcodebuild -downloadComponent MetalToolchain ||
    true
  if metal_tools_available; then
    return
  fi

  ensure_root
  local export_dir="${DOWNLOADS_DIR}/metalToolchain-export"
  rm -rf "${export_dir}"
  mkdir -p "${export_dir}"
  xcodebuild -downloadComponent metalToolchain -exportPath "${export_dir}" ||
    xcodebuild -downloadComponent MetalToolchain -exportPath "${export_dir}"

  local bundle
  bundle="$(find "${export_dir}" -maxdepth 1 -name 'MetalToolchain*.exportedBundle' -print -quit)"
  [[ -n "${bundle}" ]] || die "Metal Toolchain export bundle was not created"
  xcodebuild -importComponent metalToolchain -importPath "${bundle}" ||
    xcodebuild -importComponent MetalToolchain -importPath "${bundle}"

  metal_tools_available ||
    die "Metal Toolchain install completed but xcrun still cannot find metal/metallib"
}

setup_host() {
  log "setup-host starting on $(uname -m) host=$(hostname 2>/dev/null || true)"
  log "REPO_ROOT=${REPO_ROOT} CACHE_ROOT=${CACHE_ROOT} GITHUB_PATH=${GITHUB_PATH:-<unset>}"
  ensure_root
  log "ensure_root done"
  setup_paths
  log "PATH=${PATH}"
  ensure_homebrew_build_tools
  log "homebrew build tools ready"
  ensure_wine_rosetta_build_tools
  log "wine/rosetta tools ready"
  ensure_lunarg_vulkan_sdk
  log "vulkan sdk ready"
  ensure_meson
  log "meson ready: $(command -v meson || true) $(meson --version 2>/dev/null || true)"
  check_apple_tools
  log "setup-host completed"
}

download_file() {
  local url="$1"
  local output="$2"
  if [[ ! -f "${output}" ]]; then
    log "downloading ${url}"
    local temporary="${output}.incomplete-$$"
    trap 'rm -f "${temporary}"' RETURN
    curl -fL "${url}" -o "${temporary}"
    mv "${temporary}" "${output}"
    trap - RETURN
  fi
}

safe_name() {
  printf '%s' "$1" | tr -c 'A-Za-z0-9._-' '-'
}

wine_github_repo() {
  printf '%s\n' "${WINE_GIT_URL}" |
    sed -E 's#^https://github.com/##; s#^git@github.com:##; s#\.git$##; s#/$##'
}

gh_wine() {
  GH_TOKEN="${WINE_ACCESS_TOKEN}" gh "$@"
}

ensure_gh_wine() {
  command -v gh >/dev/null || die "gh is required for private Wine repository access"
  [[ -n "${WINE_ACCESS_TOKEN:-}" ]] || die "WINE_ACCESS_TOKEN is empty; set the repository secret for private Wine access"
}

check_wine_access() {
  [[ -n "${WINE_ACCESS_TOKEN:-}" ]] || die "WINE_ACCESS_TOKEN is empty; set the repository secret for private Wine access"

  local repo status body commit
  repo="$(wine_github_repo)"
  body="${DOWNLOADS_DIR}/wine-access-check.json"
  log "checking Wine repository access for ${repo}"
  status="$(curl -sS \
    --connect-timeout 15 \
    --max-time 60 \
    -H "Accept: application/vnd.github+json" \
    -H "Authorization: Bearer ${WINE_ACCESS_TOKEN}" \
    -H "X-GitHub-Api-Version: 2022-11-28" \
    -o "${body}" \
    -w '%{http_code}' \
    "https://api.github.com/repos/${repo}/branches/${WINE_GIT_BRANCH}")" ||
    die "failed to check Wine repository access"

  if [[ "${status}" == "200" ]]; then
    commit="$(python3 - "${body}" <<'PY'
import json
import sys
with open(sys.argv[1], "r", encoding="utf-8") as fp:
    data = json.load(fp)
print(((data.get("commit") or {}).get("sha") or "").strip())
PY
)"
    [[ -n "${commit}" ]] || die "Wine repository access check did not return a branch commit"
    WINE_SOURCE_COMMIT="${commit}"
    return
  else
    if [[ -s "${body}" ]]; then
      python3 - "${body}" <<'PY' >&2 || true
import json
import sys
with open(sys.argv[1], "r", encoding="utf-8") as fp:
    data = json.load(fp)
print(f"[dxmt-ci] GitHub API message: {data.get('message')}")
PY
    fi
    die "Wine repository access check failed with HTTP ${status}; verify WINE_ACCESS_TOKEN is the classic PAT with repo scope"
  fi
}

wine_git_auth_url() {
  local repo
  repo="$(wine_github_repo)"
  [[ -n "${WINE_ACCESS_TOKEN:-}" ]] || die "WINE_ACCESS_TOKEN is empty"
  printf 'https://x-access-token:%s@github.com/%s.git\n' \
    "${WINE_ACCESS_TOKEN}" "${repo}"
}

# Keep a persistent git worktree under SOURCES_DIR and fast-forward to the
# remote branch tip on every bootstrap (depth-1 fetch, no full history).
sync_wine_git_source() {
  local source_dir="$1"
  local source_stamp remote_url expected_commit actual_commit
  export GIT_TERMINAL_PROMPT=0
  ensure_gh_wine
  ensure_root
  WINE_SOURCE_COMMIT=""
  check_wine_access
  expected_commit="${WINE_SOURCE_COMMIT}"
  [[ -n "${expected_commit}" ]] || die "Wine source commit is empty"
  source_stamp="${source_dir}/.dxmt-ci-source-commit"
  remote_url="$(wine_git_auth_url)"

  if [[ -d "${source_dir}/.git" ]]; then
    log "updating Wine git source ${WINE_GIT_BRANCH} (expect ${expected_commit:0:12})"
    git -C "${source_dir}" remote set-url origin "${remote_url}" ||
      die "failed to set Wine git remote URL"
    # DXMT never patches Wine sources/scripts; hard-reset keeps the tree stock.
    git -C "${source_dir}" fetch --force --depth=1 origin "${WINE_GIT_BRANCH}" ||
      die "failed to fetch Wine branch ${WINE_GIT_BRANCH}"
    git -C "${source_dir}" checkout -B "${WINE_GIT_BRANCH}" FETCH_HEAD ||
      die "failed to checkout Wine FETCH_HEAD"
    git -C "${source_dir}" reset --hard FETCH_HEAD ||
      die "failed to hard-reset Wine tree to FETCH_HEAD"
  else
    log "cloning Wine git source ${WINE_GIT_BRANCH}@${expected_commit:0:12}"
    rm -rf "${source_dir}"
    mkdir -p "$(dirname "${source_dir}")"
    git clone --depth=1 --branch "${WINE_GIT_BRANCH}" "${remote_url}" "${source_dir}" ||
      die "failed to clone Wine repository"
  fi

  actual_commit="$(git -C "${source_dir}" rev-parse HEAD)"
  [[ -n "${actual_commit}" ]] || die "Wine git HEAD is empty after sync"
  WINE_SOURCE_COMMIT="${actual_commit}"
  printf '%s\n' "${actual_commit}" > "${source_stamp}"
  log "Wine source at ${actual_commit} (${source_dir})"
}

wine_development_install_ready() {
  local install_dir="$1"
  { [[ -x "${install_dir}/bin/wine" ]] || [[ -x "${install_dir}/bin/wine64" ]]; } &&
    [[ -x "${install_dir}/bin/wineserver" ]] &&
    [[ -x "${install_dir}/bin/winebuild" ]] &&
    [[ -f "${install_dir}/lib/wine/i386-windows/libwinecrt0.a" ]] &&
    [[ -f "${install_dir}/lib/wine/i386-windows/libntdll.a" ]] &&
    [[ -f "${install_dir}/lib/wine/i386-windows/dbghelp.dll" ]] &&
    [[ -f "${install_dir}/lib/wine/x86_64-windows/libwinecrt0.a" ]] &&
    [[ -f "${install_dir}/lib/wine/x86_64-windows/libntdll.a" ]] &&
    [[ -f "${install_dir}/lib/wine/x86_64-windows/dbghelp.dll" ]] &&
    [[ -f "${install_dir}/lib/wine/x86_64-unix/winemac.so" ]] &&
    [[ -f "${install_dir}/lib/wine/x86_64-unix/ntdll.so" ]]
}

wine_runtime_install_ready() {
  local install_dir="$1"
  [[ -f "${install_dir}/.dxmt-wine-runtime-cache" ]] &&
    grep -qx 'schema=1' "${install_dir}/.dxmt-wine-runtime-cache" &&
    [[ -f "${install_dir}/lib/libfreetype.6.dylib" ]] &&
    [[ -f "${install_dir}/lib/libgcrypt.20.dylib" ]] &&
    [[ -f "${install_dir}/lib/libgmp.10.dylib" ]] &&
    [[ -f "${install_dir}/lib/libgnutls.30.dylib" ]] &&
    [[ -f "${install_dir}/lib/libSDL2-2.0.0.dylib" ]] &&
    [[ -f "${install_dir}/lib/libMoltenVK.dylib" ]]
}

wine_install_ready() {
  wine_development_install_ready "$1" && wine_runtime_install_ready "$1"
}

patch_wine_build_scripts() {
  local source_dir="$1"
  chmod +x "${source_dir}/scripts/build_on_m1.sh" "${source_dir}/scripts/build_on_intel.sh"
  # MoltenVK comes from the LunarG Vulkan SDK. The x86_64 Homebrew formula
  # currently fails to parse on the self-hosted runner (Rosetta brew).
  case "$(uname -m)" in
    arm64)
      perl -0pi -e 's/(BREW_PACKAGES=\([^)]*) molten-vk( [^)]*\))/$1$2/' \
        "${source_dir}/scripts/build_on_m1.sh"
      ;;
    x86_64)
      perl -0pi -e 's/(BREW_PACKAGES=\([^)]*)\n    molten-vk\n([^)]*\))/$1\n$2/' \
        "${source_dir}/scripts/build_on_intel.sh"
      ;;
  esac
}

wine_run_make() {
  local source_dir="$1"
  local jobs
  jobs="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"
  [[ -f "${source_dir}/build/Makefile" ]] || return 1
  case "$(uname -m)" in
    arm64)
      (cd "${source_dir}/build" && arch -x86_64 make -j"${jobs}") || return 1
      (cd "${source_dir}/build" && arch -x86_64 make install -j"${jobs}") || return 1
      ;;
    x86_64)
      (cd "${source_dir}/build" && make -j"${jobs}") || return 1
      (cd "${source_dir}/build" && make install -j"${jobs}") || return 1
      ;;
    *)
      return 1
      ;;
  esac
}

# Full configure+make via wine-proton scripts after removing the broken
# Rosetta Homebrew molten-vk dependency from their package list.
build_wine_git_source_full() {
  local source_dir="$1"
  patch_wine_build_scripts "${source_dir}"
  case "$(uname -m)" in
    arm64)
      (cd "${source_dir}" && ./scripts/build_on_m1.sh)
      ;;
    x86_64)
      (cd "${source_dir}" && ./scripts/build_on_intel.sh)
      ;;
    *)
      die "unsupported host architecture for Wine build: $(uname -m)"
      ;;
  esac
}

# Prefer incremental make/install when an out-of-tree build already exists.
# Set DXMT_WINE_CLEAN=1 to wipe build/install and force a full rebuild.
build_wine_git_source() {
  local source_dir="$1"
  local install_dir="${source_dir}/install"

  if [[ "${DXMT_WINE_CLEAN:-}" == "1" || "${DXMT_WINE_CLEAN:-}" == "true" ]]; then
    log "DXMT_WINE_CLEAN set: wiping Wine build/install for a full rebuild"
    rm -rf "${source_dir}/build" "${install_dir}"
    rm -f "${source_dir}/.generated"
  fi

  if [[ -f "${source_dir}/build/Makefile" && -f "${source_dir}/.generated" ]]; then
    log "incremental Wine make/install in ${source_dir}/build"
    if wine_run_make "${source_dir}"; then
      return 0
    fi
    log "incremental Wine make failed; falling back to full configure+make"
    rm -rf "${source_dir}/build" "${install_dir}"
    rm -f "${source_dir}/.generated"
  fi

  log "full Wine configure+make via patched build_on_* scripts"
  build_wine_git_source_full "${source_dir}"
}

prepare_dxmt_wine_runtime_cache() {
  local source_dir="$1"
  local install_dir="$2"
  "${REPO_ROOT}/scripts/prepare-wine-runtime-cache.sh" \
    --wine-source "${source_dir}" \
    --install-root "${install_dir}"
}

ensure_wine_x86_64() {
  ensure_root
  local source_dir target install_dir commit stamp fingerprint temporary
  local prev_commit=""
  source_dir="${SOURCES_DIR}/wine-proton-macos-$(safe_name "${WINE_GIT_BRANCH}")"
  WINE_SOURCE_COMMIT=""
  # Always contact the remote branch tip, then git fetch/reset the worktree.
  sync_wine_git_source "${source_dir}"
  commit="${WINE_SOURCE_COMMIT}"
  [[ -n "${commit}" ]] || die "Wine source commit is empty"
  fingerprint="$(printf '%s\n' \
    "schema=2" "version=${WINE_VERSION}" "commit=${commit}" \
    "build_m1=$(shasum -a 256 "${source_dir}/scripts/build_on_m1.sh" | awk '{print $1}')" \
    "build_intel=$(shasum -a 256 "${source_dir}/scripts/build_on_intel.sh" | awk '{print $1}')" \
    "runtime=$(shasum -a 256 "${REPO_ROOT}/scripts/prepare-wine-runtime-cache.sh" | awk '{print $1}')" \
    | shasum -a 256 | awk '{print substr($1,1,16)}')"
  target="${TOOLCHAINS_DIR}/wine-x86_64-${WINE_VERSION}-${fingerprint}"
  if wine_install_ready "${target}" && [[ -f "${target}/.dxmt-builder-dependency" ]]; then
    log "Wine x86_64 immutable dependency already prepared: ${target}"
    return
  fi
  install_dir="${source_dir}/install"
  stamp="${install_dir}/.dxmt-ci-source-commit"
  if [[ -f "${stamp}" ]]; then
    prev_commit="$(cat "${stamp}" 2>/dev/null || true)"
  fi

  if wine_development_install_ready "${install_dir}" &&
     [[ -f "${stamp}" ]] &&
     [[ "${prev_commit}" == "${commit}" ]]; then
    log "Wine x86_64 development install already matches ${commit:0:12}"
  else
    if [[ -n "${prev_commit}" && "${prev_commit}" != "${commit}" ]]; then
      log "Wine tip moved ${prev_commit:0:12} -> ${commit:0:12}; incremental rebuild"
    else
      log "building Wine x86_64 at ${commit:0:12}"
    fi
    # Keep source_dir/build across tip updates for incremental make. Only a
    # failed incremental build or DXMT_WINE_CLEAN=1 wipes the tree.
    build_wine_git_source "${source_dir}"
    wine_development_install_ready "${install_dir}" ||
      die "Wine build finished but required install artifacts are missing: ${install_dir}"
    printf '%s\n' "${commit}" > "${stamp}"
  fi

  log "preparing DXMT Wine x86_64 runtime dependency cache"
  prepare_dxmt_wine_runtime_cache "${source_dir}" "${install_dir}"
  wine_install_ready "${install_dir}" ||
    die "DXMT Wine runtime cache is incomplete after dependency preparation: ${install_dir}"

  temporary="${target}.incomplete-$$"
  rm -rf "${temporary}"
  mkdir -p "${temporary}"
  # Prefer rsync when available (faster refresh of an existing install tree).
  if command -v rsync >/dev/null 2>&1; then
    rsync -a --delete "${install_dir}/" "${temporary}/"
  else
    cp -a "${install_dir}/." "${temporary}/"
  fi
  printf 'schema=2\nfingerprint=%s\nsource_commit=%s\n' \
    "${fingerprint}" "${commit}" > "${temporary}/.dxmt-builder-dependency"
  rm -rf "${target}"
  mv "${temporary}" "${target}"
  log "Wine x86_64 dependency published: ${target}"
}

ensure_wine() {
  local flavor="$1"
  case "${flavor}" in
    x86_64)
      ensure_wine_x86_64
      ;;
    *)
      die "unknown Wine flavor: ${flavor}"
      ;;
  esac
}

ensure_llvm_mingw() {
  ensure_root
  local target archive_hash temporary extraction
  local archive="${DOWNLOADS_DIR}/${LLVM_MINGW_DIR}.tar.xz"
  download_file "https://github.com/mstorsjo/llvm-mingw/releases/download/${LLVM_MINGW_VERSION}/${LLVM_MINGW_DIR}.tar.xz" "${archive}"
  archive_hash="$(shasum -a 256 "${archive}" | awk '{print substr($1,1,16)}')"
  target="${TOOLCHAINS_DIR}/llvm-mingw-${LLVM_MINGW_VERSION}-${archive_hash}"
  if [[ -x "${target}/bin/x86_64-w64-mingw32-gcc" ]]; then
    log "LLVM-MinGW already prepared: ${target}"
    return
  fi
  temporary="${target}.incomplete-$$"
  extraction="${temporary}-extract"
  rm -rf "${temporary}" "${extraction}"
  mkdir -p "${extraction}"
  tar -xf "${archive}" -C "${extraction}"
  mv "${extraction}/${LLVM_MINGW_DIR}" "${temporary}"
  rm -rf "${extraction}"
  printf 'schema=1\narchive_sha256=%s\n' "$(shasum -a 256 "${archive}" | awk '{print $1}')" \
    > "${temporary}/.dxmt-builder-dependency"
  mv "${temporary}" "${target}"
}

ensure_llvm_project() {
  ensure_root
  local target="${TOOLCHAINS_DIR}/llvm-project-${LLVM_VERSION}"
  local temporary="${target}.incomplete-$$"
  if [[ -d "${target}/llvm" ]]; then
    log "LLVM source already prepared: ${target}"
    return
  fi
  rm -rf "${temporary}"
  git clone --depth 1 --branch "${LLVM_VERSION}" https://github.com/llvm/llvm-project.git "${temporary}"
  mv "${temporary}" "${target}"
}

llvm_project_dir() {
  printf '%s\n' "${TOOLCHAINS_DIR}/llvm-project-${LLVM_VERSION}"
}

# Managed ccache for CMake LLVM bootstraps (separate from Meson profile builds).
configure_llvm_ccache() {
  ensure_command_or_formula ccache ccache
  export CCACHE_DIR="${CACHE_ROOT}/ccache/data"
  export CCACHE_CONFIGPATH="${CACHE_ROOT}/ccache/ccache.conf"
  mkdir -p "${CCACHE_DIR}" "$(dirname "${CCACHE_CONFIGPATH}")"
  if [[ ! -f "${CCACHE_CONFIGPATH}" ]]; then
    {
      printf 'cache_dir = %s\n' "${CCACHE_DIR}"
      printf 'max_size = 20G\n'
      printf 'compression = true\n'
      printf 'compiler_check = content\n'
    } > "${CCACHE_CONFIGPATH}"
  fi
  log "LLVM ccache enabled: CCACHE_DIR=${CCACHE_DIR}"
  ccache --zero-stats >/dev/null 2>&1 || true
}

llvm_install_ready() {
  local prefix="$1"
  local required
  local required_files=(
    include/llvm/ADT/StringRef.h
    include/llvm/IR/Constants.h
    lib/cmake/llvm/LLVMConfig.cmake
    lib/libLLVMBitReader.a
    lib/libLLVMCore.a
    lib/libLLVMRemarks.a
    lib/libLLVMBinaryFormat.a
    lib/libLLVMBitstreamReader.a
    lib/libLLVMSupport.a
    lib/libLLVMDemangle.a
  )
  for required in "${required_files[@]}"; do
    [[ -f "${prefix}/${required}" ]] || return 1
  done
}

# Publish an install prefix only after it contains headers, static libraries,
# LLVMConfig.cmake, and the builder marker.
# Cancelled jobs often leave *.incomplete-* trees; recover those before rebuilding.
finalize_llvm_prefix() {
  local source="$1"
  local target="$2"
  local fingerprint="$3"
  llvm_install_ready "${source}" ||
    die "LLVM install prefix is incomplete: ${source}"
  printf 'schema=1\nfingerprint=%s\n' "${fingerprint}" > "${source}/.dxmt-builder-dependency"
  sync 2>/dev/null || true
  if [[ -e "${target}" ]]; then
    log "replacing incomplete LLVM target without valid marker: ${target}"
    rm -rf "${target}"
  fi
  mv "${source}" "${target}"
  [[ -f "${target}/.dxmt-builder-dependency" ]] ||
    die "LLVM finalize lost dependency marker at ${target}"
  llvm_install_ready "${target}" ||
    die "LLVM finalize lost required files at ${target}"
  log "LLVM dependency prepared: ${target}"
}

recover_abandoned_llvm_prefix() {
  local target="$1"
  local fingerprint="$2"
  local pattern="$3"
  local abandoned

  if [[ -f "${target}/.dxmt-builder-dependency" ]] && llvm_install_ready "${target}"; then
    return 1
  fi

  shopt -s nullglob
  for abandoned in ${pattern}; do
    [[ -d "${abandoned}" ]] || continue
    if llvm_install_ready "${abandoned}"; then
      log "finalizing abandoned LLVM install: ${abandoned}"
      finalize_llvm_prefix "${abandoned}" "${target}" "${fingerprint}"
      shopt -u nullglob
      return 0
    fi
    log "removing unusable incomplete LLVM tree: ${abandoned}"
    rm -rf "${abandoned}"
  done
  shopt -u nullglob

  if [[ -e "${target}" ]]; then
    log "removing incomplete LLVM target: ${target}"
    rm -rf "${target}"
  fi
  return 1
}

ensure_llvm_darwin() {
  local arch="$1"
  [[ "${arch}" == "x86_64" ]] || die "only x86_64 LLVM Darwin dependencies are supported"
  ensure_root
  setup_paths
  local fingerprint target temporary build
  fingerprint="$(printf '%s\n' "schema=1" "version=${LLVM_VERSION}" "arch=${arch}" \
    'assertions=on' 'zstd=off' 'targets=' 'tools=off' 'tests=off' \
    'benchmarks=off' 'examples=off' | shasum -a 256 | awk '{print substr($1,1,16)}')"
  target="${TOOLCHAINS_DIR}/llvm-darwin-${LLVM_VERSION}-${fingerprint}-${arch}"
  temporary="${target}.incomplete-$$"
  build="${temporary}-build"
  if [[ -f "${target}/.dxmt-builder-dependency" ]] && llvm_install_ready "${target}"; then
    log "LLVM Darwin ${arch} already prepared: ${target}"
    return
  fi
  if recover_abandoned_llvm_prefix "${target}" "${fingerprint}" \
      "${TOOLCHAINS_DIR}/llvm-darwin-${LLVM_VERSION}-${fingerprint}-${arch}.incomplete-*"; then
    return
  fi
  ensure_llvm_project
  configure_llvm_ccache
  rm -rf "${build}" "${temporary}"
  mkdir -p "${build}"
  cmake -B "${build}" -S "$(llvm_project_dir)/llvm" \
    -DCMAKE_INSTALL_PREFIX="${temporary}" \
    -DCMAKE_C_COMPILER_LAUNCHER=ccache \
    -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
    -DCMAKE_OSX_ARCHITECTURES="${arch}" \
    -DLLVM_HOST_TRIPLE="${arch}-apple-darwin" \
    -DLLVM_ENABLE_ASSERTIONS=On \
    -DLLVM_ENABLE_ZSTD=Off \
    -DCMAKE_BUILD_TYPE=Release \
    -DLLVM_TARGETS_TO_BUILD="" \
    -DLLVM_BUILD_TOOLS=Off \
    -DLLVM_INCLUDE_TESTS=Off \
    -DLLVM_INCLUDE_BENCHMARKS=Off \
    -DLLVM_INCLUDE_EXAMPLES=Off \
    -DBUG_REPORT_URL="https://github.com/gamesir-labs/dxmt" \
    -DPACKAGE_VENDOR="DXMT" \
    -DLLVM_VERSION_PRINTER_SHOW_HOST_TARGET_INFO=Off \
    -G Ninja
  cmake --build "${build}" --target install --parallel "$(sysctl -n hw.ncpu)"
  log "LLVM Darwin cmake install finished; finalizing ${temporary}"
  finalize_llvm_prefix "${temporary}" "${target}" "${fingerprint}"
  rm -rf "${build}"
  if command -v ccache >/dev/null 2>&1; then
    log "LLVM Darwin ccache stats:"
    ccache --show-stats 2>&1 | sed 's/^/[dxmt-ci] ccache: /' >&2 || true
  fi
}

ensure_llvm_win() {
  ensure_root
  setup_paths
  local fingerprint target temporary build llvm_mingw
  fingerprint="$(printf '%s\n' "schema=1" "version=${LLVM_VERSION}" \
    'host=x86_64-w64-mingw32' 'assertions=on' 'zstd=off' 'targets=' 'tools=off' \
    'tests=off' 'benchmarks=off' 'examples=off' \
    | shasum -a 256 | awk '{print substr($1,1,16)}')"
  target="${TOOLCHAINS_DIR}/llvm-win-${LLVM_VERSION}-${fingerprint}-x86_64-w64-mingw32"
  temporary="${target}.incomplete-$$"
  build="${temporary}-build"
  if [[ -f "${target}/.dxmt-builder-dependency" ]] && llvm_install_ready "${target}"; then
    log "LLVM Windows already prepared: ${target}"
    return
  fi
  if recover_abandoned_llvm_prefix "${target}" "${fingerprint}" \
      "${TOOLCHAINS_DIR}/llvm-win-${LLVM_VERSION}-${fingerprint}-x86_64-w64-mingw32.incomplete-*"; then
    return
  fi
  ensure_llvm_project
  ensure_llvm_mingw
  configure_llvm_ccache
  llvm_mingw="$(find "${TOOLCHAINS_DIR}" -maxdepth 1 -type d -name 'llvm-mingw-*' | sort | tail -1)"
  [[ -n "${llvm_mingw}" ]] || die "managed LLVM-MinGW dependency was not found"
  rm -rf "${build}" "${temporary}"
  mkdir -p "${build}"
  append_path "${llvm_mingw}/bin"
  cmake -B "${build}" -S "$(llvm_project_dir)/llvm" -DCMAKE_SYSTEM_NAME=Windows \
    -DCMAKE_INSTALL_PREFIX="${temporary}" \
    -DCMAKE_C_COMPILER_LAUNCHER=ccache \
    -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
    -DLLVM_HOST_TRIPLE=x86_64-w64-mingw32 \
    -DLLVM_ENABLE_ASSERTIONS=On \
    -DLLVM_ENABLE_ZSTD=Off \
    -DCMAKE_BUILD_TYPE=Release \
    -DLLVM_TARGETS_TO_BUILD="" \
    -DLLVM_BUILD_TOOLS=Off \
    -DLLVM_INCLUDE_TESTS=Off \
    -DLLVM_INCLUDE_BENCHMARKS=Off \
    -DLLVM_INCLUDE_EXAMPLES=Off \
    -DCMAKE_SYSROOT="${llvm_mingw}" \
    -DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc \
    -DBUG_REPORT_URL="https://github.com/gamesir-labs/dxmt" \
    -DPACKAGE_VENDOR="DXMT" \
    -DLLVM_VERSION_PRINTER_SHOW_HOST_TARGET_INFO=Off \
    -G Ninja
  cmake --build "${build}" --target install --parallel "$(sysctl -n hw.ncpu)"
  log "LLVM Windows cmake install finished; finalizing ${temporary}"
  finalize_llvm_prefix "${temporary}" "${target}" "${fingerprint}"
  rm -rf "${build}"
  if command -v ccache >/dev/null 2>&1; then
    log "LLVM Windows ccache stats:"
    ccache --show-stats 2>&1 | sed 's/^/[dxmt-ci] ccache: /' >&2 || true
  fi
}

prepare_job() {
  setup_paths
  ensure_meson
  check_apple_tools
}

link_toolchains() {
  local wine_flavor="${1:-x86_64}"
  local wine_target llvm_darwin_x64 llvm_win
  mkdir -p "${TOOLCHAINS_DIR}"
  case "${wine_flavor}" in
    x86_64)
      wine_target="$(find "${TOOLCHAINS_DIR}" -maxdepth 1 -type d -name 'wine-x86_64-*' | sort | tail -1)"
      ;;
    *)
      die "unknown Wine flavor for link-toolchains: ${wine_flavor}"
      ;;
  esac
  [[ -n "${wine_target}" ]] || die "managed Wine dependency is missing"
  ln -sfn "${wine_target}" "${TOOLCHAINS_DIR}/wine"
  llvm_darwin_x64="$(find "${TOOLCHAINS_DIR}" -maxdepth 1 -type d -name 'llvm-darwin-*-x86_64' | sort | tail -1)"
  llvm_win="$(find "${TOOLCHAINS_DIR}" -maxdepth 1 -type d -name 'llvm-win-*-x86_64-w64-mingw32' | sort | tail -1)"
  [[ -n "${llvm_darwin_x64}" ]] && ln -sfn "${llvm_darwin_x64}" "${TOOLCHAINS_DIR}/llvm-darwin"
  [[ -n "${llvm_win}" ]] && ln -sfn "${llvm_win}" "${TOOLCHAINS_DIR}/llvm"
  for llvm_mingw in "${TOOLCHAINS_DIR}"/llvm-mingw-*; do
    [[ -d "${llvm_mingw}/bin" ]] && append_path "${llvm_mingw}/bin"
  done
}

artifact_dir() {
  local name="$1"
  printf '%s/%s/%s\n' "${ARTIFACTS_DIR}" "${RUN_KEY}" "${name}"
}

stage_artifact() {
  local name="$1"
  local source="$2"
  local dest
  dest="$(artifact_dir "${name}")"
  rm -rf "${dest}"
  mkdir -p "${dest}"
  cp -a "${source}/." "${dest}/"
  log "staged artifact ${name}: ${dest}"
}

copy_artifact() {
  local name="$1"
  local dest="$2"
  local source
  source="$(artifact_dir "${name}")"
  [[ -d "${source}" ]] || die "artifact not found: ${source}"
  mkdir -p "${dest}"
  cp -a "${source}/." "${dest}/"
}

stage_files() {
  local name="$1"
  shift
  local dest
  dest="$(artifact_dir "${name}")"
  rm -rf "${dest}"
  mkdir -p "${dest}"
  for file in "$@"; do
    cp -a "${file}" "${dest}/"
  done
  log "staged files ${name}: ${dest}"
  if [[ -n "${GITHUB_STEP_SUMMARY:-}" ]]; then
    {
      printf '### DXMT local artifact\n\n'
      printf '%s\n' "\`${dest}\`"
    } >> "${GITHUB_STEP_SUMMARY}"
  fi
}

restore_staged_files() {
  (( $# >= 3 )) || die "restore-staged-files requires <name> <dest-dir> <file>..."
  local name="$1"
  local dest="$2"
  shift 2

  if [[ ! -d "${ARTIFACTS_DIR}" ]]; then
    log "artifact root does not exist: ${ARTIFACTS_DIR}"
    return 1
  fi

  local line run_dir source missing file base
  while IFS= read -r line; do
    [[ -n "${line}" ]] || continue
    run_dir="${line#*$'\t'}"
    source="${run_dir}/${name}"
    [[ -d "${source}" ]] || continue

    missing=0
    for file in "$@"; do
      base="$(basename "${file}")"
      if [[ ! -f "${source}/${base}" ]]; then
        missing=1
        break
      fi
    done
    (( missing == 0 )) || continue

    mkdir -p "${dest}"
    for file in "$@"; do
      base="$(basename "${file}")"
      cp -a "${source}/${base}" "${dest}/"
    done
    log "restored staged files ${name} from ${source} into ${dest}"
    if [[ -n "${GITHUB_STEP_SUMMARY:-}" ]]; then
      {
        printf '### DXMT restored local artifact\n\n'
        printf '%s\n' "\`${source}\`"
      } >> "${GITHUB_STEP_SUMMARY}"
    fi
    return 0
  done < <(find "${ARTIFACTS_DIR}" -mindepth 1 -maxdepth 1 -type d -exec stat -f $'%m\t%N' {} \; | sort -rn)

  log "staged files ${name} not found under ${ARTIFACTS_DIR}"
  return 1
}

prune_artifact_runs() {
  local keep_count="${1:-3}"
  [[ "${keep_count}" =~ ^[0-9]+$ ]] || die "keep count must be a positive integer: ${keep_count}"
  (( keep_count > 0 )) || die "keep count must be greater than zero"

  if [[ ! -d "${ARTIFACTS_DIR}" ]]; then
    log "artifact root does not exist, nothing to prune: ${ARTIFACTS_DIR}"
    return
  fi

  local current_run_dir="${ARTIFACTS_DIR}/${RUN_KEY}"
  if [[ -d "${current_run_dir}" ]]; then
    touch "${current_run_dir}"
  fi

  local index=0
  local line dir
  while IFS= read -r line; do
    [[ -n "${line}" ]] || continue
    dir="${line#*$'\t'}"
    index=$((index + 1))
    if (( index <= keep_count )); then
      log "keeping artifact run ${index}/${keep_count}: ${dir}"
      continue
    fi
    [[ -d "${dir}" ]] || continue
    log "removing old artifact run: ${dir}"
    rm -rf "${dir}"
  done < <(find "${ARTIFACTS_DIR}" -mindepth 1 -maxdepth 1 -type d -exec stat -f $'%m\t%N' {} \; | sort -rn)
}

usage() {
  cat <<'EOF'
usage: ci-self-hosted.sh [--config FILE] <command> [args]

commands:
  setup-host
  prepare-job
  ensure-wine <x86_64>
  ensure-llvm-mingw
  ensure-llvm-project
  ensure-llvm-darwin <x86_64>
  ensure-llvm-win
  link-toolchains [x86_64]
  stage-artifact <name> <source-dir>
  copy-artifact <name> <dest-dir>
  stage-files <name> <file>...
  restore-staged-files <name> <dest-dir> <file>...
  prune-artifact-runs [keep-count]
EOF
}

command="${1:-}"
shift || true
case "${command}" in
  setup-host) setup_host "$@" ;;
  prepare-job) prepare_job "$@" ;;
  ensure-wine) ensure_wine "$@" ;;
  ensure-llvm-mingw) ensure_llvm_mingw "$@" ;;
  ensure-llvm-project) ensure_llvm_project "$@" ;;
  ensure-llvm-darwin) ensure_llvm_darwin "$@" ;;
  ensure-llvm-win) ensure_llvm_win "$@" ;;
  link-toolchains) link_toolchains "$@" ;;
  stage-artifact) stage_artifact "$@" ;;
  copy-artifact) copy_artifact "$@" ;;
  stage-files) stage_files "$@" ;;
  restore-staged-files) restore_staged_files "$@" ;;
  prune-artifact-runs) prune_artifact_runs "$@" ;;
  ""|-h|--help) usage ;;
  *) usage; die "unknown command: ${command}" ;;
esac
