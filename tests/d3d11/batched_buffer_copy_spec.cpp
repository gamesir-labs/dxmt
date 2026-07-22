#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

// Batched public-D3D11 buffer copies. Each logical case owns a disjoint slot,
// allowing thousands of offset/size combinations to execute before a single
// staging readback. Exact CaseId replay issues only the selected copy and also
// verifies that every unselected destination slot remains poison.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

constexpr std::uint32_t kCopyCaseCount = 4096;
constexpr std::uint32_t kSlotSize = 64;

const dxmt::test::LogicalCaseFamilyRegistration kCopyCases(
    "D3D11BatchedBufferCopySpec."
    "Copies4096DisjointRegionsWithOneReadback",
    "D3D11.Copy.Buffer.DisjointRegion.", kCopyCaseCount, 4,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "Immediate", "CopySubresourceRegion,StagingMap"},
     dxmt::test::kGpuBatchTestCost,
     "two default buffers divided into 4096 disjoint 64-byte slots; source "
     "slots contain deterministic data and destination slots contain poison",
     "issue one CopySubresourceRegion per selected logical ID with varied "
     "source offset, destination offset, and byte count, then copy the whole "
     "destination to one staging buffer",
     "every copied byte matches its source and every byte outside selected "
     "destination ranges remains poison",
     "logical ID, selection state, source/destination offsets, byte count, "
     "first mismatching slot byte, expected/actual, and exact replay "
     "argument"});

const dxmt::test::TestCostRegistration
    kCopyCost("D3D11BatchedBufferCopySpec."
              "Copies4096DisjointRegionsWithOneReadback",
              dxmt::test::kGpuBatchTestCost);

struct CopyCase {
  std::uint32_t source_offset;
  std::uint32_t destination_offset;
  std::uint32_t byte_count;
};

CopyCase MakeCopyCase(std::uint32_t logical) {
  if ((logical & 63u) == 0)
    return {0, 0, kSlotSize};

  const std::uint32_t source_offset = logical & 15u;
  const std::uint32_t destination_offset = (logical >> 4u) & 15u;
  const std::uint32_t maximum_count =
      kSlotSize - std::max(source_offset, destination_offset);
  const std::uint32_t byte_count =
      1u + ((logical * 17u + (logical >> 8u)) % maximum_count);
  return {source_offset, destination_offset, byte_count};
}

std::uint8_t SourceByte(std::uint32_t logical, std::uint32_t slot_byte) {
  return static_cast<std::uint8_t>((logical * 29u + slot_byte * 17u + 0x31u) &
                                   0xffu);
}

std::uint8_t PoisonByte(std::uint32_t logical, std::uint32_t slot_byte) {
  return static_cast<std::uint8_t>((logical * 7u + slot_byte * 3u + 0xcdu) &
                                   0xffu);
}

class D3D11BatchedBufferCopySpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_NE(context_.context(), nullptr);
  }

  D3D11TestContext context_;
};

TEST_F(D3D11BatchedBufferCopySpec, Copies4096DisjointRegionsWithOneReadback) {
  constexpr std::uint32_t buffer_size = kCopyCaseCount * kSlotSize;
  std::vector<std::uint8_t> source_data(buffer_size);
  std::vector<std::uint8_t> expected(buffer_size);
  std::vector<bool> selected(kCopyCaseCount, false);
  std::vector<std::uint32_t> selected_cases;
  selected_cases.reserve(kCopyCaseCount);

  for (std::uint32_t logical = 0; logical < kCopyCaseCount; ++logical) {
    const std::uint32_t slot_base = logical * kSlotSize;
    for (std::uint32_t slot_byte = 0; slot_byte < kSlotSize; ++slot_byte) {
      source_data[slot_base + slot_byte] = SourceByte(logical, slot_byte);
      expected[slot_base + slot_byte] = PoisonByte(logical, slot_byte);
    }
    if (!dxmt::test::LogicalCaseSelected(kCopyCases.family(), logical))
      continue;

    selected[logical] = true;
    selected_cases.push_back(logical);
    const CopyCase copy = MakeCopyCase(logical);
    std::copy_n(source_data.begin() + slot_base + copy.source_offset,
                copy.byte_count,
                expected.begin() + slot_base + copy.destination_offset);
  }
  ASSERT_FALSE(selected_cases.empty());

  D3D11_BUFFER_DESC desc = {};
  desc.ByteWidth = buffer_size;
  desc.Usage = D3D11_USAGE_DEFAULT;
  D3D11_SUBRESOURCE_DATA source_initial = {};
  source_initial.pSysMem = source_data.data();
  D3D11_SUBRESOURCE_DATA destination_initial = {};
  std::vector<std::uint8_t> destination_poison(buffer_size);
  for (std::uint32_t logical = 0; logical < kCopyCaseCount; ++logical) {
    const std::uint32_t slot_base = logical * kSlotSize;
    for (std::uint32_t slot_byte = 0; slot_byte < kSlotSize; ++slot_byte)
      destination_poison[slot_base + slot_byte] =
          PoisonByte(logical, slot_byte);
  }
  destination_initial.pSysMem = destination_poison.data();

  ComPtr<ID3D11Buffer> source;
  ComPtr<ID3D11Buffer> destination;
  ASSERT_EQ(
      context_.device()->CreateBuffer(&desc, &source_initial, source.put()),
      S_OK);
  ASSERT_EQ(context_.device()->CreateBuffer(&desc, &destination_initial,
                                            destination.put()),
            S_OK);

  for (const std::uint32_t logical : selected_cases) {
    const CopyCase copy = MakeCopyCase(logical);
    const std::uint32_t slot_base = logical * kSlotSize;
    const D3D11_BOX source_box = {slot_base + copy.source_offset,
                                  0,
                                  0,
                                  slot_base + copy.source_offset +
                                      copy.byte_count,
                                  1,
                                  1};
    context_.context()->CopySubresourceRegion(
        destination.get(), 0, slot_base + copy.destination_offset, 0, 0,
        source.get(), 0, &source_box);
  }

  D3D11_BUFFER_DESC staging_desc = desc;
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
  RecordProperty("logical_case_prefix", kCopyCases.family().case_id_prefix);
  for (std::uint32_t byte = 0; byte < buffer_size; ++byte) {
    if (actual[byte] == expected[byte])
      continue;

    const std::uint32_t logical = byte / kSlotSize;
    const std::uint32_t slot_byte = byte % kSlotSize;
    const CopyCase copy = MakeCopyCase(logical);
    const auto case_id =
        dxmt::test::LogicalCaseId(kCopyCases.family(), logical);
    const auto replay_case_id =
        selected[logical] ? case_id
                          : dxmt::test::LogicalCaseId(kCopyCases.family(),
                                                      selected_cases.front());
    ADD_FAILURE() << "LogicalCaseId: " << case_id << '\n'
                  << "Class: "
                  << dxmt::test::TestClassName(
                         kCopyCases.family().traits.test_class)
                  << '\n'
                  << "Requirements: feature_level=11_0 queue=Immediate "
                     "capability=CopySubresourceRegion,StagingMap\n"
                  << "ExecutionPath: "
                  << dxmt::test::ExecutionPathName(
                         kCopyCases.family().traits.execution_path)
                  << '\n'
                  << "Parameters: logical=" << logical
                  << " selected=" << (selected[logical] ? "true" : "false")
                  << " source_offset=" << copy.source_offset
                  << " destination_offset=" << copy.destination_offset
                  << " byte_count=" << copy.byte_count
                  << " slot_byte=" << slot_byte << '\n'
                  << "Expected: 0x" << std::hex
                  << static_cast<unsigned>(expected[byte]) << " actual=0x"
                  << static_cast<unsigned>(actual[byte]) << std::dec << '\n'
                  << "Replay: --dxmt-case-id=" << replay_case_id;
    break;
  }
  context_.context()->Unmap(staging.get(), 0);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
