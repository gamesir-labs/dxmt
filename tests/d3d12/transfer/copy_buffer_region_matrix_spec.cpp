#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// Public D3D12 CopyBufferRegion matrix over offset/size combinations.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

struct CopyRegionCase {
  UINT64 src_offset;
  UINT64 dst_offset;
  UINT64 num_bytes;
};

std::vector<CopyRegionCase> BuildCopyRegionCases() {
  std::vector<CopyRegionCase> cases;
  constexpr UINT64 kBufferSize = 4096;
  const UINT64 offsets[] = {0, 1, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048};
  const UINT64 sizes[] = {1, 4, 8, 16, 32, 64, 128, 256, 512, 1024};
  for (const UINT64 offset : offsets) {
    if (offset == 0)
      continue;
    cases.push_back({offset, 0, 1});
    cases.push_back({0, offset, 1});
  }
  for (const UINT64 size : sizes) {
    cases.push_back({0, 0, size});
    cases.push_back({kBufferSize - size, 0, size});
    cases.push_back({0, kBufferSize - size, size});
  }
  return cases;
}

class CopyBufferRegionMatrixSpec
    : public ::testing::TestWithParam<CopyRegionCase> {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  D3D12TestContext context_;
  static constexpr UINT64 kBufferSize = 4096;
};

TEST_P(CopyBufferRegionMatrixSpec, CopiesExactByteRangeBetweenDefaultBuffers) {
  const auto &test = GetParam();
  std::vector<std::uint8_t> source_bytes(kBufferSize);
  for (UINT64 i = 0; i < kBufferSize; ++i)
    source_bytes[static_cast<size_t>(i)] =
        static_cast<std::uint8_t>((i * 17u + 31u) & 0xffu);

  auto upload = context_.CreateUploadBuffer(kBufferSize, source_bytes.data(),
                                            kBufferSize);
  auto source = context_.CreateBuffer(kBufferSize, D3D12_HEAP_TYPE_DEFAULT,
                                      D3D12_RESOURCE_FLAG_NONE,
                                      D3D12_RESOURCE_STATE_COPY_DEST);
  auto destination = context_.CreateBuffer(
      kBufferSize, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(upload);
  ASSERT_TRUE(source);
  ASSERT_TRUE(destination);

  // Init destination with a poison pattern via upload of zeros then fill.
  std::vector<std::uint8_t> poison(kBufferSize, 0xcd);
  auto poison_upload =
      context_.CreateUploadBuffer(kBufferSize, poison.data(), kBufferSize);
  ASSERT_TRUE(poison_upload);
  context_.list()->CopyBufferRegion(destination.get(), 0, poison_upload.get(), 0,
                                    kBufferSize);
  context_.list()->CopyBufferRegion(source.get(), 0, upload.get(), 0,
                                    kBufferSize);
  D3D12TestContext::Transition(context_.list(), source.get(),
                               D3D12_RESOURCE_STATE_COPY_DEST,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
  context_.list()->CopyBufferRegion(destination.get(), test.dst_offset,
                                    source.get(), test.src_offset,
                                    test.num_bytes);
  D3D12TestContext::Transition(context_.list(), destination.get(),
                               D3D12_RESOURCE_STATE_COPY_DEST,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
  std::vector<std::uint8_t> actual;
  ASSERT_EQ(context_.ReadbackBuffer(destination.get(), kBufferSize, &actual),
            S_OK);
  ASSERT_EQ(actual.size(), kBufferSize);
  for (UINT64 i = 0; i < kBufferSize; ++i) {
    const std::uint8_t expected =
        (i >= test.dst_offset && i < test.dst_offset + test.num_bytes)
            ? source_bytes[static_cast<size_t>(test.src_offset +
                                               (i - test.dst_offset))]
            : 0xcd;
    EXPECT_EQ(actual[static_cast<size_t>(i)], expected)
        << "byte=" << i << " src=" << test.src_offset
        << " dst=" << test.dst_offset << " n=" << test.num_bytes;
  }
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

std::string CopyRegionName(const ::testing::TestParamInfo<CopyRegionCase> &info) {
  return "S" + std::to_string(info.param.src_offset) + "_D" +
         std::to_string(info.param.dst_offset) + "_N" +
         std::to_string(info.param.num_bytes);
}

INSTANTIATE_TEST_SUITE_P(RegionMatrix, CopyBufferRegionMatrixSpec,
                         ::testing::ValuesIn(BuildCopyRegionCases()),
                         CopyRegionName);

} // namespace
