#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

D3D12_RESOURCE_DESC BufferDesc(UINT64 size = 4096) {
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

D3D12_RESOURCE_DESC TextureDesc() {
  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width = 16;
  desc.Height = 16;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  return desc;
}

struct InvalidResourceCase {
  D3D12_RESOURCE_DESC desc;
  const char *name;
};

std::vector<InvalidResourceCase> BuildInvalidResourceCases() {
  std::vector<InvalidResourceCase> cases;
  auto add_buffer = [&](const char *name,
                        void (*mutate)(D3D12_RESOURCE_DESC &)) {
    auto desc = BufferDesc();
    mutate(desc);
    cases.push_back({desc, name});
  };
  auto add_texture = [&](const char *name,
                         void (*mutate)(D3D12_RESOURCE_DESC &)) {
    auto desc = TextureDesc();
    mutate(desc);
    cases.push_back({desc, name});
  };

  add_buffer("BufferZeroWidth", [](auto &d) { d.Width = 0; });
  add_buffer("BufferFormat", [](auto &d) { d.Format = DXGI_FORMAT_R8_UINT; });
  add_buffer("BufferHeight", [](auto &d) { d.Height = 2; });
  add_buffer("BufferDepth", [](auto &d) { d.DepthOrArraySize = 2; });
  add_buffer("BufferMipLevels", [](auto &d) { d.MipLevels = 2; });
  add_buffer("BufferSampleCountZero", [](auto &d) { d.SampleDesc.Count = 0; });
  add_buffer("BufferSampleCountTwo", [](auto &d) { d.SampleDesc.Count = 2; });
  add_buffer("BufferUnknownLayout",
             [](auto &d) { d.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN; });
  add_buffer("BufferRenderTargetFlag",
             [](auto &d) { d.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET; });
  add_buffer("BufferDepthStencilFlag",
             [](auto &d) { d.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL; });
  add_buffer("BufferSimultaneousAccessFlag", [](auto &d) {
    d.Flags = D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
  });

  add_texture("TextureZeroWidth", [](auto &d) { d.Width = 0; });
  add_texture("TextureZeroHeight", [](auto &d) { d.Height = 0; });
  add_texture("TextureZeroArraySize",
              [](auto &d) { d.DepthOrArraySize = 0; });
  add_texture("TextureTooManyMips", [](auto &d) { d.MipLevels = 6; });
  add_texture("TextureSampleCountZero",
              [](auto &d) { d.SampleDesc.Count = 0; });
  add_texture("TextureInvalidSampleCount",
              [](auto &d) { d.SampleDesc.Count = 3; });
  add_texture("TextureMsaaMipChain", [](auto &d) {
    d.SampleDesc.Count = 2;
    d.MipLevels = 2;
  });
  add_texture("TextureUnknownFormat",
              [](auto &d) { d.Format = DXGI_FORMAT_UNKNOWN; });
  add_texture("DepthFormatRenderTargetFlag", [](auto &d) {
    d.Format = DXGI_FORMAT_D32_FLOAT;
    d.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  });
  add_texture("ColorFormatDepthStencilFlag", [](auto &d) {
    d.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
  });
  add_texture("Texture1DHeight", [](auto &d) {
    d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE1D;
    d.Height = 2;
  });
  add_texture("Texture1DBlockCompressed", [](auto &d) {
    d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE1D;
    d.Height = 1;
    d.Format = DXGI_FORMAT_BC1_UNORM;
  });
  return cases;
}

class InvalidResourceDescriptionSpec
    : public ::testing::Test,
      public ::testing::WithParamInterface<InvalidResourceCase> {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.InitializeSharedDevice("invalid-resource-description"),
              S_OK);
  }
  D3D12TestContext context_;
};

TEST_P(InvalidResourceDescriptionSpec,
       CommittedCreationRejectsAndClearsOutput) {
  D3D12_HEAP_PROPERTIES heap = {};
  heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  void *output = reinterpret_cast<void *>(std::uintptr_t{1});
  EXPECT_EQ(context_.device()->CreateCommittedResource(
                &heap, D3D12_HEAP_FLAG_NONE, &GetParam().desc,
                D3D12_RESOURCE_STATE_COMMON, nullptr,
                __uuidof(ID3D12Resource), &output),
            E_INVALIDARG);
  EXPECT_EQ(output, nullptr);
}

TEST_P(InvalidResourceDescriptionSpec,
       AllocationInfoReturnsInvalidSizeSentinel) {
  const auto info = context_.device()->GetResourceAllocationInfo(
      0, 1, &GetParam().desc);
  EXPECT_EQ(info.SizeInBytes, UINT64_MAX);
}

INSTANTIATE_TEST_SUITE_P(
    DescriptionMatrix, InvalidResourceDescriptionSpec,
    ::testing::ValuesIn(BuildInvalidResourceCases()),
    [](const ::testing::TestParamInfo<InvalidResourceCase> &info) {
      return std::string(info.param.name);
    });

struct InvalidHeapCase {
  D3D12_HEAP_DESC desc;
  const char *name;
};

D3D12_HEAP_DESC ValidHeapDesc() {
  D3D12_HEAP_DESC desc = {};
  desc.SizeInBytes = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
  desc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
  desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
  desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
  return desc;
}

std::vector<InvalidHeapCase> BuildInvalidHeapCases() {
  std::vector<InvalidHeapCase> cases;
  auto add = [&](const char *name, void (*mutate)(D3D12_HEAP_DESC &)) {
    auto desc = ValidHeapDesc();
    mutate(desc);
    cases.push_back({desc, name});
  };
  add("ZeroSize", [](auto &d) { d.SizeInBytes = 0; });
  add("UnsupportedAlignment", [](auto &d) { d.Alignment = 4096; });
  add("AlignmentTooLarge", [](auto &d) {
    d.Alignment = D3D12_DEFAULT_MSAA_RESOURCE_PLACEMENT_ALIGNMENT * 2;
  });
  add("SizeAlignmentOverflow", [](auto &d) { d.SizeInBytes = UINT64_MAX; });
  add("CreationNodeMask", [](auto &d) { d.Properties.CreationNodeMask = 2; });
  add("VisibleNodeMask", [](auto &d) { d.Properties.VisibleNodeMask = 2; });
  add("TypedCpuPageProperty", [](auto &d) {
    d.Properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_BACK;
  });
  add("TypedMemoryPool", [](auto &d) {
    d.Properties.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;
  });
  add("AllowDisplay", [](auto &d) { d.Flags = D3D12_HEAP_FLAG_ALLOW_DISPLAY; });
  add("SharedCrossAdapter",
      [](auto &d) { d.Flags = D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER; });
  add("InvalidType", [](auto &d) {
    d.Properties.Type = static_cast<D3D12_HEAP_TYPE>(0x7fffffff);
  });
  add("CustomInvalidCpuPage", [](auto &d) {
    d.Properties.Type = D3D12_HEAP_TYPE_CUSTOM;
    d.Properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
  });
  add("CustomInvalidMemoryPool", [](auto &d) {
    d.Properties.Type = D3D12_HEAP_TYPE_CUSTOM;
    d.Properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_BACK;
    d.Properties.MemoryPoolPreference =
        static_cast<D3D12_MEMORY_POOL>(0x7fffffff);
  });
  add("CustomCrossAdapterCpuVisible", [](auto &d) {
    d.Properties.Type = D3D12_HEAP_TYPE_CUSTOM;
    d.Properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_BACK;
    d.Properties.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;
    d.Flags = D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER;
  });
  return cases;
}

class InvalidHeapDescriptionSpec
    : public ::testing::Test,
      public ::testing::WithParamInterface<InvalidHeapCase> {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }
  D3D12TestContext context_;
};

TEST_P(InvalidHeapDescriptionSpec, CreationRejectsAndClearsOutput) {
  void *output = reinterpret_cast<void *>(std::uintptr_t{1});
  EXPECT_EQ(context_.device()->CreateHeap(&GetParam().desc,
                                          __uuidof(ID3D12Heap), &output),
            E_INVALIDARG);
  EXPECT_EQ(output, nullptr);
}

INSTANTIATE_TEST_SUITE_P(
    DescriptionMatrix, InvalidHeapDescriptionSpec,
    ::testing::ValuesIn(BuildInvalidHeapCases()),
    [](const ::testing::TestParamInfo<InvalidHeapCase> &info) {
      return std::string(info.param.name);
    });

class D3D12MemoryContractSpec : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  ComPtr<ID3D12Heap> CreateHeap(UINT64 size, D3D12_HEAP_TYPE type,
                                D3D12_HEAP_FLAGS flags,
                                UINT64 alignment =
                                    D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT) {
    D3D12_HEAP_DESC desc = {};
    desc.SizeInBytes = size;
    desc.Properties.Type = type;
    desc.Alignment = alignment;
    desc.Flags = flags;
    ComPtr<ID3D12Heap> heap;
    EXPECT_EQ(context_.device()->CreateHeap(
                  &desc, __uuidof(ID3D12Heap),
                  reinterpret_cast<void **>(heap.put())),
              S_OK);
    return heap;
  }

  HRESULT CreatePlaced(ID3D12Heap *heap, UINT64 offset,
                       const D3D12_RESOURCE_DESC &desc,
                       D3D12_RESOURCE_STATES state, void **output) {
    return context_.device()->CreatePlacedResource(
        heap, offset, &desc, state, nullptr, __uuidof(ID3D12Resource), output);
  }

  D3D12TestContext context_;
};

TEST_F(D3D12MemoryContractSpec, CommittedBufferHeapAndStateMatrix) {
  struct Case {
    D3D12_HEAP_TYPE heap;
    D3D12_RESOURCE_STATES state;
    const char *name;
  };
  constexpr std::array cases = {
      Case{D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COMMON,
           "DefaultCommon"},
      Case{D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST,
           "DefaultCopyDest"},
      Case{D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_SOURCE,
           "DefaultCopySource"},
      Case{D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ,
           "UploadGenericRead"},
      Case{D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST,
           "ReadbackCopyDest"},
  };
  const auto desc = BufferDesc();
  for (const auto &test_case : cases) {
    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = test_case.heap;
    ComPtr<ID3D12Resource> resource;
    ASSERT_EQ(context_.device()->CreateCommittedResource(
                  &heap, D3D12_HEAP_FLAG_NONE, &desc, test_case.state,
                  nullptr, __uuidof(ID3D12Resource),
                  reinterpret_cast<void **>(resource.put())),
              S_OK)
        << test_case.name;
    ASSERT_TRUE(resource) << test_case.name;
    EXPECT_EQ(resource->GetDesc().Width, desc.Width) << test_case.name;
  }
}

TEST_F(D3D12MemoryContractSpec, CpuVisibleHeapsRequireCanonicalInitialState) {
  const auto desc = BufferDesc();
  for (const auto &[heap_type, invalid_state] : {
           std::pair{D3D12_HEAP_TYPE_UPLOAD,
                     D3D12_RESOURCE_STATE_COPY_DEST},
           std::pair{D3D12_HEAP_TYPE_READBACK,
                     D3D12_RESOURCE_STATE_GENERIC_READ},
       }) {
    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = heap_type;
    void *output = reinterpret_cast<void *>(std::uintptr_t{1});
    EXPECT_EQ(context_.device()->CreateCommittedResource(
                  &heap, D3D12_HEAP_FLAG_NONE, &desc, invalid_state, nullptr,
                  __uuidof(ID3D12Resource), &output),
              E_INVALIDARG);
    EXPECT_EQ(output, nullptr);
  }
}

TEST_F(D3D12MemoryContractSpec, PlacedBufferBoundaryMatrix) {
  constexpr UINT64 alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
  auto heap = CreateHeap(2 * alignment, D3D12_HEAP_TYPE_DEFAULT,
                         D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS);
  ASSERT_TRUE(heap);

  const auto exact = BufferDesc(alignment);
  ComPtr<ID3D12Resource> first;
  ASSERT_EQ(CreatePlaced(heap.get(), 0, exact, D3D12_RESOURCE_STATE_COMMON,
                         reinterpret_cast<void **>(first.put())),
            S_OK);
  ComPtr<ID3D12Resource> last;
  ASSERT_EQ(CreatePlaced(heap.get(), alignment, exact,
                         D3D12_RESOURCE_STATE_COMMON,
                         reinterpret_cast<void **>(last.put())),
            S_OK);

  for (const auto &[offset, desc] : {
           std::pair{alignment - 1, exact},
           std::pair{alignment, BufferDesc(alignment + 1)},
           std::pair{2 * alignment, BufferDesc(1)},
       }) {
    void *output = reinterpret_cast<void *>(std::uintptr_t{1});
    EXPECT_EQ(CreatePlaced(heap.get(), offset, desc,
                           D3D12_RESOURCE_STATE_COMMON, &output),
              E_INVALIDARG);
    EXPECT_EQ(output, nullptr);
  }
}

TEST_F(D3D12MemoryContractSpec, HeapCategoryRestrictionsAreEnforced) {
  constexpr UINT64 alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
  auto buffer_heap = CreateHeap(alignment, D3D12_HEAP_TYPE_DEFAULT,
                                D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS);
  auto texture_heap = CreateHeap(
      alignment, D3D12_HEAP_TYPE_DEFAULT,
      D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES);
  auto rt_heap = CreateHeap(alignment, D3D12_HEAP_TYPE_DEFAULT,
                            D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES);
  ASSERT_TRUE(buffer_heap);
  ASSERT_TRUE(texture_heap);
  ASSERT_TRUE(rt_heap);

  auto color = TextureDesc();
  auto render_target = color;
  render_target.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  for (const auto &[heap, desc] : {
           std::pair{buffer_heap.get(), color},
           std::pair{texture_heap.get(), BufferDesc()},
           std::pair{texture_heap.get(), render_target},
           std::pair{rt_heap.get(), BufferDesc()},
           std::pair{rt_heap.get(), color},
       }) {
    void *output = reinterpret_cast<void *>(std::uintptr_t{1});
    EXPECT_EQ(CreatePlaced(heap, 0, desc, D3D12_RESOURCE_STATE_COMMON,
                           &output),
              E_INVALIDARG);
    EXPECT_EQ(output, nullptr);
  }
}

TEST_F(D3D12MemoryContractSpec, RepeatedMapReturnsStableCoherentPointer) {
  auto upload = context_.CreateBuffer(
      4096, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_GENERIC_READ);
  ASSERT_TRUE(upload);
  void *first = nullptr;
  void *second = nullptr;
  const D3D12_RANGE no_read = {0, 0};
  ASSERT_EQ(upload->Map(0, &no_read, &first), S_OK);
  ASSERT_EQ(upload->Map(0, nullptr, &second), S_OK);
  ASSERT_NE(first, nullptr);
  EXPECT_EQ(second, first);
  constexpr std::uint32_t value = 0x1234abcdu;
  std::memcpy(first, &value, sizeof(value));
  const D3D12_RANGE written = {0, sizeof(value)};
  upload->Unmap(0, &written);
  upload->Unmap(0, nullptr);

  void *third = nullptr;
  ASSERT_EQ(upload->Map(0, nullptr, &third), S_OK);
  ASSERT_NE(third, nullptr);
  std::uint32_t actual = 0;
  std::memcpy(&actual, third, sizeof(actual));
  EXPECT_EQ(actual, value);
  upload->Unmap(0, nullptr);
}

TEST_F(D3D12MemoryContractSpec, PersistentMapSurvivesGpuSubmission) {
  constexpr std::array<std::uint32_t, 4> expected = {
      0x10203040u, 0x50607080u, 0x90a0b0c0u, 0xd0e0f001u};
  auto upload = context_.CreateBuffer(
      sizeof(expected), D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_GENERIC_READ);
  auto destination = context_.CreateBuffer(
      sizeof(expected), D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(upload);
  ASSERT_TRUE(destination);
  void *mapping = nullptr;
  ASSERT_EQ(upload->Map(0, nullptr, &mapping), S_OK);
  ASSERT_NE(mapping, nullptr);
  std::memcpy(mapping, expected.data(), sizeof(expected));
  context_.list()->CopyBufferRegion(destination.get(), 0, upload.get(), 0,
                                    sizeof(expected));
  ASSERT_EQ(context_.ExecuteAndWait(), S_OK);
  EXPECT_EQ(std::memcmp(mapping, expected.data(), sizeof(expected)), 0);
  upload->Unmap(0, nullptr);

  void *readback = nullptr;
  ASSERT_EQ(destination->Map(0, nullptr, &readback), S_OK);
  ASSERT_NE(readback, nullptr);
  EXPECT_EQ(std::memcmp(readback, expected.data(), sizeof(expected)), 0);
  destination->Unmap(0, nullptr);
}

TEST_F(D3D12MemoryContractSpec, BufferGpuVirtualAddressesAreStableAndDisjoint) {
  constexpr UINT64 size = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
  std::array<ComPtr<ID3D12Resource>, 4> resources;
  std::array<D3D12_GPU_VIRTUAL_ADDRESS, resources.size()> addresses = {};
  for (std::size_t i = 0; i < resources.size(); ++i) {
    resources[i] = context_.CreateBuffer(
        size, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
        D3D12_RESOURCE_STATE_COMMON);
    ASSERT_TRUE(resources[i]);
    addresses[i] = resources[i]->GetGPUVirtualAddress();
    EXPECT_NE(addresses[i], 0u);
    EXPECT_EQ(resources[i]->GetGPUVirtualAddress(), addresses[i]);
    for (std::size_t j = 0; j < i; ++j) {
      const auto low = std::min(addresses[i], addresses[j]);
      const auto high = std::max(addresses[i], addresses[j]);
      EXPECT_GE(high - low, size) << "resources " << j << " and " << i;
    }
  }
}

TEST_F(D3D12MemoryContractSpec,
       GpuVirtualAddressesRespectResourceKindAndAlignment) {
  struct BufferCase {
    D3D12_HEAP_TYPE heap_type;
    D3D12_RESOURCE_STATES initial_state;
    const char *name;
  };
  constexpr std::array cases = {
      BufferCase{D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COMMON,
                 "Default"},
      BufferCase{D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ,
                 "Upload"},
      BufferCase{D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST,
                 "Readback"},
  };

  const auto buffer_desc = BufferDesc(4096);
  const auto allocation = context_.device()->GetResourceAllocationInfo(
      0, 1, &buffer_desc);
  ASSERT_NE(allocation.Alignment, 0u);
  ASSERT_NE(allocation.Alignment, UINT64_MAX);
  for (const auto &test_case : cases) {
    auto buffer = context_.CreateBuffer(
        buffer_desc.Width, test_case.heap_type, D3D12_RESOURCE_FLAG_NONE,
        test_case.initial_state);
    ASSERT_TRUE(buffer) << test_case.name;
    const auto address = buffer->GetGPUVirtualAddress();
    EXPECT_NE(address, 0u) << test_case.name;
    EXPECT_EQ(address % allocation.Alignment, 0u) << test_case.name;
  }

  auto texture = context_.CreateTexture2D(
      16, 16, 1, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COMMON);
  ASSERT_TRUE(texture);
  EXPECT_EQ(texture->GetGPUVirtualAddress(), 0u);
}

TEST_F(D3D12MemoryContractSpec, PlacedBufferGpuVaIncludesHeapOffset) {
  constexpr UINT64 alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
  auto heap = CreateHeap(2 * alignment, D3D12_HEAP_TYPE_DEFAULT,
                         D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS);
  ASSERT_TRUE(heap);
  const auto desc = BufferDesc(4096);
  ComPtr<ID3D12Resource> first;
  ComPtr<ID3D12Resource> second;
  ASSERT_EQ(CreatePlaced(heap.get(), 0, desc, D3D12_RESOURCE_STATE_COMMON,
                         reinterpret_cast<void **>(first.put())),
            S_OK);
  ASSERT_EQ(CreatePlaced(heap.get(), alignment, desc,
                         D3D12_RESOURCE_STATE_COMMON,
                         reinterpret_cast<void **>(second.put())),
            S_OK);
  ASSERT_TRUE(first);
  ASSERT_TRUE(second);
  EXPECT_EQ(second->GetGPUVirtualAddress() - first->GetGPUVirtualAddress(),
            alignment);
}

TEST_F(D3D12MemoryContractSpec, AllocationInfoZeroCountIsEmpty) {
  const auto info =
      context_.device()->GetResourceAllocationInfo(0, 0, nullptr);
  EXPECT_EQ(info.SizeInBytes, 0u);
  EXPECT_EQ(info.Alignment, 1u);
}

TEST_F(D3D12MemoryContractSpec, AllocationInfo1MatchesAggregateLayout) {
  const std::array descs = {BufferDesc(1), BufferDesc(65536),
                            BufferDesc(65537)};
  const auto aggregate = context_.device()->GetResourceAllocationInfo(
      0, static_cast<UINT>(descs.size()), descs.data());
  ComPtr<ID3D12Device4> device4;
  ASSERT_EQ(context_.device()->QueryInterface(
                __uuidof(ID3D12Device4),
                reinterpret_cast<void **>(device4.put())),
            S_OK);
  std::array<D3D12_RESOURCE_ALLOCATION_INFO1, descs.size()> entries = {};
  const auto detailed = device4->GetResourceAllocationInfo1(
      0, static_cast<UINT>(descs.size()), descs.data(), entries.data());
  EXPECT_EQ(detailed.SizeInBytes, aggregate.SizeInBytes);
  EXPECT_EQ(detailed.Alignment, aggregate.Alignment);
  for (std::size_t i = 0; i < entries.size(); ++i) {
    EXPECT_EQ(entries[i].Alignment,
              D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);
    EXPECT_EQ(entries[i].Offset % entries[i].Alignment, 0u);
    EXPECT_GE(entries[i].SizeInBytes, descs[i].Width);
  }
}

} // namespace
