#!/bin/sh
set -eu

script_dir=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
project_root=$(CDPATH='' cd -- "$script_dir/.." && pwd)
profile=${1:-gcc-x64-release-full}
profile_root=$project_root/.cache/managed/profiles/$profile
source_build=$profile_root/build
coverage_build=$profile_root/coverage-build
coverage_prefix=$profile_root/prefix
report_dir=$profile_root/coverage-report
coverage_python=${DXMT_COVERAGE_PYTHON:-python3}

if [ ! -f "$source_build/meson-private/coredata.dat" ]; then
  printf 'configure the builder profile first: %s\n' "$profile" >&2
  exit 2
fi
if ! "$coverage_python" -c 'import gcovr' >/dev/null 2>&1; then
  printf '%s\n' 'gcovr is required in DXMT_COVERAGE_PYTHON' >&2
  exit 2
fi

option_value() {
  meson introspect "$source_build" --buildoptions | python3 -c '
import json
import sys

name = sys.argv[1]
for item in json.load(sys.stdin):
    if item.get("name") == name:
        print(item["value"])
        break
' "$1"
}

native_llvm_path=$(option_value native_llvm_path)
wine_install_path=$(option_value wine_install_path)
dxmt_builder_path=$(option_value dxmt_builder_path)
dxmt_cache_root=$(option_value dxmt_cache_root)

if [ -f "$coverage_build/meson-private/coredata.dat" ]; then
  meson setup --reconfigure "$coverage_build" \
    --buildtype custom \
    -Doptimization=0 \
    -Ddebug=true \
    -Db_ndebug=true \
    -Db_coverage=true \
    -Denable_tests=true \
    -Denable_nvapi=false \
    -Denable_nvngx=false \
    -Dnative_llvm_path="$native_llvm_path" \
    -Dwine_build_path= \
    -Dwine_install_path="$wine_install_path" \
    -Ddxmt_builder_path="$dxmt_builder_path" \
    -Ddxmt_cache_root="$dxmt_cache_root"
else
  meson setup "$coverage_build" "$project_root" \
    --cross-file "$profile_root/meta/cross.ini" \
    --native-file "$profile_root/meta/native.ini" \
    --buildtype custom \
    -Doptimization=0 \
    -Ddebug=true \
    -Db_ndebug=true \
    -Db_coverage=true \
    -Denable_tests=true \
    -Denable_nvapi=false \
    -Denable_nvngx=false \
    -Dnative_llvm_path="$native_llvm_path" \
    -Dwine_build_path= \
    -Dwine_install_path="$wine_install_path" \
    -Ddxmt_builder_path="$dxmt_builder_path" \
    -Ddxmt_cache_root="$dxmt_cache_root"
fi

rm -rf "$report_dir"
mkdir -p "$report_dir"
find "$coverage_build" -name '*.gcda' -delete
runtime_root=$("$script_dir/stage-wine-test-runtime.sh" \
  "$coverage_build" "" d3d12 unit)
test_executable=$coverage_build/tests/dxmt-wine-d3d12-tests.exe

run_coverage_group() {
  group_name=$1
  test_filter=$2
  printf 'Running D3D12 coverage group: %s\n' "$group_name"
  DXMT_TEST_RUNTIME_ROOT=$runtime_root \
  DXMT_TEST_WINEPREFIX=$coverage_prefix \
  DXMT_TEST_WINE_ROOT=$wine_install_path \
  DXMT_TEST_REQUIRE_RUNTIME=1 \
    "$script_dir/wine-test-wrapper.sh" "$test_executable" \
      --dxmt-test-worker --gtest_brief=1 --gtest_filter="$test_filter"
}

# Isolating the major API families gives gcov a deterministic flush boundary and
# avoids accumulating Metal/Wine process state across the complete 488-test set.
run_coverage_group object-device \
  'D3D12Device*:*PrivateData*:*ComIdentity*:*FeatureCoherence*:*UnrealD3D12FeaturePolicySpec*:*D3D12UnrealCapabilitySpec*:*D3D12PipelineArchiveSpec*:*D3D12PersistentAirCacheSpec*:*CapabilityMatrix*:*UnrealDeviceQueues*:*UnrealBootstrapFormats*'
run_coverage_group resource-transfer \
  'D3D12Resource*:*Residency*:*Copy*:*Clear*:*D3D12SparseResourceSpec*'
run_coverage_group root-signature 'Root*'
run_coverage_group shader-wave 'ShaderWave*'
run_coverage_group shader-corruption 'InvalidContainers*'
run_coverage_group descriptors \
  'Descriptor*:*Sampler*:*BufferView*:*D3D12BindingHotspot*:*D3D12BindlessGraphicsStageSpec*:*D3D12Descriptor*'
run_coverage_group command \
  '*Command*:*BundleLegality*:*ComputeDispatch*:*GraphicsState*:*ComputeState*'
run_coverage_group sync-query-graphics \
  '*Barrier*:*Transition*:*Fence*:*Queue*:*Query*:*Predication*:*Occlusion*:*ExecuteIndirect*:*Integration*:*GraphicsOutputMerger*:*ExecutionPathBoundary*:*GraphicsInputAssembler*:*DepthCompare*:*Stencil*:*GraphicsBlend*:*FramePaths*:*TargetCounts*:*Indexing*:*Functions*:*Operations*:*Equations*'

"$coverage_python" -m gcovr \
  --gcov-executable x86_64-w64-mingw32-gcov \
  --gcov-ignore-parse-errors=negative_hits.warn_once_per_file \
  --root "$project_root" \
  --filter 'src/d3d12/' \
  --json "$report_dir/gcovr.json" \
  "$coverage_build/src/d3d12"
"$coverage_python" "$script_dir/check-d3d12-coverage.py" \
  --repo-root "$project_root" \
  --gcovr "$report_dir/gcovr.json" \
  --output "$report_dir/gate.json"
printf 'D3D12 coverage report: %s\n' "$report_dir/gate.json"
