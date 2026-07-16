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
  wine/d3d12_pipeline_creation_benchmark.cpp
                                   Parallel cold PSO creation burst
  wine/ue_*_initialization.cpp     UE-like RHI initialization workload
```

## Modes

`integration` runs every registered workload exactly once and fails on any
Google Benchmark error row. It is intended for long queue, resource-lifetime,
mixed-encoder, and repeated-submission correctness scenarios and does not claim
a timing budget.

The `ue-rhi-initialization` integration suite reconstructs one full engine-like
startup workload. Its benchmark executable has no D3D imports and sequentially
starts API-isolated D3D11 and D3D12 workers inside the existing Wine session.
Each worker performs a temporary support probe, recreates the selected adapter
and final device, sweeps startup capabilities, creates the API's queue/view/
descriptor/state structures, submits representative resource work, validates
GPU output by readback, waits for idle, and tears down in reverse order. This
keeps D3D11's Metal 3 and D3D12's Metal 4 backends out of the same PE while
still presenting one serial integration result. The D3D12 probe makes the same
combined SM6/SM5 decision as UE before the formal device bootstrap continues.

The `d3d12-pipeline-creation` integration suite models an engine loading-screen
PSO burst. It synchronizes one or four application worker threads before they
create 96 unique graphics pipelines, mixes regular and tessellation pipelines,
and includes adversarial sample-count requests when the device reports an
unsupported count. The process disables the DXMT shader cache before creating
the D3D12 device so the result exposes cold creation-boundary serialization.
HLSL compilation is outside the measured interval. Result counters report
mean, p50, p95, maximum CreatePSO latency, and summed-call-time to wall-time
ratio; the Google Benchmark entry itself remains serial so Wine benchmark
suite isolation is preserved.

The `d3d12-binding-hotspots` performance suite establishes independent
baselines for the three bindless CPU paths reported by FH4. Argument-table
update measures only shader-visible descriptor overwrites. Root-table
materialization measures only the compiled command-list `Close()` interval
after pipeline warm-up. Descriptor-mirror mutation measures bulk descriptor
copies into a shader-visible heap. Every case performs a GPU readback after the
timed interval, so a fast result cannot bypass descriptor publication or root
table materialization. The argument-table total contains mirror publication by
design; the mirror benchmark isolates the bulk-copy mutation workload used to
measure that nested subpath.

`performance` performs repeated serial measurements and validates the median
against a reviewed JSON baseline plus an explicit maximum regression
percentage. Every result must be single-threaded, and missing or stale budget
entries fail acceptance. Performance budgets are machine-specific contracts
and should only be updated from the designated Wine benchmark machine. The
current binding baselines and host metadata live in
`benchmarks/budgets/d3d12_binding_hotspots.json`.

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

A Wine performance suite uses `mode: 'performance'`, adds a budget file, and
may set `repetitions`, `min_time`, and `warmup_time`. The acceptance runner
supports `cpu_time` and `real_time`, baseline-plus-regression thresholds, and
legacy absolute `max_median_ns` thresholds.

## Build and run

Benchmarks are built by the same full profile as the Wine tests:

```sh
scripts/dxmt-builder build --profile gcc-x64-release-full benchmarks
scripts/dxmt-builder test --profile gcc-x64-release-full integration
scripts/dxmt-builder test --profile gcc-x64-release-full performance
```

The helper compiles and stages the current DXMT runtime before Meson executes
the serial benchmark entries. Managed and prebuilt Wine cache selection is
identical to the unit-test path described in `tests/README.md`.
