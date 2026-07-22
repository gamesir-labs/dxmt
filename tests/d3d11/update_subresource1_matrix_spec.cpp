#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iomanip>
#include <vector>

// Batched public-D3D11.1 UpdateSubresource1 coverage. Each logical case owns a
// fixed-size buffer slot and updates a varied byte range through a D3D11_BOX;
// poison bytes make both underwrite and overwrite observable.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

constexpr std::uint32_t kUpdateSubresource1CaseCount = 4096;
constexpr std::uint32_t kSlotSize = 16;

const dxmt::test::LogicalCaseFamilyRegistration kUpdateSubresource1Cases(
    "D3D11UpdateSubresource1MatrixSpec."
    "Updates4096BufferRegionsWithZeroCopyFlags",
    "D3D11.UpdateSubresource1.BufferRegion.", kUpdateSubresource1CaseCount, 4,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "Immediate",
      "Context1,UpdateSubresource1,CopyFlagsZero,CopyResource,StagingMap"},
     dxmt::test::kGpuBatchTestCost,
     "a poison-initialized default-usage buffer divided into 4096 independent "
     "16-byte slots and an ID3D11DeviceContext1 immediate context",
     "update each selected slot's varied byte range with CopyFlags zero, with "
     "half of the ranges extending exactly to the slot boundary",
     "updated bytes match their logical-case payload and every byte outside "
     "selected ranges remains poison",
     "logical ID, selection state, slot and absolute byte offsets, byte count, "
     "slot byte, expected/actual value, and exact replay argument"});

const dxmt::test::TestCostRegistration
    kUpdateSubresource1Cost("D3D11UpdateSubresource1MatrixSpec."
                            "Updates4096BufferRegionsWithZeroCopyFlags",
                            dxmt::test::kGpuBatchTestCost);

struct UpdateCase {
  std::uint32_t offset;
  std::uint32_t byte_count;
};

UpdateCase MakeUpdateCase(std::uint32_t logical) {
  const std::uint32_t offset = logical & 7u;
  if ((logical & 8u) != 0)
    return {offset, kSlotSize - offset};
  const std::uint32_t varied_count = 1u + ((logical >> 4u) & 7u);
  return {offset, std::min(varied_count, kSlotSize - offset)};
}

std::uint8_t PoisonByte(std::uint32_t logical, std::uint32_t slot_byte) {
  return static_cast<std::uint8_t>(0x80u ^ (logical * 29u) ^ (slot_byte * 17u));
}

std::uint8_t PayloadByte(std::uint32_t logical, std::uint32_t payload_byte) {
  return static_cast<std::uint8_t>(0x3du + logical * 73u + payload_byte * 41u);
}

class D3D11UpdateSubresource1MatrixSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_NE(context_.context(), nullptr);
    ASSERT_EQ(context_.context()->QueryInterface(
                  __uuidof(ID3D11DeviceContext1),
                  reinterpret_cast<void **>(context1_.put())),
              S_OK);
  }

  D3D11TestContext context_;
  ComPtr<ID3D11DeviceContext1> context1_;
};

TEST_F(D3D11UpdateSubresource1MatrixSpec,
       Updates4096BufferRegionsWithZeroCopyFlags) {
  const std::uint32_t buffer_size = kUpdateSubresource1CaseCount * kSlotSize;
  std::vector<std::uint8_t> initial(buffer_size);
  std::vector<std::uint8_t> expected(buffer_size);
  std::vector<bool> selected(kUpdateSubresource1CaseCount, false);
  std::vector<std::uint32_t> selected_cases;
  selected_cases.reserve(kUpdateSubresource1CaseCount);
  for (std::uint32_t logical = 0; logical < kUpdateSubresource1CaseCount;
       ++logical) {
    const std::uint32_t slot_base = logical * kSlotSize;
    for (std::uint32_t slot_byte = 0; slot_byte < kSlotSize; ++slot_byte)
      initial[slot_base + slot_byte] = PoisonByte(logical, slot_byte);
    if (!dxmt::test::LogicalCaseSelected(kUpdateSubresource1Cases.family(),
                                         logical))
      continue;

    selected[logical] = true;
    selected_cases.push_back(logical);
  }
  ASSERT_FALSE(selected_cases.empty());
  expected = initial;

  D3D11_BUFFER_DESC buffer_desc = {};
  buffer_desc.ByteWidth = buffer_size;
  buffer_desc.Usage = D3D11_USAGE_DEFAULT;
  D3D11_SUBRESOURCE_DATA buffer_data = {};
  buffer_data.pSysMem = initial.data();
  ComPtr<ID3D11Buffer> destination;
  ASSERT_EQ(context_.device()->CreateBuffer(&buffer_desc, &buffer_data,
                                            destination.put()),
            S_OK);

  for (const std::uint32_t logical : selected_cases) {
    const UpdateCase update = MakeUpdateCase(logical);
    const std::uint32_t slot_base = logical * kSlotSize;
    std::array<std::uint8_t, kSlotSize> payload = {};
    for (std::uint32_t payload_byte = 0; payload_byte < update.byte_count;
         ++payload_byte) {
      payload[payload_byte] = PayloadByte(logical, payload_byte);
      expected[slot_base + update.offset + payload_byte] =
          payload[payload_byte];
    }

    const D3D11_BOX destination_box = {slot_base + update.offset,
                                       0,
                                       0,
                                       slot_base + update.offset +
                                           update.byte_count,
                                       1,
                                       1};
    context1_->UpdateSubresource1(destination.get(), 0, &destination_box,
                                  payload.data(), 0, 0, 0);
  }

  D3D11_BUFFER_DESC staging_desc = buffer_desc;
  staging_desc.Usage = D3D11_USAGE_STAGING;
  staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Buffer> staging;
  ASSERT_EQ(
      context_.device()->CreateBuffer(&staging_desc, nullptr, staging.put()),
      S_OK);
  context_.context()->CopyResource(staging.get(), destination.get());
  context_.context()->Flush();

  D3D11_MAPPED_SUBRESOURCE mapped = {};
  ASSERT_EQ(
      context_.context()->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped),
      S_OK);
  const auto *actual = static_cast<const std::uint8_t *>(mapped.pData);
  RecordProperty("logical_cases_executed", selected_cases.size());
  RecordProperty("logical_case_prefix",
                 kUpdateSubresource1Cases.family().case_id_prefix);
  for (std::uint32_t byte = 0; byte < buffer_size; ++byte) {
    if (actual[byte] == expected[byte])
      continue;

    const std::uint32_t logical = byte / kSlotSize;
    const std::uint32_t slot_byte = byte % kSlotSize;
    const UpdateCase update = MakeUpdateCase(logical);
    const auto case_id =
        dxmt::test::LogicalCaseId(kUpdateSubresource1Cases.family(), logical);
    const auto replay_case_id =
        selected[logical]
            ? case_id
            : dxmt::test::LogicalCaseId(kUpdateSubresource1Cases.family(),
                                        selected_cases.front());
    ADD_FAILURE()
        << "LogicalCaseId: " << case_id << '\n'
        << "Class: "
        << dxmt::test::TestClassName(
               kUpdateSubresource1Cases.family().traits.test_class)
        << '\n'
        << "Requirements: feature_level=11_0 queue=Immediate "
           "capability=Context1,UpdateSubresource1,CopyFlagsZero,"
           "CopyResource,StagingMap\n"
        << "ExecutionPath: "
        << dxmt::test::ExecutionPathName(
               kUpdateSubresource1Cases.family().traits.execution_path)
        << '\n'
        << "Parameters: logical=" << logical
        << " selected=" << (selected[logical] ? "true" : "false")
        << " slot_offset=" << update.offset
        << " absolute_offset=" << logical * kSlotSize + update.offset
        << " byte_count=" << update.byte_count
        << " copy_flags=0 slot_byte=" << slot_byte << '\n'
        << "GpuCaseResult: status=" << (selected[logical] ? 1u : 2u)
        << " first_mismatch_index=" << byte << " expected=0x" << std::hex
        << static_cast<unsigned>(expected[byte]) << " actual=0x"
        << static_cast<unsigned>(actual[byte]) << std::dec << '\n'
        << "Replay: --dxmt-case-id=" << replay_case_id;
    break;
  }
  context_.context()->Unmap(staging.get(), 0);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
