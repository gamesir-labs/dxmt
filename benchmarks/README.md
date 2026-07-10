# DXMT performance benchmarks

DXMT benchmarks use Google Benchmark and are separate from correctness unit
tests. Every benchmark suite runs as a native macOS executable and is
registered with Meson as non-parallel work.

## Layout

```text
benchmarks/
  include/dxmt_benchmark.hpp       Stable include for benchmark sources
  support/main.cpp                 Shared benchmark executable entry point
  support/benchmark_acceptance.py  Measurement and budget enforcement
  suites/meson.build               Benchmark suite manifest
  suites/*_benchmark.cpp           Performance measurement sources
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
