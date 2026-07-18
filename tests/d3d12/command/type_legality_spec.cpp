#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

#include <array>

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

struct CommandListPair {
  ComPtr<ID3D12CommandAllocator> allocator;
  ComPtr<ID3D12GraphicsCommandList> list;
};

class CommandTypeLegalitySpec : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  CommandListPair CreateList(D3D12_COMMAND_LIST_TYPE type) {
    CommandListPair pair;
    EXPECT_EQ(context_.device()->CreateCommandAllocator(
                  type, __uuidof(ID3D12CommandAllocator),
                  reinterpret_cast<void **>(pair.allocator.put())),
              S_OK);
    if (!pair.allocator)
      return pair;
    EXPECT_EQ(context_.device()->CreateCommandList(
                  0, type, pair.allocator.get(), nullptr,
                  __uuidof(ID3D12GraphicsCommandList),
                  reinterpret_cast<void **>(pair.list.put())),
              S_OK);
    return pair;
  }

  D3D12TestContext context_;
};

TEST_F(CommandTypeLegalitySpec, RejectsGraphicsCommandsOnComputeAndCopyLists) {
  auto target = context_.CreateTexture2D(
      4, 4, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
      D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
      D3D12_RESOURCE_STATE_RENDER_TARGET);
  auto rtv_heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
  ASSERT_TRUE(target);
  ASSERT_TRUE(rtv_heap);
  const auto rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
  context_.device()->CreateRenderTargetView(target.get(), nullptr, rtv);
  constexpr FLOAT clear_color[4] = {};

  for (const auto type : {D3D12_COMMAND_LIST_TYPE_COMPUTE,
                          D3D12_COMMAND_LIST_TYPE_COPY}) {
    SCOPED_TRACE(static_cast<UINT>(type));
    auto draw = CreateList(type);
    ASSERT_TRUE(draw.list);
    draw.list->DrawInstanced(3, 1, 0, 0);
    EXPECT_EQ(draw.list->Close(), E_FAIL);

    auto input_assembler = CreateList(type);
    ASSERT_TRUE(input_assembler.list);
    input_assembler.list->IASetPrimitiveTopology(
        D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    EXPECT_EQ(input_assembler.list->Close(), E_FAIL);

    auto clear = CreateList(type);
    ASSERT_TRUE(clear.list);
    clear.list->ClearRenderTargetView(rtv, clear_color, 0, nullptr);
    EXPECT_EQ(clear.list->Close(), E_FAIL);
  }
}

TEST_F(CommandTypeLegalitySpec, ReportsCreationTypeForEveryListKind) {
  for (const auto type : {D3D12_COMMAND_LIST_TYPE_DIRECT,
                          D3D12_COMMAND_LIST_TYPE_BUNDLE,
                          D3D12_COMMAND_LIST_TYPE_COMPUTE,
                          D3D12_COMMAND_LIST_TYPE_COPY}) {
    SCOPED_TRACE(static_cast<UINT>(type));
    auto pair = CreateList(type);
    ASSERT_TRUE(pair.list);
    EXPECT_EQ(pair.list->GetType(), type);
    EXPECT_EQ(pair.list->Close(), S_OK);
  }
}

TEST_F(CommandTypeLegalitySpec, RejectsComputeAndBindingCommandsOnCopyLists) {
  auto dispatch = CreateList(D3D12_COMMAND_LIST_TYPE_COPY);
  ASSERT_TRUE(dispatch.list);
  dispatch.list->Dispatch(1, 1, 1);
  EXPECT_EQ(dispatch.list->Close(), E_FAIL);

  auto clear_state = CreateList(D3D12_COMMAND_LIST_TYPE_COPY);
  ASSERT_TRUE(clear_state.list);
  clear_state.list->ClearState(nullptr);
  EXPECT_EQ(clear_state.list->Close(), E_FAIL);

  auto pipeline = CreateList(D3D12_COMMAND_LIST_TYPE_COPY);
  ASSERT_TRUE(pipeline.list);
  pipeline.list->SetPipelineState(nullptr);
  EXPECT_EQ(pipeline.list->Close(), E_FAIL);

  auto heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true);
  ASSERT_TRUE(heap);
  ID3D12DescriptorHeap *heaps[] = {heap.get()};
  auto descriptors = CreateList(D3D12_COMMAND_LIST_TYPE_COPY);
  ASSERT_TRUE(descriptors.list);
  descriptors.list->SetDescriptorHeaps(1, heaps);
  EXPECT_EQ(descriptors.list->Close(), E_FAIL);
}

TEST_F(CommandTypeLegalitySpec, AcceptsCopiesOnEveryCopyCapableList) {
  constexpr std::array<UINT, 4> values = {1, 2, 3, 4};
  auto source = context_.CreateUploadBuffer(
      sizeof(values), values.data(), sizeof(values));
  auto destination = context_.CreateBuffer(
      sizeof(values), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(source);
  ASSERT_TRUE(destination);

  for (const auto type : {D3D12_COMMAND_LIST_TYPE_DIRECT,
                          D3D12_COMMAND_LIST_TYPE_COMPUTE,
                          D3D12_COMMAND_LIST_TYPE_COPY}) {
    SCOPED_TRACE(static_cast<UINT>(type));
    auto pair = CreateList(type);
    ASSERT_TRUE(pair.list);
    pair.list->CopyBufferRegion(destination.get(), 0, source.get(), 0,
                                sizeof(values));
    EXPECT_EQ(pair.list->Close(), S_OK);
  }
}

TEST_F(CommandTypeLegalitySpec, AcceptsBarriersOnEveryBarrierCapableList) {
  auto resource = context_.CreateBuffer(
      256, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COMMON);
  ASSERT_TRUE(resource);
  D3D12_RESOURCE_BARRIER barrier = {};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource = resource.get();
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
  barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;

  for (const auto type : {D3D12_COMMAND_LIST_TYPE_DIRECT,
                          D3D12_COMMAND_LIST_TYPE_COMPUTE,
                          D3D12_COMMAND_LIST_TYPE_COPY}) {
    SCOPED_TRACE(static_cast<UINT>(type));
    auto pair = CreateList(type);
    ASSERT_TRUE(pair.list);
    pair.list->ResourceBarrier(1, &barrier);
    EXPECT_EQ(pair.list->Close(), S_OK);
  }
}

TEST_F(CommandTypeLegalitySpec, EnforcesQueryCommandListTypes) {
  D3D12_QUERY_HEAP_DESC timestamp_desc = {};
  timestamp_desc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
  timestamp_desc.Count = 1;
  ComPtr<ID3D12QueryHeap> timestamp_heap;
  ASSERT_EQ(context_.device()->CreateQueryHeap(
                &timestamp_desc, __uuidof(ID3D12QueryHeap),
                reinterpret_cast<void **>(timestamp_heap.put())),
            S_OK);
  auto destination = context_.CreateBuffer(
      sizeof(UINT64), D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(destination);

  for (const auto type : {D3D12_COMMAND_LIST_TYPE_DIRECT,
                          D3D12_COMMAND_LIST_TYPE_COMPUTE,
                          D3D12_COMMAND_LIST_TYPE_COPY}) {
    SCOPED_TRACE(static_cast<UINT>(type));
    auto pair = CreateList(type);
    ASSERT_TRUE(pair.list);
    pair.list->EndQuery(timestamp_heap.get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
    pair.list->ResolveQueryData(timestamp_heap.get(),
                                D3D12_QUERY_TYPE_TIMESTAMP, 0, 1,
                                destination.get(), 0);
    EXPECT_EQ(pair.list->Close(), S_OK);
  }

  D3D12_QUERY_HEAP_DESC occlusion_desc = {};
  occlusion_desc.Type = D3D12_QUERY_HEAP_TYPE_OCCLUSION;
  occlusion_desc.Count = 1;
  ComPtr<ID3D12QueryHeap> occlusion_heap;
  ASSERT_EQ(context_.device()->CreateQueryHeap(
                &occlusion_desc, __uuidof(ID3D12QueryHeap),
                reinterpret_cast<void **>(occlusion_heap.put())),
            S_OK);
  for (const auto type : {D3D12_COMMAND_LIST_TYPE_COMPUTE,
                          D3D12_COMMAND_LIST_TYPE_COPY}) {
    SCOPED_TRACE(static_cast<UINT>(type));
    auto pair = CreateList(type);
    ASSERT_TRUE(pair.list);
    pair.list->BeginQuery(occlusion_heap.get(), D3D12_QUERY_TYPE_OCCLUSION, 0);
    EXPECT_EQ(pair.list->Close(), E_FAIL);
  }
}

TEST_F(CommandTypeLegalitySpec, RejectsExecuteIndirectOnCopyLists) {
  D3D12_INDIRECT_ARGUMENT_DESC argument = {};
  argument.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
  D3D12_COMMAND_SIGNATURE_DESC signature_desc = {};
  signature_desc.ByteStride = sizeof(D3D12_DISPATCH_ARGUMENTS);
  signature_desc.NumArgumentDescs = 1;
  signature_desc.pArgumentDescs = &argument;
  ComPtr<ID3D12CommandSignature> signature;
  ASSERT_EQ(context_.device()->CreateCommandSignature(
                &signature_desc, nullptr, __uuidof(ID3D12CommandSignature),
                reinterpret_cast<void **>(signature.put())),
            S_OK);
  constexpr D3D12_DISPATCH_ARGUMENTS arguments = {1, 1, 1};
  auto buffer = context_.CreateUploadBuffer(
      sizeof(arguments), &arguments, sizeof(arguments));
  ASSERT_TRUE(buffer);

  auto pair = CreateList(D3D12_COMMAND_LIST_TYPE_COPY);
  ASSERT_TRUE(pair.list);
  pair.list->ExecuteIndirect(signature.get(), 1, buffer.get(), 0, nullptr, 0);
  EXPECT_EQ(pair.list->Close(), E_FAIL);
}

TEST_F(CommandTypeLegalitySpec, FreshListWorksAfterTypeValidationFailure) {
  auto invalid = CreateList(D3D12_COMMAND_LIST_TYPE_COPY);
  ASSERT_TRUE(invalid.list);
  invalid.list->Dispatch(1, 1, 1);
  ASSERT_EQ(invalid.list->Close(), E_FAIL);

  constexpr UINT value = 0x12345678;
  auto source = context_.CreateUploadBuffer(sizeof(value), &value,
                                             sizeof(value));
  auto destination = context_.CreateBuffer(
      sizeof(value), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(source);
  ASSERT_TRUE(destination);
  auto valid = CreateList(D3D12_COMMAND_LIST_TYPE_COPY);
  ASSERT_TRUE(valid.list);
  valid.list->CopyBufferRegion(destination.get(), 0, source.get(), 0,
                               sizeof(value));
  EXPECT_EQ(valid.list->Close(), S_OK);
}

} // namespace
