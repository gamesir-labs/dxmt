#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;
using dxmt::test::TextureReadback;

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
    result.resource =
        context_.CreateBuffer(initial.size_bytes(), D3D12_HEAP_TYPE_DEFAULT,
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
    D3D12TestContext::Transition(context_.list(), result.resource.get(),
                                 D3D12_RESOURCE_STATE_COPY_DEST,
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
    D3D12TestContext::Transition(context_.list(), uav.resource.get(),
                                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                 D3D12_RESOURCE_STATE_COPY_SOURCE);
    std::vector<std::uint8_t> bytes;
    ASSERT_TRUE(SUCCEEDED(context_.ReadbackBuffer(
        uav.resource.get(), actual->size() * sizeof((*actual)[0]), &bytes)));
    ASSERT_EQ(bytes.size(), actual->size() * sizeof((*actual)[0]));
    std::memcpy(actual->data(), bytes.data(), bytes.size());
  }

  ComPtr<ID3D12Resource>
  CreateTexture(UINT width, UINT height, UINT16 mip_levels = 1,
                UINT16 array_size = 1,
                DXGI_FORMAT format = DXGI_FORMAT_R32_UINT) {
    D3D12_HEAP_PROPERTIES heap_properties = {};
    heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = array_size;
    desc.MipLevels = mip_levels;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    ComPtr<ID3D12Resource> resource;
    HRESULT hr = context_.device()->CreateCommittedResource(
        &heap_properties, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
        IID_PPV_ARGS(resource.put()));
    return SUCCEEDED(hr) ? std::move(resource) : ComPtr<ID3D12Resource>();
  }

  std::uint32_t UintPixel(const TextureReadback &readback, UINT x, UINT y) {
    std::uint32_t value = 0;
    std::memcpy(&value,
                readback.data.data() + y * readback.row_pitch +
                    x * sizeof(value),
                sizeof(value));
    return value;
  }

  float FloatPixel(const TextureReadback &readback, UINT x, UINT y) {
    float value = 0.0f;
    std::memcpy(&value,
                readback.data.data() + y * readback.row_pitch +
                    x * sizeof(value),
                sizeof(value));
    return value;
  }

  HRESULT ReadbackAndReset(ID3D12Resource *texture, TextureReadback *readback,
                           UINT subresource = 0) {
    HRESULT hr = context_.ReadbackTexture(texture, readback, subresource);
    return FAILED(hr) ? hr : context_.ResetCommandList();
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
  EXPECT_EQ(actual,
            (std::vector<std::uint32_t>(expected.begin(), expected.end())));
}

TEST_F(ClearUavSpec, ClearTypedBufferFloat) {
  std::array<std::uint32_t, 8> expected;
  expected.fill(std::bit_cast<std::uint32_t>(9.0f));
  D3D12_UNORDERED_ACCESS_VIEW_DESC desc = {};
  desc.Format = DXGI_FORMAT_R32_FLOAT;
  desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  desc.Buffer.FirstElement = 2;
  desc.Buffer.NumElements = 4;
  auto uav = CreateBufferUav(desc, expected);
  ASSERT_TRUE(uav.resource);
  ASSERT_TRUE(uav.heap);
  constexpr std::array<FLOAT, 4> clear = {0.25f, 2.0f, 3.0f, 4.0f};
  std::fill(expected.begin() + 2, expected.begin() + 6,
            std::bit_cast<std::uint32_t>(clear[0]));
  ID3D12DescriptorHeap *heaps[] = {uav.heap.get()};
  context_.list()->SetDescriptorHeaps(1, heaps);
  context_.list()->ClearUnorderedAccessViewFloat(
      uav.heap->GetGPUDescriptorHandleForHeapStart(),
      uav.heap->GetCPUDescriptorHandleForHeapStart(), uav.resource.get(),
      clear.data(), 0, nullptr);
  D3D12TestContext::Transition(context_.list(), uav.resource.get(),
                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);

  std::vector<std::uint8_t> bytes;
  ASSERT_TRUE(SUCCEEDED(context_.ReadbackBuffer(
      uav.resource.get(), expected.size() * sizeof(expected[0]), &bytes)));
  ASSERT_EQ(bytes.size(), expected.size() * sizeof(expected[0]));
  EXPECT_EQ(std::memcmp(bytes.data(), expected.data(), bytes.size()), 0);
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
  EXPECT_EQ(actual,
            (std::vector<std::uint32_t>(expected.begin(), expected.end())));
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
  constexpr std::array<UINT, 4> clear = {0x11223344, 0x55667788, 0x99aabbcc,
                                         0xddeeff00};
  std::fill(expected.begin() + 4, expected.begin() + 12, clear[0]);

  std::vector<std::uint32_t> actual(expected.size());
  ClearAndReadback(uav, clear, &actual);
  EXPECT_EQ(actual,
            (std::vector<std::uint32_t>(expected.begin(), expected.end())));
}

TEST_F(ClearUavSpec, ClearTextureMip) {
  auto texture = CreateTexture(8, 8, 3);
  auto heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true);
  ASSERT_TRUE(texture);
  ASSERT_TRUE(heap);
  D3D12_UNORDERED_ACCESS_VIEW_DESC desc = {};
  desc.Format = DXGI_FORMAT_R32_UINT;
  desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
  desc.Texture2D.MipSlice = 2;
  const auto cpu = heap->GetCPUDescriptorHandleForHeapStart();
  context_.device()->CreateUnorderedAccessView(texture.get(), nullptr, &desc,
                                               cpu);
  ID3D12DescriptorHeap *heaps[] = {heap.get()};
  context_.list()->SetDescriptorHeaps(1, heaps);
  constexpr std::array<UINT, 4> clear = {0x10203040, 2, 3, 4};
  context_.list()->ClearUnorderedAccessViewUint(
      heap->GetGPUDescriptorHandleForHeapStart(), cpu, texture.get(),
      clear.data(), 0, nullptr);
  D3D12TestContext::Transition(context_.list(), texture.get(),
                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);

  TextureReadback readback;
  ASSERT_TRUE(SUCCEEDED(ReadbackAndReset(texture.get(), &readback, 2)));
  ASSERT_EQ(readback.width, 2u);
  ASSERT_EQ(readback.height, 2u);
  for (UINT y = 0; y < readback.height; ++y) {
    for (UINT x = 0; x < readback.width; ++x)
      EXPECT_EQ(UintPixel(readback, x, y), clear[0]);
  }
}

TEST_F(ClearUavSpec, ClearFloatTexture) {
  auto texture = CreateTexture(4, 4, 1, 1, DXGI_FORMAT_R32_FLOAT);
  auto heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true);
  ASSERT_TRUE(texture);
  ASSERT_TRUE(heap);
  D3D12_UNORDERED_ACCESS_VIEW_DESC desc = {};
  desc.Format = DXGI_FORMAT_R32_FLOAT;
  desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
  const auto cpu = heap->GetCPUDescriptorHandleForHeapStart();
  context_.device()->CreateUnorderedAccessView(texture.get(), nullptr, &desc,
                                               cpu);
  ID3D12DescriptorHeap *heaps[] = {heap.get()};
  context_.list()->SetDescriptorHeaps(1, heaps);
  constexpr std::array<FLOAT, 4> clear = {0.375f, -2.0f, 3.0f, 4.0f};
  context_.list()->ClearUnorderedAccessViewFloat(
      heap->GetGPUDescriptorHandleForHeapStart(), cpu, texture.get(),
      clear.data(), 0, nullptr);
  D3D12TestContext::Transition(context_.list(), texture.get(),
                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);

  TextureReadback readback;
  ASSERT_TRUE(SUCCEEDED(ReadbackAndReset(texture.get(), &readback)));
  for (UINT y = 0; y < readback.height; ++y) {
    for (UINT x = 0; x < readback.width; ++x)
      EXPECT_FLOAT_EQ(FloatPixel(readback, x, y), clear[0]);
  }
}

TEST_F(ClearUavSpec, ClearTextureArraySlice) {
  auto texture = CreateTexture(4, 4, 1, 2);
  auto heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, true);
  ASSERT_TRUE(texture);
  ASSERT_TRUE(heap);
  D3D12_UNORDERED_ACCESS_VIEW_DESC desc = {};
  desc.Format = DXGI_FORMAT_R32_UINT;
  desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
  desc.Texture2DArray.ArraySize = 1;
  const auto first_cpu = context_.CpuDescriptorHandle(heap.get(), 0);
  context_.device()->CreateUnorderedAccessView(texture.get(), nullptr, &desc,
                                               first_cpu);
  desc.Texture2DArray.FirstArraySlice = 1;
  const auto second_cpu = context_.CpuDescriptorHandle(heap.get(), 1);
  context_.device()->CreateUnorderedAccessView(texture.get(), nullptr, &desc,
                                               second_cpu);
  ID3D12DescriptorHeap *heaps[] = {heap.get()};
  context_.list()->SetDescriptorHeaps(1, heaps);
  constexpr std::array<UINT, 4> first_clear = {11, 0, 0, 0};
  constexpr std::array<UINT, 4> second_clear = {22, 0, 0, 0};
  context_.list()->ClearUnorderedAccessViewUint(
      context_.GpuDescriptorHandle(heap.get(), 0), first_cpu, texture.get(),
      first_clear.data(), 0, nullptr);
  context_.list()->ClearUnorderedAccessViewUint(
      context_.GpuDescriptorHandle(heap.get(), 1), second_cpu, texture.get(),
      second_clear.data(), 0, nullptr);
  D3D12TestContext::Transition(context_.list(), texture.get(),
                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);

  TextureReadback first;
  ASSERT_TRUE(SUCCEEDED(ReadbackAndReset(texture.get(), &first, 0)));
  TextureReadback second;
  ASSERT_TRUE(SUCCEEDED(ReadbackAndReset(texture.get(), &second, 1)));
  for (UINT y = 0; y < first.height; ++y) {
    for (UINT x = 0; x < first.width; ++x) {
      EXPECT_EQ(UintPixel(first, x, y), first_clear[0]);
      EXPECT_EQ(UintPixel(second, x, y), second_clear[0]);
    }
  }
}

TEST_F(ClearUavSpec, ClearTextureRectDoesNotAffectOutside) {
  auto texture = CreateTexture(8, 6);
  auto heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true);
  ASSERT_TRUE(texture);
  ASSERT_TRUE(heap);
  D3D12_UNORDERED_ACCESS_VIEW_DESC desc = {};
  desc.Format = DXGI_FORMAT_R32_UINT;
  desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
  const auto cpu = heap->GetCPUDescriptorHandleForHeapStart();
  context_.device()->CreateUnorderedAccessView(texture.get(), nullptr, &desc,
                                               cpu);
  ID3D12DescriptorHeap *heaps[] = {heap.get()};
  context_.list()->SetDescriptorHeaps(1, heaps);
  constexpr std::array<UINT, 4> baseline = {7, 0, 0, 0};
  constexpr std::array<UINT, 4> clear = {42, 0, 0, 0};
  constexpr D3D12_RECT rect = {2, 1, 6, 5};
  const auto gpu = heap->GetGPUDescriptorHandleForHeapStart();
  context_.list()->ClearUnorderedAccessViewUint(gpu, cpu, texture.get(),
                                                baseline.data(), 0, nullptr);
  context_.list()->ClearUnorderedAccessViewUint(gpu, cpu, texture.get(),
                                                clear.data(), 1, &rect);
  D3D12TestContext::Transition(context_.list(), texture.get(),
                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);

  TextureReadback readback;
  ASSERT_TRUE(SUCCEEDED(ReadbackAndReset(texture.get(), &readback)));
  for (UINT y = 0; y < readback.height; ++y) {
    for (UINT x = 0; x < readback.width; ++x) {
      const bool inside = x >= 2 && x < 6 && y >= 1 && y < 5;
      EXPECT_EQ(UintPixel(readback, x, y), inside ? clear[0] : baseline[0]);
    }
  }
}

TEST_F(ClearUavSpec, ClearWithDistinctCpuAndGpuDescriptors) {
  auto texture = CreateTexture(4, 4);
  auto gpu_heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true);
  auto cpu_heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, false);
  ASSERT_TRUE(texture);
  ASSERT_TRUE(gpu_heap);
  ASSERT_TRUE(cpu_heap);
  D3D12_UNORDERED_ACCESS_VIEW_DESC desc = {};
  desc.Format = DXGI_FORMAT_R32_UINT;
  desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
  const auto gpu_cpu = gpu_heap->GetCPUDescriptorHandleForHeapStart();
  const auto cpu = cpu_heap->GetCPUDescriptorHandleForHeapStart();
  context_.device()->CreateUnorderedAccessView(texture.get(), nullptr, &desc,
                                               gpu_cpu);
  context_.device()->CreateUnorderedAccessView(texture.get(), nullptr, &desc,
                                               cpu);
  ID3D12DescriptorHeap *heaps[] = {gpu_heap.get()};
  context_.list()->SetDescriptorHeaps(1, heaps);
  constexpr std::array<UINT, 4> clear = {0x55667788, 0, 0, 0};
  context_.list()->ClearUnorderedAccessViewUint(
      gpu_heap->GetGPUDescriptorHandleForHeapStart(), cpu, texture.get(),
      clear.data(), 0, nullptr);
  D3D12TestContext::Transition(context_.list(), texture.get(),
                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);

  TextureReadback readback;
  ASSERT_TRUE(SUCCEEDED(ReadbackAndReset(texture.get(), &readback)));
  for (UINT y = 0; y < readback.height; ++y) {
    for (UINT x = 0; x < readback.width; ++x)
      EXPECT_EQ(UintPixel(readback, x, y), clear[0]);
  }
}

} // namespace
