#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <vector>

// Batched public-D3D11.1 CopySubresourceRegion1 coverage. Every logical case
// owns source and destination slots with varied offsets and copy lengths;
// destination poison makes range underwrite and overwrite directly visible.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

constexpr std::uint32_t kCopySubresourceRegion1CaseCount = 4096;
constexpr std::uint32_t kSlotSize = 16;

const dxmt::test::LogicalCaseFamilyRegistration kCopySubresourceRegion1Cases(
    "D3D11CopySubresourceRegion1MatrixSpec."
    "Copies4096BufferRegionsWithZeroCopyFlags",
    "D3D11.CopySubresourceRegion1.BufferRegion.",
    kCopySubresourceRegion1CaseCount, 4,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "Immediate",
      "Context1,CopySubresourceRegion1,CopyFlagsZero,StagingMap"},
     dxmt::test::kGpuBatchTestCost,
     "separate default-usage source and poison-initialized destination buffers "
     "divided into 4096 independent 16-byte slots",
     "copy each selected slot's varied source range to a varied destination "
     "offset through ID3D11DeviceContext1 with CopyFlags zero",
     "copied bytes match the source payload and every destination byte outside "
     "selected ranges remains poison",
     "logical ID, selection state, source and destination offsets, byte count, "
     "slot byte, expected/actual value, and exact replay argument"});

const dxmt::test::TestCostRegistration
    kCopySubresourceRegion1Cost("D3D11CopySubresourceRegion1MatrixSpec."
                                "Copies4096BufferRegionsWithZeroCopyFlags",
                                dxmt::test::kGpuBatchTestCost);

struct CopyCase {
  std::uint32_t source_offset;
  std::uint32_t destination_offset;
  std::uint32_t byte_count;
};

CopyCase MakeCopyCase(std::uint32_t logical) {
  const std::uint32_t source_offset = logical & 3u;
  const std::uint32_t destination_offset = (logical >> 2u) & 3u;
  const std::uint32_t maximum_count =
      kSlotSize - std::max(source_offset, destination_offset);
  if ((logical & 16u) != 0)
    return {source_offset, destination_offset, maximum_count};
  const std::uint32_t varied_count = 1u + ((logical >> 5u) & 7u);
  return {source_offset, destination_offset,
          std::min(varied_count, maximum_count)};
}

std::uint8_t SourceByte(std::uint32_t logical, std::uint32_t slot_byte) {
  return static_cast<std::uint8_t>(0x2bu + logical * 19u + slot_byte * 37u);
}

std::uint8_t PoisonByte(std::uint32_t logical, std::uint32_t slot_byte) {
  return static_cast<std::uint8_t>(0xd4u ^ (logical * 43u) ^ (slot_byte * 23u));
}

class D3D11CopySubresourceRegion1MatrixSpec : public ::testing::Test {
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

TEST_F(D3D11CopySubresourceRegion1MatrixSpec,
       Copies4096BufferRegionsWithZeroCopyFlags) {
  const std::uint32_t buffer_size =
      kCopySubresourceRegion1CaseCount * kSlotSize;
  std::vector<std::uint8_t> source_data(buffer_size);
  std::vector<std::uint8_t> destination_initial(buffer_size);
  std::vector<std::uint8_t> expected(buffer_size);
  std::vector<bool> selected(kCopySubresourceRegion1CaseCount, false);
  std::vector<std::uint32_t> selected_cases;
  selected_cases.reserve(kCopySubresourceRegion1CaseCount);
  for (std::uint32_t logical = 0; logical < kCopySubresourceRegion1CaseCount;
       ++logical) {
    const std::uint32_t slot_base = logical * kSlotSize;
    for (std::uint32_t slot_byte = 0; slot_byte < kSlotSize; ++slot_byte) {
      source_data[slot_base + slot_byte] = SourceByte(logical, slot_byte);
      destination_initial[slot_base + slot_byte] =
          PoisonByte(logical, slot_byte);
    }
    if (!dxmt::test::LogicalCaseSelected(kCopySubresourceRegion1Cases.family(),
                                         logical))
      continue;

    selected[logical] = true;
    selected_cases.push_back(logical);
  }
  ASSERT_FALSE(selected_cases.empty());
  expected = destination_initial;

  D3D11_BUFFER_DESC buffer_desc = {};
  buffer_desc.ByteWidth = buffer_size;
  buffer_desc.Usage = D3D11_USAGE_DEFAULT;
  D3D11_SUBRESOURCE_DATA source_initial = {};
  source_initial.pSysMem = source_data.data();
  D3D11_SUBRESOURCE_DATA destination_data = {};
  destination_data.pSysMem = destination_initial.data();
  ComPtr<ID3D11Buffer> source;
  ComPtr<ID3D11Buffer> destination;
  ASSERT_EQ(context_.device()->CreateBuffer(&buffer_desc, &source_initial,
                                            source.put()),
            S_OK);
  ASSERT_EQ(context_.device()->CreateBuffer(&buffer_desc, &destination_data,
                                            destination.put()),
            S_OK);

  for (const std::uint32_t logical : selected_cases) {
    const CopyCase copy = MakeCopyCase(logical);
    const std::uint32_t slot_base = logical * kSlotSize;
    for (std::uint32_t copied_byte = 0; copied_byte < copy.byte_count;
         ++copied_byte) {
      expected[slot_base + copy.destination_offset + copied_byte] =
          source_data[slot_base + copy.source_offset + copied_byte];
    }

    const D3D11_BOX source_box = {slot_base + copy.source_offset,
                                  0,
                                  0,
                                  slot_base + copy.source_offset +
                                      copy.byte_count,
                                  1,
                                  1};
    context1_->CopySubresourceRegion1(destination.get(), 0,
                                      slot_base + copy.destination_offset, 0, 0,
                                      source.get(), 0, &source_box, 0);
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
                 kCopySubresourceRegion1Cases.family().case_id_prefix);
  for (std::uint32_t byte = 0; byte < buffer_size; ++byte) {
    if (actual[byte] == expected[byte])
      continue;

    const std::uint32_t logical = byte / kSlotSize;
    const std::uint32_t slot_byte = byte % kSlotSize;
    const CopyCase copy = MakeCopyCase(logical);
    const auto case_id = dxmt::test::LogicalCaseId(
        kCopySubresourceRegion1Cases.family(), logical);
    const auto replay_case_id =
        selected[logical]
            ? case_id
            : dxmt::test::LogicalCaseId(kCopySubresourceRegion1Cases.family(),
                                        selected_cases.front());
    ADD_FAILURE()
        << "LogicalCaseId: " << case_id << '\n'
        << "Class: "
        << dxmt::test::TestClassName(
               kCopySubresourceRegion1Cases.family().traits.test_class)
        << '\n'
        << "Requirements: feature_level=11_0 queue=Immediate "
           "capability=Context1,CopySubresourceRegion1,CopyFlagsZero,"
           "StagingMap\n"
        << "ExecutionPath: "
        << dxmt::test::ExecutionPathName(
               kCopySubresourceRegion1Cases.family().traits.execution_path)
        << '\n'
        << "Parameters: logical=" << logical
        << " selected=" << (selected[logical] ? "true" : "false")
        << " source_offset=" << copy.source_offset
        << " destination_offset=" << copy.destination_offset
        << " absolute_source=" << logical * kSlotSize + copy.source_offset
        << " absolute_destination="
        << logical * kSlotSize + copy.destination_offset
        << " byte_count=" << copy.byte_count
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
