#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <array>
#include <cstdint>
#include <vector>

// Public-D3D11 sampler binding-state coverage for all six programmable shader
// stages. Two disjoint 64-state pools form 4096 base pairs; stage-specific
// permutations and slots verify that the state spaces remain isolated.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

constexpr std::uint32_t kShaderSamplerBindingCaseCount = 4096;
constexpr std::uint32_t kPrimaryPoolSize = 64;
constexpr std::uint32_t kSamplerCount = 2 * kPrimaryPoolSize;
constexpr UINT kBoundSamplerCount = 2;
constexpr UINT kSamplerSlotCount = D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT;
constexpr UINT kStartSlotCount = kSamplerSlotCount - kBoundSamplerCount + 1u;
constexpr UINT kShaderStageCount = 6;

const dxmt::test::LogicalCaseFamilyRegistration kShaderSamplerBindingCases(
    "D3D11ShaderSamplerBindingMatrixSpec."
    "RoundTrips4096PairsAcrossAllShaderStages",
    "D3D11.GetSamplers.Binding.", kShaderSamplerBindingCaseCount, 4,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "Immediate",
      "VSSetSamplers,VSGetSamplers,PSSetSamplers,PSGetSamplers,GSSetSamplers,"
      "GSGetSamplers,HSSetSamplers,HSGetSamplers,DSSetSamplers,DSGetSamplers,"
      "CSSetSamplers,CSGetSamplers,ComReferenceState"},
     dxmt::test::kResourceTestCost,
     "two disjoint pools of sixty-four uniquely described test-local sampler "
     "states",
     "bind a stage-specific permutation of every selected sampler pair to all "
     "six shader stages, query both the exact range and all sixteen slots, "
     "release getter references, then unbind every stage",
     "each getter returns the exact two COM objects only in that stage's bound "
     "slots and all stages contain only null slots after unbinding",
     "logical ID, selected-case count, base pool indexes, failing stage, phase "
     "and slot, expected and actual addresses, and exact replay argument"});

const dxmt::test::TestCostRegistration
    kShaderSamplerBindingCost("D3D11ShaderSamplerBindingMatrixSpec."
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

void SetStageSamplers(ID3D11DeviceContext *context, ShaderStage stage,
                      UINT start_slot, UINT count,
                      ID3D11SamplerState *const *samplers) {
  switch (stage) {
  case ShaderStage::Vertex:
    context->VSSetSamplers(start_slot, count, samplers);
    break;
  case ShaderStage::Pixel:
    context->PSSetSamplers(start_slot, count, samplers);
    break;
  case ShaderStage::Geometry:
    context->GSSetSamplers(start_slot, count, samplers);
    break;
  case ShaderStage::Hull:
    context->HSSetSamplers(start_slot, count, samplers);
    break;
  case ShaderStage::Domain:
    context->DSSetSamplers(start_slot, count, samplers);
    break;
  case ShaderStage::Compute:
    context->CSSetSamplers(start_slot, count, samplers);
    break;
  }
}

void GetStageSamplers(ID3D11DeviceContext *context, ShaderStage stage,
                      UINT start_slot, UINT count,
                      ID3D11SamplerState **samplers) {
  switch (stage) {
  case ShaderStage::Vertex:
    context->VSGetSamplers(start_slot, count, samplers);
    break;
  case ShaderStage::Pixel:
    context->PSGetSamplers(start_slot, count, samplers);
    break;
  case ShaderStage::Geometry:
    context->GSGetSamplers(start_slot, count, samplers);
    break;
  case ShaderStage::Hull:
    context->HSGetSamplers(start_slot, count, samplers);
    break;
  case ShaderStage::Domain:
    context->DSGetSamplers(start_slot, count, samplers);
    break;
  case ShaderStage::Compute:
    context->CSGetSamplers(start_slot, count, samplers);
    break;
  }
}

struct StageBinding {
  ShaderStage stage;
  std::array<std::uint32_t, kBoundSamplerCount> sampler_indexes;
  UINT start_slot;
};

StageBinding BindingForStage(std::uint32_t logical, UINT stage_index) {
  const std::uint32_t first = logical & 63u;
  const std::uint32_t second = (logical >> 6u) & 63u;
  const std::uint32_t stage_first = (first + stage_index * 7u) & 63u;
  const std::uint32_t stage_second = (second + stage_index * 11u) & 63u;
  return {static_cast<ShaderStage>(stage_index),
          {{stage_first, kPrimaryPoolSize + stage_second}},
          static_cast<UINT>((stage_first ^ stage_second ^ stage_index) %
                            kStartSlotCount)};
}

class D3D11ShaderSamplerBindingMatrixSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_NE(context_.context(), nullptr);
  }

  D3D11TestContext context_;
};

TEST_F(D3D11ShaderSamplerBindingMatrixSpec,
       RoundTrips4096PairsAcrossAllShaderStages) {
  std::vector<std::uint32_t> selected_cases;
  selected_cases.reserve(kShaderSamplerBindingCaseCount);
  for (std::uint32_t logical = 0; logical < kShaderSamplerBindingCaseCount;
       ++logical) {
    if (dxmt::test::LogicalCaseSelected(kShaderSamplerBindingCases.family(),
                                        logical))
      selected_cases.push_back(logical);
  }
  ASSERT_FALSE(selected_cases.empty());

  std::vector<ComPtr<ID3D11SamplerState>> samplers(kSamplerCount);
  for (std::uint32_t index = 0; index < kSamplerCount; ++index) {
    D3D11_SAMPLER_DESC desc = {};
    desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    desc.AddressU = D3D11_TEXTURE_ADDRESS_BORDER;
    desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    desc.MaxAnisotropy = 1;
    desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    desc.BorderColor[0] = static_cast<FLOAT>(index) / 128.0f;
    desc.MaxLOD = D3D11_FLOAT32_MAX;
    ASSERT_EQ(
        context_.device()->CreateSamplerState(&desc, samplers[index].put()),
        S_OK)
        << "sampler_index=" << index;
  }

  RecordProperty("logical_cases_executed", selected_cases.size());
  RecordProperty("logical_case_prefix",
                 kShaderSamplerBindingCases.family().case_id_prefix);
  for (const std::uint32_t logical : selected_cases) {
    std::array<StageBinding, kShaderStageCount> bindings;
    for (UINT stage_index = 0; stage_index < kShaderStageCount; ++stage_index) {
      bindings[stage_index] = BindingForStage(logical, stage_index);
      std::array<ID3D11SamplerState *, kBoundSamplerCount> expected = {
          samplers[bindings[stage_index].sampler_indexes[0]].get(),
          samplers[bindings[stage_index].sampler_indexes[1]].get(),
      };
      SetStageSamplers(context_.context(), bindings[stage_index].stage,
                       bindings[stage_index].start_slot, kBoundSamplerCount,
                       expected.data());
    }

    UINT failing_stage = kShaderStageCount;
    UINT failing_slot = kSamplerSlotCount;
    const char *failing_phase = "none";
    const void *expected_address = nullptr;
    const void *actual_address = nullptr;
    for (UINT stage_index = 0; stage_index < kShaderStageCount; ++stage_index) {
      const StageBinding &binding = bindings[stage_index];
      std::array<ID3D11SamplerState *, kBoundSamplerCount> expected = {
          samplers[binding.sampler_indexes[0]].get(),
          samplers[binding.sampler_indexes[1]].get(),
      };

      std::array<ID3D11SamplerState *, kBoundSamplerCount> ranged = {};
      GetStageSamplers(context_.context(), binding.stage, binding.start_slot,
                       kBoundSamplerCount, ranged.data());
      for (UINT index = 0; index < kBoundSamplerCount; ++index) {
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

      std::array<ID3D11SamplerState *, kSamplerSlotCount> all = {};
      GetStageSamplers(context_.context(), binding.stage, 0, kSamplerSlotCount,
                       all.data());
      for (UINT slot = 0; slot < kSamplerSlotCount; ++slot) {
        ID3D11SamplerState *expected_sampler = nullptr;
        if (slot >= binding.start_slot &&
            slot < binding.start_slot + kBoundSamplerCount)
          expected_sampler = expected[slot - binding.start_slot];
        if (all[slot] != expected_sampler &&
            failing_stage == kShaderStageCount) {
          failing_stage = stage_index;
          failing_slot = slot;
          failing_phase = "all_slots_get";
          expected_address = expected_sampler;
          actual_address = all[slot];
        }
        if (all[slot])
          all[slot]->Release();
      }
    }

    std::array<ID3D11SamplerState *, kBoundSamplerCount> null_samplers = {};
    for (const StageBinding &binding : bindings) {
      SetStageSamplers(context_.context(), binding.stage, binding.start_slot,
                       kBoundSamplerCount, null_samplers.data());
    }
    for (UINT stage_index = 0; stage_index < kShaderStageCount; ++stage_index) {
      std::array<ID3D11SamplerState *, kSamplerSlotCount> after_unbind = {};
      GetStageSamplers(context_.context(), bindings[stage_index].stage, 0,
                       kSamplerSlotCount, after_unbind.data());
      for (UINT slot = 0; slot < kSamplerSlotCount; ++slot) {
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

    const auto case_id =
        dxmt::test::LogicalCaseId(kShaderSamplerBindingCases.family(), logical);
    ADD_FAILURE()
        << "LogicalCaseId: " << case_id << '\n'
        << "Class: "
        << dxmt::test::TestClassName(
               kShaderSamplerBindingCases.family().traits.test_class)
        << '\n'
        << "Requirements: feature_level=11_0 queue=Immediate "
           "capability=VS,PS,GS,HS,DS,CS,SetSamplers,GetSamplers,"
           "ComReferenceState\n"
        << "ExecutionPath: "
        << dxmt::test::ExecutionPathName(
               kShaderSamplerBindingCases.family().traits.execution_path)
        << '\n'
        << "Parameters: logical=" << logical << " base_pool_indexes=("
        << (logical & 63u) << ',' << ((logical >> 6u) & 63u)
        << ") selected_cases=" << selected_cases.size() << '\n'
        << "Observed: stage=" << ShaderStageName(bindings[failing_stage].stage)
        << " phase=" << failing_phase << " slot=" << failing_slot
        << " expected_sampler=" << expected_address
        << " actual_sampler=" << actual_address << '\n'
        << "Replay: --dxmt-case-id=" << case_id;
    break;
  }

  std::array<ID3D11SamplerState *, kSamplerSlotCount> null_samplers = {};
  for (UINT stage_index = 0; stage_index < kShaderStageCount; ++stage_index)
    SetStageSamplers(context_.context(), static_cast<ShaderStage>(stage_index),
                     0, kSamplerSlotCount, null_samplers.data());
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
