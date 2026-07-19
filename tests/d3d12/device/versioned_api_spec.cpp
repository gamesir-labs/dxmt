#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

constexpr UINT64 kBufferSize = 4096;

D3D12_HEAP_PROPERTIES HeapProperties(D3D12_HEAP_TYPE type) {
  D3D12_HEAP_PROPERTIES properties = {};
  properties.Type = type;
  return properties;
}

D3D12_RESOURCE_DESC BufferDesc(UINT64 size = kBufferSize) {
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

D3D12_RESOURCE_DESC1 BufferDesc1(UINT64 size = kBufferSize) {
  const auto legacy = BufferDesc(size);
  D3D12_RESOURCE_DESC1 desc = {};
  desc.Dimension = legacy.Dimension;
  desc.Alignment = legacy.Alignment;
  desc.Width = legacy.Width;
  desc.Height = legacy.Height;
  desc.DepthOrArraySize = legacy.DepthOrArraySize;
  desc.MipLevels = legacy.MipLevels;
  desc.Format = legacy.Format;
  desc.SampleDesc = legacy.SampleDesc;
  desc.Layout = legacy.Layout;
  desc.Flags = legacy.Flags;
  return desc;
}

D3D12_RESOURCE_DESC1 Desc1FromLegacy(const D3D12_RESOURCE_DESC &legacy) {
  D3D12_RESOURCE_DESC1 desc = {};
  desc.Dimension = legacy.Dimension;
  desc.Alignment = legacy.Alignment;
  desc.Width = legacy.Width;
  desc.Height = legacy.Height;
  desc.DepthOrArraySize = legacy.DepthOrArraySize;
  desc.MipLevels = legacy.MipLevels;
  desc.Format = legacy.Format;
  desc.SampleDesc = legacy.SampleDesc;
  desc.Layout = legacy.Layout;
  desc.Flags = legacy.Flags;
  return desc;
}

void ExpectResourceDescEqual(const D3D12_RESOURCE_DESC &actual,
                             const D3D12_RESOURCE_DESC &expected) {
  EXPECT_EQ(actual.Dimension, expected.Dimension);
  EXPECT_EQ(actual.Alignment, expected.Alignment);
  EXPECT_EQ(actual.Width, expected.Width);
  EXPECT_EQ(actual.Height, expected.Height);
  EXPECT_EQ(actual.DepthOrArraySize, expected.DepthOrArraySize);
  EXPECT_EQ(actual.MipLevels, expected.MipLevels);
  EXPECT_EQ(actual.Format, expected.Format);
  EXPECT_EQ(actual.SampleDesc.Count, expected.SampleDesc.Count);
  EXPECT_EQ(actual.SampleDesc.Quality, expected.SampleDesc.Quality);
  EXPECT_EQ(actual.Layout, expected.Layout);
  EXPECT_EQ(actual.Flags, expected.Flags);
}

void ExpectResourceDesc1Equal(const D3D12_RESOURCE_DESC1 &actual,
                              const D3D12_RESOURCE_DESC1 &expected) {
  EXPECT_EQ(actual.Dimension, expected.Dimension);
  EXPECT_EQ(actual.Alignment, expected.Alignment);
  EXPECT_EQ(actual.Width, expected.Width);
  EXPECT_EQ(actual.Height, expected.Height);
  EXPECT_EQ(actual.DepthOrArraySize, expected.DepthOrArraySize);
  EXPECT_EQ(actual.MipLevels, expected.MipLevels);
  EXPECT_EQ(actual.Format, expected.Format);
  EXPECT_EQ(actual.SampleDesc.Count, expected.SampleDesc.Count);
  EXPECT_EQ(actual.SampleDesc.Quality, expected.SampleDesc.Quality);
  EXPECT_EQ(actual.Layout, expected.Layout);
  EXPECT_EQ(actual.Flags, expected.Flags);
  EXPECT_EQ(actual.SamplerFeedbackMipRegion.Width,
            expected.SamplerFeedbackMipRegion.Width);
  EXPECT_EQ(actual.SamplerFeedbackMipRegion.Height,
            expected.SamplerFeedbackMipRegion.Height);
  EXPECT_EQ(actual.SamplerFeedbackMipRegion.Depth,
            expected.SamplerFeedbackMipRegion.Depth);
}

template <typename Resource>
void WritePatternAndExpectReadback(D3D12TestContext &context,
                                   Resource *resource) {
  constexpr std::array<std::uint32_t, 8> expected = {
      0x10203040u, 0x50607080u, 0x90a0b0c0u, 0xd0e0f001u,
      0x13579bdfu, 0x2468ace0u, 0xfeedc0deu, 0x0badf00du};
  void *mapping = nullptr;
  D3D12_RANGE read_range = {0, 0};
  ASSERT_EQ(resource->Map(0, &read_range, &mapping), S_OK);
  ASSERT_NE(mapping, nullptr);
  std::memcpy(mapping, expected.data(), sizeof(expected));
  D3D12_RANGE written_range = {0, sizeof(expected)};
  resource->Unmap(0, &written_range);

  std::vector<std::uint8_t> actual;
  ASSERT_EQ(context.ReadbackBuffer(resource, sizeof(expected), &actual), S_OK);
  ASSERT_EQ(actual.size(), sizeof(expected));
  EXPECT_EQ(std::memcmp(actual.data(), expected.data(), sizeof(expected)), 0);
}

class VersionedApiSpec : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  template <typename Interface> ComPtr<Interface> QueryDevice() {
    ComPtr<Interface> result;
    EXPECT_EQ(context_.device()->QueryInterface(
                  __uuidof(Interface),
                  reinterpret_cast<void **>(result.put())),
              S_OK);
    return result;
  }

  D3D12TestContext context_;
};

TEST_F(VersionedApiSpec,
       CreateCommittedResource1WithoutSessionExecutesLikeBaseResource) {
  auto device4 = QueryDevice<ID3D12Device4>();
  ASSERT_TRUE(device4);
  const auto properties = HeapProperties(D3D12_HEAP_TYPE_UPLOAD);
  const auto desc = BufferDesc();
  ComPtr<ID3D12Resource> base_resource;
  ComPtr<ID3D12Resource> resource;

  ASSERT_EQ(context_.device()->CreateCommittedResource(
                &properties, D3D12_HEAP_FLAG_NONE, &desc,
                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                __uuidof(ID3D12Resource),
                reinterpret_cast<void **>(base_resource.put())),
            S_OK);
  ASSERT_EQ(device4->CreateCommittedResource1(
                &properties, D3D12_HEAP_FLAG_NONE, &desc,
                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, nullptr,
                __uuidof(ID3D12Resource),
                reinterpret_cast<void **>(resource.put())),
            S_OK);
  ASSERT_TRUE(base_resource);
  ASSERT_TRUE(resource);
  ExpectResourceDescEqual(resource->GetDesc(), base_resource->GetDesc());

  D3D12_HEAP_PROPERTIES observed_properties = {};
  D3D12_HEAP_FLAGS observed_flags = static_cast<D3D12_HEAP_FLAGS>(UINT_MAX);
  ASSERT_EQ(resource->GetHeapProperties(&observed_properties, &observed_flags),
            S_OK);
  EXPECT_EQ(observed_properties.Type, D3D12_HEAP_TYPE_UPLOAD);
  EXPECT_EQ(observed_flags & ~D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS, 0u);
  WritePatternAndExpectReadback(context_, resource.get());
}

TEST_F(VersionedApiSpec, CreateHeap1WithoutSessionBacksPlacedResource) {
  auto device4 = QueryDevice<ID3D12Device4>();
  ASSERT_TRUE(device4);
  D3D12_HEAP_DESC heap_desc = {};
  heap_desc.SizeInBytes = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
  heap_desc.Properties = HeapProperties(D3D12_HEAP_TYPE_UPLOAD);
  heap_desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
  heap_desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
#ifdef __ID3D12Heap1_INTERFACE_DEFINED__
  using HeapInterface = ID3D12Heap1;
#else
  using HeapInterface = ID3D12Heap;
#endif
  ComPtr<HeapInterface> heap;

  ASSERT_EQ(device4->CreateHeap1(&heap_desc, nullptr,
                                 __uuidof(HeapInterface),
                                 reinterpret_cast<void **>(heap.put())),
            S_OK);
  ASSERT_TRUE(heap);
  const auto observed_heap = heap->GetDesc();
  EXPECT_EQ(observed_heap.SizeInBytes, heap_desc.SizeInBytes);
  EXPECT_EQ(observed_heap.Properties.Type, heap_desc.Properties.Type);
  EXPECT_EQ(observed_heap.Alignment, heap_desc.Alignment);
  EXPECT_EQ(observed_heap.Flags, heap_desc.Flags);

#ifdef __ID3D12Heap1_INTERFACE_DEFINED__
  ComPtr<IUnknown> heap_identity;
  ComPtr<ID3D12Heap> base_heap;
  ComPtr<IUnknown> base_identity;
  ASSERT_EQ(heap->QueryInterface(IID_PPV_ARGS(heap_identity.put())), S_OK);
  ASSERT_EQ(heap->QueryInterface(IID_PPV_ARGS(base_heap.put())), S_OK);
  ASSERT_EQ(base_heap->QueryInterface(IID_PPV_ARGS(base_identity.put())),
            S_OK);
  EXPECT_EQ(heap_identity.get(), base_identity.get());
#endif

  const auto resource_desc = BufferDesc();
  ComPtr<ID3D12Resource> resource;
  ASSERT_EQ(context_.device()->CreatePlacedResource(
                heap.get(), 0, &resource_desc,
                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                __uuidof(ID3D12Resource),
                reinterpret_cast<void **>(resource.put())),
            S_OK);
  ASSERT_TRUE(resource);
  WritePatternAndExpectReadback(context_, resource.get());
}

TEST_F(VersionedApiSpec, HeapCreationCapabilityProbesMatchBaseApi) {
  auto device4 = QueryDevice<ID3D12Device4>();
  ASSERT_TRUE(device4);
  D3D12_HEAP_DESC desc = {};
  desc.SizeInBytes = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
  desc.Properties = HeapProperties(D3D12_HEAP_TYPE_UPLOAD);
  desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
  desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;

  EXPECT_EQ(context_.device()->CreateHeap(
                &desc, __uuidof(ID3D12Heap), nullptr),
            S_FALSE);
  EXPECT_EQ(device4->CreateHeap1(
                &desc, nullptr, __uuidof(ID3D12Heap), nullptr),
            S_FALSE);

  desc.SizeInBytes = 0;
  EXPECT_EQ(context_.device()->CreateHeap(
                &desc, __uuidof(ID3D12Heap), nullptr),
            E_INVALIDARG);
  EXPECT_EQ(device4->CreateHeap1(
                &desc, nullptr, __uuidof(ID3D12Heap), nullptr),
            E_INVALIDARG);
}

TEST_F(VersionedApiSpec,
       CreateReservedResource1WithoutSessionMatchesBaseCapability) {
  auto device4 = QueryDevice<ID3D12Device4>();
  ASSERT_TRUE(device4);
  const auto desc = BufferDesc(D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);
  ComPtr<ID3D12Resource> base_resource;
  ComPtr<ID3D12Resource> versioned_resource;

  const HRESULT base_hr = context_.device()->CreateReservedResource(
      &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, __uuidof(ID3D12Resource),
      reinterpret_cast<void **>(base_resource.put()));
  const HRESULT versioned_hr = device4->CreateReservedResource1(
      &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, nullptr,
      __uuidof(ID3D12Resource),
      reinterpret_cast<void **>(versioned_resource.put()));

  EXPECT_EQ(versioned_hr, base_hr);
  EXPECT_EQ(static_cast<bool>(versioned_resource),
            static_cast<bool>(base_resource));
  if (base_resource && versioned_resource) {
    ExpectResourceDescEqual(versioned_resource->GetDesc(),
                            base_resource->GetDesc());
  }
}

TEST_F(VersionedApiSpec,
       ReservedResourceCapabilityProbesMatchBaseApi) {
  auto device4 = QueryDevice<ID3D12Device4>();
  ASSERT_TRUE(device4);
  auto desc = BufferDesc(D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);

  const HRESULT base_hr = context_.device()->CreateReservedResource(
      &desc, D3D12_RESOURCE_STATE_COMMON, nullptr,
      __uuidof(ID3D12Resource), nullptr);
  const HRESULT versioned_hr = device4->CreateReservedResource1(
      &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, nullptr,
      __uuidof(ID3D12Resource), nullptr);
  EXPECT_EQ(versioned_hr, base_hr);
}

TEST_F(VersionedApiSpec,
       CreateCommittedResource2PreservesDesc1AndExecutes) {
  auto device8 = QueryDevice<ID3D12Device8>();
  ASSERT_TRUE(device8);
  const auto properties = HeapProperties(D3D12_HEAP_TYPE_UPLOAD);
  const auto desc = BufferDesc1();
  ComPtr<ID3D12Resource> base_resource;
  ComPtr<ID3D12Resource2> resource;

  const auto legacy_desc = BufferDesc(desc.Width);
  ASSERT_EQ(context_.device()->CreateCommittedResource(
                &properties, D3D12_HEAP_FLAG_NONE, &legacy_desc,
                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                __uuidof(ID3D12Resource),
                reinterpret_cast<void **>(base_resource.put())),
            S_OK);
  ASSERT_EQ(device8->CreateCommittedResource2(
                &properties, D3D12_HEAP_FLAG_NONE, &desc,
                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, nullptr,
                __uuidof(ID3D12Resource2),
                reinterpret_cast<void **>(resource.put())),
            S_OK);
  ASSERT_TRUE(base_resource);
  ASSERT_TRUE(resource);
  ExpectResourceDesc1Equal(resource->GetDesc1(),
                           Desc1FromLegacy(base_resource->GetDesc()));
  WritePatternAndExpectReadback(context_, resource.get());
}

TEST_F(VersionedApiSpec,
       CreatePlacedResource1PreservesOffsetAndExecutes) {
  auto device8 = QueryDevice<ID3D12Device8>();
  ASSERT_TRUE(device8);
  constexpr UINT64 offset = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
  D3D12_HEAP_DESC heap_desc = {};
  heap_desc.SizeInBytes = offset * 2;
  heap_desc.Properties = HeapProperties(D3D12_HEAP_TYPE_UPLOAD);
  heap_desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
  heap_desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
  ComPtr<ID3D12Heap> heap;
  ASSERT_EQ(context_.device()->CreateHeap(
                &heap_desc, __uuidof(ID3D12Heap),
                reinterpret_cast<void **>(heap.put())),
            S_OK);
  ASSERT_TRUE(heap);

  const auto desc = BufferDesc1();
  const auto legacy_desc = BufferDesc(desc.Width);
  ComPtr<ID3D12Resource> base_resource;
  ComPtr<ID3D12Resource2> resource;
  ASSERT_EQ(context_.device()->CreatePlacedResource(
                heap.get(), 0, &legacy_desc,
                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                __uuidof(ID3D12Resource),
                reinterpret_cast<void **>(base_resource.put())),
            S_OK);
  ASSERT_EQ(device8->CreatePlacedResource1(
                heap.get(), offset, &desc, D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr, __uuidof(ID3D12Resource2),
                reinterpret_cast<void **>(resource.put())),
            S_OK);
  ASSERT_TRUE(base_resource);
  ASSERT_TRUE(resource);
  ExpectResourceDesc1Equal(resource->GetDesc1(),
                           Desc1FromLegacy(base_resource->GetDesc()));
  EXPECT_NE(resource->GetGPUVirtualAddress(), 0u);
  EXPECT_EQ(resource->GetGPUVirtualAddress(),
            base_resource->GetGPUVirtualAddress() + offset);
  WritePatternAndExpectReadback(context_, resource.get());
}

TEST_F(VersionedApiSpec,
       GetResourceAllocationInfo2MatchesLegacyCompatibleDescriptions) {
  auto device8 = QueryDevice<ID3D12Device8>();
  ASSERT_TRUE(device8);
  const std::array<D3D12_RESOURCE_DESC1, 3> descs1 = {
      BufferDesc1(1024), BufferDesc1(70000), BufferDesc1(8192)};
  std::array<D3D12_RESOURCE_DESC, descs1.size()> legacy_descs = {};
  for (std::size_t i = 0; i < descs1.size(); ++i)
    legacy_descs[i] = BufferDesc(descs1[i].Width);
  std::array<D3D12_RESOURCE_ALLOCATION_INFO1, descs1.size()> info2_entries = {};
  std::array<D3D12_RESOURCE_ALLOCATION_INFO1, descs1.size()> info1_entries = {};

  const auto info2 = device8->GetResourceAllocationInfo2(
      0, descs1.size(), descs1.data(), info2_entries.data());
  const auto info1 = device8->GetResourceAllocationInfo1(
      0, legacy_descs.size(), legacy_descs.data(), info1_entries.data());

  EXPECT_EQ(info2.SizeInBytes, info1.SizeInBytes);
  EXPECT_EQ(info2.Alignment, info1.Alignment);
  for (std::size_t i = 0; i < descs1.size(); ++i) {
    EXPECT_EQ(info2_entries[i].Offset, info1_entries[i].Offset) << i;
    EXPECT_EQ(info2_entries[i].Alignment, info1_entries[i].Alignment) << i;
    EXPECT_EQ(info2_entries[i].SizeInBytes, info1_entries[i].SizeInBytes) << i;
  }
}

TEST_F(VersionedApiSpec,
       GetResourceAllocationInfo2InvalidatesFromFirstInvalidDescription) {
  auto device4 = QueryDevice<ID3D12Device4>();
  auto device8 = QueryDevice<ID3D12Device8>();
  ASSERT_TRUE(device4);
  ASSERT_TRUE(device8);
  std::array<D3D12_RESOURCE_DESC, 3> legacy_descs = {
      BufferDesc(1024), BufferDesc(2048), BufferDesc(4096)};
  std::array<D3D12_RESOURCE_DESC1, 3> descs1 = {
      BufferDesc1(1024), BufferDesc1(2048), BufferDesc1(4096)};
  legacy_descs[1].Width = 0;
  descs1[1].Width = 0;

  std::array<D3D12_RESOURCE_ALLOCATION_INFO1, 3> legacy_entries = {};
  std::array<D3D12_RESOURCE_ALLOCATION_INFO1, 3> entries2 = {};
  for (auto *entries : {&legacy_entries, &entries2}) {
    for (auto &entry : *entries) {
      entry.Offset = 7;
      entry.Alignment = 9;
      entry.SizeInBytes = 11;
    }
  }

  const auto legacy_info = device4->GetResourceAllocationInfo1(
      0, legacy_descs.size(), legacy_descs.data(), legacy_entries.data());
  const auto info2 = device8->GetResourceAllocationInfo2(
      0, descs1.size(), descs1.data(), entries2.data());

  EXPECT_EQ(info2.SizeInBytes, UINT64_MAX);
  EXPECT_EQ(info2.Alignment, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);
  EXPECT_EQ(info2.SizeInBytes, legacy_info.SizeInBytes);
  EXPECT_EQ(info2.Alignment, legacy_info.Alignment);
  for (std::size_t i = 0; i < entries2.size(); ++i) {
    EXPECT_EQ(entries2[i].Offset, legacy_entries[i].Offset) << i;
    EXPECT_EQ(entries2[i].Alignment, legacy_entries[i].Alignment) << i;
    EXPECT_EQ(entries2[i].SizeInBytes, legacy_entries[i].SizeInBytes) << i;
  }
}

TEST_F(VersionedApiSpec,
       AllocationInfoValidatesVisibleNodesAndDescriptorArrays) {
  auto device4 = QueryDevice<ID3D12Device4>();
  auto device8 = QueryDevice<ID3D12Device8>();
  ASSERT_TRUE(device4);
  ASSERT_TRUE(device8);
  const auto desc = BufferDesc(1024);
  const auto desc1 = BufferDesc1(1024);

  const auto base_node0 = context_.device()->GetResourceAllocationInfo(
      0, 1, &desc);
  const auto base_node1 = context_.device()->GetResourceAllocationInfo(
      1, 1, &desc);
  EXPECT_EQ(base_node1.SizeInBytes, base_node0.SizeInBytes);
  EXPECT_EQ(base_node1.Alignment, base_node0.Alignment);

  D3D12_RESOURCE_ALLOCATION_INFO1 entry1 = {};
  const auto info1 = device4->GetResourceAllocationInfo1(
      1, 1, &desc, &entry1);
  D3D12_RESOURCE_ALLOCATION_INFO1 entry2 = {};
  const auto info2 = device8->GetResourceAllocationInfo2(
      1, 1, &desc1, &entry2);
  EXPECT_EQ(info1.SizeInBytes, base_node0.SizeInBytes);
  EXPECT_EQ(info1.Alignment, base_node0.Alignment);
  EXPECT_EQ(info2.SizeInBytes, base_node0.SizeInBytes);
  EXPECT_EQ(info2.Alignment, base_node0.Alignment);
  EXPECT_EQ(entry2.Offset, entry1.Offset);
  EXPECT_EQ(entry2.Alignment, entry1.Alignment);
  EXPECT_EQ(entry2.SizeInBytes, entry1.SizeInBytes);

  EXPECT_EQ(context_.device()
                ->GetResourceAllocationInfo(2, 1, &desc)
                .SizeInBytes,
            UINT64_MAX);
  EXPECT_EQ(context_.device()
                ->GetResourceAllocationInfo(0, 1, nullptr)
                .SizeInBytes,
            UINT64_MAX);

  auto expect_invalid_entry = [](const D3D12_RESOURCE_ALLOCATION_INFO1 &entry) {
    EXPECT_EQ(entry.Offset, UINT64_MAX);
    EXPECT_EQ(entry.Alignment, 0u);
    EXPECT_EQ(entry.SizeInBytes, UINT64_MAX);
  };
  for (const bool null_desc : {false, true}) {
    entry1 = {7, 9, 11};
    const auto invalid_info1 = device4->GetResourceAllocationInfo1(
        null_desc ? 0 : 2, 1, null_desc ? nullptr : &desc, &entry1);
    EXPECT_EQ(invalid_info1.SizeInBytes, UINT64_MAX);
    EXPECT_EQ(invalid_info1.Alignment, 1u);
    expect_invalid_entry(entry1);

    entry2 = {7, 9, 11};
    const auto invalid_info2 = device8->GetResourceAllocationInfo2(
        null_desc ? 0 : 2, 1, null_desc ? nullptr : &desc1, &entry2);
    EXPECT_EQ(invalid_info2.SizeInBytes, UINT64_MAX);
    EXPECT_EQ(invalid_info2.Alignment, 1u);
    expect_invalid_entry(entry2);
  }
}

TEST_F(VersionedApiSpec,
       ResourceCreationCapabilityProbesMatchBaseApis) {
  auto device4 = QueryDevice<ID3D12Device4>();
  auto device8 = QueryDevice<ID3D12Device8>();
  ASSERT_TRUE(device4);
  ASSERT_TRUE(device8);
  const auto properties = HeapProperties(D3D12_HEAP_TYPE_UPLOAD);
  const auto desc = BufferDesc();
  const auto desc1 = BufferDesc1();

  EXPECT_EQ(context_.device()->CreateCommittedResource(
                &properties, D3D12_HEAP_FLAG_NONE, &desc,
                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                __uuidof(ID3D12Resource), nullptr),
            S_FALSE);
  EXPECT_EQ(device4->CreateCommittedResource1(
                &properties, D3D12_HEAP_FLAG_NONE, &desc,
                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, nullptr,
                __uuidof(ID3D12Resource), nullptr),
            S_FALSE);
  EXPECT_EQ(device8->CreateCommittedResource2(
                &properties, D3D12_HEAP_FLAG_NONE, &desc1,
                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, nullptr,
                __uuidof(ID3D12Resource), nullptr),
            S_FALSE);

  D3D12_HEAP_DESC heap_desc = {};
  heap_desc.SizeInBytes = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
  heap_desc.Properties = properties;
  heap_desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
  heap_desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
  ComPtr<ID3D12Heap> heap;
  ASSERT_EQ(context_.device()->CreateHeap(
                &heap_desc, __uuidof(ID3D12Heap),
                reinterpret_cast<void **>(heap.put())),
            S_OK);
  ASSERT_TRUE(heap);
  const HRESULT base_probe = context_.device()->CreatePlacedResource(
      heap.get(), 0, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
      __uuidof(ID3D12Resource), nullptr);
  const HRESULT versioned_probe = device8->CreatePlacedResource1(
      heap.get(), 0, &desc1, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
      __uuidof(ID3D12Resource), nullptr);
  EXPECT_EQ(versioned_probe, base_probe);
  EXPECT_TRUE(base_probe == S_FALSE || base_probe == E_UNEXPECTED);

  auto invalid_desc = desc;
  invalid_desc.Width = 0;
  auto invalid_desc1 = desc1;
  invalid_desc1.Width = 0;
  EXPECT_EQ(context_.device()->CreateCommittedResource(
                &properties, D3D12_HEAP_FLAG_NONE, &invalid_desc,
                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                __uuidof(ID3D12Resource), nullptr),
            E_INVALIDARG);
  EXPECT_EQ(device4->CreateCommittedResource1(
                &properties, D3D12_HEAP_FLAG_NONE, &invalid_desc,
                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, nullptr,
                __uuidof(ID3D12Resource), nullptr),
            E_INVALIDARG);
  EXPECT_EQ(device8->CreateCommittedResource2(
                &properties, D3D12_HEAP_FLAG_NONE, &invalid_desc1,
                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, nullptr,
                __uuidof(ID3D12Resource), nullptr),
            E_INVALIDARG);
  EXPECT_EQ(context_.device()->CreatePlacedResource(
                heap.get(), 1, &desc, D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr, __uuidof(ID3D12Resource), nullptr),
            E_INVALIDARG);
  EXPECT_EQ(device8->CreatePlacedResource1(
                heap.get(), 1, &desc1, D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr, __uuidof(ID3D12Resource), nullptr),
            E_INVALIDARG);
}

TEST_F(VersionedApiSpec, CreateCommandQueue1ExecutesFenceSignal) {
  auto device9 = QueryDevice<ID3D12Device9>();
  ASSERT_TRUE(device9);
  D3D12_COMMAND_QUEUE_DESC desc = {};
  desc.Type = D3D12_COMMAND_LIST_TYPE_COPY;
  desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
  const GUID creator = {0x6f0ed0c6,
                        0xbec6,
                        0x4f97,
                        {0x83, 0xee, 0x25, 0x75, 0xeb, 0x28, 0x9d, 0x6d}};
  ComPtr<ID3D12CommandQueue> base_queue;
  ComPtr<ID3D12CommandQueue> queue;

  ASSERT_EQ(context_.device()->CreateCommandQueue(
                &desc, __uuidof(ID3D12CommandQueue),
                reinterpret_cast<void **>(base_queue.put())),
            S_OK);
  ASSERT_EQ(device9->CreateCommandQueue1(
                &desc, creator, __uuidof(ID3D12CommandQueue),
                reinterpret_cast<void **>(queue.put())),
            S_OK);
  ASSERT_TRUE(base_queue);
  ASSERT_TRUE(queue);
  const auto base_desc = base_queue->GetDesc();
  const auto observed = queue->GetDesc();
  EXPECT_EQ(observed.Type, base_desc.Type);
  EXPECT_EQ(observed.Priority, base_desc.Priority);
  EXPECT_EQ(observed.Flags, base_desc.Flags);
  EXPECT_EQ(observed.NodeMask, base_desc.NodeMask);

  ComPtr<ID3D12Fence> fence;
  ASSERT_EQ(context_.device()->CreateFence(
                0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence),
                reinterpret_cast<void **>(fence.put())),
            S_OK);
  ASSERT_EQ(queue->Signal(fence.get(), 1), S_OK);
  EXPECT_EQ(context_.WaitForFence(fence.get(), 1), S_OK);
}

TEST_F(VersionedApiSpec, CreateCommandQueue1FailureClearsOutput) {
  auto device9 = QueryDevice<ID3D12Device9>();
  ASSERT_TRUE(device9);
  D3D12_COMMAND_QUEUE_DESC desc = {};
  desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
  const GUID creator = {0x51810ec4,
                        0x962d,
                        0x48fd,
                        {0x89, 0x30, 0x19, 0xb7, 0x4b, 0xc3, 0x77, 0xc5}};

  auto expect_rejected = [&](const D3D12_COMMAND_QUEUE_DESC *candidate) {
    void *output = reinterpret_cast<void *>(std::uintptr_t{1});
    EXPECT_EQ(device9->CreateCommandQueue1(
                  candidate, creator, __uuidof(ID3D12CommandQueue), &output),
              E_INVALIDARG);
    EXPECT_EQ(output, nullptr);
  };

  expect_rejected(nullptr);
  desc.Type = D3D12_COMMAND_LIST_TYPE_BUNDLE;
  expect_rejected(&desc);
  desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  desc.Priority = INT_MAX;
  expect_rejected(&desc);
  desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
  desc.Flags = static_cast<D3D12_COMMAND_QUEUE_FLAGS>(2);
  expect_rejected(&desc);
  desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
  desc.NodeMask = 2;
  expect_rejected(&desc);
}

TEST_F(VersionedApiSpec,
       BackgroundProcessingSignalsEventAndClearsMeasurementRequest) {
  auto device6 = QueryDevice<ID3D12Device6>();
  ASSERT_TRUE(device6);
  constexpr std::array modes = {
      D3D12_BACKGROUND_PROCESSING_MODE_ALLOWED,
      D3D12_BACKGROUND_PROCESSING_MODE_ALLOW_INTRUSIVE_MEASUREMENTS,
      D3D12_BACKGROUND_PROCESSING_MODE_DISABLE_BACKGROUND_WORK,
      D3D12_BACKGROUND_PROCESSING_MODE_DISABLE_PROFILING_BY_SYSTEM};
  constexpr std::array actions = {
      D3D12_MEASUREMENTS_ACTION_KEEP_ALL,
      D3D12_MEASUREMENTS_ACTION_COMMIT_RESULTS,
      D3D12_MEASUREMENTS_ACTION_COMMIT_RESULTS_HIGH_PRIORITY,
      D3D12_MEASUREMENTS_ACTION_DISCARD_PREVIOUS};

  for (const auto mode : modes) {
    for (const auto action : actions) {
      SCOPED_TRACE(::testing::Message()
                   << "mode=" << mode << " action=" << action);
      HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
      ASSERT_NE(event, nullptr);
      BOOL further_measurements_desired = TRUE;
      EXPECT_EQ(device6->SetBackgroundProcessingMode(
                    mode, action, event, &further_measurements_desired),
                S_OK);
      EXPECT_FALSE(further_measurements_desired);
      EXPECT_EQ(WaitForSingleObject(event, 0), WAIT_OBJECT_0);
      CloseHandle(event);
    }
  }

  EXPECT_EQ(device6->SetBackgroundProcessingMode(
                D3D12_BACKGROUND_PROCESSING_MODE_ALLOWED,
                D3D12_MEASUREMENTS_ACTION_KEEP_ALL, nullptr, nullptr),
            S_OK);
}

TEST_F(VersionedApiSpec, BackgroundProcessingRejectsInvalidEnumsAtomically) {
  auto device6 = QueryDevice<ID3D12Device6>();
  ASSERT_TRUE(device6);
  HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  ASSERT_NE(event, nullptr);

  BOOL further_measurements_desired = TRUE;
  EXPECT_EQ(device6->SetBackgroundProcessingMode(
                static_cast<D3D12_BACKGROUND_PROCESSING_MODE>(UINT_MAX),
                D3D12_MEASUREMENTS_ACTION_KEEP_ALL, event,
                &further_measurements_desired),
            E_INVALIDARG);
  EXPECT_TRUE(further_measurements_desired);
  EXPECT_EQ(WaitForSingleObject(event, 0), WAIT_TIMEOUT);

  EXPECT_EQ(device6->SetBackgroundProcessingMode(
                D3D12_BACKGROUND_PROCESSING_MODE_ALLOWED,
                static_cast<D3D12_MEASUREMENTS_ACTION>(UINT_MAX), event,
                &further_measurements_desired),
            E_INVALIDARG);
  EXPECT_TRUE(further_measurements_desired);
  EXPECT_EQ(WaitForSingleObject(event, 0), WAIT_TIMEOUT);
  CloseHandle(event);
}

TEST_F(VersionedApiSpec, ShaderCacheSessionFailureIsCapabilityCoherent) {
  auto device9 = QueryDevice<ID3D12Device9>();
  ASSERT_TRUE(device9);
  D3D12_FEATURE_DATA_SHADER_CACHE feature = {};
  ASSERT_EQ(context_.device()->CheckFeatureSupport(
                D3D12_FEATURE_SHADER_CACHE, &feature, sizeof(feature)),
            S_OK);
  ASSERT_EQ(feature.SupportFlags, D3D12_SHADER_CACHE_SUPPORT_NONE);

  D3D12_SHADER_CACHE_SESSION_DESC desc = {};
  desc.Identifier = {0xffa8df11,
                     0x3791,
                     0x4d9b,
                     {0xa7, 0x82, 0x36, 0xb8, 0x5e, 0x82, 0x77, 0xd8}};
  desc.Mode = D3D12_SHADER_CACHE_MODE_MEMORY;
  desc.Flags = D3D12_SHADER_CACHE_FLAG_NONE;
  constexpr std::array modes = {D3D12_SHADER_CACHE_MODE_MEMORY,
                                D3D12_SHADER_CACHE_MODE_DISK};
  const std::array flags = {
      D3D12_SHADER_CACHE_FLAG_NONE,
      D3D12_SHADER_CACHE_FLAG_DRIVER_VERSIONED,
      D3D12_SHADER_CACHE_FLAG_USE_WORKING_DIR,
      static_cast<D3D12_SHADER_CACHE_FLAGS>(
          D3D12_SHADER_CACHE_FLAG_DRIVER_VERSIONED |
          D3D12_SHADER_CACHE_FLAG_USE_WORKING_DIR),
  };
  for (const auto mode : modes) {
    for (const auto session_flags : flags) {
      SCOPED_TRACE(::testing::Message()
                   << "mode=" << mode << " flags=" << session_flags);
      desc.Mode = mode;
      desc.Flags = session_flags;
      void *output = reinterpret_cast<void *>(std::uintptr_t{1});
      EXPECT_EQ(device9->CreateShaderCacheSession(
                    &desc, __uuidof(ID3D12ShaderCacheSession), &output),
                E_NOTIMPL);
      EXPECT_EQ(output, nullptr);
    }
  }

  void *output = reinterpret_cast<void *>(std::uintptr_t{1});
  EXPECT_EQ(device9->CreateShaderCacheSession(
                nullptr, __uuidof(ID3D12ShaderCacheSession), &output),
            E_INVALIDARG);
  EXPECT_EQ(output, nullptr);
  EXPECT_EQ(device9->CreateShaderCacheSession(
                &desc, __uuidof(ID3D12ShaderCacheSession), nullptr),
            E_POINTER);

  desc.Mode = static_cast<D3D12_SHADER_CACHE_MODE>(UINT_MAX);
  output = reinterpret_cast<void *>(std::uintptr_t{1});
  EXPECT_EQ(device9->CreateShaderCacheSession(
                &desc, __uuidof(ID3D12ShaderCacheSession), &output),
            E_INVALIDARG);
  EXPECT_EQ(output, nullptr);

  desc.Mode = D3D12_SHADER_CACHE_MODE_MEMORY;
  desc.Flags = static_cast<D3D12_SHADER_CACHE_FLAGS>(4);
  output = reinterpret_cast<void *>(std::uintptr_t{1});
  EXPECT_EQ(device9->CreateShaderCacheSession(
                &desc, __uuidof(ID3D12ShaderCacheSession), &output),
            E_INVALIDARG);
  EXPECT_EQ(output, nullptr);

  const auto kinds = static_cast<D3D12_SHADER_CACHE_KIND_FLAGS>(
      D3D12_SHADER_CACHE_KIND_FLAG_IMPLICIT_D3D_CACHE_FOR_DRIVER |
      D3D12_SHADER_CACHE_KIND_FLAG_IMPLICIT_D3D_CONVERSIONS |
      D3D12_SHADER_CACHE_KIND_FLAG_IMPLICIT_DRIVER_MANAGED |
      D3D12_SHADER_CACHE_KIND_FLAG_APPLICATION_MANAGED);
  const std::array valid_controls = {
      D3D12_SHADER_CACHE_CONTROL_FLAG_DISABLE,
      D3D12_SHADER_CACHE_CONTROL_FLAG_ENABLE,
      D3D12_SHADER_CACHE_CONTROL_FLAG_CLEAR,
      static_cast<D3D12_SHADER_CACHE_CONTROL_FLAGS>(
          D3D12_SHADER_CACHE_CONTROL_FLAG_DISABLE |
          D3D12_SHADER_CACHE_CONTROL_FLAG_CLEAR),
      static_cast<D3D12_SHADER_CACHE_CONTROL_FLAGS>(
          D3D12_SHADER_CACHE_CONTROL_FLAG_ENABLE |
          D3D12_SHADER_CACHE_CONTROL_FLAG_CLEAR),
  };
  for (const auto control : valid_controls)
    EXPECT_EQ(device9->ShaderCacheControl(kinds, control), S_OK)
        << "control=" << control;

  const std::array invalid_controls = {
      static_cast<D3D12_SHADER_CACHE_CONTROL_FLAGS>(0),
      static_cast<D3D12_SHADER_CACHE_CONTROL_FLAGS>(
          D3D12_SHADER_CACHE_CONTROL_FLAG_DISABLE |
          D3D12_SHADER_CACHE_CONTROL_FLAG_ENABLE),
      static_cast<D3D12_SHADER_CACHE_CONTROL_FLAGS>(8),
  };
  for (const auto control : invalid_controls)
    EXPECT_EQ(device9->ShaderCacheControl(kinds, control), E_INVALIDARG)
        << "control=" << control;

  const auto unknown_kind = static_cast<D3D12_SHADER_CACHE_KIND_FLAGS>(16);
  EXPECT_EQ(device9->ShaderCacheControl(
                unknown_kind, D3D12_SHADER_CACHE_CONTROL_FLAG_CLEAR),
            E_INVALIDARG);
}

} // namespace
