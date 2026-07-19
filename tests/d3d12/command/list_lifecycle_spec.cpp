#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"
#include "shaders/runtime_test_shaders.hpp"

#include <array>
#include <cstdint>
#include <cstring>

namespace {

using dxmt::test::ComPtr;
using dxmt::test::CreateIsolatedD3D12Device;
using dxmt::test::D3D12TestContext;

class CommandListLifecycleSpec : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_TRUE(SUCCEEDED(context_.Initialize())); }

  ComPtr<ID3D12Device> CreateIsolatedDevice() {
    return CreateIsolatedD3D12Device();
  }

  ComPtr<ID3D12PipelineState> CreateComputePipeline(ID3D12Device *device) {
    D3D12_ROOT_SIGNATURE_DESC root_desc = {};
    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> error;
    EXPECT_EQ(D3D12SerializeRootSignature(
                  &root_desc, D3D_ROOT_SIGNATURE_VERSION_1_0, signature.put(),
                  error.put()),
              S_OK);
    if (!signature)
      return {};

    ComPtr<ID3D12RootSignature> root_signature;
    EXPECT_EQ(device->CreateRootSignature(
                  0, signature->GetBufferPointer(), signature->GetBufferSize(),
                  __uuidof(ID3D12RootSignature),
                  reinterpret_cast<void **>(root_signature.put())),
              S_OK);
    if (!root_signature)
      return {};

    D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
    desc.pRootSignature = root_signature.get();
    desc.CS = dxmt::test::ClearBufferComputeShader();
    ComPtr<ID3D12PipelineState> pipeline;
    EXPECT_EQ(device->CreateComputePipelineState(
                  &desc, __uuidof(ID3D12PipelineState),
                  reinterpret_cast<void **>(pipeline.put())),
              S_OK);
    return pipeline;
  }

  ComPtr<ID3D12Resource> CreateBuffer(ID3D12Device *device, UINT64 size,
                                      D3D12_HEAP_TYPE heap_type,
                                      D3D12_RESOURCE_STATES state) {
    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = heap_type;
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = size;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> resource;
    EXPECT_EQ(device->CreateCommittedResource(
                  &heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr,
                  __uuidof(ID3D12Resource),
                  reinterpret_cast<void **>(resource.put())),
              S_OK);
    return resource;
  }

  D3D12TestContext context_;
};

TEST_F(CommandListLifecycleSpec, CreateCommandListStartsRecording) {
  ComPtr<ID3D12CommandAllocator> allocator;
  ASSERT_TRUE(SUCCEEDED(context_.device()->CreateCommandAllocator(
      D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator),
      reinterpret_cast<void **>(allocator.put()))));
  ComPtr<ID3D12GraphicsCommandList> list;
  ASSERT_TRUE(SUCCEEDED(context_.device()->CreateCommandList(
      0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.get(), nullptr,
      __uuidof(ID3D12GraphicsCommandList),
      reinterpret_cast<void **>(list.put()))));

  EXPECT_TRUE(SUCCEEDED(list->Close()));
}

TEST_F(CommandListLifecycleSpec, CreateCommandList1StartsClosedForCoreTypes) {
  ComPtr<ID3D12Device4> device4;
  ASSERT_EQ(context_.device()->QueryInterface(
                __uuidof(ID3D12Device4),
                reinterpret_cast<void **>(device4.put())),
            S_OK);
  constexpr std::array<D3D12_COMMAND_LIST_TYPE, 4> types = {
      D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_LIST_TYPE_BUNDLE,
      D3D12_COMMAND_LIST_TYPE_COMPUTE, D3D12_COMMAND_LIST_TYPE_COPY};

  for (std::size_t i = 0; i < types.size(); ++i) {
    ComPtr<ID3D12GraphicsCommandList> list;
    ASSERT_EQ(device4->CreateCommandList1(
                  static_cast<UINT>(i & 1), types[i],
                  D3D12_COMMAND_LIST_FLAG_NONE,
                  __uuidof(ID3D12GraphicsCommandList),
                  reinterpret_cast<void **>(list.put())),
              S_OK)
        << "type=" << types[i];
    ASSERT_TRUE(list) << "type=" << types[i];
    EXPECT_EQ(list->GetType(), types[i]);
    EXPECT_EQ(list->Close(), E_FAIL) << "type=" << types[i];

    ComPtr<ID3D12CommandAllocator> allocator;
    ASSERT_EQ(context_.device()->CreateCommandAllocator(
                  types[i], IID_PPV_ARGS(allocator.put())),
              S_OK)
        << "type=" << types[i];
    EXPECT_EQ(list->Reset(allocator.get(), nullptr), S_OK)
        << "type=" << types[i];
    EXPECT_EQ(list->Close(), S_OK) << "type=" << types[i];
  }
}

TEST_F(CommandListLifecycleSpec,
       CreateCommandList1RejectsInvalidInputsAndRecovers) {
  ComPtr<ID3D12Device4> device4;
  ASSERT_EQ(context_.device()->QueryInterface(
                __uuidof(ID3D12Device4),
                reinterpret_cast<void **>(device4.put())),
            S_OK);
  const GUID unsupported = {0x47de88d6,
                            0xd32f,
                            0x4129,
                            {0xb3, 0xca, 0xf7, 0x6a, 0x6e, 0x7c, 0x84, 0xc4}};
  void *output = reinterpret_cast<void *>(std::uintptr_t{1});

  EXPECT_EQ(device4->CreateCommandList1(
                2, D3D12_COMMAND_LIST_TYPE_DIRECT,
                D3D12_COMMAND_LIST_FLAG_NONE,
                __uuidof(ID3D12GraphicsCommandList), &output),
            E_INVALIDARG);
  EXPECT_EQ(output, nullptr);

  output = reinterpret_cast<void *>(std::uintptr_t{1});
  EXPECT_EQ(device4->CreateCommandList1(
                0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                static_cast<D3D12_COMMAND_LIST_FLAGS>(1),
                __uuidof(ID3D12GraphicsCommandList), &output),
            E_INVALIDARG);
  EXPECT_EQ(output, nullptr);

  output = reinterpret_cast<void *>(std::uintptr_t{1});
  EXPECT_EQ(device4->CreateCommandList1(
                0, D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE,
                D3D12_COMMAND_LIST_FLAG_NONE,
                __uuidof(ID3D12GraphicsCommandList), &output),
            E_INVALIDARG);
  EXPECT_EQ(output, nullptr);

  output = reinterpret_cast<void *>(std::uintptr_t{1});
  EXPECT_EQ(device4->CreateCommandList1(
                0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                D3D12_COMMAND_LIST_FLAG_NONE, unsupported, &output),
            E_NOINTERFACE);
  EXPECT_EQ(output, nullptr);
  EXPECT_EQ(device4->CreateCommandList1(
                0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                D3D12_COMMAND_LIST_FLAG_NONE,
                __uuidof(ID3D12GraphicsCommandList), nullptr),
            E_POINTER);

  ComPtr<ID3D12CommandList> list;
  ASSERT_EQ(device4->CreateCommandList1(
                1, D3D12_COMMAND_LIST_TYPE_DIRECT,
                D3D12_COMMAND_LIST_FLAG_NONE, IID_PPV_ARGS(list.put())),
            S_OK);
  ASSERT_TRUE(list);
  EXPECT_EQ(list->GetType(), D3D12_COMMAND_LIST_TYPE_DIRECT);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(CommandListLifecycleSpec, SecondCloseFails) {
  ASSERT_EQ(context_.list()->Close(), S_OK);

  EXPECT_EQ(context_.list()->Close(), E_FAIL);
}

TEST_F(CommandListLifecycleSpec, CreateCommandListRejectsInvalidNodeMask) {
  ComPtr<ID3D12GraphicsCommandList> list;
  EXPECT_EQ(context_.device()->CreateCommandList(
                2, D3D12_COMMAND_LIST_TYPE_DIRECT, context_.allocator(),
                nullptr, __uuidof(ID3D12GraphicsCommandList),
                reinterpret_cast<void **>(list.put())),
            E_INVALIDARG);
  EXPECT_FALSE(list);
}

TEST_F(CommandListLifecycleSpec, CreateCommandListRejectsWrongAllocatorType) {
  ComPtr<ID3D12CommandAllocator> allocator;
  ASSERT_EQ(context_.device()->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_COMPUTE,
                __uuidof(ID3D12CommandAllocator),
                reinterpret_cast<void **>(allocator.put())),
            S_OK);
  ComPtr<ID3D12GraphicsCommandList> list;
  EXPECT_EQ(context_.device()->CreateCommandList(
                0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.get(), nullptr,
                __uuidof(ID3D12GraphicsCommandList),
                reinterpret_cast<void **>(list.put())),
            E_INVALIDARG);
  EXPECT_FALSE(list);
}

TEST_F(CommandListLifecycleSpec,
       CreateCommandListAcceptsSameDeviceInitialPipelineState) {
  auto pipeline = CreateComputePipeline(context_.device());
  ASSERT_TRUE(pipeline);
  ComPtr<ID3D12CommandAllocator> allocator;
  ASSERT_EQ(context_.device()->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                __uuidof(ID3D12CommandAllocator),
                reinterpret_cast<void **>(allocator.put())),
            S_OK);
  ComPtr<ID3D12GraphicsCommandList> list;
  ASSERT_EQ(context_.device()->CreateCommandList(
                0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.get(),
                pipeline.get(), __uuidof(ID3D12GraphicsCommandList),
                reinterpret_cast<void **>(list.put())),
            S_OK);
  EXPECT_EQ(list->Close(), S_OK);
}

TEST_F(CommandListLifecycleSpec, CreateCommandListRejectsCrossDeviceAllocator) {
  auto foreign_device = CreateIsolatedDevice();
  ASSERT_TRUE(foreign_device);
  ComPtr<ID3D12CommandAllocator> allocator;
  ASSERT_EQ(foreign_device->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                __uuidof(ID3D12CommandAllocator),
                reinterpret_cast<void **>(allocator.put())),
            S_OK);
  ComPtr<ID3D12GraphicsCommandList> list;
  EXPECT_EQ(context_.device()->CreateCommandList(
                0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.get(), nullptr,
                __uuidof(ID3D12GraphicsCommandList),
                reinterpret_cast<void **>(list.put())),
            E_INVALIDARG);
  EXPECT_FALSE(list);
}

TEST_F(CommandListLifecycleSpec,
       CreateCommandListRejectsCrossDeviceInitialPipelineState) {
  auto foreign_device = CreateIsolatedDevice();
  ASSERT_TRUE(foreign_device);
  auto pipeline = CreateComputePipeline(foreign_device.get());
  ASSERT_TRUE(pipeline);
  ComPtr<ID3D12GraphicsCommandList> list;
  EXPECT_EQ(context_.device()->CreateCommandList(
                0, D3D12_COMMAND_LIST_TYPE_DIRECT, context_.allocator(),
                pipeline.get(), __uuidof(ID3D12GraphicsCommandList),
                reinterpret_cast<void **>(list.put())),
            E_INVALIDARG);
  EXPECT_FALSE(list);
}

TEST_F(CommandListLifecycleSpec, ResetClosedListSucceeds) {
  ASSERT_TRUE(SUCCEEDED(context_.list()->Close()));

  EXPECT_TRUE(SUCCEEDED(context_.list()->Reset(context_.allocator(), nullptr)));
}

TEST_F(CommandListLifecycleSpec, ResetRecordingListFails) {
  EXPECT_EQ(context_.list()->Reset(context_.allocator(), nullptr), E_FAIL);
}

TEST_F(CommandListLifecycleSpec, ResetWithWrongAllocatorTypeFails) {
  ASSERT_EQ(context_.list()->Close(), S_OK);
  ComPtr<ID3D12CommandAllocator> allocator;
  ASSERT_EQ(context_.device()->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_COMPUTE,
                __uuidof(ID3D12CommandAllocator),
                reinterpret_cast<void **>(allocator.put())),
            S_OK);

  EXPECT_EQ(context_.list()->Reset(allocator.get(), nullptr), E_INVALIDARG);
  ASSERT_EQ(context_.list()->Reset(context_.allocator(), nullptr), S_OK);
  EXPECT_EQ(context_.list()->Close(), S_OK);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(CommandListLifecycleSpec,
       ResetWithSameDeviceInitialPipelineStateSucceeds) {
  ASSERT_EQ(context_.list()->Close(), S_OK);
  auto pipeline = CreateComputePipeline(context_.device());
  ASSERT_TRUE(pipeline);

  EXPECT_EQ(context_.list()->Reset(context_.allocator(), pipeline.get()),
            S_OK);
}

TEST_F(CommandListLifecycleSpec, ResetWithCrossDeviceAllocatorFails) {
  ASSERT_EQ(context_.list()->Close(), S_OK);
  auto foreign_device = CreateIsolatedDevice();
  ASSERT_TRUE(foreign_device);
  ComPtr<ID3D12CommandAllocator> allocator;
  ASSERT_EQ(foreign_device->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                __uuidof(ID3D12CommandAllocator),
                reinterpret_cast<void **>(allocator.put())),
            S_OK);

  EXPECT_EQ(context_.list()->Reset(allocator.get(), nullptr), E_INVALIDARG);
  ASSERT_EQ(context_.list()->Reset(context_.allocator(), nullptr), S_OK);
  EXPECT_EQ(context_.list()->Close(), S_OK);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(CommandListLifecycleSpec,
       ResetWithCrossDeviceInitialPipelineStateFails) {
  ASSERT_EQ(context_.list()->Close(), S_OK);
  auto foreign_device = CreateIsolatedDevice();
  ASSERT_TRUE(foreign_device);
  auto pipeline = CreateComputePipeline(foreign_device.get());
  ASSERT_TRUE(pipeline);

  EXPECT_EQ(context_.list()->Reset(context_.allocator(), pipeline.get()),
            E_INVALIDARG);
  ASSERT_EQ(context_.list()->Reset(context_.allocator(), nullptr), S_OK);
  EXPECT_EQ(context_.list()->Close(), S_OK);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(CommandListLifecycleSpec, CreatePlacedResourceRejectsCrossDeviceHeap) {
  auto foreign_device = CreateIsolatedDevice();
  ASSERT_TRUE(foreign_device);

  D3D12_HEAP_DESC heap_desc = {};
  heap_desc.SizeInBytes = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
  heap_desc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
  heap_desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
  heap_desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
  ComPtr<ID3D12Heap> foreign_heap;
  ASSERT_EQ(foreign_device->CreateHeap(
                &heap_desc, __uuidof(ID3D12Heap),
                reinterpret_cast<void **>(foreign_heap.put())),
            S_OK);

  D3D12_RESOURCE_DESC resource_desc = {};
  resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  resource_desc.Width = 256;
  resource_desc.Height = 1;
  resource_desc.DepthOrArraySize = 1;
  resource_desc.MipLevels = 1;
  resource_desc.SampleDesc.Count = 1;
  resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  void *resource = reinterpret_cast<void *>(std::uintptr_t{1});
  EXPECT_EQ(context_.device()->CreatePlacedResource(
                foreign_heap.get(), 0, &resource_desc,
                D3D12_RESOURCE_STATE_COMMON, nullptr,
                __uuidof(ID3D12Resource), &resource),
            E_INVALIDARG);
  EXPECT_EQ(resource, nullptr);
}

TEST_F(CommandListLifecycleSpec, QueueSkipsCrossDeviceCommandList) {
  auto foreign_device = CreateIsolatedDevice();
  ASSERT_TRUE(foreign_device);
  constexpr std::array<std::uint32_t, 4> expected = {
      0x11223344u, 0x55667788u, 0x99aabbccu, 0xddeeff00u};
  auto source = CreateBuffer(foreign_device.get(), sizeof(expected),
                             D3D12_HEAP_TYPE_UPLOAD,
                             D3D12_RESOURCE_STATE_GENERIC_READ);
  auto destination = CreateBuffer(foreign_device.get(), sizeof(expected),
                                  D3D12_HEAP_TYPE_READBACK,
                                  D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(source);
  ASSERT_TRUE(destination);

  void *mapped = nullptr;
  ASSERT_EQ(source->Map(0, nullptr, &mapped), S_OK);
  ASSERT_NE(mapped, nullptr);
  std::memcpy(mapped, expected.data(), sizeof(expected));
  source->Unmap(0, nullptr);
  ASSERT_EQ(destination->Map(0, nullptr, &mapped), S_OK);
  ASSERT_NE(mapped, nullptr);
  std::memset(mapped, 0, sizeof(expected));
  destination->Unmap(0, nullptr);

  ComPtr<ID3D12CommandAllocator> allocator;
  ASSERT_EQ(foreign_device->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                __uuidof(ID3D12CommandAllocator),
                reinterpret_cast<void **>(allocator.put())),
            S_OK);
  ComPtr<ID3D12GraphicsCommandList> list;
  ASSERT_EQ(foreign_device->CreateCommandList(
                0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.get(), nullptr,
                __uuidof(ID3D12GraphicsCommandList),
                reinterpret_cast<void **>(list.put())),
            S_OK);
  list->CopyBufferRegion(destination.get(), 0, source.get(), 0,
                         sizeof(expected));
  ASSERT_EQ(list->Close(), S_OK);

  ID3D12CommandList *submission[] = {list.get()};
  context_.queue()->ExecuteCommandLists(1, submission);
  ASSERT_EQ(context_.SignalAndWait(), S_OK);

  ASSERT_EQ(destination->Map(0, nullptr, &mapped), S_OK);
  ASSERT_NE(mapped, nullptr);
  std::array<std::uint32_t, expected.size()> actual = {};
  std::memcpy(actual.data(), mapped, sizeof(actual));
  destination->Unmap(0, nullptr);
  EXPECT_EQ(actual, (std::array<std::uint32_t, expected.size()>{}));
}

TEST_F(CommandListLifecycleSpec, ResetWithAllocatorUsedByRecordingListFails) {
  ASSERT_TRUE(SUCCEEDED(context_.list()->Close()));
  ComPtr<ID3D12CommandAllocator> occupied_allocator;
  ASSERT_TRUE(SUCCEEDED(context_.device()->CreateCommandAllocator(
      D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator),
      reinterpret_cast<void **>(occupied_allocator.put()))));
  ComPtr<ID3D12GraphicsCommandList> recording_list;
  ASSERT_TRUE(SUCCEEDED(context_.device()->CreateCommandList(
      0, D3D12_COMMAND_LIST_TYPE_DIRECT, occupied_allocator.get(), nullptr,
      __uuidof(ID3D12GraphicsCommandList),
      reinterpret_cast<void **>(recording_list.put()))));

  EXPECT_TRUE(
      FAILED(context_.list()->Reset(occupied_allocator.get(), nullptr)));
}

} // namespace
