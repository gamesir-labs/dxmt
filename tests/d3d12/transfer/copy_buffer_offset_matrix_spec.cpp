#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// Plan §15.1: CopyBufferRegion source/dest offset × size matrix.
// Public D3D12 API only. Density kept PR/runtime friendly.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

struct CopyOffsetCase {
  UINT64 buffer_size;
  UINT64 src_offset;
  UINT64 dst_offset;
  UINT64 copy_size;
};

std::vector<CopyOffsetCase> BuildCopyOffsetCases() {
  std::vector<CopyOffsetCase> cases;
  constexpr UINT64 kBuf = 1024;
  const UINT64 sizes[] = {1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024};
  for (const UINT64 size : sizes) {
    const UINT64 slack = kBuf - size;
    cases.push_back({kBuf, 0, 0, size});
    cases.push_back({kBuf, slack, 0, size});
    cases.push_back({kBuf, 0, slack, size});
    cases.push_back({kBuf, slack / 2, slack - slack / 2, size});
  }
  // Extra smaller buffers for edge sizes.
  for (UINT64 buf : {16ull, 64ull, 256ull}) {
    for (UINT64 size : {1ull, 4ull, buf / 2, buf}) {
      if (size == 0 || size > buf)
        continue;
      cases.push_back({buf, 0, 0, size});
      if (buf > size) {
        cases.push_back({buf, buf - size, 0, size});
        cases.push_back({buf, 0, buf - size, size});
      }
    }
  }
  return cases;
}

class CopyBufferOffsetMatrixSpec
    : public ::testing::TestWithParam<CopyOffsetCase> {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }
  D3D12TestContext context_;
};

TEST_P(CopyBufferOffsetMatrixSpec, CopiesBytesExactly) {
  const auto &test = GetParam();
  std::vector<std::uint8_t> pattern(static_cast<size_t>(test.buffer_size));
  for (size_t i = 0; i < pattern.size(); ++i)
    pattern[i] = static_cast<std::uint8_t>((i * 17u + 31u) & 0xff);

  auto src_upload = context_.CreateUploadBuffer(
      test.buffer_size, pattern.data(), pattern.size());
  auto src = context_.CreateBuffer(test.buffer_size, D3D12_HEAP_TYPE_DEFAULT,
                                   D3D12_RESOURCE_FLAG_NONE,
                                   D3D12_RESOURCE_STATE_COPY_DEST);
  auto dst = context_.CreateBuffer(test.buffer_size, D3D12_HEAP_TYPE_DEFAULT,
                                   D3D12_RESOURCE_FLAG_NONE,
                                   D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(src_upload);
  ASSERT_TRUE(src);
  ASSERT_TRUE(dst);

  std::vector<std::uint8_t> zeros(static_cast<size_t>(test.buffer_size), 0);
  auto zero_upload = context_.CreateUploadBuffer(test.buffer_size, zeros.data(),
                                                 zeros.size());
  ASSERT_TRUE(zero_upload);
  context_.list()->CopyBufferRegion(src.get(), 0, src_upload.get(), 0,
                                    test.buffer_size);
  context_.list()->CopyBufferRegion(dst.get(), 0, zero_upload.get(), 0,
                                    test.buffer_size);
  D3D12TestContext::Transition(context_.list(), src.get(),
                               D3D12_RESOURCE_STATE_COPY_DEST,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
  context_.list()->CopyBufferRegion(dst.get(), test.dst_offset, src.get(),
                                    test.src_offset, test.copy_size);
  D3D12TestContext::Transition(context_.list(), dst.get(),
                               D3D12_RESOURCE_STATE_COPY_DEST,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);

  std::vector<std::uint8_t> actual;
  ASSERT_EQ(context_.ReadbackBuffer(dst.get(), test.buffer_size, &actual),
            S_OK);
  for (UINT64 i = 0; i < test.buffer_size; ++i) {
    std::uint8_t expected = 0;
    if (i >= test.dst_offset && i < test.dst_offset + test.copy_size) {
      const UINT64 src_i = test.src_offset + (i - test.dst_offset);
      expected = pattern[static_cast<size_t>(src_i)];
    }
    EXPECT_EQ(actual[static_cast<size_t>(i)], expected)
        << "i=" << i << " src=" << test.src_offset << " dst=" << test.dst_offset
        << " n=" << test.copy_size;
  }
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

std::string CopyOffsetName(
    const ::testing::TestParamInfo<CopyOffsetCase> &info) {
  return "B" + std::to_string(info.param.buffer_size) + "S" +
         std::to_string(info.param.src_offset) + "D" +
         std::to_string(info.param.dst_offset) + "N" +
         std::to_string(info.param.copy_size) + "I" +
         std::to_string(info.index);
}

INSTANTIATE_TEST_SUITE_P(OffsetMatrix, CopyBufferOffsetMatrixSpec,
                         ::testing::ValuesIn(BuildCopyOffsetCases()),
                         CopyOffsetName);

} // namespace
