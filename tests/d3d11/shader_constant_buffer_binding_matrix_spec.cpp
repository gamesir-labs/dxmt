#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <array>
#include <cstdint>
#include <vector>

// Public-D3D11 constant-buffer binding-state coverage for all six programmable
// stages. Two disjoint 64-buffer pools form 4096 base pairs; stage-specific
// permutations and start slots verify that the 14-slot state spaces are
// isolated.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

constexpr std::uint32_t kConstantBufferBindingCaseCount = 4096;
constexpr std::uint32_t kPrimaryPoolSize = 64;
constexpr std::uint32_t kBufferCount = 2 * kPrimaryPoolSize;
constexpr UINT kBoundBufferCount = 2;
constexpr UINT kBufferSlotCount =
    D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT;
constexpr UINT kStartSlotCount = kBufferSlotCount - kBoundBufferCount + 1u;
constexpr UINT kShaderStageCount = 6;

const dxmt::test::LogicalCaseFamilyRegistration kConstantBufferBindingCases(
    "D3D11ShaderConstantBufferBindingMatrixSpec."
    "RoundTrips4096PairsAcrossAllShaderStages",
    "D3D11.GetConstantBuffers.Binding.", kConstantBufferBindingCaseCount, 4,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "Immediate",
      "VSSetConstantBuffers,VSGetConstantBuffers,PSSetConstantBuffers,"
      "PSGetConstantBuffers,GSSetConstantBuffers,GSGetConstantBuffers,"
      "HSSetConstantBuffers,HSGetConstantBuffers,DSSetConstantBuffers,"
      "DSGetConstantBuffers,CSSetConstantBuffers,CSGetConstantBuffers,"
      "ComReferenceState"},
     dxmt::test::kResourceTestCost,
     "two disjoint pools of sixty-four test-local constant buffers",
     "bind a stage-specific permutation of every selected buffer pair to all "
     "six shader stages, query both the exact range and all fourteen slots, "
     "release getter references, then unbind every stage",
     "each getter returns the exact two COM objects only in that stage's bound "
     "slots and all stages contain only null slots after unbinding",
     "logical ID, selected-case count, base pool indexes, failing stage, phase "
     "and slot, expected and actual addresses, and exact replay argument"});

const dxmt::test::TestCostRegistration
    kConstantBufferBindingCost("D3D11ShaderConstantBufferBindingMatrixSpec."
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

void SetStageBuffers(ID3D11DeviceContext *context, ShaderStage stage,
                     UINT start_slot, UINT count,
                     ID3D11Buffer *const *buffers) {
  switch (stage) {
  case ShaderStage::Vertex:
    context->VSSetConstantBuffers(start_slot, count, buffers);
    break;
  case ShaderStage::Pixel:
    context->PSSetConstantBuffers(start_slot, count, buffers);
    break;
  case ShaderStage::Geometry:
    context->GSSetConstantBuffers(start_slot, count, buffers);
    break;
  case ShaderStage::Hull:
    context->HSSetConstantBuffers(start_slot, count, buffers);
    break;
  case ShaderStage::Domain:
    context->DSSetConstantBuffers(start_slot, count, buffers);
    break;
  case ShaderStage::Compute:
    context->CSSetConstantBuffers(start_slot, count, buffers);
    break;
  }
}

void GetStageBuffers(ID3D11DeviceContext *context, ShaderStage stage,
                     UINT start_slot, UINT count, ID3D11Buffer **buffers) {
  switch (stage) {
  case ShaderStage::Vertex:
    context->VSGetConstantBuffers(start_slot, count, buffers);
    break;
  case ShaderStage::Pixel:
    context->PSGetConstantBuffers(start_slot, count, buffers);
    break;
  case ShaderStage::Geometry:
    context->GSGetConstantBuffers(start_slot, count, buffers);
    break;
  case ShaderStage::Hull:
    context->HSGetConstantBuffers(start_slot, count, buffers);
    break;
  case ShaderStage::Domain:
    context->DSGetConstantBuffers(start_slot, count, buffers);
    break;
  case ShaderStage::Compute:
    context->CSGetConstantBuffers(start_slot, count, buffers);
    break;
  }
}

struct StageBinding {
  ShaderStage stage;
  std::array<std::uint32_t, kBoundBufferCount> buffer_indexes;
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
              (stage_first * 5u + stage_second * 9u + stage_index * 3u) %
              kStartSlotCount)};
}

class D3D11ShaderConstantBufferBindingMatrixSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_NE(context_.context(), nullptr);
  }

  D3D11TestContext context_;
};

TEST_F(D3D11ShaderConstantBufferBindingMatrixSpec,
       RoundTrips4096PairsAcrossAllShaderStages) {
  std::vector<std::uint32_t> selected_cases;
  selected_cases.reserve(kConstantBufferBindingCaseCount);
  for (std::uint32_t logical = 0; logical < kConstantBufferBindingCaseCount;
       ++logical) {
    if (dxmt::test::LogicalCaseSelected(kConstantBufferBindingCases.family(),
                                        logical))
      selected_cases.push_back(logical);
  }
  ASSERT_FALSE(selected_cases.empty());

  D3D11_BUFFER_DESC buffer_desc = {};
  buffer_desc.ByteWidth = 16;
  buffer_desc.Usage = D3D11_USAGE_DEFAULT;
  buffer_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

  std::vector<ComPtr<ID3D11Buffer>> buffers(kBufferCount);
  for (std::uint32_t index = 0; index < kBufferCount; ++index) {
    ASSERT_EQ(context_.device()->CreateBuffer(&buffer_desc, nullptr,
                                              buffers[index].put()),
              S_OK)
        << "buffer_index=" << index;
  }

  RecordProperty("logical_cases_executed", selected_cases.size());
  RecordProperty("logical_case_prefix",
                 kConstantBufferBindingCases.family().case_id_prefix);
  for (const std::uint32_t logical : selected_cases) {
    std::array<StageBinding, kShaderStageCount> bindings;
    for (UINT stage_index = 0; stage_index < kShaderStageCount; ++stage_index) {
      bindings[stage_index] = BindingForStage(logical, stage_index);
      std::array<ID3D11Buffer *, kBoundBufferCount> expected = {
          buffers[bindings[stage_index].buffer_indexes[0]].get(),
          buffers[bindings[stage_index].buffer_indexes[1]].get(),
      };
      SetStageBuffers(context_.context(), bindings[stage_index].stage,
                      bindings[stage_index].start_slot, kBoundBufferCount,
                      expected.data());
    }

    UINT failing_stage = kShaderStageCount;
    UINT failing_slot = kBufferSlotCount;
    const char *failing_phase = "none";
    const void *expected_address = nullptr;
    const void *actual_address = nullptr;
    for (UINT stage_index = 0; stage_index < kShaderStageCount; ++stage_index) {
      const StageBinding &binding = bindings[stage_index];
      std::array<ID3D11Buffer *, kBoundBufferCount> expected = {
          buffers[binding.buffer_indexes[0]].get(),
          buffers[binding.buffer_indexes[1]].get(),
      };

      std::array<ID3D11Buffer *, kBoundBufferCount> ranged = {};
      GetStageBuffers(context_.context(), binding.stage, binding.start_slot,
                      kBoundBufferCount, ranged.data());
      for (UINT index = 0; index < kBoundBufferCount; ++index) {
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

      std::array<ID3D11Buffer *, kBufferSlotCount> all = {};
      GetStageBuffers(context_.context(), binding.stage, 0, kBufferSlotCount,
                      all.data());
      for (UINT slot = 0; slot < kBufferSlotCount; ++slot) {
        ID3D11Buffer *expected_buffer = nullptr;
        if (slot >= binding.start_slot &&
            slot < binding.start_slot + kBoundBufferCount)
          expected_buffer = expected[slot - binding.start_slot];
        if (all[slot] != expected_buffer &&
            failing_stage == kShaderStageCount) {
          failing_stage = stage_index;
          failing_slot = slot;
          failing_phase = "all_slots_get";
          expected_address = expected_buffer;
          actual_address = all[slot];
        }
        if (all[slot])
          all[slot]->Release();
      }
    }

    std::array<ID3D11Buffer *, kBoundBufferCount> null_buffers = {};
    for (const StageBinding &binding : bindings) {
      SetStageBuffers(context_.context(), binding.stage, binding.start_slot,
                      kBoundBufferCount, null_buffers.data());
    }
    for (UINT stage_index = 0; stage_index < kShaderStageCount; ++stage_index) {
      std::array<ID3D11Buffer *, kBufferSlotCount> after_unbind = {};
      GetStageBuffers(context_.context(), bindings[stage_index].stage, 0,
                      kBufferSlotCount, after_unbind.data());
      for (UINT slot = 0; slot < kBufferSlotCount; ++slot) {
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
        kConstantBufferBindingCases.family(), logical);
    ADD_FAILURE()
        << "LogicalCaseId: " << case_id << '\n'
        << "Class: "
        << dxmt::test::TestClassName(
               kConstantBufferBindingCases.family().traits.test_class)
        << '\n'
        << "Requirements: feature_level=11_0 shader_model=None "
           "queue=Immediate capability=VS,PS,GS,HS,DS,CS,"
           "SetConstantBuffers,GetConstantBuffers,ComReferenceState\n"
        << "ExecutionPath: "
        << dxmt::test::ExecutionPathName(
               kConstantBufferBindingCases.family().traits.execution_path)
        << '\n'
        << "Parameters: logical=" << logical << " base_pool_indexes=("
        << (logical & 63u) << ',' << ((logical >> 6u) & 63u)
        << ") selected_cases=" << selected_cases.size() << '\n'
        << "Observed: stage=" << ShaderStageName(bindings[failing_stage].stage)
        << " phase=" << failing_phase << " slot=" << failing_slot
        << " expected_buffer=" << expected_address
        << " actual_buffer=" << actual_address << '\n'
        << "Replay: --dxmt-case-id=" << case_id;
    break;
  }

  std::array<ID3D11Buffer *, kBufferSlotCount> null_buffers = {};
  for (UINT stage_index = 0; stage_index < kShaderStageCount; ++stage_index)
    SetStageBuffers(context_.context(), static_cast<ShaderStage>(stage_index),
                    0, kBufferSlotCount, null_buffers.data());
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
