#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <array>
#include <cstdint>
#include <vector>

// Public-D3D11 input-assembler vertex-buffer state coverage. Two disjoint
// 64-buffer pools form 4096 pairs with varying slots, strides, and offsets.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

constexpr std::uint32_t kVertexBufferBindingCaseCount = 4096;
constexpr std::uint32_t kPrimaryPoolSize = 64;
constexpr std::uint32_t kBufferCount = 2 * kPrimaryPoolSize;
constexpr UINT kBoundBufferCount = 2;
constexpr UINT kVertexBufferSlotCount =
    D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT;
constexpr UINT kStartSlotCount =
    kVertexBufferSlotCount - kBoundBufferCount + 1u;

const dxmt::test::LogicalCaseFamilyRegistration kVertexBufferBindingCases(
    "D3D11VertexBufferBindingMatrixSpec."
    "RoundTrips4096PairsWithStrideOffsetState",
    "D3D11.IAGetVertexBuffers.Binding.", kVertexBufferBindingCaseCount, 4,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "Immediate",
      "IASetVertexBuffers,IAGetVertexBuffers,ComReferenceState"},
     dxmt::test::kResourceTestCost,
     "two disjoint pools of sixty-four test-local 4 KiB vertex buffers",
     "bind every selected buffer pair at a varying consecutive slot range with "
     "independent strides and offsets, query the exact range and all 32 slots, "
     "release getter references, then unbind the range",
     "each getter returns the exact two buffer objects, strides, and offsets "
     "only at the selected slots, with zeroed state everywhere after unbinding",
     "logical ID, selected-case count, buffer indexes, start slot, buffer "
     "addresses, stride and offset values, failure location, and exact replay "
     "argument"});

const dxmt::test::TestCostRegistration
    kVertexBufferBindingCost("D3D11VertexBufferBindingMatrixSpec."
                             "RoundTrips4096PairsWithStrideOffsetState",
                             dxmt::test::kResourceTestCost);

struct VertexBufferBinding {
  std::array<std::uint32_t, kBoundBufferCount> buffer_indexes;
  std::array<UINT, kBoundBufferCount> strides;
  std::array<UINT, kBoundBufferCount> offsets;
  UINT start_slot;
};

VertexBufferBinding BindingForCase(std::uint32_t logical) {
  const std::uint32_t first = logical & 63u;
  const std::uint32_t second = (logical >> 6u) & 63u;
  return {{{first, kPrimaryPoolSize + second}},
          {{1u + (first * 13u + second * 3u) % 256u,
            1u + (first * 5u + second * 17u) % 256u}},
          {{(first * 53u + second * 29u) % 2048u,
            (first * 31u + second * 47u) % 2048u}},
          static_cast<UINT>((first * 7u + second * 11u) % kStartSlotCount)};
}

class D3D11VertexBufferBindingMatrixSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_NE(context_.context(), nullptr);
  }

  D3D11TestContext context_;
};

TEST_F(D3D11VertexBufferBindingMatrixSpec,
       RoundTrips4096PairsWithStrideOffsetState) {
  std::vector<std::uint32_t> selected_cases;
  selected_cases.reserve(kVertexBufferBindingCaseCount);
  for (std::uint32_t logical = 0; logical < kVertexBufferBindingCaseCount;
       ++logical) {
    if (dxmt::test::LogicalCaseSelected(kVertexBufferBindingCases.family(),
                                        logical))
      selected_cases.push_back(logical);
  }
  ASSERT_FALSE(selected_cases.empty());

  D3D11_BUFFER_DESC buffer_desc = {};
  buffer_desc.ByteWidth = 4096;
  buffer_desc.Usage = D3D11_USAGE_DEFAULT;
  buffer_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  std::vector<ComPtr<ID3D11Buffer>> buffers(kBufferCount);
  for (std::uint32_t index = 0; index < kBufferCount; ++index) {
    ASSERT_EQ(context_.device()->CreateBuffer(&buffer_desc, nullptr,
                                              buffers[index].put()),
              S_OK)
        << "buffer_index=" << index;
  }

  RecordProperty("logical_cases_executed", selected_cases.size());
  RecordProperty("logical_case_prefix",
                 kVertexBufferBindingCases.family().case_id_prefix);
  for (const std::uint32_t logical : selected_cases) {
    const VertexBufferBinding binding = BindingForCase(logical);
    std::array<ID3D11Buffer *, kBoundBufferCount> expected_buffers = {
        buffers[binding.buffer_indexes[0]].get(),
        buffers[binding.buffer_indexes[1]].get(),
    };
    context_.context()->IASetVertexBuffers(
        binding.start_slot, kBoundBufferCount, expected_buffers.data(),
        binding.strides.data(), binding.offsets.data());

    bool failed = false;
    const char *failing_phase = "none";
    UINT failing_slot = kVertexBufferSlotCount;
    const void *expected_address = nullptr;
    const void *actual_address = nullptr;
    UINT expected_stride = 0;
    UINT actual_stride = 0;
    UINT expected_offset = 0;
    UINT actual_offset = 0;

    std::array<ID3D11Buffer *, kBoundBufferCount> ranged_buffers = {};
    std::array<UINT, kBoundBufferCount> ranged_strides = {};
    std::array<UINT, kBoundBufferCount> ranged_offsets = {};
    context_.context()->IAGetVertexBuffers(
        binding.start_slot, kBoundBufferCount, ranged_buffers.data(),
        ranged_strides.data(), ranged_offsets.data());
    for (UINT index = 0; index < kBoundBufferCount; ++index) {
      if (!failed && (ranged_buffers[index] != expected_buffers[index] ||
                      ranged_strides[index] != binding.strides[index] ||
                      ranged_offsets[index] != binding.offsets[index])) {
        failed = true;
        failing_phase = "ranged_get";
        failing_slot = binding.start_slot + index;
        expected_address = expected_buffers[index];
        actual_address = ranged_buffers[index];
        expected_stride = binding.strides[index];
        actual_stride = ranged_strides[index];
        expected_offset = binding.offsets[index];
        actual_offset = ranged_offsets[index];
      }
      if (ranged_buffers[index])
        ranged_buffers[index]->Release();
    }

    std::array<ID3D11Buffer *, kVertexBufferSlotCount> all_buffers = {};
    std::array<UINT, kVertexBufferSlotCount> all_strides = {};
    std::array<UINT, kVertexBufferSlotCount> all_offsets = {};
    context_.context()->IAGetVertexBuffers(
        0, kVertexBufferSlotCount, all_buffers.data(), all_strides.data(),
        all_offsets.data());
    for (UINT slot = 0; slot < kVertexBufferSlotCount; ++slot) {
      ID3D11Buffer *expected_buffer = nullptr;
      UINT slot_stride = 0;
      UINT slot_offset = 0;
      if (slot >= binding.start_slot &&
          slot < binding.start_slot + kBoundBufferCount) {
        const UINT index = slot - binding.start_slot;
        expected_buffer = expected_buffers[index];
        slot_stride = binding.strides[index];
        slot_offset = binding.offsets[index];
      }
      if (!failed && (all_buffers[slot] != expected_buffer ||
                      all_strides[slot] != slot_stride ||
                      all_offsets[slot] != slot_offset)) {
        failed = true;
        failing_phase = "all_slots_get";
        failing_slot = slot;
        expected_address = expected_buffer;
        actual_address = all_buffers[slot];
        expected_stride = slot_stride;
        actual_stride = all_strides[slot];
        expected_offset = slot_offset;
        actual_offset = all_offsets[slot];
      }
      if (all_buffers[slot])
        all_buffers[slot]->Release();
    }

    std::array<ID3D11Buffer *, kBoundBufferCount> null_buffers = {};
    std::array<UINT, kBoundBufferCount> zero_values = {};
    context_.context()->IASetVertexBuffers(
        binding.start_slot, kBoundBufferCount, null_buffers.data(),
        zero_values.data(), zero_values.data());
    all_buffers = {};
    all_strides.fill(~0u);
    all_offsets.fill(~0u);
    context_.context()->IAGetVertexBuffers(
        0, kVertexBufferSlotCount, all_buffers.data(), all_strides.data(),
        all_offsets.data());
    for (UINT slot = 0; slot < kVertexBufferSlotCount; ++slot) {
      if (!failed && (all_buffers[slot] || all_strides[slot] != 0 ||
                      all_offsets[slot] != 0)) {
        failed = true;
        failing_phase = "unbind";
        failing_slot = slot;
        actual_address = all_buffers[slot];
        actual_stride = all_strides[slot];
        actual_offset = all_offsets[slot];
      }
      if (all_buffers[slot])
        all_buffers[slot]->Release();
    }

    if (!failed)
      continue;

    const auto case_id =
        dxmt::test::LogicalCaseId(kVertexBufferBindingCases.family(), logical);
    ADD_FAILURE()
        << "LogicalCaseId: " << case_id << '\n'
        << "Class: "
        << dxmt::test::TestClassName(
               kVertexBufferBindingCases.family().traits.test_class)
        << '\n'
        << "Requirements: feature_level=11_0 shader_model=None "
           "queue=Immediate capability=IASetVertexBuffers,IAGetVertexBuffers,"
           "ComReferenceState\n"
        << "ExecutionPath: "
        << dxmt::test::ExecutionPathName(
               kVertexBufferBindingCases.family().traits.execution_path)
        << '\n'
        << "Parameters: logical=" << logical << " buffer_indexes=("
        << binding.buffer_indexes[0] << ',' << binding.buffer_indexes[1]
        << ") start_slot=" << binding.start_slot
        << " selected_cases=" << selected_cases.size() << '\n'
        << "Observed: phase=" << failing_phase << " slot=" << failing_slot
        << " expected_buffer=" << expected_address
        << " actual_buffer=" << actual_address
        << " expected_stride=" << expected_stride
        << " actual_stride=" << actual_stride
        << " expected_offset=" << expected_offset
        << " actual_offset=" << actual_offset << '\n'
        << "Replay: --dxmt-case-id=" << case_id;
    break;
  }

  std::array<ID3D11Buffer *, kVertexBufferSlotCount> null_buffers = {};
  std::array<UINT, kVertexBufferSlotCount> zero_values = {};
  context_.context()->IASetVertexBuffers(
      0, kVertexBufferSlotCount, null_buffers.data(), zero_values.data(),
      zero_values.data());
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
