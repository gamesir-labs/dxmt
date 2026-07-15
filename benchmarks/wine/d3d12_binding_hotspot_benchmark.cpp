#include <dxmt_benchmark.hpp>

#include "d3d12_binding_hotspot_scenarios.hpp"

#include <cstdint>

namespace {

using dxmt::test::BindingHotspotMeasurement;

template <typename Scenario>
void RunMeasuredScenario(benchmark::State &state, Scenario scenario) {
  for (auto _ : state) {
    BindingHotspotMeasurement measurement;
    if (const auto error = scenario(&measurement)) {
      state.SkipWithError(error->c_str());
      return;
    }
    state.SetIterationTime(measurement.measured_ms / 1000.0);
    state.counters["measured_ms"] = measurement.measured_ms;
    state.counters["operations"] = static_cast<double>(measurement.operations);
    state.counters["ns_per_operation"] =
        measurement.operations
            ? measurement.measured_ms * 1000000.0 / measurement.operations
            : 0.0;
  }
}

void BI_D3D12ArgumentTableUpdate(benchmark::State &state) {
  const auto descriptor_count = static_cast<std::uint32_t>(state.range(0));
  const auto overwrite_rounds = static_cast<std::uint32_t>(state.range(1));
  RunMeasuredScenario(state, [&](BindingHotspotMeasurement *measurement) {
    return dxmt::test::RunArgumentTableUpdateScenario(
        descriptor_count, overwrite_rounds, measurement);
  });
  state.counters["descriptors"] = descriptor_count;
  state.counters["rounds"] = overwrite_rounds;
}

void BI_D3D12RootTableMaterialization(benchmark::State &state) {
  const auto draw_count = static_cast<std::uint32_t>(state.range(0));
  const auto root_table_count = static_cast<std::uint32_t>(state.range(1));
  RunMeasuredScenario(state, [&](BindingHotspotMeasurement *measurement) {
    return dxmt::test::RunRootTableMaterializationScenario(
        draw_count, root_table_count, measurement);
  });
  state.counters["draws"] = draw_count;
  state.counters["root_tables"] = root_table_count;
}

void BI_D3D12DescriptorMirrorMutation(benchmark::State &state) {
  const auto descriptor_count = static_cast<std::uint32_t>(state.range(0));
  const auto copy_rounds = static_cast<std::uint32_t>(state.range(1));
  RunMeasuredScenario(state, [&](BindingHotspotMeasurement *measurement) {
    return dxmt::test::RunDescriptorMirrorMutationScenario(
        descriptor_count, copy_rounds, measurement);
  });
  state.counters["descriptors"] = descriptor_count;
  state.counters["rounds"] = copy_rounds;
}

BENCHMARK(BI_D3D12ArgumentTableUpdate)
    ->Args({32, 1})
    ->Args({32, 16})
    ->Args({128, 1})
    ->Args({128, 16})
    ->Args({512, 1})
    ->Args({512, 16})
    ->ArgNames({"descriptors", "rounds"})
    ->Iterations(1)
    ->UseManualTime();

BENCHMARK(BI_D3D12RootTableMaterialization)
    ->Args({128, 1})
    ->Args({128, 4})
    ->Args({128, 8})
    ->Args({512, 1})
    ->Args({512, 4})
    ->Args({512, 8})
    ->Args({2048, 8})
    ->ArgNames({"draws", "root_tables"})
    ->Iterations(1)
    ->UseManualTime();

BENCHMARK(BI_D3D12DescriptorMirrorMutation)
    ->Args({32, 1})
    ->Args({32, 16})
    ->Args({128, 1})
    ->Args({128, 16})
    ->Args({512, 1})
    ->Args({512, 16})
    ->ArgNames({"descriptors", "rounds"})
    ->Iterations(1)
    ->UseManualTime();

} // namespace
