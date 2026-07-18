#include <dxmt_test.hpp>

#include "../../src/d3d12/d3d12_compiled_descriptor_range.hpp"

#include "dxmt_bindless_buffer_table.hpp"
#include "dxmt_descriptor_mirror.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace {

MTL_SM50_SHADER_ARGUMENT MakeArgument(SM50BindingType type, uint32_t slot,
                                      uint32_t count,
                                      MTL_SM50_SHADER_ARGUMENT_FLAG flags) {
  return {
      .Type = type,
      .SM50BindingSlot = slot,
      .Flags = flags,
      .RegisterCount = count,
  };
}

struct DescriptorRangeCase {
  uint32_t count;
  uint32_t expected_qwords;
};

class DescriptorRangeTest
    : public ::testing::TestWithParam<DescriptorRangeCase> {};

TEST_P(DescriptorRangeTest, ReservesOneCompleteRecordPerDescriptor) {
  const auto test = GetParam();
  EXPECT_EQ(dxmt::BufferTableRangeQwordCount(test.count), test.expected_qwords);
}

INSTANTIATE_TEST_SUITE_P(
    BoundedAndUnbounded, DescriptorRangeTest,
    ::testing::Values(DescriptorRangeCase{0, 3}, DescriptorRangeCase{1, 3},
                      DescriptorRangeCase{4, 12},
                      DescriptorRangeCase{std::numeric_limits<uint32_t>::max(),
                                          dxmt::kBindlessMirrorCapacity * 3}));

TEST(DescriptorTable, AcceptsOnlyTheReflectedSpanAtTheEndOfAHeap) {
  std::uint32_t resolved_base = 0;

  EXPECT_TRUE(dxmt::d3d12::TryResolveCompiledNativeDescriptorSpan(
      63, 64, 0, 0, 1, &resolved_base));
  EXPECT_EQ(resolved_base, 63u);

  // A root signature may declare many descriptors after this base. That
  // unused declaration is deliberately absent from the helper: only the
  // shader-reflected access span controls native eligibility.
  EXPECT_FALSE(dxmt::d3d12::TryResolveCompiledNativeDescriptorSpan(
      63, 64, 0, 0, 2, &resolved_base));
  EXPECT_FALSE(dxmt::d3d12::TryResolveCompiledNativeDescriptorSpan(
      63, 64, 1, 0, 1, &resolved_base));
}

TEST(DescriptorTable, RejectsOverflowWhileResolvingAReflectedSpan) {
  std::uint32_t resolved_base = 0;
  EXPECT_FALSE(dxmt::d3d12::TryResolveCompiledNativeDescriptorSpan(
      std::numeric_limits<std::uint32_t>::max(),
      std::numeric_limits<std::uint32_t>::max(), 1, 0, 1, &resolved_base));
  EXPECT_FALSE(dxmt::d3d12::TryResolveCompiledNativeDescriptorSpan(
      1, std::numeric_limits<std::uint32_t>::max(),
      std::numeric_limits<std::uint32_t>::max(), 1, 1, &resolved_base));
}

TEST(DescriptorTable, RejectsEmptySpansAndAllowsAnOmittedResultPointer) {
  std::uint32_t resolved_base = 0xccccccccu;
  EXPECT_FALSE(dxmt::d3d12::TryResolveCompiledNativeDescriptorSpan(
      0, 4, 0, 0, 0, &resolved_base));
  EXPECT_EQ(resolved_base, 0xccccccccu);

  EXPECT_TRUE(dxmt::d3d12::TryResolveCompiledNativeDescriptorSpan(
      1, 4, 1, 0, 2, nullptr));
  EXPECT_TRUE(dxmt::d3d12::TryResolveCompiledNativeDescriptorSpan(
      0, 4, 1, 1, 2, &resolved_base));
  EXPECT_EQ(resolved_base, 2u);
}

TEST(DescriptorTable, MapsBindingTypesToDisjointArgumentRanges) {
  EXPECT_EQ(GetArgumentIndex(SM50BindingType::ConstantBuffer, 7), 7u);
  EXPECT_EQ(GetArgumentIndex(SM50BindingType::Sampler, 7), 39u);
  EXPECT_EQ(GetArgumentIndex(SM50BindingType::SRV, 7), 149u);
  EXPECT_EQ(GetArgumentIndex(SM50BindingType::UAV, 7), 533u);
  EXPECT_EQ(GetArgumentIndex(static_cast<SM50BindingType>(UINT32_MAX), 7),
            UINT32_MAX);
}

TEST(DescriptorTable, OrdersCbvThenSrvThenUavAndSkipsTextures) {
  const std::array cbuffers = {
      MakeArgument(SM50BindingType::ConstantBuffer, 0, 1,
                   MTL_SM50_SHADER_ARGUMENT_BUFFER),
      MakeArgument(SM50BindingType::ConstantBuffer, 1, 2,
                   MTL_SM50_SHADER_ARGUMENT_BUFFER),
  };
  const std::array arguments = {
      MakeArgument(SM50BindingType::UAV, 3, 1, MTL_SM50_SHADER_ARGUMENT_BUFFER),
      MakeArgument(SM50BindingType::SRV, 4, 8,
                   MTL_SM50_SHADER_ARGUMENT_TEXTURE),
      MakeArgument(SM50BindingType::SRV, 5, 3, MTL_SM50_SHADER_ARGUMENT_BUFFER),
      MakeArgument(SM50BindingType::Sampler, 6, 1,
                   MTL_SM50_SHADER_ARGUMENT_FLAG(0)),
  };

  std::vector<std::pair<uint32_t, uint32_t>> visited;
  const auto total = dxmt::ForEachBufferTableField(
      cbuffers.data(), cbuffers.size(), arguments.data(), arguments.size(),
      [&](const auto &argument, uint32_t base) {
        visited.emplace_back(argument.SM50BindingSlot, base);
      });

  EXPECT_EQ(visited, (std::vector<std::pair<uint32_t, uint32_t>>{
                         {0, 0}, {1, 3}, {5, 9}, {3, 18}}));
  EXPECT_EQ(total, 21u);
  EXPECT_EQ(dxmt::BufferTableQwordCount(cbuffers.data(), cbuffers.size(),
                                        arguments.data(), arguments.size()),
            total);
}

TEST(DescriptorTable, BuildsPerReflectionCompactBases) {
  const std::array cbuffers = {
      MakeArgument(SM50BindingType::ConstantBuffer, 0, 1,
                   MTL_SM50_SHADER_ARGUMENT_BUFFER),
  };
  const std::array arguments = {
      MakeArgument(SM50BindingType::UAV, 1, 2, MTL_SM50_SHADER_ARGUMENT_BUFFER),
      MakeArgument(SM50BindingType::SRV, 2, 1,
                   MTL_SM50_SHADER_ARGUMENT_TEXTURE),
      MakeArgument(SM50BindingType::SRV, 3, 1, MTL_SM50_SHADER_ARGUMENT_BUFFER),
  };
  std::array<uint32_t, cbuffers.size()> cb_bases = {};
  std::array<uint32_t, arguments.size()> resource_bases = {};

  const auto total = dxmt::BuildBufferTableCompactBases(
      cbuffers.data(), cbuffers.size(), arguments.data(), arguments.size(),
      cb_bases.data(), resource_bases.data());

  EXPECT_EQ(cb_bases, (std::array<uint32_t, 1>{0}));
  EXPECT_EQ(resource_bases,
            (std::array<uint32_t, 3>{6, dxmt::kNotABufferTableField, 3}));
  EXPECT_EQ(total, 12u);
}

TEST(DescriptorTable, HandlesAnEmptyReflectionWithoutOutputStorage) {
  uint32_t callbacks = 0;
  EXPECT_EQ(dxmt::ForEachBufferTableField(
                nullptr, 0, nullptr, 0,
                [&](const auto &, uint32_t) { ++callbacks; }),
            0u);
  EXPECT_EQ(callbacks, 0u);
  EXPECT_EQ(dxmt::BufferTableQwordCount(nullptr, 0, nullptr, 0), 0u);
  EXPECT_EQ(dxmt::BuildBufferTableCompactBases(nullptr, 0, nullptr, 0,
                                               nullptr, nullptr),
            0u);
}

TEST(DescriptorTable, WritesAddressMetadataAndCounterWithoutAliasing) {
  std::array<uint64_t, 7> table;
  table.fill(0xccccccccccccccccull);

  dxmt::WriteBufferTableSlot(table.data(), 0, 0x1000, 64);
  dxmt::WriteBufferTableCounter(table.data(), 0, 0x2000);
  dxmt::WriteBufferTableSlot(table.data(), 3, 0x3000, 128);

  EXPECT_EQ(table, (std::array<uint64_t, 7>{0x1000, 64, 0x2000, 0x3000, 128,
                                            0xccccccccccccccccull,
                                            0xccccccccccccccccull}));
}

TEST(DescriptorMirror, EncodesTextureMetadataBitExactly) {
  std::array<uint64_t, 4> slot = {};
  dxmt::EncodeMirrorTextureSlot(slot.data() + 1, 0x123456789abcdef0ull, 7,
                                -0.5f);

  EXPECT_EQ(slot[0], 0u);
  EXPECT_EQ(slot[1], 0x123456789abcdef0ull);
  EXPECT_EQ(slot[2], (uint64_t(7) << 32) | std::bit_cast<uint32_t>(-0.5f));
  EXPECT_EQ(slot[3], 0u);
}

TEST(DescriptorMirror, EncodesSamplerAndNullPayloads) {
  std::array<uint64_t, 3> slot = {};

  dxmt::EncodeMirrorSamplerSlot(slot.data(), 0x10, 0x20, 1.25f);
  EXPECT_EQ(slot, (std::array<uint64_t, 3>{0x10, 0x20,
                                           std::bit_cast<uint32_t>(1.25f)}));

  dxmt::EncodeMirrorSamplerSlotNull(slot.data(), 0x30);
  EXPECT_EQ(slot, (std::array<uint64_t, 3>{0x30, 0x30, 0}));
}

} // namespace
