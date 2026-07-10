# DXMT tests

DXMT unit tests use GoogleTest and run through one native macOS test image. A
small C++ coordinator discovers the filtered tests in memory, partitions them,
and starts one worker process per batch. This keeps process launches bounded by
the worker count instead of the test count.

## Layout

```text
tests/
  include/dxmt_test.hpp  Stable include for all tests
  include/dxmt_test_scheduler.hpp  Scheduler API and slow-test markers
  support/main.cpp       Shared DXMT test runner entry point
  support/scheduler.cpp  Native parallel scheduler and worker launcher
  support/scheduler_plan.cpp  Filter, worker-count, and LPT planning core
  unit/meson.build       Unit-test suite manifest
  unit/placeholder_test.cpp  Minimal copyable test example
  unit/scheduler_test.cpp  Scheduler contract and parallel smoke cases
  unit/layout_test.cpp   Test-infrastructure layout contract
  unit/*_test.cpp        Unit-test sources grouped by subsystem
```

`tests/meson.build` owns the common runner and dependency wiring. Individual
source groups must not create their own runner or repeat GoogleTest
dependencies. `layout_test.cpp` intentionally fails if core
test-infrastructure files are moved without updating the contract.

## Add a test

Add the test source under `tests/unit/`, include the DXMT test header, and use
GoogleTest normally:

```cpp
#include <dxmt_test.hpp>

TEST(ExampleTest, DoesSomething) {
  EXPECT_EQ(1 + 1, 2);
}
```

Then add the file to an existing entry in `tests/unit/meson.build`:

```meson
'sources': files(
  'existing_test.cpp',
  'new_test.cpp',
),
```

To create a logical source group with its own dependencies, add one dictionary
entry. The shared manifest collects every group into the same test image:

```meson
'util': {
  'sources': files('util_test.cpp'),
  'dependencies': [util_dep],
},
```

## Mark slow tests

`slow` is only a relative scheduling-cost hint. A slow unit test might take
hundreds of milliseconds instead of a few milliseconds, but it remains a
correctness test and is always included in the standard `unit` run.

Use `DXMT_SLOW_TEST` or `DXMT_SLOW_TEST_F` in place of the corresponding
GoogleTest macro:

```cpp
DXMT_SLOW_TEST(PipelineCacheTest, RestoresLargeArchive) {
  // Correctness assertions remain unchanged.
}
```

Parameterized or typed tests can attach the same cost through a GoogleTest glob
without changing whether they run:

```cpp
DXMT_SLOW_TEST_PATTERN("LargeCases/PipelineCacheTest.*/*");
```

The scheduler assigns slow cases a larger estimated cost and uses
longest-processing-time-first partitioning. This spreads them across workers
instead of placing several hundred-millisecond cases in one tail batch.

## Scheduler model

- GoogleTest filtering and disabled-test behavior are applied before planning.
- The default worker count is the smaller of the runnable test count and the
  machine's logical CPU count, then amortized so each worker receives at least
  four normal-test cost units. Large test sets still use every logical CPU.
- Every worker is launched exactly once and receives a batch through
  `--gtest_filter`; tests within a batch stay in one process.
- A single-threaded coordinator uses macOS copy-on-write `fork` so workers reuse
  the already loaded test image. If code starts threads before `main`, the
  scheduler automatically falls back to `posix_spawn`. Set
  `DXMT_TEST_WORKER_MODE=fork` or `spawn` to diagnose either path explicitly.
- Meson registers the coordinator with `is_parallel: false` because the
  coordinator owns all internal parallelism. This prevents nested schedulers
  from oversubscribing the machine; it does not make the tests serial.
- Workers print failures only by default, avoiding per-test status output that
  can cost more than millisecond-scale tests. Set
  `DXMT_TEST_VERBOSE_WORKERS=1` when full worker output is needed.
- Failure short-circuiting is disabled by policy: `fail_fast`,
  `break_on_failure`, and `throw_on_failure` are forced off, while C++
  exceptions are caught as test failures. Every worker finishes its complete
  batch, then the coordinator prints one combined failure summary.
- A GoogleTest XML output or result-stream request forces one worker because
  multiple writers cannot safely share one output destination.

Override the automatic concurrency decision for diagnostics with either
`--dxmt-test-jobs=<count>` or `DXMT_TEST_JOBS=<count>`. Setting the count to one
runs GoogleTest directly without spawning a worker. Standard
`--gtest_filter=...` arguments continue to work.

Parallel workers are separate processes. Unit tests must not rely on mutable
process-global state created by another test, and shared filesystem artifacts
must use unique temporary paths. A process-level crash or forced termination
cannot continue the affected batch; the no-short-circuit guarantee applies to
GoogleTest assertion failures and caught exceptions.

Performance benchmarks are not unit tests and must not be registered in this
manifest. They have a separate dependency, runner, directory, Meson
abstraction, and acceptance policy under `benchmarks/`; see
`benchmarks/README.md`.

## Build and run

Configure, build, and run all unit-test suites with:

```sh
meson setup \
  -Dnative_llvm_path=/usr/local/opt/llvm@15 \
  -Denable_tests=true \
  build-tests \
  --buildtype release
meson compile -C build-tests dxmt-unit-tests
# Lowest-overhead local path; run from the repository root.
build-tests/tests/unit/dxmt-unit-tests
# Meson/CI registration; also runs every correctness test.
meson test -C build-tests --suite unit --print-errorlogs
```

Use a separate cross-build directory for Wine-facing DLLs. GoogleTest unit
tests intentionally reject cross-build configuration so the test runner and
its scheduler never pay Wine process startup overhead.

Windows D3D unit tests use that separate cross-build and run one GoogleTest
image per API through Wine. Every operation remains an independently filterable
`TEST` case; aggregating the cases into one image only amortizes Wine process
startup. Long queue, resource-lifetime, and mixed-encoder workloads belong to
the Wine integration benchmark layer instead of the unit-test image.

The managed mode builds Wine once under `wine_build_path`, installs the runtime
at `external/wine/build/dxmt-install`, and uses the Wine Proton development
cache preparer to bundle relocatable host dylibs without removing the static
archives needed by DXMT. The result is reused for DXMT linking, tests,
benchmarks, and apitrace-enabled builds:

```sh
meson setup \
  --cross-file build-win64.txt \
  -Dnative_llvm_path=/usr/local/opt/llvm@15 \
  -Denable_d3d12_tests=true \
  -Dwine_source_path=../wine-proton-macos \
  build-wine-tests \
  --buildtype release
scripts/run-wine-tests.sh build-wine-tests unit
```

The Wine Proton source tree supplies its official development-cache preparer;
`wine_runtime_preparer_path` remains available only when the build source and
the cache preparer intentionally come from different trees.

`run-wine-tests.sh` compiles the build, stages the current DXMT runtime, and
exports the stage to every Wine process before Meson schedules the tests. The
Wine root itself is supplied by Meson, so no launcher path is required.

To reuse a prebuilt cache, disable the managed build and point both build-time
and runtime consumers at one complete Wine installation:

```sh
meson setup \
  --cross-file build-win64.txt \
  -Dnative_llvm_path=/usr/local/opt/llvm@15 \
  -Denable_d3d12_tests=true \
  -Dwine_build_path= \
  -Dwine_install_path=/path/to/wine-cache \
  build-wine-tests \
  --buildtype release
```

The cache must contain `bin/wine` (or `bin/wine64`), `bin/wineserver`,
`bin/winebuild`, the Wine development libraries under `lib/wine/`, and bundled
host runtime dependencies such as `lib/libfreetype.6.dylib`. A raw Wine
`make install` directory is intentionally rejected for runtime tests.
`DXMT_TEST_WINE` remains an explicit diagnostic override; normal test and CI
runs use the configured cache root. Meson enables
`DXMT_EXPERIMENT_DX12_SUPPORT` automatically.

GoogleTest is compiled from the pinned source snapshot in
`external/googletest`.
