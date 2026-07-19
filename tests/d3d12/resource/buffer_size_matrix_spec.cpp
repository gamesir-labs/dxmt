#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// Public D3D12 API only: CreateCommittedResource / GetDesc / Map / readback.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

struct BufferSizeCase {
  UINT64 size;
  const char *name;
};

std::vector<BufferSizeCase> BuildBufferSizeCases() {
  std::vector<BufferSizeCase> cases;
  // Powers of two and one-past / one-before boundaries around common limits.
  static const UINT64 kPowers[] = {1,     4,    16,   64,    256,
                                   1024, 4096, 65536, 1048576};
  for (const UINT64 power : kPowers) {
    cases.push_back({power, nullptr});
    if (power > 1)
      cases.push_back({power - 1, nullptr});
    cases.push_back({power + 1, nullptr});
  }
  // Unique by size.
  std::sort(cases.begin(), cases.end(),
            [](const BufferSizeCase &a, const BufferSizeCase &b) {
              return a.size < b.size;
            });
  cases.erase(std::unique(cases.begin(), cases.end(),
                          [](const BufferSizeCase &a, const BufferSizeCase &b) {
                            return a.size == b.size;
                          }),
              cases.end());
  return cases;
}

class BufferSizeMatrixSpec
    : public ::testing::TestWithParam<BufferSizeCase> {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  D3D12TestContext context_;
};

TEST_P(BufferSizeMatrixSpec, CreatesCommittedDefaultBufferWithMatchingDesc) {
  const auto &test = GetParam();
  D3D12_HEAP_PROPERTIES heap = {};
  heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  desc.Width = test.size;
  desc.Height = 1;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  ComPtr<ID3D12Resource> resource;
  ASSERT_EQ(context_.device()->CreateCommittedResource(
                &heap, D3D12_HEAP_FLAG_NONE, &desc,
                D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(resource.put())),
            S_OK)
      << "size=" << test.size;
  ASSERT_TRUE(resource);
  const auto actual = resource->GetDesc();
  EXPECT_EQ(actual.Dimension, D3D12_RESOURCE_DIMENSION_BUFFER);
  EXPECT_EQ(actual.Width, test.size);
  EXPECT_EQ(actual.Height, 1u);
  EXPECT_EQ(actual.DepthOrArraySize, 1u);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_P(BufferSizeMatrixSpec, CreatesUploadBufferAndMapsFullRange) {
  const auto &test = GetParam();
  // Cap map stress to 64 KiB so the suite stays PR-friendly while still
  // covering every small size and selected large powers via create-only above.
  if (test.size > 65536)
    GTEST_SKIP() << "map matrix limited to 64KiB";
  D3D12_HEAP_PROPERTIES heap = {};
  heap.Type = D3D12_HEAP_TYPE_UPLOAD;
  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  desc.Width = test.size;
  desc.Height = 1;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  ComPtr<ID3D12Resource> resource;
  ASSERT_EQ(context_.device()->CreateCommittedResource(
                &heap, D3D12_HEAP_FLAG_NONE, &desc,
                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                IID_PPV_ARGS(resource.put())),
            S_OK)
      << "size=" << test.size;
  void *mapped = nullptr;
  ASSERT_EQ(resource->Map(0, nullptr, &mapped), S_OK);
  ASSERT_NE(mapped, nullptr);
  std::memset(mapped, 0x5a, static_cast<size_t>(test.size));
  resource->Unmap(0, nullptr);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

std::string BufferSizeName(const ::testing::TestParamInfo<BufferSizeCase> &info) {
  return "Size" + std::to_string(info.param.size);
}

INSTANTIATE_TEST_SUITE_P(SizeMatrix, BufferSizeMatrixSpec,
                         ::testing::ValuesIn(BuildBufferSizeCases()),
                         BufferSizeName);

} // namespace
