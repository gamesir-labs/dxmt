#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// Public D3D12 ClearUnorderedAccessViewUint value matrix.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

struct ClearUavValueCase {
  std::array<UINT, 4> values;
};

std::vector<ClearUavValueCase> BuildClearUavValueCases() {
  std::vector<ClearUavValueCase> cases;
  const UINT seeds[] = {0, 1, 2, 3, 7, 8, 15, 16, 31, 32, 63, 64, 127, 128,
                        255, 256, 1023, 1024, 0x7fffffff, 0x80000000, 0xffffffff,
                        0x12345678, 0xa5a5a5a5, 0x5a5a5a5a, 0xdeadbeef,
                        0x0f0f0f0f, 0xf0f0f0f0};
  for (const UINT v : seeds)
    cases.push_back({{v, v, v, v}});
  return cases;
}

class ClearUavValueMatrixSpec
    : public ::testing::TestWithParam<ClearUavValueCase> {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  D3D12TestContext context_;
};

TEST_P(ClearUavValueMatrixSpec, ClearsTypedUintBufferToRequestedValue) {
  const auto &test = GetParam();
  constexpr UINT kCount = 64;
  auto buffer = context_.CreateBuffer(
      kCount * sizeof(UINT), D3D12_HEAP_TYPE_DEFAULT,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  auto heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true);
  ASSERT_TRUE(buffer);
  ASSERT_TRUE(heap);
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
  uav.Format = DXGI_FORMAT_R32_UINT;
  uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav.Buffer.NumElements = kCount;
  context_.device()->CreateUnorderedAccessView(
      buffer.get(), nullptr, &uav,
      heap->GetCPUDescriptorHandleForHeapStart());
  ID3D12DescriptorHeap *heaps[] = {heap.get()};
  context_.list()->SetDescriptorHeaps(1, heaps);
  context_.list()->ClearUnorderedAccessViewUint(
      heap->GetGPUDescriptorHandleForHeapStart(),
      heap->GetCPUDescriptorHandleForHeapStart(), buffer.get(),
      test.values.data(), 0, nullptr);
  D3D12TestContext::Transition(
      context_.list(), buffer.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_COPY_SOURCE);
  std::vector<std::uint8_t> bytes;
  ASSERT_EQ(context_.ReadbackBuffer(buffer.get(), kCount * sizeof(UINT),
                                    &bytes),
            S_OK);
  for (UINT i = 0; i < kCount; ++i) {
    UINT actual = 0;
    std::memcpy(&actual, bytes.data() + i * sizeof(UINT), sizeof(actual));
    EXPECT_EQ(actual, test.values[0]) << "element=" << i;
  }
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

std::string ClearUavName(const ::testing::TestParamInfo<ClearUavValueCase> &info) {
  return "V" + std::to_string(info.param.values[0]) + "_" +
         std::to_string(info.param.values[1]) + "_" +
         std::to_string(info.param.values[2]) + "_" +
         std::to_string(info.param.values[3]) + "_N" +
         std::to_string(info.index);
}

INSTANTIATE_TEST_SUITE_P(ValueMatrix, ClearUavValueMatrixSpec,
                         ::testing::ValuesIn(BuildClearUavValueCases()),
                         ClearUavName);

} // namespace
