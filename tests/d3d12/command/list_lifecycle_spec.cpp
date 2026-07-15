#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"
#include "shaders/runtime_test_shaders.hpp"

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

class CommandListLifecycleSpec : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_TRUE(SUCCEEDED(context_.Initialize())); }

  ComPtr<ID3D12Device> CreateIsolatedDevice() {
    using CreateDeviceProc = HRESULT(WINAPI *)(IUnknown *, D3D_FEATURE_LEVEL,
                                                REFIID, void **);
    const auto create_device = reinterpret_cast<CreateDeviceProc>(
        GetProcAddress(GetModuleHandleW(L"d3d12.dll"),
                       "DXMTCreateD3D12DeviceFromFactory"));
    EXPECT_NE(create_device, nullptr);
    if (!create_device)
      return {};

    ComPtr<ID3D12Device> device;
    EXPECT_TRUE(SUCCEEDED(create_device(
        nullptr, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device),
        reinterpret_cast<void **>(device.put()))));
    return device;
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

TEST_F(CommandListLifecycleSpec, CreateCommandList1StartsClosed) {
  ComPtr<ID3D12Device4> device4;
  ASSERT_TRUE(SUCCEEDED(context_.device()->QueryInterface(
      __uuidof(ID3D12Device4), reinterpret_cast<void **>(device4.put()))));
  ComPtr<ID3D12GraphicsCommandList> list;
  ASSERT_TRUE(SUCCEEDED(device4->CreateCommandList1(
      0, D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_LIST_FLAG_NONE,
      __uuidof(ID3D12GraphicsCommandList),
      reinterpret_cast<void **>(list.put()))));

  EXPECT_EQ(list->Close(), E_FAIL);
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

TEST_F(CommandListLifecycleSpec, ResetWithNullAllocatorFails) {
  ASSERT_TRUE(SUCCEEDED(context_.list()->Close()));

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

TEST_F(CommandListLifecycleSpec, FailedCloseCannotBeReset) {
  D3D12_RESOURCE_BARRIER invalid = {};
  invalid.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  invalid.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
  invalid.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
  context_.list()->ResourceBarrier(1, &invalid);

  EXPECT_EQ(context_.list()->Close(), E_INVALIDARG);
  EXPECT_EQ(context_.list()->Reset(context_.allocator(), nullptr), E_FAIL);
}

} // namespace
