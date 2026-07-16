#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace dxmt::test {

struct BindingHotspotMeasurement {
  double measured_ms = 0.0;
  std::uint64_t operations = 0;
  std::uint32_t expected = 0;
  std::uint32_t actual = 0;
};

using BindingHotspotError = std::optional<std::string>;

BindingHotspotError
RunArgumentTableUpdateScenario(std::uint32_t descriptor_count,
                               std::uint32_t overwrite_rounds,
                               BindingHotspotMeasurement *measurement);

BindingHotspotError
RunRootTableMaterializationScenario(std::uint32_t draw_count,
                                    std::uint32_t root_table_count,
                                    BindingHotspotMeasurement *measurement);

BindingHotspotError
RunDescriptorMirrorMutationScenario(std::uint32_t descriptor_count,
                                    std::uint32_t copy_rounds,
                                    BindingHotspotMeasurement *measurement);

BindingHotspotError
RunTypedBufferDescriptorScenario(BindingHotspotMeasurement *measurement);

BindingHotspotError
RunSharedDescriptorResidencyScenario(BindingHotspotMeasurement *measurement);

BindingHotspotError
RunRootTableInvalidationScenario(BindingHotspotMeasurement *measurement);

BindingHotspotError
RunCompiledRootDescriptorResidencyScenario(
    std::uint32_t draw_count, BindingHotspotMeasurement *measurement);

BindingHotspotError
RunCompiledDescriptorSubmissionSnapshotScenario(
    BindingHotspotMeasurement *measurement);

} // namespace dxmt::test
