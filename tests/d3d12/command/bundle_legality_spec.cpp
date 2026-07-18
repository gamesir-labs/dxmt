#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"

#include <array>
#include <cstring>
#include <vector>

namespace {

using dxmt::test::ComPtr;
using dxmt::test::CompileShader;
using dxmt::test::CreateIsolatedD3D12Device;
using dxmt::test::D3D12TestContext;

class BundleLegalitySpec : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  D3D12TestContext context_;
};

TEST_F(BundleLegalitySpec, DropsCommandsForbiddenInBundles) {
  constexpr std::array<UINT, 8> initial = {};
  constexpr std::array<UINT, 8> forbidden_copy = {
      0x01020304, 0x11121314, 0x21222324, 0x31323334,
      0x41424344, 0x51525354, 0x61626364, 0x71727374,
  };
  auto initial_upload = context_.CreateUploadBuffer(
      sizeof(initial), initial.data(), sizeof(initial));
  auto forbidden_upload = context_.CreateUploadBuffer(
      sizeof(forbidden_copy), forbidden_copy.data(), sizeof(forbidden_copy));
  auto destination = context_.CreateBuffer(
      sizeof(initial), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(initial_upload);
  ASSERT_TRUE(forbidden_upload);
  ASSERT_TRUE(destination);
  context_.list()->CopyBufferRegion(destination.get(), 0, initial_upload.get(),
                                    0, sizeof(initial));
  ASSERT_EQ(context_.ExecuteAndWait(), S_OK);
  ASSERT_EQ(context_.ResetCommandList(), S_OK);

  ComPtr<ID3D12CommandAllocator> allocator;
  ComPtr<ID3D12GraphicsCommandList> bundle;
  ASSERT_EQ(context_.device()->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_BUNDLE,
                __uuidof(ID3D12CommandAllocator),
                reinterpret_cast<void **>(allocator.put())),
            S_OK);
  ASSERT_EQ(context_.device()->CreateCommandList(
                0, D3D12_COMMAND_LIST_TYPE_BUNDLE, allocator.get(), nullptr,
                __uuidof(ID3D12GraphicsCommandList),
                reinterpret_cast<void **>(bundle.put())),
            S_OK);
  ComPtr<ID3D12GraphicsCommandList1> bundle1;
  ASSERT_EQ(bundle->QueryInterface(
                __uuidof(ID3D12GraphicsCommandList1),
                reinterpret_cast<void **>(bundle1.put())),
            S_OK);
  D3D12_QUERY_HEAP_DESC query_desc = {};
  query_desc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
  query_desc.Count = 1;
  ComPtr<ID3D12QueryHeap> query_heap;
  ASSERT_EQ(context_.device()->CreateQueryHeap(
                &query_desc, __uuidof(ID3D12QueryHeap),
                reinterpret_cast<void **>(query_heap.put())),
            S_OK);
  bundle->CopyBufferRegion(destination.get(), 0, forbidden_upload.get(), 0,
                           sizeof(forbidden_copy));
  D3D12_RESOURCE_BARRIER barrier = {};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource = destination.get();
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
  barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
  bundle->ResourceBarrier(1, &barrier);
  D3D12_STREAM_OUTPUT_BUFFER_VIEW stream_output = {};
  stream_output.BufferLocation = destination->GetGPUVirtualAddress();
  stream_output.SizeInBytes = sizeof(initial);
  stream_output.BufferFilledSizeLocation = destination->GetGPUVirtualAddress();
  bundle->SOSetTargets(0, 1, &stream_output);
  bundle->ResolveQueryData(query_heap.get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 1,
                           destination.get(), 0);
  bundle1->AtomicCopyBufferUINT(destination.get(), 0, forbidden_upload.get(),
                                0, 0, nullptr, nullptr);
  bundle1->AtomicCopyBufferUINT64(destination.get(), 0,
                                  forbidden_upload.get(), 0, 0, nullptr,
                                  nullptr);
  ASSERT_EQ(bundle->Close(), S_OK);

  context_.list()->ExecuteBundle(bundle.get());
  D3D12TestContext::Transition(
      context_.list(), destination.get(), D3D12_RESOURCE_STATE_COPY_DEST,
      D3D12_RESOURCE_STATE_COPY_SOURCE);
  std::vector<std::uint8_t> bytes;
  ASSERT_EQ(context_.ReadbackBuffer(destination.get(), sizeof(initial),
                                    &bytes),
            S_OK);
  ASSERT_EQ(bytes.size(), sizeof(initial));
  EXPECT_EQ(std::memcmp(bytes.data(), initial.data(), sizeof(initial)), 0);
}

TEST_F(BundleLegalitySpec, ExecutesDispatchAndDropsClearState) {
  constexpr UINT expected = 0x13579bdf;
  const auto shader = CompileShader(R"(
    RWByteAddressBuffer output : register(u0);

    [numthreads(1, 1, 1)]
    void main() {
      output.Store(0, 0x13579bdf);
    }
  )",
                                    "cs_5_0");
  ASSERT_EQ(shader.result, S_OK) << shader.diagnostic_text();

  D3D12_ROOT_PARAMETER parameter = {};
  parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
  parameter.Descriptor.ShaderRegister = 0;
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  root_desc.NumParameters = 1;
  root_desc.pParameters = &parameter;
  auto root_signature = context_.CreateRootSignature(root_desc);
  const D3D12_SHADER_BYTECODE bytecode = {
      shader.bytecode->GetBufferPointer(), shader.bytecode->GetBufferSize()};
  auto pipeline =
      context_.CreateComputePipeline(root_signature.get(), bytecode);
  auto output = context_.CreateBuffer(
      sizeof(expected), D3D12_HEAP_TYPE_DEFAULT,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  ASSERT_TRUE(root_signature);
  ASSERT_TRUE(pipeline);
  ASSERT_TRUE(output);

  ComPtr<ID3D12CommandAllocator> allocator;
  ComPtr<ID3D12GraphicsCommandList> bundle;
  ASSERT_EQ(context_.device()->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_BUNDLE,
                __uuidof(ID3D12CommandAllocator),
                reinterpret_cast<void **>(allocator.put())),
            S_OK);
  ASSERT_EQ(context_.device()->CreateCommandList(
                0, D3D12_COMMAND_LIST_TYPE_BUNDLE, allocator.get(), nullptr,
                __uuidof(ID3D12GraphicsCommandList),
                reinterpret_cast<void **>(bundle.put())),
            S_OK);
  bundle->SetPipelineState(pipeline.get());
  bundle->ClearState(nullptr);
  bundle->SetComputeRootSignature(root_signature.get());
  bundle->SetComputeRootUnorderedAccessView(
      0, output->GetGPUVirtualAddress());
  bundle->Dispatch(1, 1, 1);
  ASSERT_EQ(bundle->Close(), S_OK);

  context_.list()->ExecuteBundle(bundle.get());
  D3D12TestContext::Transition(
      context_.list(), output.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_COPY_SOURCE);
  std::vector<std::uint8_t> bytes;
  ASSERT_EQ(context_.ReadbackBuffer(output.get(), sizeof(expected), &bytes),
            S_OK);
  ASSERT_EQ(bytes.size(), sizeof(expected));
  UINT actual = 0;
  std::memcpy(&actual, bytes.data(), sizeof(actual));
  EXPECT_EQ(actual, expected);
}

TEST_F(BundleLegalitySpec, RejectsForeignBundleAndAllowsFreshListRecovery) {
  auto foreign_device = CreateIsolatedD3D12Device();
  ASSERT_TRUE(foreign_device);
  D3D12TestContext foreign_context;
  ASSERT_EQ(foreign_context.Initialize(foreign_device.get()), S_OK);
  ComPtr<ID3D12CommandAllocator> foreign_allocator;
  ComPtr<ID3D12GraphicsCommandList> foreign_bundle;
  ASSERT_EQ(foreign_device->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_BUNDLE,
                IID_PPV_ARGS(foreign_allocator.put())),
            S_OK);
  ASSERT_EQ(foreign_device->CreateCommandList(
                0, D3D12_COMMAND_LIST_TYPE_BUNDLE, foreign_allocator.get(),
                nullptr, IID_PPV_ARGS(foreign_bundle.put())),
            S_OK);
  ASSERT_EQ(foreign_bundle->Close(), S_OK);

  context_.list()->ExecuteBundle(foreign_bundle.get());
  EXPECT_EQ(context_.list()->Close(), E_INVALIDARG);

  ComPtr<ID3D12CommandAllocator> bundle_allocator;
  ComPtr<ID3D12GraphicsCommandList> bundle;
  ASSERT_EQ(context_.device()->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_BUNDLE,
                IID_PPV_ARGS(bundle_allocator.put())),
            S_OK);
  ASSERT_EQ(context_.device()->CreateCommandList(
                0, D3D12_COMMAND_LIST_TYPE_BUNDLE, bundle_allocator.get(),
                nullptr, IID_PPV_ARGS(bundle.put())),
            S_OK);
  ASSERT_EQ(bundle->Close(), S_OK);
  ComPtr<ID3D12CommandAllocator> recovery_allocator;
  ComPtr<ID3D12GraphicsCommandList> recovery_list;
  ASSERT_EQ(context_.device()->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                IID_PPV_ARGS(recovery_allocator.put())),
            S_OK);
  ASSERT_EQ(context_.device()->CreateCommandList(
                0, D3D12_COMMAND_LIST_TYPE_DIRECT, recovery_allocator.get(),
                nullptr, IID_PPV_ARGS(recovery_list.put())),
            S_OK);
  recovery_list->ExecuteBundle(bundle.get());
  EXPECT_EQ(recovery_list->Close(), S_OK);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(BundleLegalitySpec,
       ExecutesSameBundleFromTwoDirectListsAfterBundleRelease) {
  ComPtr<ID3D12CommandAllocator> bundle_allocator;
  ComPtr<ID3D12GraphicsCommandList> bundle;
  ASSERT_EQ(context_.device()->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_BUNDLE,
                IID_PPV_ARGS(bundle_allocator.put())),
            S_OK);
  ASSERT_EQ(context_.device()->CreateCommandList(
                0, D3D12_COMMAND_LIST_TYPE_BUNDLE, bundle_allocator.get(),
                nullptr, IID_PPV_ARGS(bundle.put())),
            S_OK);
  bundle->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  ASSERT_EQ(bundle->Close(), S_OK);

  ComPtr<ID3D12CommandAllocator> second_allocator;
  ComPtr<ID3D12GraphicsCommandList> second_list;
  ASSERT_EQ(context_.device()->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                IID_PPV_ARGS(second_allocator.put())),
            S_OK);
  ASSERT_EQ(context_.device()->CreateCommandList(
                0, D3D12_COMMAND_LIST_TYPE_DIRECT, second_allocator.get(),
                nullptr, IID_PPV_ARGS(second_list.put())),
            S_OK);
  context_.list()->ExecuteBundle(bundle.get());
  second_list->ExecuteBundle(bundle.get());
  bundle.reset();
  bundle_allocator.reset();

  EXPECT_EQ(context_.list()->Close(), S_OK);
  EXPECT_EQ(second_list->Close(), S_OK);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
