# DXMT Wine benchmarks

All DXMT benchmarks run through Wine and execute serially. GoogleTest owns
granular correctness cases; Google Benchmark owns long integration workloads
and reviewed performance thresholds without adding native-only test paths.

## Layout

```text
benchmarks/
  include/dxmt_benchmark.hpp       Stable include for benchmark sources
  support/main.cpp                 Shared Windows benchmark entry point
  support/benchmark_acceptance.py  Integration and performance validation
  wine/meson.build                 Wine benchmark manifest
  wine/*_benchmark.cpp             Wine D3D workloads
```

## Modes

`integration` runs every registered workload exactly once and fails on any
Google Benchmark error row. It is intended for long queue, resource-lifetime,
mixed-encoder, and repeated-submission correctness scenarios and does not claim
a timing budget.

`performance` performs repeated serial measurements and validates the median
against a reviewed JSON budget. Every result must be single-threaded, and
missing or stale budget entries fail acceptance. Performance budgets are
machine-specific contracts and should only be updated from the designated Wine
benchmark machine.

## Add a Wine workload

Write the workload under `benchmarks/wine/` and register it in
`benchmarks/wine/meson.build`. Keep related `BENCHMARK` cases in one executable
so one Wine launch covers the whole group.

```cpp
#include <dxmt_benchmark.hpp>

static void BI_Example(benchmark::State &state) {
  for (auto _ : state) {
    if (!RunOperation()) {
      state.SkipWithError("operation failed");
      return;
    }
  }
}

BENCHMARK(BI_Example)->Iterations(1)->UseRealTime();
```

Register an integration suite as follows:

```meson
'd3d12-integration': {
  'sources': files('d3d12_integration_benchmark.cpp'),
  'dependencies': [d3d12_runtime_support_dep],
  'mode': 'integration',
  'suite': ['wine', 'integration', 'd3d12'],
  'timeout': 600,
},
```

A future Wine performance suite uses `mode: 'performance'`, adds a budget file,
and may set `repetitions`, `min_time`, and `warmup_time`. The acceptance runner
supports `cpu_time` and `real_time` with `max_median_ns` thresholds.

## Build and run

Benchmarks are built by the same Wine test configuration:

```sh
meson setup \
  --cross-file build-win64.txt \
  -Dnative_llvm_path=/usr/local/opt/llvm@15 \
  -Denable_tests=true \
  -Dwine_source_path=../wine-proton-macos \
  .cache/build/wine-tests \
  --buildtype release
scripts/run-wine-tests.sh .cache/build/wine-tests integration
```

The helper compiles and stages the current DXMT runtime before Meson executes
the serial benchmark entries. Managed and prebuilt Wine cache selection is
identical to the unit-test path described in `tests/README.md`.
