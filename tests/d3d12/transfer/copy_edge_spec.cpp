#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

struct CopyBufferCase {
  UINT64 byte_count;
  UINT64 source_offset;
  UINT64 destination_offset;
  const char *name;
};

class CopyBufferBoundarySpec
    : public ::testing::TestWithParam<CopyBufferCase> {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  D3D12TestContext context_;
};

TEST_P(CopyBufferBoundarySpec, CopiesExactRequestedInterval) {
  constexpr UINT64 kSize = 128;
  std::array<std::uint8_t, kSize> source_data = {};
  std::array<std::uint8_t, kSize> expected = {};
  source_data.fill(0);
  expected.fill(0xcd);
  for (UINT64 index = 0; index < kSize; ++index)
    source_data[index] = static_cast<std::uint8_t>(index * 37u + 11u);
  const auto &test = GetParam();
  std::copy_n(source_data.begin() + test.source_offset, test.byte_count,
              expected.begin() + test.destination_offset);

  auto source = context_.CreateUploadBuffer(kSize, source_data.data(), kSize);
  auto destination = context_.CreateBuffer(
      kSize, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(source);
  ASSERT_TRUE(destination);
  void *mapped = nullptr;
  ASSERT_EQ(destination->Map(0, nullptr, &mapped), S_OK);
  std::memset(mapped, 0xcd, kSize);
  destination->Unmap(0, nullptr);

  context_.list()->CopyBufferRegion(
      destination.get(), test.destination_offset, source.get(),
      test.source_offset, test.byte_count);
  ASSERT_EQ(context_.ExecuteAndWait(), S_OK);
  ASSERT_EQ(destination->Map(0, nullptr, &mapped), S_OK);
  ASSERT_NE(mapped, nullptr);
  EXPECT_EQ(std::memcmp(mapped, expected.data(), kSize), 0);
  destination->Unmap(0, nullptr);
}

std::string CopyBufferCaseName(
    const ::testing::TestParamInfo<CopyBufferCase> &info) {
  return info.param.name;
}

INSTANTIATE_TEST_SUITE_P(
    BoundaryMatrix, CopyBufferBoundarySpec,
    ::testing::Values(
        CopyBufferCase{1, 0, 0, "FirstByte"},
        CopyBufferCase{1, 127, 127, "LastByte"},
        CopyBufferCase{31, 1, 65, "Count31"},
        CopyBufferCase{32, 16, 64, "Count32"},
        CopyBufferCase{33, 7, 73, "Count33"},
        CopyBufferCase{64, 0, 64, "UpperHalf"},
        CopyBufferCase{128, 0, 0, "WholeBuffer"}),
    CopyBufferCaseName);


class CopyResourceCompatibilitySpec : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  D3D12TestContext context_;

};





TEST_F(CopyResourceCompatibilitySpec, AcceptsFormatsInSameTypeGroup) {
  auto source = context_.CreateTexture2D(
      4, 4, 1, DXGI_FORMAT_R8G8B8A8_TYPELESS, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_SOURCE);
  auto destination = context_.CreateTexture2D(
      4, 4, 1, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(source);
  ASSERT_TRUE(destination);
  context_.list()->CopyResource(destination.get(), source.get());
  EXPECT_EQ(context_.list()->Close(), S_OK);
}





TEST_F(CopyResourceCompatibilitySpec, ZeroLengthBufferCopyIsNoOp) {
  auto source = context_.CreateBuffer(64, D3D12_HEAP_TYPE_UPLOAD,
                                      D3D12_RESOURCE_FLAG_NONE,
                                      D3D12_RESOURCE_STATE_GENERIC_READ);
  auto destination = context_.CreateBuffer(64, D3D12_HEAP_TYPE_READBACK,
                                           D3D12_RESOURCE_FLAG_NONE,
                                           D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(source);
  ASSERT_TRUE(destination);
  context_.list()->CopyBufferRegion(destination.get(), 64, source.get(), 64,
                                    0);
  EXPECT_EQ(context_.list()->Close(), S_OK);
}

} // namespace
