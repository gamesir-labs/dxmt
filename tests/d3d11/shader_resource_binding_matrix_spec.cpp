#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <array>
#include <cstdint>
#include <vector>

// Public-D3D11 shader-resource binding-state coverage for all six programmable
// stages. Two disjoint 64-view pools form 4096 base pairs; stage-specific
// permutations and start slots exercise the complete 128-slot state spaces.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

constexpr std::uint32_t kShaderResourceBindingCaseCount = 4096;
constexpr std::uint32_t kPrimaryPoolSize = 64;
constexpr std::uint32_t kViewCount = 2 * kPrimaryPoolSize;
constexpr UINT kBoundViewCount = 2;
constexpr UINT kResourceSlotCount =
    D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT;
constexpr UINT kStartSlotCount = kResourceSlotCount - kBoundViewCount + 1u;
constexpr UINT kShaderStageCount = 6;

const dxmt::test::LogicalCaseFamilyRegistration kShaderResourceBindingCases(
    "D3D11ShaderResourceBindingMatrixSpec."
    "RoundTrips4096PairsAcrossAllShaderStages",
    "D3D11.GetShaderResources.Binding.", kShaderResourceBindingCaseCount, 4,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "Immediate",
      "VSSetShaderResources,VSGetShaderResources,PSSetShaderResources,"
      "PSGetShaderResources,GSSetShaderResources,GSGetShaderResources,"
      "HSSetShaderResources,HSGetShaderResources,DSSetShaderResources,"
      "DSGetShaderResources,CSSetShaderResources,CSGetShaderResources,"
      "ComReferenceState"},
     dxmt::test::kResourceTestCost,
     "two disjoint pools of sixty-four test-local structured buffers and SRVs",
     "bind a stage-specific permutation of every selected SRV pair to all six "
     "shader stages, query both the exact range and all 128 slots, release "
     "getter references, then unbind every stage",
     "each getter returns the exact two COM objects only in that stage's bound "
     "slots and all stages contain only null slots after unbinding",
     "logical ID, selected-case count, base pool indexes, failing stage, phase "
     "and slot, expected and actual addresses, and exact replay argument"});

const dxmt::test::TestCostRegistration
    kShaderResourceBindingCost("D3D11ShaderResourceBindingMatrixSpec."
                               "RoundTrips4096PairsAcrossAllShaderStages",
                               dxmt::test::kResourceTestCost);

enum class ShaderStage : UINT {
  Vertex,
  Pixel,
  Geometry,
  Hull,
  Domain,
  Compute
};

const char *ShaderStageName(ShaderStage stage) {
  switch (stage) {
  case ShaderStage::Vertex:
    return "VS";
  case ShaderStage::Pixel:
    return "PS";
  case ShaderStage::Geometry:
    return "GS";
  case ShaderStage::Hull:
    return "HS";
  case ShaderStage::Domain:
    return "DS";
  case ShaderStage::Compute:
    return "CS";
  }
  return "unknown";
}

void SetStageResources(ID3D11DeviceContext *context, ShaderStage stage,
                       UINT start_slot, UINT count,
                       ID3D11ShaderResourceView *const *views) {
  switch (stage) {
  case ShaderStage::Vertex:
    context->VSSetShaderResources(start_slot, count, views);
    break;
  case ShaderStage::Pixel:
    context->PSSetShaderResources(start_slot, count, views);
    break;
  case ShaderStage::Geometry:
    context->GSSetShaderResources(start_slot, count, views);
    break;
  case ShaderStage::Hull:
    context->HSSetShaderResources(start_slot, count, views);
    break;
  case ShaderStage::Domain:
    context->DSSetShaderResources(start_slot, count, views);
    break;
  case ShaderStage::Compute:
    context->CSSetShaderResources(start_slot, count, views);
    break;
  }
}

void GetStageResources(ID3D11DeviceContext *context, ShaderStage stage,
                       UINT start_slot, UINT count,
                       ID3D11ShaderResourceView **views) {
  switch (stage) {
  case ShaderStage::Vertex:
    context->VSGetShaderResources(start_slot, count, views);
    break;
  case ShaderStage::Pixel:
    context->PSGetShaderResources(start_slot, count, views);
    break;
  case ShaderStage::Geometry:
    context->GSGetShaderResources(start_slot, count, views);
    break;
  case ShaderStage::Hull:
    context->HSGetShaderResources(start_slot, count, views);
    break;
  case ShaderStage::Domain:
    context->DSGetShaderResources(start_slot, count, views);
    break;
  case ShaderStage::Compute:
    context->CSGetShaderResources(start_slot, count, views);
    break;
  }
}

struct StageBinding {
  ShaderStage stage;
  std::array<std::uint32_t, kBoundViewCount> view_indexes;
  UINT start_slot;
};

StageBinding BindingForStage(std::uint32_t logical, UINT stage_index) {
  const std::uint32_t first = logical & 63u;
  const std::uint32_t second = (logical >> 6u) & 63u;
  const std::uint32_t stage_first = (first + stage_index * 7u) & 63u;
  const std::uint32_t stage_second = (second + stage_index * 11u) & 63u;
  return {static_cast<ShaderStage>(stage_index),
          {{stage_first, kPrimaryPoolSize + stage_second}},
          static_cast<UINT>(
              (stage_first * 17u + stage_second * 29u + stage_index * 13u) %
              kStartSlotCount)};
}

class D3D11ShaderResourceBindingMatrixSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_NE(context_.context(), nullptr);
  }

  D3D11TestContext context_;
};

TEST_F(D3D11ShaderResourceBindingMatrixSpec,
       RoundTrips4096PairsAcrossAllShaderStages) {
  std::vector<std::uint32_t> selected_cases;
  selected_cases.reserve(kShaderResourceBindingCaseCount);
  for (std::uint32_t logical = 0; logical < kShaderResourceBindingCaseCount;
       ++logical) {
    if (dxmt::test::LogicalCaseSelected(kShaderResourceBindingCases.family(),
                                        logical))
      selected_cases.push_back(logical);
  }
  ASSERT_FALSE(selected_cases.empty());

  D3D11_BUFFER_DESC buffer_desc = {};
  buffer_desc.ByteWidth = 4u * sizeof(std::uint32_t);
  buffer_desc.Usage = D3D11_USAGE_DEFAULT;
  buffer_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  buffer_desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
  buffer_desc.StructureByteStride = sizeof(std::uint32_t);
  D3D11_SHADER_RESOURCE_VIEW_DESC view_desc = {};
  view_desc.Format = DXGI_FORMAT_UNKNOWN;
  view_desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
  view_desc.Buffer.NumElements = 4;

  std::vector<ComPtr<ID3D11Buffer>> buffers(kViewCount);
  std::vector<ComPtr<ID3D11ShaderResourceView>> views(kViewCount);
  for (std::uint32_t index = 0; index < kViewCount; ++index) {
    ASSERT_EQ(context_.device()->CreateBuffer(&buffer_desc, nullptr,
                                              buffers[index].put()),
              S_OK)
        << "buffer_index=" << index;
    ASSERT_EQ(context_.device()->CreateShaderResourceView(
                  buffers[index].get(), &view_desc, views[index].put()),
              S_OK)
        << "view_index=" << index;
  }

  RecordProperty("logical_cases_executed", selected_cases.size());
  RecordProperty("logical_case_prefix",
                 kShaderResourceBindingCases.family().case_id_prefix);
  for (const std::uint32_t logical : selected_cases) {
    std::array<StageBinding, kShaderStageCount> bindings;
    for (UINT stage_index = 0; stage_index < kShaderStageCount; ++stage_index) {
      bindings[stage_index] = BindingForStage(logical, stage_index);
      std::array<ID3D11ShaderResourceView *, kBoundViewCount> expected = {
          views[bindings[stage_index].view_indexes[0]].get(),
          views[bindings[stage_index].view_indexes[1]].get(),
      };
      SetStageResources(context_.context(), bindings[stage_index].stage,
                        bindings[stage_index].start_slot, kBoundViewCount,
                        expected.data());
    }

    UINT failing_stage = kShaderStageCount;
    UINT failing_slot = kResourceSlotCount;
    const char *failing_phase = "none";
    const void *expected_address = nullptr;
    const void *actual_address = nullptr;
    for (UINT stage_index = 0; stage_index < kShaderStageCount; ++stage_index) {
      const StageBinding &binding = bindings[stage_index];
      std::array<ID3D11ShaderResourceView *, kBoundViewCount> expected = {
          views[binding.view_indexes[0]].get(),
          views[binding.view_indexes[1]].get(),
      };

      std::array<ID3D11ShaderResourceView *, kBoundViewCount> ranged = {};
      GetStageResources(context_.context(), binding.stage, binding.start_slot,
                        kBoundViewCount, ranged.data());
      for (UINT index = 0; index < kBoundViewCount; ++index) {
        if (ranged[index] != expected[index] &&
            failing_stage == kShaderStageCount) {
          failing_stage = stage_index;
          failing_slot = binding.start_slot + index;
          failing_phase = "ranged_get";
          expected_address = expected[index];
          actual_address = ranged[index];
        }
        if (ranged[index])
          ranged[index]->Release();
      }

      std::array<ID3D11ShaderResourceView *, kResourceSlotCount> all = {};
      GetStageResources(context_.context(), binding.stage, 0,
                        kResourceSlotCount, all.data());
      for (UINT slot = 0; slot < kResourceSlotCount; ++slot) {
        ID3D11ShaderResourceView *expected_view = nullptr;
        if (slot >= binding.start_slot &&
            slot < binding.start_slot + kBoundViewCount)
          expected_view = expected[slot - binding.start_slot];
        if (all[slot] != expected_view && failing_stage == kShaderStageCount) {
          failing_stage = stage_index;
          failing_slot = slot;
          failing_phase = "all_slots_get";
          expected_address = expected_view;
          actual_address = all[slot];
        }
        if (all[slot])
          all[slot]->Release();
      }
    }

    std::array<ID3D11ShaderResourceView *, kBoundViewCount> null_views = {};
    for (const StageBinding &binding : bindings) {
      SetStageResources(context_.context(), binding.stage, binding.start_slot,
                        kBoundViewCount, null_views.data());
    }
    for (UINT stage_index = 0; stage_index < kShaderStageCount; ++stage_index) {
      std::array<ID3D11ShaderResourceView *, kResourceSlotCount> after_unbind =
          {};
      GetStageResources(context_.context(), bindings[stage_index].stage, 0,
                        kResourceSlotCount, after_unbind.data());
      for (UINT slot = 0; slot < kResourceSlotCount; ++slot) {
        if (after_unbind[slot] && failing_stage == kShaderStageCount) {
          failing_stage = stage_index;
          failing_slot = slot;
          failing_phase = "unbind";
          actual_address = after_unbind[slot];
        }
        if (after_unbind[slot])
          after_unbind[slot]->Release();
      }
    }

    if (failing_stage == kShaderStageCount)
      continue;

    const auto case_id = dxmt::test::LogicalCaseId(
        kShaderResourceBindingCases.family(), logical);
    ADD_FAILURE()
        << "LogicalCaseId: " << case_id << '\n'
        << "Class: "
        << dxmt::test::TestClassName(
               kShaderResourceBindingCases.family().traits.test_class)
        << '\n'
        << "Requirements: feature_level=11_0 shader_model=None "
           "queue=Immediate capability=VS,PS,GS,HS,DS,CS,"
           "SetShaderResources,GetShaderResources,ComReferenceState\n"
        << "ExecutionPath: "
        << dxmt::test::ExecutionPathName(
               kShaderResourceBindingCases.family().traits.execution_path)
        << '\n'
        << "Parameters: logical=" << logical << " base_pool_indexes=("
        << (logical & 63u) << ',' << ((logical >> 6u) & 63u)
        << ") selected_cases=" << selected_cases.size() << '\n'
        << "Observed: stage=" << ShaderStageName(bindings[failing_stage].stage)
        << " phase=" << failing_phase << " slot=" << failing_slot
        << " expected_view=" << expected_address
        << " actual_view=" << actual_address << '\n'
        << "Replay: --dxmt-case-id=" << case_id;
    break;
  }

  std::array<ID3D11ShaderResourceView *, kResourceSlotCount> null_views = {};
  for (UINT stage_index = 0; stage_index < kShaderStageCount; ++stage_index)
    SetStageResources(context_.context(), static_cast<ShaderStage>(stage_index),
                      0, kResourceSlotCount, null_views.data());
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
