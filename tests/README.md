# DXMT tests

All DXMT correctness tests run through Wine. Meson launches one Windows suite
scheduler, which starts API-isolated GoogleTest coordinators with Win32
`CreateProcessW`. Each coordinator discovers, partitions, and starts its own
test workers inside the same Wine session. This reuses the Wine prefix, Wine
server, loaded runtime, and staged DXMT build without loading incompatible
Metal3 and Metal4 backends into one Windows process.

## Layout

```text
tests/
  include/dxmt_test.hpp             Stable include for every test
  include/dxmt_test_com.hpp         Move-only COM ownership helper
  include/dxmt_test_scheduler.hpp   Scheduler API and slow-test markers
  support/main.cpp                  Shared Windows scheduler entry point
  support/scheduler.cpp             Wine worker launcher and failure aggregation
  support/scheduler_plan.cpp        Filter, worker-count, and LPT planning core
  support/wine_process.hpp          Shared Win32 process-launch abstraction
  support/wine_suite_scheduler.cpp  Top-level API suite scheduler
  meson.build                       Unified Wine unit-test manifest
  wine/*_spec.cpp                   Framework and scheduler contract tests
  wine/*_test.cpp                   Internal implementation unit tests
  d3d10/*_spec.cpp                  D3D10 correctness tests
  d3d11/*_spec.cpp                  D3D11 correctness tests
  d3d12/*_spec.cpp                  D3D12 correctness tests
```

`d3d11/capability_spec.cpp` and `d3d12/capability_spec.cpp` reconstruct the
observable device-probe contracts used by Unreal Engine 5.7.4's Windows RHI.
They deliberately depend only on public DXGI/D3D interfaces: no Unreal Engine
headers, source, shaders, binaries, configuration types, or implementation text
are copied into DXMT. The cases are split by adapter identity, probe/final
device creation, feature queries, format support, MSAA, memory budgets, COM
interface consistency, descriptors, and queue behavior so failures identify a
single capability boundary. The D3D12 suite also feeds the live feature level,
shader model, resource binding tier, WaveOps, and typed atomic64 reports through
UE's complete SM6 gate, including the Intel-emulation policy branch and valid
SM5 fallback. The live macOS/Wine probe treats that Windows-only vendor
extension as unavailable.

`tests/meson.build` owns the only Meson/Wine test entry, the top-level scheduler,
the shared GoogleTest runner, and the API-isolated test images. The API-specific
Meson files only export source and dependency lists; they must not register
their own Wine invocation.

## Add a test

Place API-visible behavior under the matching `tests/d3d*/` directory, include
the stable header, and use GoogleTest normally:

```cpp
#include <dxmt_test.hpp>

TEST(D3D12ExampleSpec, DoesSomething) {
  EXPECT_EQ(1 + 1, 2);
}
```

Add the source and any additional dependency to that API's `meson.build`. The
central manifest automatically links it into the matching Wine suite image.
Framework-only Wine tests live under `tests/wine/` and are registered directly
in `tests/meson.build`. Internal implementation tests use the same framework
image and remain independently filterable GoogleTest cases.

## Mark slow tests

`slow` is a relative scheduling-cost hint. A slow test remains a correctness
test and is always included in the standard `unit` run.

```cpp
DXMT_SLOW_TEST(PipelineCacheTest, RestoresLargeArchive) {
  // Correctness assertions remain unchanged.
}
```

Parameterized or typed tests can attach the same cost through a GoogleTest
glob:

```cpp
DXMT_SLOW_TEST_PATTERN("LargeCases/PipelineCacheTest.*/*");
```

The scheduler assigns slow cases a larger estimated cost and uses
longest-processing-time-first partitioning so they do not accumulate in one
tail worker.

## Scheduler model

- The outer wrapper starts Wine exactly once for
  `dxmt-wine-test-scheduler.exe`.
- The suite scheduler allocates the global worker budget across framework,
  D3D10, D3D11, and D3D12 coordinators. A one-job run executes suites
  sequentially; larger budgets run API suites concurrently.
- API isolation prevents D3D11's Metal3 DXGI backend and D3D12's Metal4 backend
  from competing inside one process.
- Each API coordinator applies GoogleTest filtering and disabled-test rules in
  memory before planning.
- The default worker count is bounded by the logical CPU count and amortized so
  each worker receives at least four normal-test cost units.
- Every worker is created inside Wine with `CreateProcessW`, receives one
  `--gtest_filter` batch, and is awaited by the coordinator.
- `DXMT_SERIAL_TEST_F` removes a test from the parallel wave. It keeps a
  dedicated worker by default; `DXMT_GROUP_SERIAL_TESTS(pattern, group)` may
  reuse one worker for a reviewed set of serial tests that safely share
  process-global, device, and window state. `DXMT_SERIAL_TEST_DOMAIN` identifies
  groups that conflict with themselves but may share a bounded two-worker wave
  with another domain. Untagged serial workers remain globally exclusive.
- Workers record per-test elapsed time in the managed Wine prefix. Later runs
  use an exponential moving average of those measurements as LPT shard costs;
  source annotations remain the cold-run fallback and minimum cost.
- Meson registers only the suite scheduler with `is_parallel: false` because
  all unit parallelism is owned inside the Wine process tree.
- Workers print failures only by default. Set `DXMT_TEST_VERBOSE_WORKERS=1` for
  full GoogleTest output.
- `fail_fast`, `break_on_failure`, and `throw_on_failure` are forced off, while
  C++ exceptions are caught as failures. Every coordinator waits for its
  workers, and the suite scheduler waits for every coordinator before printing
  the collected output and final status.
- Within one suite, GoogleTest XML output, result streaming, or external
  GoogleTest sharding uses one worker because multiple writers cannot share an
  output safely.

Override concurrency with `--dxmt-test-jobs=<count>` or
`DXMT_TEST_JOBS=<count>`. The top-level scheduler treats this as a global budget
and divides it among active API suites. Tests must not depend on mutable
process-global state from another test, and shared filesystem artifacts must
use unique paths.

A crash terminates the affected worker batch; other workers still finish. The
no-short-circuit guarantee applies to assertion failures and caught exceptions,
not to the remaining tests inside a process that has crashed.

## Build and run

Use the full x64 builder profile. It shares one incremental build directory and
one staged Wine runtime across runtime, tests, and benchmarks:

```sh
scripts/dxmt-builder build --profile gcc-x64-release-full tests-all
scripts/dxmt-builder test --profile gcc-x64-release-full unit
```

Filter a subsystem without creating another Wine test image:

```sh
scripts/dxmt-builder test --profile gcc-x64-release-full unit \
  --suite d3d12 --test-args='--gtest_filter=D3D12*'
```

Every discovered test also has a process-independent CaseId formed from the
API suite and full GoogleTest name. List IDs or replay an exact case with:

```sh
scripts/dxmt-builder test --profile gcc-x64-release-full unit \
  --suite d3d12 --test-args='--dxmt-list-case-ids'
scripts/dxmt-builder test --profile gcc-x64-release-full unit \
  --suite d3d12 \
  --test-args='--dxmt-case-id=D3D12.CopyTextureSpec.FullFootprintMatrix'
```

CaseId filters accept the same `*`, `?`, `:`, and negative-pattern syntax as
GoogleTest filters. `DXMT_TEST_CASE_ID` provides the environment equivalent,
and the documented replay spelling `--dxmt_case_id=` remains accepted. Worker
failure summaries print the CaseId before the assertion diagnostic.

Batched tests may register stable logical CaseIds below the outer GoogleTest.
The copy matrix exposes `D3D12.Copy.Buffer.ShuffledRegion.0000` through
`.4095`, and the uint arithmetic matrix exposes
`D3D12.Shader.Arithmetic.Uint32.00000` through `.16383`. Together they provide
20,480 deterministic logical cases while using only two batched outer tests.
Selecting one ID records and validates only that logical case, while also
checking that every unselected output remains poison. Registered logical cases
can emit one JSON object per line with their class, requirements, execution
path, setup, operation sequence, oracle, diagnostic state, and scheduler cost:

```sh
scripts/dxmt-builder test --profile gcc-x64-release-full unit \
  --suite d3d12 \
  --test-args='--dxmt-case-id=D3D12.Copy.Buffer.ShuffledRegion.0042'

DXMT_WINE_ROOT=.cache/wine-source/install \
  DXMT_TEST_VERBOSE_WORKERS=1 scripts/wine-test-wrapper.sh \
  .cache/managed/profiles/gcc-x64-release-full/build/tests/dxmt-wine-d3d12-tests.exe \
  --dxmt-list-case-metadata \
  --dxmt-case-id=D3D12.Shader.Arithmetic.Uint32.00042
```

On a mismatch, a matrix reports the logical CaseId and parameters, first
mismatching index, expected and actual values, and an exact replay argument.

The native CBV materialization tests also provide a targeted fault injection.
The CBV keeps a valid GPU virtual address backed by a live upload resource, but
the native-only resource lookup is forced to miss. The legacy descriptor entry
is left untouched. A correct implementation must preserve the CBV value or
fall back before executing the native shader:

```sh
DXMT_TEST_FORCE_NATIVE_CBV_RESOURCE_LOOKUP_MISS=1 \
MTL_SHADER_VALIDATION=1 \
MTL_SHADER_VALIDATION_DEFAULT_STATE=all \
MTL_SHADER_VALIDATION_REPORT_TO_STDERR=1 \
DXMT_TEST_FAIL_ON_METAL_VALIDATION=1 \
scripts/dxmt-builder test --profile gcc-x64-release-full unit \
  --suite d3d12 \
  --test-args='--gtest_filter=D3D12DescriptorSpec.*NativeResourceLookupIsUnavailable'
```

These tests are skipped unless the fault injection is explicitly enabled.

The complementary stale-entry test keeps the descriptor's non-zero resource
index but clears the indexed resource-table entry. It verifies that native
shader execution rejects or falls back from a stale zero-base entry instead of
dereferencing it:

```sh
DXMT_TEST_FORCE_NATIVE_CBV_STALE_RESOURCE_TABLE_ENTRY=1 \
MTL_SHADER_VALIDATION=1 \
MTL_SHADER_VALIDATION_DEFAULT_STATE=all \
MTL_SHADER_VALIDATION_REPORT_TO_STDERR=1 \
DXMT_TEST_FAIL_ON_METAL_VALIDATION=1 \
scripts/dxmt-builder test --profile gcc-x64-release-full unit \
  --suite d3d12 \
  --test-args='--gtest_filter=D3D12DescriptorSpec.RejectsStaleNativeCbvResourceTableEntryBeforeShaderExecution'
```

`scripts/dxmt-builder test` compiles and stages the current DXMT DLLs, initializes the
dedicated prefix when needed, and injects the staged runtime before starting the
coordinator. Do not run the PE directly when validating DXMT behavior, because
that can silently select stale DLLs.

## Quality-gate assets (not wired into the CLI yet)

Standalone runner scripts (`run-d3d12-*.sh/py`, `check-d3d12-coverage.py`,
`run-wine-tests.sh`, `wine-test-wrapper.sh`, …) were removed. The supported
test entry point is only:

```sh
scripts/dxmt-builder test --profile gcc-x64-release-full unit|integration|performance
```

Supporting data and helpers remain in-tree for a future CLI subcommand:

| Path | Role |
|------|------|
| `tests/d3d12/differential/` | WARP oracle PE + snapshot compare |
| `tests/coverage/d3d12_coverage.json` | Public API / coverage thresholds |
| `tests/mutation/d3d12_mutations.json` | Reviewed mutation manifest |
| `tests/fault_injection/d3d12_faults.json` | Fault matrix descriptors |

Targeted D3D12 fault switches still work via env vars on the unit suite (see
above). There is no separate coverage/mutation/differential wrapper script.

The managed Wine dependency is fingerprinted below `.cache/managed/deps`.

To use a complete prebuilt Wine cache for a one-off run:

```sh
meson setup \
  --cross-file build-win64.txt \
  -Dnative_llvm_path=/usr/local/opt/llvm@15 \
  -Denable_tests=true \
  -Dwine_build_path= \
  -Dwine_install_path=/path/to/wine-cache \
  .cache/managed/manual/wine-tests \
  --buildtype release
```

The cache must include the Wine launcher, wineserver, development libraries,
and bundled host runtime dylibs. A raw `make install` directory is rejected.

Benchmarks are separate from unit tests but use the same Wine runtime and
staging path. See `benchmarks/README.md`.
