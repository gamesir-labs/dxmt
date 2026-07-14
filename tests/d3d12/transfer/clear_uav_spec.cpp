#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

#include <array>
#include <cstring>
#include <span>
#include <vector>

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

struct BufferUav {
  ComPtr<ID3D12Resource> resource;
  ComPtr<ID3D12DescriptorHeap> heap;
};

class ClearUavSpec : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_TRUE(SUCCEEDED(context_.Initialize())); }

  BufferUav CreateBufferUav(const D3D12_UNORDERED_ACCESS_VIEW_DESC &desc,
                            std::span<const std::uint32_t> initial) {
    BufferUav result;
    auto upload = context_.CreateUploadBuffer(
        initial.size_bytes(), initial.data(), initial.size_bytes());
    result.resource = context_.CreateBuffer(
        initial.size_bytes(), D3D12_HEAP_TYPE_DEFAULT,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_DEST);
    result.heap = context_.CreateDescriptorHeap(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true);
    if (!upload || !result.resource || !result.heap)
      return {};
    context_.device()->CreateUnorderedAccessView(
        result.resource.get(), nullptr, &desc,
        result.heap->GetCPUDescriptorHandleForHeapStart());
    context_.list()->CopyBufferRegion(result.resource.get(), 0, upload.get(), 0,
                                      initial.size_bytes());
    D3D12TestContext::Transition(
        context_.list(), result.resource.get(), D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    return result;
  }

  void ClearAndReadback(const BufferUav &uav,
                        const std::array<UINT, 4> &clear_value,
                        std::vector<std::uint32_t> *actual) {
    ID3D12DescriptorHeap *heaps[] = {uav.heap.get()};
    context_.list()->SetDescriptorHeaps(1, heaps);
    context_.list()->ClearUnorderedAccessViewUint(
        uav.heap->GetGPUDescriptorHandleForHeapStart(),
        uav.heap->GetCPUDescriptorHandleForHeapStart(), uav.resource.get(),
        clear_value.data(), 0, nullptr);
    D3D12TestContext::Transition(
        context_.list(), uav.resource.get(),
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_SOURCE);
    std::vector<std::uint8_t> bytes;
    ASSERT_TRUE(SUCCEEDED(context_.ReadbackBuffer(
        uav.resource.get(), actual->size() * sizeof((*actual)[0]), &bytes)));
    ASSERT_EQ(bytes.size(), actual->size() * sizeof((*actual)[0]));
    std::memcpy(actual->data(), bytes.data(), bytes.size());
  }

  D3D12TestContext context_;
};

TEST_F(ClearUavSpec, ClearTypedBufferUint) {
  std::array<std::uint32_t, 8> expected;
  expected.fill(0xdeadbeef);
  D3D12_UNORDERED_ACCESS_VIEW_DESC desc = {};
  desc.Format = DXGI_FORMAT_R32_UINT;
  desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  desc.Buffer.FirstElement = 2;
  desc.Buffer.NumElements = 4;
  auto uav = CreateBufferUav(desc, expected);
  ASSERT_TRUE(uav.resource);
  ASSERT_TRUE(uav.heap);
  constexpr std::array<UINT, 4> clear = {0x10203040, 2, 3, 4};
  std::fill(expected.begin() + 2, expected.begin() + 6, clear[0]);

  std::vector<std::uint32_t> actual(expected.size());
  ClearAndReadback(uav, clear, &actual);
  EXPECT_EQ(actual, (std::vector<std::uint32_t>(expected.begin(),
                                                expected.end())));
}

TEST_F(ClearUavSpec, ClearRawBuffer) {
  std::array<std::uint32_t, 8> expected;
  expected.fill(0xdeadbeef);
  D3D12_UNORDERED_ACCESS_VIEW_DESC desc = {};
  desc.Format = DXGI_FORMAT_R32_TYPELESS;
  desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  desc.Buffer.FirstElement = 1;
  desc.Buffer.NumElements = 4;
  desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
  auto uav = CreateBufferUav(desc, expected);
  ASSERT_TRUE(uav.resource);
  ASSERT_TRUE(uav.heap);
  constexpr std::array<UINT, 4> clear = {0x50607080, 2, 3, 4};
  std::fill(expected.begin() + 1, expected.begin() + 5, clear[0]);

  std::vector<std::uint32_t> actual(expected.size());
  ClearAndReadback(uav, clear, &actual);
  EXPECT_EQ(actual, (std::vector<std::uint32_t>(expected.begin(),
                                                expected.end())));
}

TEST_F(ClearUavSpec, ClearStructuredBuffer) {
  std::array<std::uint32_t, 16> expected;
  expected.fill(0xdeadbeef);
  D3D12_UNORDERED_ACCESS_VIEW_DESC desc = {};
  desc.Format = DXGI_FORMAT_UNKNOWN;
  desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  desc.Buffer.FirstElement = 1;
  desc.Buffer.NumElements = 2;
  desc.Buffer.StructureByteStride = 4 * sizeof(std::uint32_t);
  auto uav = CreateBufferUav(desc, expected);
  ASSERT_TRUE(uav.resource);
  ASSERT_TRUE(uav.heap);
  constexpr std::array<UINT, 4> clear = {
      0x11223344, 0x55667788, 0x99aabbcc, 0xddeeff00};
  std::fill(expected.begin() + 4, expected.begin() + 12, clear[0]);

  std::vector<std::uint32_t> actual(expected.size());
  ClearAndReadback(uav, clear, &actual);
  EXPECT_EQ(actual, (std::vector<std::uint32_t>(expected.begin(),
                                                expected.end())));
}

} // namespace
