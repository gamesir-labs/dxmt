#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <thread>
#include <utility>
#include <vector>

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;
using dxmt::test::TextureReadback;

D3D12_RESOURCE_DESC BufferDesc(UINT64 size) {
  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  desc.Width = size;
  desc.Height = 1;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.Format = DXGI_FORMAT_UNKNOWN;
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  return desc;
}

D3D12_RESOURCE_DESC Texture2DDesc(UINT64 width, UINT height,
                                 DXGI_FORMAT format,
                                 D3D12_RESOURCE_FLAGS flags) {
  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width = width;
  desc.Height = height;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.Format = format;
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  desc.Flags = flags;
  return desc;
}

ComPtr<ID3D12Heap> CreateHeap(ID3D12Device *device, UINT64 size,
                              D3D12_HEAP_TYPE type,
                              D3D12_HEAP_FLAGS flags) {
  D3D12_HEAP_DESC desc = {};
  desc.SizeInBytes = size;
  desc.Properties.Type = type;
  desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
  desc.Flags = flags;
  ComPtr<ID3D12Heap> heap;
  HRESULT hr = device->CreateHeap(
      &desc, __uuidof(ID3D12Heap), reinterpret_cast<void **>(heap.put()));
  return SUCCEEDED(hr) ? std::move(heap) : ComPtr<ID3D12Heap>();
}

ComPtr<ID3D12Resource> CreatePlacedResource(
    ID3D12Device *device, ID3D12Heap *heap, UINT64 offset,
    const D3D12_RESOURCE_DESC &desc, D3D12_RESOURCE_STATES initial_state,
    const D3D12_CLEAR_VALUE *clear_value = nullptr) {
  ComPtr<ID3D12Resource> resource;
  HRESULT hr = device->CreatePlacedResource(
      heap, offset, &desc, initial_state, clear_value,
      __uuidof(ID3D12Resource), reinterpret_cast<void **>(resource.put()));
  return SUCCEEDED(hr) ? std::move(resource) : ComPtr<ID3D12Resource>();
}

class D3D12ResourceSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_TRUE(SUCCEEDED(context_.Initialize()));
  }

  D3D12TestContext context_;
};

TEST_F(D3D12ResourceSpec, ComputesArrayMipCopyableFootprintsExactly) {
  D3D12_RESOURCE_DESC desc = Texture2DDesc(
      17, 9, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_NONE);
  desc.DepthOrArraySize = 2;
  desc.MipLevels = 3;
  std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT, 6> layouts = {};
  std::array<UINT, 6> rows = {};
  std::array<UINT64, 6> row_sizes = {};
  UINT64 total_bytes = 0;

  context_.device()->GetCopyableFootprints(
      &desc, 0, static_cast<UINT>(layouts.size()), 0, layouts.data(),
      rows.data(), row_sizes.data(), &total_bytes);

  constexpr std::array<UINT64, 6> expected_offsets = {
      0, 2560, 3584, 4096, 6656, 7680};
  constexpr std::array<UINT, 6> expected_widths = {17, 8, 4, 17, 8, 4};
  constexpr std::array<UINT, 6> expected_heights = {9, 4, 2, 9, 4, 2};
  constexpr std::array<UINT64, 6> expected_row_sizes = {68, 32, 16,
                                                        68, 32, 16};
  for (std::size_t i = 0; i < layouts.size(); ++i) {
    EXPECT_EQ(layouts[i].Offset, expected_offsets[i]) << "subresource " << i;
    EXPECT_EQ(layouts[i].Footprint.Format, DXGI_FORMAT_R8G8B8A8_UNORM);
    EXPECT_EQ(layouts[i].Footprint.Width, expected_widths[i]);
    EXPECT_EQ(layouts[i].Footprint.Height, expected_heights[i]);
    EXPECT_EQ(layouts[i].Footprint.Depth, 1u);
    EXPECT_EQ(layouts[i].Footprint.RowPitch,
              D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
    EXPECT_EQ(rows[i], expected_heights[i]);
    EXPECT_EQ(row_sizes[i], expected_row_sizes[i]);
  }
  EXPECT_EQ(total_bytes, 7952u);
}

TEST_F(D3D12ResourceSpec,
       ComputesBlockCompressedThreeDimensionalFootprintsExactly) {
  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
  desc.Width = 7;
  desc.Height = 5;
  desc.DepthOrArraySize = 3;
  desc.MipLevels = 3;
  desc.Format = DXGI_FORMAT_BC1_UNORM;
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT, 3> layouts = {};
  std::array<UINT, 3> rows = {};
  std::array<UINT64, 3> row_sizes = {};
  UINT64 total_bytes = 0;

  context_.device()->GetCopyableFootprints(
      &desc, 0, static_cast<UINT>(layouts.size()), 0, layouts.data(),
      rows.data(), row_sizes.data(), &total_bytes);

  constexpr std::array<UINT64, 3> expected_offsets = {0, 1536, 2048};
  constexpr std::array<UINT, 3> expected_widths = {8, 4, 4};
  constexpr std::array<UINT, 3> expected_heights = {8, 4, 4};
  constexpr std::array<UINT, 3> expected_depths = {3, 1, 1};
  constexpr std::array<UINT, 3> expected_rows = {2, 1, 1};
  constexpr std::array<UINT64, 3> expected_row_sizes = {16, 8, 8};
  for (std::size_t i = 0; i < layouts.size(); ++i) {
    EXPECT_EQ(layouts[i].Offset, expected_offsets[i]) << "mip " << i;
    EXPECT_EQ(layouts[i].Footprint.Format, DXGI_FORMAT_BC1_UNORM);
    EXPECT_EQ(layouts[i].Footprint.Width, expected_widths[i]);
    EXPECT_EQ(layouts[i].Footprint.Height, expected_heights[i]);
    EXPECT_EQ(layouts[i].Footprint.Depth, expected_depths[i]);
    EXPECT_EQ(layouts[i].Footprint.RowPitch,
              D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
    EXPECT_EQ(rows[i], expected_rows[i]);
    EXPECT_EQ(row_sizes[i], expected_row_sizes[i]);
  }
  EXPECT_EQ(total_bytes, 2056u);
}

TEST_F(D3D12ResourceSpec,
       InvalidCopyableFootprintRangeFillsEveryOutputWithSentinels) {
  D3D12_RESOURCE_DESC desc = Texture2DDesc(
      16, 8, DXGI_FORMAT_R8_UNORM, D3D12_RESOURCE_FLAG_NONE);
  desc.DepthOrArraySize = 2;
  desc.MipLevels = 3;
  std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT, 2> layouts = {};
  std::array<UINT, 2> rows = {};
  std::array<UINT64, 2> row_sizes = {};
  UINT64 total_bytes = 0;

  context_.device()->GetCopyableFootprints(
      &desc, 5, static_cast<UINT>(layouts.size()), 0, layouts.data(),
      rows.data(), row_sizes.data(), &total_bytes);

  for (std::size_t i = 0; i < layouts.size(); ++i) {
    EXPECT_EQ(layouts[i].Offset, UINT64_MAX);
    EXPECT_EQ(static_cast<UINT>(layouts[i].Footprint.Format), ~UINT{0});
    EXPECT_EQ(layouts[i].Footprint.Width, ~UINT{0});
    EXPECT_EQ(layouts[i].Footprint.Height, ~UINT{0});
    EXPECT_EQ(layouts[i].Footprint.Depth, ~UINT{0});
    EXPECT_EQ(layouts[i].Footprint.RowPitch, ~UINT{0});
    EXPECT_EQ(rows[i], ~UINT{0});
    EXPECT_EQ(row_sizes[i], UINT64_MAX);
  }
  EXPECT_EQ(total_bytes, UINT64_MAX);
}

TEST_F(D3D12ResourceSpec, CopiesPaddedThreeDimensionalSubresourceRows) {
  D3D12_FEATURE_DATA_ARCHITECTURE architecture = {};
  if (FAILED(context_.device()->CheckFeatureSupport(
          D3D12_FEATURE_ARCHITECTURE, &architecture,
          sizeof(architecture))) ||
      !architecture.UMA)
    GTEST_SKIP() << "CPU-visible custom textures require UMA";

  D3D12_HEAP_PROPERTIES heap_properties = {};
  heap_properties.Type = D3D12_HEAP_TYPE_CUSTOM;
  heap_properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_BACK;
  heap_properties.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;
  heap_properties.CreationNodeMask = 1;
  heap_properties.VisibleNodeMask = 1;
  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
  desc.Width = 7;
  desc.Height = 5;
  desc.DepthOrArraySize = 3;
  desc.MipLevels = 1;
  desc.Format = DXGI_FORMAT_R8_UINT;
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

  ComPtr<ID3D12Resource> texture;
  ASSERT_TRUE(SUCCEEDED(context_.device()->CreateCommittedResource(
      &heap_properties, D3D12_HEAP_FLAG_NONE, &desc,
      D3D12_RESOURCE_STATE_COMMON, nullptr, __uuidof(ID3D12Resource),
      reinterpret_cast<void **>(texture.put()))));
  ASSERT_TRUE(texture);
  ASSERT_TRUE(SUCCEEDED(texture->Map(0, nullptr, nullptr)));

  constexpr UINT source_row_pitch = 11;
  constexpr UINT source_slice_pitch = 64;
  constexpr UINT destination_row_pitch = 13;
  constexpr UINT destination_slice_pitch = 80;
  std::array<std::uint8_t, source_slice_pitch * 3> source = {};
  std::array<std::uint8_t, destination_slice_pitch * 3> destination = {};
  for (UINT z = 0; z < desc.DepthOrArraySize; ++z) {
    for (UINT y = 0; y < desc.Height; ++y) {
      for (UINT x = 0; x < desc.Width; ++x) {
        source[z * source_slice_pitch + y * source_row_pitch + x] =
            static_cast<std::uint8_t>(1 + x + 11 * y + 37 * z);
      }
    }
  }

  ASSERT_TRUE(SUCCEEDED(texture->WriteToSubresource(
      0, nullptr, source.data(), source_row_pitch, source_slice_pitch)));
  ASSERT_TRUE(SUCCEEDED(texture->ReadFromSubresource(
      destination.data(), destination_row_pitch, destination_slice_pitch, 0,
      nullptr)));
  for (UINT z = 0; z < desc.DepthOrArraySize; ++z) {
    for (UINT y = 0; y < desc.Height; ++y) {
      for (UINT x = 0; x < desc.Width; ++x) {
        EXPECT_EQ(destination[z * destination_slice_pitch +
                              y * destination_row_pitch + x],
                  source[z * source_slice_pitch + y * source_row_pitch + x]);
      }
    }
  }
  texture->Unmap(0, nullptr);
}

TEST_F(D3D12ResourceSpec,
       HonorsEmptyBoxesAndRejectsInvalidSubresourceCopies) {
  D3D12_FEATURE_DATA_ARCHITECTURE architecture = {};
  if (FAILED(context_.device()->CheckFeatureSupport(
          D3D12_FEATURE_ARCHITECTURE, &architecture,
          sizeof(architecture))) ||
      !architecture.UMA)
    GTEST_SKIP() << "CPU-visible custom textures require UMA";

  D3D12_HEAP_PROPERTIES heap_properties = {};
  heap_properties.Type = D3D12_HEAP_TYPE_CUSTOM;
  heap_properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_BACK;
  heap_properties.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;
  heap_properties.CreationNodeMask = 1;
  heap_properties.VisibleNodeMask = 1;
  constexpr UINT width = 4;
  constexpr UINT height = 3;
  const D3D12_RESOURCE_DESC desc = Texture2DDesc(
      width, height, DXGI_FORMAT_R8_UINT, D3D12_RESOURCE_FLAG_NONE);
  ComPtr<ID3D12Resource> texture;
  ASSERT_EQ(context_.device()->CreateCommittedResource(
                &heap_properties, D3D12_HEAP_FLAG_NONE, &desc,
                D3D12_RESOURCE_STATE_COMMON, nullptr,
                __uuidof(ID3D12Resource),
                reinterpret_cast<void **>(texture.put())),
            S_OK);
  ASSERT_TRUE(texture);
  ASSERT_EQ(texture->Map(0, nullptr, nullptr), S_OK);

  std::array<std::uint8_t, width * height> baseline = {};
  for (UINT i = 0; i < baseline.size(); ++i)
    baseline[i] = static_cast<std::uint8_t>(i + 1);
  ASSERT_EQ(texture->WriteToSubresource(
                0, nullptr, baseline.data(), width, baseline.size()),
            S_OK);

  std::array<std::uint8_t, width * height> poison = {};
  poison.fill(0xff);
  constexpr std::array<D3D12_BOX, 3> empty_boxes = {
      D3D12_BOX{1, 0, 0, 1, height, 1},
      D3D12_BOX{0, 2, 0, width, 2, 1},
      D3D12_BOX{0, 0, 0, width, height, 0},
  };
  for (const D3D12_BOX &box : empty_boxes) {
    EXPECT_EQ(texture->WriteToSubresource(
                  0, &box, poison.data(), width, poison.size()),
              S_OK);
    std::array<std::uint8_t, width * height> empty_read = {};
    empty_read.fill(0xa5);
    const auto expected_empty_read = empty_read;
    EXPECT_EQ(texture->ReadFromSubresource(
                  empty_read.data(), width, empty_read.size(), 0, &box),
              S_OK);
    EXPECT_EQ(empty_read, expected_empty_read);
  }

  constexpr D3D12_BOX out_of_bounds = {0, 0, 0, width + 1, height, 1};
  EXPECT_EQ(texture->WriteToSubresource(
                0, nullptr, nullptr, width, baseline.size()),
            E_POINTER);
  EXPECT_EQ(texture->ReadFromSubresource(
                nullptr, width, baseline.size(), 0, nullptr),
            E_POINTER);
  EXPECT_EQ(texture->WriteToSubresource(
                1, nullptr, poison.data(), width, poison.size()),
            E_INVALIDARG);
  EXPECT_EQ(texture->ReadFromSubresource(
                poison.data(), width, poison.size(), 1, nullptr),
            E_INVALIDARG);
  EXPECT_EQ(texture->WriteToSubresource(
                0, &out_of_bounds, poison.data(), width, poison.size()),
            E_INVALIDARG);
  EXPECT_EQ(texture->WriteToSubresource(
                0, nullptr, poison.data(), width - 1, poison.size()),
            E_INVALIDARG);
  EXPECT_EQ(texture->WriteToSubresource(
                0, nullptr, poison.data(), width, baseline.size() - 1),
            E_INVALIDARG);

  std::array<std::uint8_t, width * height> readback = {};
  readback.fill(0xa5);
  const auto failed_read_sentinel = readback;
  EXPECT_EQ(texture->ReadFromSubresource(
                readback.data(), width, readback.size(), 0, &out_of_bounds),
            E_INVALIDARG);
  EXPECT_EQ(readback, failed_read_sentinel);
  EXPECT_EQ(texture->ReadFromSubresource(
                readback.data(), width - 1, readback.size(), 0, nullptr),
            E_INVALIDARG);
  EXPECT_EQ(readback, failed_read_sentinel);
  EXPECT_EQ(texture->ReadFromSubresource(
                readback.data(), width, readback.size() - 1, 0, nullptr),
            E_INVALIDARG);
  EXPECT_EQ(readback, failed_read_sentinel);

  ASSERT_EQ(texture->ReadFromSubresource(
                readback.data(), width, readback.size(), 0, nullptr),
            S_OK);
  EXPECT_EQ(readback, baseline);
  texture->Unmap(0, nullptr);
}

TEST_F(D3D12ResourceSpec,
       CopiesSubresourcesWhileUnrelatedQueueSubmissionsProgress) {
  D3D12_FEATURE_DATA_ARCHITECTURE architecture = {};
  if (FAILED(context_.device()->CheckFeatureSupport(
          D3D12_FEATURE_ARCHITECTURE, &architecture,
          sizeof(architecture))) ||
      !architecture.UMA)
    GTEST_SKIP() << "CPU-visible custom textures require UMA";

  constexpr UINT width = 32;
  constexpr UINT height = 32;
  constexpr UINT write_count = 32;
  D3D12_HEAP_PROPERTIES heap_properties = {};
  heap_properties.Type = D3D12_HEAP_TYPE_CUSTOM;
  heap_properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_BACK;
  heap_properties.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;
  heap_properties.CreationNodeMask = 1;
  heap_properties.VisibleNodeMask = 1;
  D3D12_RESOURCE_DESC desc = Texture2DDesc(
      width, height, DXGI_FORMAT_R8_UINT, D3D12_RESOURCE_FLAG_NONE);
  desc.MipLevels = 2;
  ComPtr<ID3D12Resource> texture;
  ASSERT_TRUE(SUCCEEDED(context_.device()->CreateCommittedResource(
      &heap_properties, D3D12_HEAP_FLAG_NONE, &desc,
      D3D12_RESOURCE_STATE_COMMON, nullptr, __uuidof(ID3D12Resource),
      reinterpret_cast<void **>(texture.put()))));
  ASSERT_TRUE(SUCCEEDED(texture->Map(0, nullptr, nullptr)));

  std::array<std::uint8_t, width * height> source = {};
  for (UINT i = 0; i < source.size(); ++i)
    source[i] = static_cast<std::uint8_t>(i * 13u + 7u);
  ComPtr<ID3D12Fence> fence;
  ASSERT_TRUE(SUCCEEDED(context_.device()->CreateFence(
      0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence),
      reinterpret_cast<void **>(fence.put()))));

  std::array<HRESULT, write_count> write_results = {};
  std::atomic_bool ready = false;
  std::atomic_bool start = false;
  std::thread writer([&] {
    ready.store(true, std::memory_order_release);
    while (!start.load(std::memory_order_acquire))
      std::this_thread::yield();
    for (UINT i = 0; i < write_count; ++i)
      write_results[i] = texture->WriteToSubresource(
          0, nullptr, source.data(), width, source.size());
  });

  while (!ready.load(std::memory_order_acquire))
    std::this_thread::yield();
  start.store(true, std::memory_order_release);
  constexpr UINT64 signal_count = 128;
  HRESULT signal_result = S_OK;
  UINT64 last_signal = 0;
  for (UINT64 value = 1; value <= signal_count; ++value) {
    signal_result = context_.queue()->Signal(fence.get(), value);
    if (FAILED(signal_result))
      break;
    last_signal = value;
  }
  const HRESULT wait_result =
      last_signal ? context_.WaitForFence(fence.get(), last_signal) : E_FAIL;
  writer.join();

  ASSERT_TRUE(SUCCEEDED(signal_result));
  ASSERT_TRUE(SUCCEEDED(wait_result));
  for (HRESULT result : write_results)
    EXPECT_TRUE(SUCCEEDED(result));
  std::array<std::uint8_t, width * height> readback = {};
  ASSERT_TRUE(SUCCEEDED(texture->ReadFromSubresource(
      readback.data(), width, readback.size(), 0, nullptr)));
  EXPECT_EQ(readback, source);
  texture->Unmap(0, nullptr);
}

TEST_F(D3D12ResourceSpec, PreservesPlacedTextureContentsAcrossAliasing) {
  constexpr UINT width = 8;
  constexpr UINT height = 8;
  const D3D12_RESOURCE_DESC desc = Texture2DDesc(
      width, height, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_NONE);
  const D3D12_RESOURCE_ALLOCATION_INFO allocation =
      context_.device()->GetResourceAllocationInfo(0, 1, &desc);
  ASSERT_NE(allocation.SizeInBytes, UINT64_MAX);
  ASSERT_NE(allocation.SizeInBytes, 0u);
  ComPtr<ID3D12Heap> heap = CreateHeap(
      context_.device(), allocation.SizeInBytes, D3D12_HEAP_TYPE_DEFAULT,
      D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES);
  ASSERT_TRUE(heap);

  ComPtr<ID3D12Resource> before = CreatePlacedResource(
      context_.device(), heap.get(), 0, desc, D3D12_RESOURCE_STATE_COPY_DEST);
  ComPtr<ID3D12Resource> after = CreatePlacedResource(
      context_.device(), heap.get(), 0, desc, D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(before);
  ASSERT_TRUE(after);

  std::array<std::uint32_t, width * height> expected = {};
  for (UINT y = 0; y < height; ++y) {
    for (UINT x = 0; x < width; ++x)
      expected[y * width + x] = 0xff000000u | (y << 12) | (x << 4) | 3u;
  }
  D3D12_RESOURCE_BARRIER activation = {};
  activation.Type = D3D12_RESOURCE_BARRIER_TYPE_ALIASING;
  activation.Aliasing.pResourceBefore = nullptr;
  activation.Aliasing.pResourceAfter = before.get();
  context_.list()->ResourceBarrier(1, &activation);
  ASSERT_TRUE(SUCCEEDED(context_.UploadTextureAndReset(
      before.get(), expected.data(), width * sizeof(std::uint32_t),
      sizeof(expected))));

  D3D12_RESOURCE_BARRIER aliasing = {};
  aliasing.Type = D3D12_RESOURCE_BARRIER_TYPE_ALIASING;
  aliasing.Aliasing.pResourceBefore = before.get();
  aliasing.Aliasing.pResourceAfter = after.get();
  context_.list()->ResourceBarrier(1, &aliasing);
  ASSERT_TRUE(SUCCEEDED(context_.ExecuteAndWait()));
  ASSERT_TRUE(SUCCEEDED(context_.ResetCommandList()));
  D3D12TestContext::Transition(
      context_.list(), after.get(), D3D12_RESOURCE_STATE_COPY_DEST,
      D3D12_RESOURCE_STATE_COPY_SOURCE);

  TextureReadback readback;
  ASSERT_TRUE(SUCCEEDED(context_.ReadbackTexture(after.get(), &readback)));
  ASSERT_EQ(readback.width, width);
  ASSERT_EQ(readback.height, height);
  for (UINT y = 0; y < height; ++y) {
    EXPECT_EQ(std::memcmp(readback.data.data() + y * readback.row_pitch,
                          expected.data() + y * width,
                          width * sizeof(std::uint32_t)),
              0);
  }
}

TEST_F(D3D12ResourceSpec,
       CreatesFullMipPlacedTextureFromAllocationInfoSizedHeap) {
  D3D12_RESOURCE_DESC desc = Texture2DDesc(
      1024, 1024, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_NONE);
  desc.MipLevels = 0;

  const D3D12_RESOURCE_ALLOCATION_INFO allocation =
      context_.device()->GetResourceAllocationInfo(0, 1, &desc);
  ASSERT_NE(allocation.SizeInBytes, UINT64_MAX);
  ASSERT_NE(allocation.SizeInBytes, 0u);
  ASSERT_NE(allocation.Alignment, 0u);

  ComPtr<ID3D12Heap> heap = CreateHeap(
      context_.device(), allocation.SizeInBytes, D3D12_HEAP_TYPE_DEFAULT,
      D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES);
  ASSERT_TRUE(heap);

  ComPtr<ID3D12Resource> resource = CreatePlacedResource(
      context_.device(), heap.get(), 0, desc, D3D12_RESOURCE_STATE_COMMON);
  ASSERT_TRUE(resource);
  EXPECT_EQ(resource->GetDesc().MipLevels, 11u);
}

TEST_F(D3D12ResourceSpec,
       RejectsTexturesOnAbstractedCpuVisibleHeaps) {
  constexpr UINT width = 17;
  constexpr UINT height = 3;
  D3D12_RESOURCE_DESC desc = Texture2DDesc(
      width, height, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_NONE);

  const D3D12_RESOURCE_ALLOCATION_INFO allocation =
      context_.device()->GetResourceAllocationInfo(0, 1, &desc);
  ASSERT_NE(allocation.SizeInBytes, UINT64_MAX);
  ASSERT_NE(allocation.SizeInBytes, 0u);

  for (const auto [heap_type, initial_state] : {
           std::pair{D3D12_HEAP_TYPE_UPLOAD,
                     D3D12_RESOURCE_STATE_GENERIC_READ},
           std::pair{D3D12_HEAP_TYPE_READBACK,
                     D3D12_RESOURCE_STATE_COPY_DEST},
       }) {
    D3D12_HEAP_PROPERTIES properties = {};
    properties.Type = heap_type;
    properties.CreationNodeMask = 1;
    properties.VisibleNodeMask = 1;
    ComPtr<ID3D12Resource> committed;
    EXPECT_TRUE(FAILED(context_.device()->CreateCommittedResource(
        &properties, D3D12_HEAP_FLAG_NONE, &desc, initial_state, nullptr,
        __uuidof(ID3D12Resource),
        reinterpret_cast<void **>(committed.put()))));

    ComPtr<ID3D12Heap> heap = CreateHeap(
        context_.device(), allocation.SizeInBytes, heap_type,
        D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS);
    ASSERT_TRUE(heap);
    ComPtr<ID3D12Resource> placed = CreatePlacedResource(
        context_.device(), heap.get(), 0, desc, initial_state);
    EXPECT_FALSE(placed);
  }
}

TEST_F(D3D12ResourceSpec,
       RejectsRowMajorTexturesUntilCrossAdapterBackingIsImplemented) {
  D3D12_RESOURCE_DESC desc = Texture2DDesc(
      64, 32, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_NONE);
  desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  const auto allocation =
      context_.device()->GetResourceAllocationInfo(0, 1, &desc);
  ASSERT_NE(allocation.SizeInBytes, UINT64_MAX);

  D3D12_HEAP_PROPERTIES properties = {};
  properties.Type = D3D12_HEAP_TYPE_DEFAULT;
  properties.CreationNodeMask = 1;
  properties.VisibleNodeMask = 1;
  ComPtr<ID3D12Resource> committed;
  EXPECT_TRUE(FAILED(context_.device()->CreateCommittedResource(
      &properties, D3D12_HEAP_FLAG_NONE, &desc,
      D3D12_RESOURCE_STATE_COMMON, nullptr,
      __uuidof(ID3D12Resource),
      reinterpret_cast<void **>(committed.put()))));

  auto heap = CreateHeap(context_.device(), allocation.SizeInBytes,
                         D3D12_HEAP_TYPE_DEFAULT,
                         D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES);
  ASSERT_TRUE(heap);
  EXPECT_FALSE(CreatePlacedResource(context_.device(), heap.get(), 0, desc,
                                    D3D12_RESOURCE_STATE_COMMON));

  desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER;
  properties.Type = D3D12_HEAP_TYPE_CUSTOM;
  properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_NOT_AVAILABLE;
  properties.MemoryPoolPreference = D3D12_MEMORY_POOL_L1;
  constexpr D3D12_HEAP_FLAGS shared_cross_adapter =
      D3D12_HEAP_FLAG_SHARED | D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER;
  EXPECT_TRUE(FAILED(context_.device()->CreateCommittedResource(
      &properties, shared_cross_adapter, &desc,
      D3D12_RESOURCE_STATE_COMMON, nullptr,
      __uuidof(ID3D12Resource),
      reinterpret_cast<void **>(committed.put()))));

  D3D12_HEAP_DESC heap_desc = {};
  heap_desc.SizeInBytes = allocation.SizeInBytes;
  heap_desc.Properties = properties;
  heap_desc.Alignment = allocation.Alignment;
  heap_desc.Flags = static_cast<D3D12_HEAP_FLAGS>(
      shared_cross_adapter | D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES);
  ComPtr<ID3D12Heap> cross_adapter_heap;
  ASSERT_TRUE(SUCCEEDED(context_.device()->CreateHeap(
      &heap_desc, __uuidof(ID3D12Heap),
      reinterpret_cast<void **>(cross_adapter_heap.put()))));
  EXPECT_FALSE(CreatePlacedResource(context_.device(), cross_adapter_heap.get(),
                                    0, desc, D3D12_RESOURCE_STATE_COMMON));
}

TEST_F(D3D12ResourceSpec, MapsPlacedUploadBufferAtNonzeroHeapOffset) {
  constexpr UINT64 heap_size = 2 * D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
  constexpr UINT64 resource_size = 4096;
  ComPtr<ID3D12Heap> heap = CreateHeap(
      context_.device(), heap_size, D3D12_HEAP_TYPE_UPLOAD,
      D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS);
  ASSERT_TRUE(heap);
  const D3D12_RESOURCE_DESC desc = BufferDesc(resource_size);
  ComPtr<ID3D12Resource> resource = CreatePlacedResource(
      context_.device(), heap.get(), D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT,
      desc, D3D12_RESOURCE_STATE_GENERIC_READ);
  ASSERT_TRUE(resource);

  std::array<std::uint32_t, 32> expected = {};
  for (std::size_t i = 0; i < expected.size(); ++i)
    expected[i] = 0x81230000u + static_cast<std::uint32_t>(i);
  void *mapped = nullptr;
  D3D12_RANGE read_range = {0, 0};
  ASSERT_TRUE(SUCCEEDED(resource->Map(0, &read_range, &mapped)));
  ASSERT_NE(mapped, nullptr);
  std::memcpy(mapped, expected.data(), sizeof(expected));
  D3D12_RANGE written_range = {0, sizeof(expected)};
  resource->Unmap(0, &written_range);

  std::vector<std::uint8_t> actual;
  ASSERT_TRUE(SUCCEEDED(
      context_.ReadbackBuffer(resource.get(), sizeof(expected), &actual)));
  ASSERT_EQ(actual.size(), sizeof(expected));
  EXPECT_EQ(std::memcmp(actual.data(), expected.data(), sizeof(expected)), 0);
}

TEST_F(D3D12ResourceSpec, ReportsCommittedBufferHeapPropertiesAndMapRules) {
  struct HeapCase {
    D3D12_HEAP_TYPE type;
    D3D12_RESOURCE_STATES state;
    bool mappable;
  };
  constexpr std::array cases = {
      HeapCase{D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COMMON, false},
      HeapCase{D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ,
               true},
      HeapCase{D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST,
               true},
  };

  for (const auto &test_case : cases) {
    auto resource = context_.CreateBuffer(
        4096, test_case.type, D3D12_RESOURCE_FLAG_NONE, test_case.state);
    ASSERT_TRUE(resource) << "heap type " << test_case.type;
    D3D12_HEAP_PROPERTIES properties = {};
    D3D12_HEAP_FLAGS flags = static_cast<D3D12_HEAP_FLAGS>(~UINT{0});
    ASSERT_TRUE(SUCCEEDED(resource->GetHeapProperties(&properties, &flags)));
    EXPECT_EQ(properties.Type, test_case.type);
    EXPECT_EQ(properties.CreationNodeMask, 0u);
    EXPECT_EQ(properties.VisibleNodeMask, 0u);
    EXPECT_EQ(flags, D3D12_HEAP_FLAG_NONE);

    void *mapping = reinterpret_cast<void *>(uintptr_t{1});
    const HRESULT map_result = resource->Map(0, nullptr, &mapping);
    if (test_case.mappable) {
      EXPECT_TRUE(SUCCEEDED(map_result));
      EXPECT_NE(mapping, nullptr);
      resource->Unmap(0, nullptr);
    } else {
      EXPECT_TRUE(FAILED(map_result));
      EXPECT_EQ(mapping, nullptr);
    }

    mapping = reinterpret_cast<void *>(uintptr_t{1});
    EXPECT_EQ(resource->Map(1, nullptr, &mapping), E_INVALIDARG);
    EXPECT_EQ(mapping, nullptr);
  }
}

TEST_F(D3D12ResourceSpec, HeapPropertiesOutputsAreIndependentlyOptional) {
  auto resource = context_.CreateBuffer(
      4096, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_GENERIC_READ);
  ASSERT_TRUE(resource);

  D3D12_HEAP_PROPERTIES properties = {};
  EXPECT_EQ(resource->GetHeapProperties(&properties, nullptr), S_OK);
  EXPECT_EQ(properties.Type, D3D12_HEAP_TYPE_UPLOAD);

  D3D12_HEAP_FLAGS flags = static_cast<D3D12_HEAP_FLAGS>(UINT_MAX);
  EXPECT_EQ(resource->GetHeapProperties(nullptr, &flags), S_OK);
  EXPECT_EQ(flags, D3D12_HEAP_FLAG_NONE);
  EXPECT_EQ(resource->GetHeapProperties(nullptr, nullptr), S_OK);
}

TEST_F(D3D12ResourceSpec, PlacedResourceReportsOwningHeapProperties) {
  auto heap = CreateHeap(
      context_.device(), D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT,
      D3D12_HEAP_TYPE_UPLOAD, D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS);
  ASSERT_TRUE(heap);
  auto resource = CreatePlacedResource(
      context_.device(), heap.get(), 0, BufferDesc(4096),
      D3D12_RESOURCE_STATE_GENERIC_READ);
  ASSERT_TRUE(resource);

  D3D12_HEAP_PROPERTIES properties = {};
  D3D12_HEAP_FLAGS flags = D3D12_HEAP_FLAG_NONE;
  ASSERT_EQ(resource->GetHeapProperties(&properties, &flags), S_OK);
  const auto heap_desc = heap->GetDesc();
  EXPECT_EQ(properties.Type, heap_desc.Properties.Type);
  EXPECT_EQ(properties.CPUPageProperty,
            heap_desc.Properties.CPUPageProperty);
  EXPECT_EQ(properties.MemoryPoolPreference,
            heap_desc.Properties.MemoryPoolPreference);
  EXPECT_EQ(properties.CreationNodeMask,
            heap_desc.Properties.CreationNodeMask);
  EXPECT_EQ(properties.VisibleNodeMask,
            heap_desc.Properties.VisibleNodeMask);
  EXPECT_EQ(flags, heap_desc.Flags);
}

TEST_F(D3D12ResourceSpec, ReservedResourceRejectsHeapPropertyQueries) {
  D3D12_FEATURE_DATA_D3D12_OPTIONS options = {};
  ASSERT_EQ(context_.device()->CheckFeatureSupport(
                D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options)),
            S_OK);
  if (options.TiledResourcesTier < D3D12_TILED_RESOURCES_TIER_1)
    GTEST_SKIP() << "Tiled resources are not supported";

  auto desc = BufferDesc(D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);
  ComPtr<ID3D12Resource> resource;
  ASSERT_EQ(context_.device()->CreateReservedResource(
                &desc, D3D12_RESOURCE_STATE_COMMON, nullptr,
                IID_PPV_ARGS(resource.put())),
            S_OK);
  ASSERT_TRUE(resource);

  D3D12_HEAP_PROPERTIES properties = {};
  properties.Type = D3D12_HEAP_TYPE_CUSTOM;
  properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_BACK;
  properties.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;
  properties.CreationNodeMask = 0x1234;
  properties.VisibleNodeMask = 0x5678;
  D3D12_HEAP_FLAGS flags = D3D12_HEAP_FLAG_SHARED;

  EXPECT_EQ(resource->GetHeapProperties(&properties, &flags), E_INVALIDARG);
  EXPECT_EQ(properties.Type, D3D12_HEAP_TYPE_CUSTOM);
  EXPECT_EQ(properties.CPUPageProperty,
            D3D12_CPU_PAGE_PROPERTY_WRITE_BACK);
  EXPECT_EQ(properties.MemoryPoolPreference, D3D12_MEMORY_POOL_L0);
  EXPECT_EQ(properties.CreationNodeMask, 0x1234u);
  EXPECT_EQ(properties.VisibleNodeMask, 0x5678u);
  EXPECT_EQ(flags, D3D12_HEAP_FLAG_SHARED);
  EXPECT_EQ(resource->GetHeapProperties(nullptr, nullptr), E_INVALIDARG);
}

TEST_F(D3D12ResourceSpec, RoundsPlacedHeapBackendSizeToHeapAlignment) {
  constexpr UINT64 alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
  constexpr UINT64 heap_size = 2 * alignment - 1;
  ComPtr<ID3D12Heap> heap = CreateHeap(
      context_.device(), heap_size, D3D12_HEAP_TYPE_DEFAULT,
      D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS);
  ASSERT_TRUE(heap);
  EXPECT_EQ(heap->GetDesc().SizeInBytes, heap_size);

  const D3D12_RESOURCE_DESC desc = BufferDesc(alignment);
  ComPtr<ID3D12Resource> resource = CreatePlacedResource(
      context_.device(), heap.get(), alignment, desc,
      D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(resource);
}

TEST_F(D3D12ResourceSpec, RejectsPlacedBufferAtSubresourceAlignmentOffset) {
  constexpr UINT64 alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
  ComPtr<ID3D12Heap> heap = CreateHeap(
      context_.device(), 2 * alignment, D3D12_HEAP_TYPE_DEFAULT,
      D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS);
  ASSERT_TRUE(heap);

  const D3D12_RESOURCE_DESC desc = BufferDesc(4096);
  ComPtr<ID3D12Resource> resource;
  const HRESULT hr = context_.device()->CreatePlacedResource(
      heap.get(), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, &desc,
      D3D12_RESOURCE_STATE_COPY_DEST, nullptr, __uuidof(ID3D12Resource),
      reinterpret_cast<void **>(resource.put()));
  EXPECT_TRUE(FAILED(hr));
  EXPECT_FALSE(resource);
}

TEST_F(D3D12ResourceSpec,
       CreatesAndUsesPlacedBuffersConcurrently) {
  constexpr UINT thread_count = 8;
  constexpr UINT64 alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
  ComPtr<ID3D12Heap> heap = CreateHeap(
      context_.device(), thread_count * alignment, D3D12_HEAP_TYPE_DEFAULT,
      D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS);
  ASSERT_TRUE(heap);
  const D3D12_RESOURCE_DESC desc = BufferDesc(4096);
  std::array<ComPtr<ID3D12Resource>, thread_count> resources;
  std::array<HRESULT, thread_count> results = {};
  std::atomic<UINT> ready = 0;
  std::atomic_bool start = false;
  std::vector<std::thread> threads;
  threads.reserve(thread_count);
  for (UINT i = 0; i < thread_count; ++i) {
    threads.emplace_back([&, i] {
      ready.fetch_add(1, std::memory_order_release);
      while (!start.load(std::memory_order_acquire))
        std::this_thread::yield();
      results[i] = context_.device()->CreatePlacedResource(
          heap.get(), i * alignment, &desc, D3D12_RESOURCE_STATE_COPY_DEST,
          nullptr, __uuidof(ID3D12Resource),
          reinterpret_cast<void **>(resources[i].put()));
    });
  }
  while (ready.load(std::memory_order_acquire) != thread_count)
    std::this_thread::yield();
  start.store(true, std::memory_order_release);
  for (auto &thread : threads)
    thread.join();

  for (UINT i = 0; i < thread_count; ++i) {
    EXPECT_TRUE(SUCCEEDED(results[i])) << "thread " << i;
    EXPECT_TRUE(resources[i]) << "thread " << i;
  }
  std::array<std::uint32_t, thread_count> expected = {};
  for (UINT i = 0; i < thread_count; ++i) {
    ASSERT_TRUE(resources[i]);
    const auto address = resources[i]->GetGPUVirtualAddress();
    EXPECT_NE(address, 0u) << "resource " << i;
    EXPECT_EQ(resources[i]->GetGPUVirtualAddress(), address)
        << "resource " << i << " returned an unstable GPU VA";
    for (UINT previous = 0; previous < i; ++previous)
      EXPECT_NE(address, resources[previous]->GetGPUVirtualAddress())
          << "resources " << previous << " and " << i;
    expected[i] = 0x9a120000u + i;
  }

  ComPtr<ID3D12Resource> upload = context_.CreateUploadBuffer(
      sizeof(expected), expected.data(), sizeof(expected));
  ComPtr<ID3D12Resource> readback = context_.CreateBuffer(
      sizeof(expected), D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(upload);
  ASSERT_TRUE(readback);
  for (UINT i = 0; i < thread_count; ++i) {
    context_.list()->CopyBufferRegion(
        resources[i].get(), 0, upload.get(), i * sizeof(expected[i]),
        sizeof(expected[i]));
    D3D12TestContext::Transition(
        context_.list(), resources[i].get(), D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_COPY_SOURCE);
    context_.list()->CopyBufferRegion(
        readback.get(), i * sizeof(expected[i]), resources[i].get(), 0,
        sizeof(expected[i]));
  }
  ASSERT_TRUE(SUCCEEDED(context_.ExecuteAndWait()));

  void *mapped = nullptr;
  D3D12_RANGE read_range = {0, sizeof(expected)};
  ASSERT_TRUE(SUCCEEDED(readback->Map(0, &read_range, &mapped)));
  ASSERT_NE(mapped, nullptr);
  EXPECT_EQ(std::memcmp(mapped, expected.data(), sizeof(expected)), 0);
  D3D12_RANGE written_range = {0, 0};
  readback->Unmap(0, &written_range);
}

TEST_F(D3D12ResourceSpec, CreatesPlacedDepthStencilTexture) {
  D3D12_FEATURE_DATA_FORMAT_SUPPORT format_support = {};
  format_support.Format = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
  ASSERT_TRUE(SUCCEEDED(context_.device()->CheckFeatureSupport(
      D3D12_FEATURE_FORMAT_SUPPORT, &format_support,
      sizeof(format_support))));
  if (!(format_support.Support1 & D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL))
    GTEST_SKIP() << "D32_FLOAT_S8X24 depth/stencil is unsupported";

  const D3D12_RESOURCE_DESC desc = Texture2DDesc(
      32, 32, DXGI_FORMAT_D32_FLOAT_S8X24_UINT,
      D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
  const D3D12_RESOURCE_ALLOCATION_INFO allocation =
      context_.device()->GetResourceAllocationInfo(0, 1, &desc);
  ASSERT_NE(allocation.SizeInBytes, UINT64_MAX);
  ASSERT_NE(allocation.SizeInBytes, 0u);
  ComPtr<ID3D12Heap> heap = CreateHeap(
      context_.device(), allocation.SizeInBytes, D3D12_HEAP_TYPE_DEFAULT,
      D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES);
  ASSERT_TRUE(heap);
  D3D12_CLEAR_VALUE clear_value = {};
  clear_value.Format = desc.Format;
  clear_value.DepthStencil.Depth = 1.0f;
  ComPtr<ID3D12Resource> resource = CreatePlacedResource(
      context_.device(), heap.get(), 0, desc,
      D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear_value);
  ASSERT_TRUE(resource);
}

TEST_F(D3D12ResourceSpec, RejectsUnsupportedSplitPlanePlacementCleanly) {
  const D3D12_RESOURCE_DESC desc = Texture2DDesc(
      64, 64, DXGI_FORMAT_NV12, D3D12_RESOURCE_FLAG_NONE);
  ComPtr<ID3D12Heap> heap = CreateHeap(
      context_.device(), D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT,
      D3D12_HEAP_TYPE_DEFAULT,
      D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES);
  ASSERT_TRUE(heap);
  ComPtr<ID3D12Resource> resource;
  HRESULT hr = context_.device()->CreatePlacedResource(
      heap.get(), 0, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr,
      __uuidof(ID3D12Resource), reinterpret_cast<void **>(resource.put()));
  EXPECT_TRUE(FAILED(hr));
  EXPECT_FALSE(resource);
}

TEST_F(D3D12ResourceSpec, RejectsTextureDescriptionsBeyondD3D12Limits) {
  D3D12_HEAP_PROPERTIES heap_properties = {};
  heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;
  auto expect_rejected = [&](const D3D12_RESOURCE_DESC &desc) {
    ComPtr<ID3D12Resource> resource;
    HRESULT hr = context_.device()->CreateCommittedResource(
        &heap_properties, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_COMMON, nullptr, __uuidof(ID3D12Resource),
        reinterpret_cast<void **>(resource.put()));
    EXPECT_TRUE(FAILED(hr));
    EXPECT_FALSE(resource);
  };

  D3D12_RESOURCE_DESC desc = Texture2DDesc(
      UINT64(D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION) + 1, 1,
      DXGI_FORMAT_R8_UINT, D3D12_RESOURCE_FLAG_NONE);
  expect_rejected(desc);
  desc.Width = 1;
  desc.DepthOrArraySize = D3D12_REQ_TEXTURE2D_ARRAY_AXIS_DIMENSION + 1;
  expect_rejected(desc);
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 2;
  expect_rejected(desc);

  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
  desc.Width = 1;
  desc.Height = 1;
  desc.DepthOrArraySize = D3D12_REQ_TEXTURE3D_U_V_OR_W_DIMENSION + 1;
  desc.MipLevels = 1;
  expect_rejected(desc);
}

TEST_F(D3D12ResourceSpec,
       RejectsAggregateAllocationSizeOverflowAndInvalidatesTrailingEntries) {
  constexpr UINT64 alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
  constexpr UINT64 largest_aligned_size = UINT64_MAX - (alignment - 1);
  const std::array<D3D12_RESOURCE_DESC, 3> descs = {
      BufferDesc(alignment),
      BufferDesc(largest_aligned_size),
      BufferDesc(4096),
  };

  const D3D12_RESOURCE_ALLOCATION_INFO basic =
      context_.device()->GetResourceAllocationInfo(
          0, static_cast<UINT>(descs.size()), descs.data());
  EXPECT_EQ(basic.SizeInBytes, UINT64_MAX);
  EXPECT_EQ(basic.Alignment, alignment);

#ifdef __ID3D12Device4_INTERFACE_DEFINED__
  ComPtr<ID3D12Device4> device4;
  ASSERT_TRUE(SUCCEEDED(context_.device()->QueryInterface(
      __uuidof(ID3D12Device4),
      reinterpret_cast<void **>(device4.put()))));
  std::array<D3D12_RESOURCE_ALLOCATION_INFO1, descs.size()> entries = {};
  const D3D12_RESOURCE_ALLOCATION_INFO detailed =
      device4->GetResourceAllocationInfo1(
          0, static_cast<UINT>(descs.size()), descs.data(), entries.data());

  EXPECT_EQ(detailed.SizeInBytes, UINT64_MAX);
  EXPECT_EQ(detailed.Alignment, alignment);
  EXPECT_EQ(entries[0].Offset, 0u);
  EXPECT_EQ(entries[0].Alignment, alignment);
  EXPECT_EQ(entries[0].SizeInBytes, alignment);
  EXPECT_EQ(entries[1].Offset, UINT64_MAX);
  EXPECT_EQ(entries[1].Alignment, alignment);
  EXPECT_EQ(entries[1].SizeInBytes, UINT64_MAX);
  EXPECT_EQ(entries[2].Offset, UINT64_MAX);
  EXPECT_EQ(entries[2].Alignment, 0u);
  EXPECT_EQ(entries[2].SizeInBytes, UINT64_MAX);
#endif
}

} // namespace
