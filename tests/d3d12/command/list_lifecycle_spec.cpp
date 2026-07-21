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
    D3D12_DESCRIPTOR_RANGE range = {};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    range.NumDescriptors = 1;
    D3D12_ROOT_PARAMETER parameters[2] = {};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[0].Constants.Num32BitValues = 1;
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[1].DescriptorTable.NumDescriptorRanges = 1;
    parameters[1].DescriptorTable.pDescriptorRanges = &range;
    D3D12_ROOT_SIGNATURE_DESC root_desc = {};
    root_desc.NumParameters = 2;
    root_desc.pParameters = parameters;
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


TEST_F(CommandListLifecycleSpec, SecondCloseFails) {
  ASSERT_EQ(context_.list()->Close(), S_OK);

  EXPECT_EQ(context_.list()->Close(), E_FAIL);
}

TEST_F(CommandListLifecycleSpec, FailedCloseCannotBeReset) {
  ComPtr<ID3D12GraphicsCommandList4> list4;
  ASSERT_EQ(context_.list()->QueryInterface(IID_PPV_ARGS(list4.put())), S_OK);
  list4->BeginRenderPass(0, nullptr, nullptr, D3D12_RENDER_PASS_FLAG_NONE);

  const HRESULT close_result = context_.list()->Close();
  ASSERT_EQ(close_result, E_FAIL);
  EXPECT_EQ(context_.list()->Reset(context_.allocator(), nullptr),
            close_result);
}

TEST_F(CommandListLifecycleSpec, FailedCloseErrorIsStickyAcrossReset) {
  ComPtr<ID3D12GraphicsCommandList4> list4;
  ASSERT_EQ(context_.list()->QueryInterface(IID_PPV_ARGS(list4.put())), S_OK);
  list4->BeginRenderPass(0, nullptr, nullptr, D3D12_RENDER_PASS_FLAG_NONE);
  list4->BeginRenderPass(0, nullptr, nullptr, D3D12_RENDER_PASS_FLAG_NONE);
  list4->EndRenderPass();

  const HRESULT close_result = context_.list()->Close();
  ASSERT_EQ(close_result, E_INVALIDARG);
  EXPECT_EQ(context_.list()->Reset(context_.allocator(), nullptr),
            close_result);
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

TEST_F(CommandListLifecycleSpec, CreateCommandListRejectsNullAllocator) {
  ComPtr<ID3D12GraphicsCommandList> list;
  EXPECT_EQ(context_.device()->CreateCommandList(
                0, D3D12_COMMAND_LIST_TYPE_DIRECT, nullptr, nullptr,
                IID_PPV_ARGS(list.put())),
            E_INVALIDARG);
  EXPECT_FALSE(list);
}

TEST_F(CommandListLifecycleSpec,
       CreateCommandListRejectsAllocatorUsedByRecordingList) {
  ComPtr<ID3D12GraphicsCommandList> list;
  EXPECT_EQ(context_.device()->CreateCommandList(
                0, D3D12_COMMAND_LIST_TYPE_DIRECT, context_.allocator(),
                nullptr, IID_PPV_ARGS(list.put())),
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


TEST_F(CommandListLifecycleSpec, ResetClosedListSucceeds) {
  ASSERT_TRUE(SUCCEEDED(context_.list()->Close()));

  EXPECT_TRUE(SUCCEEDED(context_.list()->Reset(context_.allocator(), nullptr)));
}

TEST_F(CommandListLifecycleSpec, ResetRecordingListFails) {
  EXPECT_EQ(context_.list()->Reset(context_.allocator(), nullptr), E_FAIL);
}

TEST_F(CommandListLifecycleSpec, ResetWithNullAllocatorFails) {
  ASSERT_EQ(context_.list()->Close(), S_OK);

  EXPECT_TRUE(FAILED(context_.list()->Reset(nullptr, nullptr)));
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

  EXPECT_EQ(context_.list()->Reset(occupied_allocator.get(), nullptr),
            E_INVALIDARG);
  ASSERT_EQ(recording_list->Close(), S_OK);
  ASSERT_EQ(context_.list()->Reset(context_.allocator(), nullptr), S_OK);
  EXPECT_EQ(context_.list()->Close(), S_OK);
}

} // namespace
