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
  ComPtr<ID3D12Resource> initial_upload;
  ComPtr<ID3D12Resource> resource;
  ComPtr<ID3D12DescriptorHeap> gpu_heap;
  ComPtr<ID3D12DescriptorHeap> cpu_heap;
};

class ClearUavSpec : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_TRUE(SUCCEEDED(context_.Initialize())); }

  BufferUav CreateBufferUav(const D3D12_UNORDERED_ACCESS_VIEW_DESC &desc,
                            std::span<const std::uint32_t> initial) {
    BufferUav result;
    result.initial_upload = context_.CreateUploadBuffer(
        initial.size_bytes(), initial.data(), initial.size_bytes());
    result.resource =
        context_.CreateBuffer(initial.size_bytes(), D3D12_HEAP_TYPE_DEFAULT,
                              D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                              D3D12_RESOURCE_STATE_COPY_DEST);
    result.gpu_heap = context_.CreateDescriptorHeap(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true);
    result.cpu_heap = context_.CreateDescriptorHeap(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, false);
    if (!result.initial_upload || !result.resource || !result.gpu_heap ||
        !result.cpu_heap)
      return {};
    context_.device()->CreateUnorderedAccessView(
        result.resource.get(), nullptr, &desc,
        result.gpu_heap->GetCPUDescriptorHandleForHeapStart());
    context_.device()->CreateUnorderedAccessView(
        result.resource.get(), nullptr, &desc,
        result.cpu_heap->GetCPUDescriptorHandleForHeapStart());
    context_.list()->CopyBufferRegion(result.resource.get(), 0,
                                      result.initial_upload.get(), 0,
                                      initial.size_bytes());
    D3D12TestContext::Transition(context_.list(), result.resource.get(),
                                 D3D12_RESOURCE_STATE_COPY_DEST,
                                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    return result;
  }

  void ClearAndReadback(const BufferUav &uav,
                        const std::array<UINT, 4> &clear_value,
                        std::vector<std::uint32_t> *actual) {
    ID3D12DescriptorHeap *heaps[] = {uav.gpu_heap.get()};
    context_.list()->SetDescriptorHeaps(1, heaps);
    context_.list()->ClearUnorderedAccessViewUint(
        uav.gpu_heap->GetGPUDescriptorHandleForHeapStart(),
        uav.cpu_heap->GetCPUDescriptorHandleForHeapStart(), uav.resource.get(),
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

  ComPtr<ID3D12Resource> CreateTexture3D(UINT width, UINT height, UINT16 depth,
                                         DXGI_FORMAT format) {
    D3D12_HEAP_PROPERTIES heap_properties = {};
    heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = depth;
    desc.MipLevels = 1;
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

  std::uint32_t UintVoxel(const TextureReadback &readback, UINT x, UINT y,
                          UINT z) {
    std::uint32_t value = 0;
    const std::size_t offset =
        std::size_t(z) * readback.row_pitch * readback.height +
        std::size_t(y) * readback.row_pitch + x * sizeof(value);
    EXPECT_LE(offset + sizeof(value), readback.data.size());
    if (offset + sizeof(value) <= readback.data.size())
      std::memcpy(&value, readback.data.data() + offset, sizeof(value));
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
  ASSERT_TRUE(uav.gpu_heap);
  ASSERT_TRUE(uav.cpu_heap);
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
  ASSERT_TRUE(uav.gpu_heap);
  ASSERT_TRUE(uav.cpu_heap);
  constexpr std::array<FLOAT, 4> clear = {0.25f, 2.0f, 3.0f, 4.0f};
  std::fill(expected.begin() + 2, expected.begin() + 6,
            std::bit_cast<std::uint32_t>(clear[0]));
  ID3D12DescriptorHeap *heaps[] = {uav.gpu_heap.get()};
  context_.list()->SetDescriptorHeaps(1, heaps);
  context_.list()->ClearUnorderedAccessViewFloat(
      uav.gpu_heap->GetGPUDescriptorHandleForHeapStart(),
      uav.cpu_heap->GetCPUDescriptorHandleForHeapStart(), uav.resource.get(),
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

TEST_F(ClearUavSpec, ClearTypedFloat2Buffer) {
  std::array<std::uint32_t, 8> expected;
  expected.fill(std::bit_cast<std::uint32_t>(9.0f));
  D3D12_UNORDERED_ACCESS_VIEW_DESC desc = {};
  desc.Format = DXGI_FORMAT_R32G32_FLOAT;
  desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  desc.Buffer.FirstElement = 1;
  desc.Buffer.NumElements = 2;
  auto uav = CreateBufferUav(desc, expected);
  ASSERT_TRUE(uav.resource);
  ASSERT_TRUE(uav.gpu_heap);
  ASSERT_TRUE(uav.cpu_heap);
  constexpr std::array<FLOAT, 4> clear = {0.25f, -2.0f, 3.0f, 4.0f};
  for (UINT element = 1; element < 3; ++element) {
    expected[element * 2] = std::bit_cast<std::uint32_t>(clear[0]);
    expected[element * 2 + 1] = std::bit_cast<std::uint32_t>(clear[1]);
  }
  ID3D12DescriptorHeap *heaps[] = {uav.gpu_heap.get()};
  context_.list()->SetDescriptorHeaps(1, heaps);
  context_.list()->ClearUnorderedAccessViewFloat(
      uav.gpu_heap->GetGPUDescriptorHandleForHeapStart(),
      uav.cpu_heap->GetCPUDescriptorHandleForHeapStart(), uav.resource.get(),
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

TEST_F(ClearUavSpec, ClearUintBufferAtOddElementOffset) {
  std::array<std::uint32_t, 8> expected;
  expected.fill(0xdeadbeef);
  D3D12_UNORDERED_ACCESS_VIEW_DESC desc = {};
  desc.Format = DXGI_FORMAT_R32_UINT;
  desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  desc.Buffer.FirstElement = 1;
  desc.Buffer.NumElements = 4;
  auto uav = CreateBufferUav(desc, expected);
  ASSERT_TRUE(uav.resource);
  ASSERT_TRUE(uav.gpu_heap);
  ASSERT_TRUE(uav.cpu_heap);
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
  ASSERT_TRUE(uav.gpu_heap);
  ASSERT_TRUE(uav.cpu_heap);
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
  desc.Texture2D.MipSlice = 2;
  const auto gpu_cpu = gpu_heap->GetCPUDescriptorHandleForHeapStart();
  const auto cpu = cpu_heap->GetCPUDescriptorHandleForHeapStart();
  context_.device()->CreateUnorderedAccessView(texture.get(), nullptr, &desc,
                                               gpu_cpu);
  context_.device()->CreateUnorderedAccessView(texture.get(), nullptr, &desc,
                                               cpu);
  ID3D12DescriptorHeap *heaps[] = {gpu_heap.get()};
  context_.list()->SetDescriptorHeaps(1, heaps);
  constexpr std::array<UINT, 4> clear = {0x10203040, 2, 3, 4};
  context_.list()->ClearUnorderedAccessViewUint(
      gpu_heap->GetGPUDescriptorHandleForHeapStart(), cpu, texture.get(),
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
  auto gpu_heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true);
  auto cpu_heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, false);
  ASSERT_TRUE(texture);
  ASSERT_TRUE(gpu_heap);
  ASSERT_TRUE(cpu_heap);
  D3D12_UNORDERED_ACCESS_VIEW_DESC desc = {};
  desc.Format = DXGI_FORMAT_R32_FLOAT;
  desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
  const auto gpu_cpu = gpu_heap->GetCPUDescriptorHandleForHeapStart();
  const auto cpu = cpu_heap->GetCPUDescriptorHandleForHeapStart();
  context_.device()->CreateUnorderedAccessView(texture.get(), nullptr, &desc,
                                               gpu_cpu);
  context_.device()->CreateUnorderedAccessView(texture.get(), nullptr, &desc,
                                               cpu);
  ID3D12DescriptorHeap *heaps[] = {gpu_heap.get()};
  context_.list()->SetDescriptorHeaps(1, heaps);
  constexpr std::array<FLOAT, 4> clear = {0.375f, -2.0f, 3.0f, 4.0f};
  context_.list()->ClearUnorderedAccessViewFloat(
      gpu_heap->GetGPUDescriptorHandleForHeapStart(), cpu, texture.get(),
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
  auto gpu_heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, true);
  auto cpu_heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, false);
  ASSERT_TRUE(texture);
  ASSERT_TRUE(gpu_heap);
  ASSERT_TRUE(cpu_heap);
  D3D12_UNORDERED_ACCESS_VIEW_DESC desc = {};
  desc.Format = DXGI_FORMAT_R32_UINT;
  desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
  desc.Texture2DArray.ArraySize = 1;
  const auto first_gpu_cpu = context_.CpuDescriptorHandle(gpu_heap.get(), 0);
  const auto first_cpu = context_.CpuDescriptorHandle(cpu_heap.get(), 0);
  context_.device()->CreateUnorderedAccessView(texture.get(), nullptr, &desc,
                                               first_gpu_cpu);
  context_.device()->CreateUnorderedAccessView(texture.get(), nullptr, &desc,
                                               first_cpu);
  desc.Texture2DArray.FirstArraySlice = 1;
  const auto second_gpu_cpu = context_.CpuDescriptorHandle(gpu_heap.get(), 1);
  const auto second_cpu = context_.CpuDescriptorHandle(cpu_heap.get(), 1);
  context_.device()->CreateUnorderedAccessView(texture.get(), nullptr, &desc,
                                               second_gpu_cpu);
  context_.device()->CreateUnorderedAccessView(texture.get(), nullptr, &desc,
                                               second_cpu);
  ID3D12DescriptorHeap *heaps[] = {gpu_heap.get()};
  context_.list()->SetDescriptorHeaps(1, heaps);
  constexpr std::array<UINT, 4> first_clear = {11, 0, 0, 0};
  constexpr std::array<UINT, 4> second_clear = {22, 0, 0, 0};
  context_.list()->ClearUnorderedAccessViewUint(
      context_.GpuDescriptorHandle(gpu_heap.get(), 0), first_cpu,
      texture.get(),
      first_clear.data(), 0, nullptr);
  context_.list()->ClearUnorderedAccessViewUint(
      context_.GpuDescriptorHandle(gpu_heap.get(), 1), second_cpu,
      texture.get(),
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

TEST_F(ClearUavSpec, ClearTexture3DSliceRange) {
  constexpr UINT kWidth = 3;
  constexpr UINT kHeight = 2;
  constexpr UINT16 kDepth = 4;
  auto texture =
      CreateTexture3D(kWidth, kHeight, kDepth, DXGI_FORMAT_R32_UINT);
  auto gpu_heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, true);
  auto cpu_heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, false);
  ASSERT_TRUE(texture);
  ASSERT_TRUE(gpu_heap);
  ASSERT_TRUE(cpu_heap);

  D3D12_UNORDERED_ACCESS_VIEW_DESC desc = {};
  desc.Format = DXGI_FORMAT_R32_UINT;
  desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
  desc.Texture3D.WSize = kDepth;
  const auto full_gpu_cpu = context_.CpuDescriptorHandle(gpu_heap.get(), 0);
  const auto full_cpu = context_.CpuDescriptorHandle(cpu_heap.get(), 0);
  context_.device()->CreateUnorderedAccessView(texture.get(), nullptr, &desc,
                                               full_gpu_cpu);
  context_.device()->CreateUnorderedAccessView(texture.get(), nullptr, &desc,
                                               full_cpu);
  desc.Texture3D.FirstWSlice = 1;
  desc.Texture3D.WSize = 2;
  const auto middle_gpu_cpu = context_.CpuDescriptorHandle(gpu_heap.get(), 1);
  const auto middle_cpu = context_.CpuDescriptorHandle(cpu_heap.get(), 1);
  context_.device()->CreateUnorderedAccessView(texture.get(), nullptr, &desc,
                                               middle_gpu_cpu);
  context_.device()->CreateUnorderedAccessView(texture.get(), nullptr, &desc,
                                               middle_cpu);

  ID3D12DescriptorHeap *heaps[] = {gpu_heap.get()};
  context_.list()->SetDescriptorHeaps(1, heaps);
  constexpr std::array<UINT, 4> baseline = {7, 0, 0, 0};
  constexpr std::array<UINT, 4> selected = {42, 0, 0, 0};
  context_.list()->ClearUnorderedAccessViewUint(
      context_.GpuDescriptorHandle(gpu_heap.get(), 0), full_cpu,
      texture.get(),
      baseline.data(), 0, nullptr);
  context_.list()->ClearUnorderedAccessViewUint(
      context_.GpuDescriptorHandle(gpu_heap.get(), 1), middle_cpu,
      texture.get(),
      selected.data(), 0, nullptr);
  D3D12TestContext::Transition(context_.list(), texture.get(),
                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);

  TextureReadback readback;
  ASSERT_TRUE(SUCCEEDED(ReadbackAndReset(texture.get(), &readback)));
  ASSERT_EQ(readback.width, kWidth);
  ASSERT_EQ(readback.height, kHeight);
  for (UINT z = 0; z < kDepth; ++z) {
    for (UINT y = 0; y < kHeight; ++y) {
      for (UINT x = 0; x < kWidth; ++x) {
        const UINT expected = z == 1 || z == 2 ? selected[0] : baseline[0];
        EXPECT_EQ(UintVoxel(readback, x, y, z), expected)
            << "voxel (" << x << ", " << y << ", " << z << ")";
      }
    }
  }
}

TEST_F(ClearUavSpec, ClearTextureRectDoesNotAffectOutside) {
  auto texture = CreateTexture(8, 6);
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
  constexpr std::array<UINT, 4> baseline = {7, 0, 0, 0};
  constexpr std::array<UINT, 4> clear = {42, 0, 0, 0};
  constexpr D3D12_RECT rect = {2, 1, 6, 5};
  const auto gpu = gpu_heap->GetGPUDescriptorHandleForHeapStart();
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
