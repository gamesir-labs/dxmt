#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
CACHE_ROOT="${DXMT_MANAGED_CACHE_ROOT:-${REPO_ROOT}/.cache/managed}"
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
    printf '%s\n' "${dir}" >> "${GITHUB_PATH}"
  fi
}

setup_paths() {
  append_path /opt/homebrew/bin
  append_path /usr/local/bin
  append_path /opt/homebrew/opt/bison/bin
  append_path /usr/local/opt/bison/bin
  for pybin in "${HOME}"/Library/Python/*/bin; do
    append_path "${pybin}"
  done
  for llvm_mingw in "${TOOLCHAINS_DIR}"/llvm-mingw-*; do
    [[ -d "${llvm_mingw}/bin" ]] && append_path "${llvm_mingw}/bin"
  done
}

ensure_root() {
  mkdir -p "${TOOLCHAINS_DIR}" "${DOWNLOADS_DIR}" \
    "${ARTIFACTS_DIR}" "${SOURCES_DIR}"
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
  xcrun --sdk macosx --show-sdk-path >/dev/null
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
  ensure_root
  setup_paths
  ensure_homebrew_build_tools
  ensure_wine_rosetta_build_tools
  ensure_lunarg_vulkan_sdk
  ensure_meson
  check_apple_tools
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

download_wine_source_tarball() {
  local source_dir="$1"
  local repo archive
  repo="$(wine_github_repo)"
  archive="${DOWNLOADS_DIR}/wine-${WINE_GIT_BRANCH}-${WINE_SOURCE_COMMIT}.tar.gz"

  if [[ ! -f "${archive}" ]]; then
    log "downloading Wine source tarball ${repo}@${WINE_SOURCE_COMMIT}"
    gh_wine api \
      -H "Accept: application/vnd.github+json" \
      "/repos/${repo}/tarball/${WINE_GIT_BRANCH}" > "${archive}" ||
      die "failed to download Wine source tarball"
  fi

  rm -rf "${source_dir}"
  mkdir -p "${source_dir}"
  tar -zxf "${archive}" --strip-components 1 -C "${source_dir}"
  printf '%s\n' "${WINE_SOURCE_COMMIT}" > "${source_dir}/.dxmt-ci-source-commit"
}

sync_wine_git_source() {
  local source_dir="$1"
  local source_stamp
  export GIT_TERMINAL_PROMPT=0
  ensure_gh_wine
  WINE_SOURCE_COMMIT=""
  check_wine_access
  source_stamp="${source_dir}/.dxmt-ci-source-commit"

  if [[ -d "${source_dir}" &&
        -f "${source_stamp}" &&
        "$(cat "${source_stamp}")" == "${WINE_SOURCE_COMMIT}" ]]; then
    log "Wine source already prepared from ${WINE_SOURCE_COMMIT}: ${source_dir}"
    return
  fi

  log "preparing Wine source ${WINE_GIT_BRANCH}@${WINE_SOURCE_COMMIT}"
  download_wine_source_tarball "${source_dir}"
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

build_wine_git_source() {
  local source_dir="$1"
  chmod +x "${source_dir}/scripts/build_on_m1.sh" "${source_dir}/scripts/build_on_intel.sh"
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
  source_dir="${SOURCES_DIR}/wine-proton-macos-$(safe_name "${WINE_GIT_BRANCH}")"
  WINE_SOURCE_COMMIT=""
  sync_wine_git_source "${source_dir}"
  commit="${WINE_SOURCE_COMMIT}"
  [[ -n "${commit}" ]] || die "Wine source commit is empty"
  fingerprint="$(printf '%s\n' \
    "schema=1" "version=${WINE_VERSION}" "commit=${commit}" \
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

  if wine_development_install_ready "${install_dir}" &&
     [[ -f "${stamp}" ]] &&
     [[ "$(cat "${stamp}")" == "${commit}" ]]; then
    log "Wine x86_64 development artifacts already prepared from ${commit}: ${install_dir}"
  else
    log "building Wine x86_64 from ${commit}"
    rm -rf "${source_dir}/build" "${source_dir}/install"
    rm -f "${source_dir}/.generated"
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
  cp -a "${install_dir}/." "${temporary}/"
  printf 'schema=1\nfingerprint=%s\nsource_commit=%s\n' \
    "${fingerprint}" "${commit}" > "${temporary}/.dxmt-builder-dependency"
  mv "${temporary}" "${target}"
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
  if [[ -f "${target}/.dxmt-builder-dependency" ]]; then
    log "LLVM Darwin ${arch} already prepared: ${target}"
    return
  fi
  ensure_llvm_project
  rm -rf "${build}" "${temporary}"
  mkdir -p "${build}"
  cmake -B "${build}" -S "$(llvm_project_dir)/llvm" \
    -DCMAKE_INSTALL_PREFIX="${temporary}" \
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
  printf 'schema=1\nfingerprint=%s\n' "${fingerprint}" > "${temporary}/.dxmt-builder-dependency"
  mv "${temporary}" "${target}"
  rm -rf "${build}"
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
  if [[ -f "${target}/.dxmt-builder-dependency" ]]; then
    log "LLVM Windows already prepared: ${target}"
    return
  fi
  ensure_llvm_project
  ensure_llvm_mingw
  llvm_mingw="$(find "${TOOLCHAINS_DIR}" -maxdepth 1 -type d -name 'llvm-mingw-*' | sort | tail -1)"
  [[ -n "${llvm_mingw}" ]] || die "managed LLVM-MinGW dependency was not found"
  rm -rf "${build}" "${temporary}"
  mkdir -p "${build}"
  append_path "${llvm_mingw}/bin"
  cmake -B "${build}" -S "$(llvm_project_dir)/llvm" -DCMAKE_SYSTEM_NAME=Windows \
    -DCMAKE_INSTALL_PREFIX="${temporary}" \
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
  printf 'schema=1\nfingerprint=%s\n' "${fingerprint}" > "${temporary}/.dxmt-builder-dependency"
  mv "${temporary}" "${target}"
  rm -rf "${build}"
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
usage: ci-self-hosted.sh <command> [args]

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
