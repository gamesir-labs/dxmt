# DXMT performance benchmarks

DXMT benchmarks use Google Benchmark and are separate from granular GoogleTest
unit tests. Native macOS suites enforce reviewed performance budgets. Wine
suites carry longer end-to-end D3D integration workloads through the same
benchmark abstraction and Meson benchmark scheduler.

## Layout

```text
benchmarks/
  include/dxmt_benchmark.hpp       Stable include for benchmark sources
  support/main.cpp                 Shared benchmark executable entry point
  support/benchmark_acceptance.py  Integration validation and budget enforcement
  suites/meson.build               Native performance suite manifest
  suites/*_benchmark.cpp           Native performance sources
  wine/meson.build                 Wine integration/performance suite manifest
  wine/*_benchmark.cpp             Wine D3D workloads
  budgets/*.json                   Reviewed acceptance thresholds
```

## Add a benchmark

Write a benchmark under `benchmarks/suites/`:

```cpp
#include <dxmt_benchmark.hpp>

static void BM_Example(benchmark::State& state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(OperationUnderTest());
  }
}

BENCHMARK(BM_Example);
```

Add a suite entry to `benchmarks/suites/meson.build` and define an exact budget
for every benchmark name in its JSON budget file. Budgets currently accept
`cpu_time` or `real_time` and enforce `max_median_ns`.

```meson
'example': {
  'sources': files('example_benchmark.cpp'),
  'budget': files('../budgets/example.json'),
  'dependencies': [util_dep],
},
```

```json
{
  "schema_version": 1,
  "benchmarks": {
    "BM_Example": {
      "metric": "cpu_time",
      "max_median_ns": 500
    }
  }
}
```

The acceptance runner performs five repetitions by default, validates the
median, rejects missing or stale budgets, and rejects any benchmark result that
uses more than one thread. Meson's dedicated benchmark entries are
non-parallel by design, so suites execute serially.

## Add a Wine workload

Wine workloads use the same Google Benchmark entry point but declare a mode in
`benchmarks/wine/meson.build`. Keep integration cases few and representative;
split operation-level correctness into individual GoogleTest cases under
`tests/d3d*/`.

```meson
'd3d12-integration': {
  'sources': files('d3d12_integration_benchmark.cpp'),
  'dependencies': [d3d12_runtime_support_dep],
  'mode': 'integration',
  'suite': ['wine', 'integration', 'd3d12'],
  'timeout': 600,
},
```

Integration mode runs each registered workload exactly once and fails on a
Google Benchmark error row. It does not claim a timing budget. A future Wine
performance suite uses `mode: 'performance'` plus a reviewed budget file, with
the same serial scheduler and median acceptance policy as native benchmarks.

## Build and run

Use a dedicated release build directory:

```sh
meson setup \
  -Dnative_llvm_path=/usr/local/opt/llvm@15 \
  -Denable_benchmarks=true \
  build-benchmarks \
  --buildtype release
meson compile -C build-benchmarks dxmt-benchmarks
meson test -C build-benchmarks --benchmark --suite performance \
  --print-errorlogs
```

Performance budgets are machine-specific acceptance contracts. Update them
only from the designated benchmark machine and review threshold changes like
source changes.

Use the Wine cross-build for D3D integration workloads. The helper compiles
DXMT, stages its current runtime, and then lets Meson schedule the benchmark
entries:

```sh
meson setup \
  --cross-file build-win64.txt \
  -Dnative_llvm_path=/usr/local/opt/llvm@15 \
  -Denable_d3d12_tests=true \
  -Dwine_source_path=../wine-proton-macos \
  build-wine-tests \
  --buildtype release
scripts/run-wine-tests.sh build-wine-tests integration
```

Managed and prebuilt Wine cache selection is identical to the unit-test path;
see `tests/README.md`. The same complete Wine root is used by DXMT compilation,
Wine benchmarks, and apitrace-enabled cross-build validation.
