#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

namespace {

using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;
using dxmt::test::TextureReadback;

constexpr UINT kElementCount = 32;
constexpr UINT kTextureWidth = 13;
constexpr UINT kTextureHeight = 9;

struct ComputeProgram {
  ComPtr<ID3D12RootSignature> root_signature;
  ComPtr<ID3D12PipelineState> pipeline;
};

class LegacyProducerConsumerExecutionSpec : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  ComputeProgram CreateComputeProgram(
      const char *source,
      const std::vector<D3D12_ROOT_PARAMETER_TYPE> &parameter_types) {
    const auto shader = CompileShader(source, "cs_5_0");
    EXPECT_EQ(shader.result, S_OK) << shader.diagnostic_text();
    if (shader.result != S_OK || !shader.bytecode)
      return {};

    std::vector<D3D12_ROOT_PARAMETER> parameters(parameter_types.size());
    for (std::size_t index = 0; index < parameter_types.size(); ++index) {
      parameters[index].ParameterType = parameter_types[index];
      parameters[index].Descriptor.ShaderRegister = 0;
      parameters[index].Descriptor.RegisterSpace = 0;
      parameters[index].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }

    D3D12_ROOT_SIGNATURE_DESC root_desc = {};
    root_desc.NumParameters = static_cast<UINT>(parameters.size());
    root_desc.pParameters = parameters.data();
    ComputeProgram result;
    result.root_signature = context_.CreateRootSignature(root_desc);
    EXPECT_TRUE(result.root_signature);
    if (!result.root_signature)
      return {};

    result.pipeline = context_.CreateComputePipeline(
        result.root_signature.get(),
        {shader.bytecode->GetBufferPointer(),
         shader.bytecode->GetBufferSize()});
    EXPECT_TRUE(result.pipeline);
    return result;
  }

  ComPtr<ID3D12Resource> CreateColorTexture(
      UINT sample_count, D3D12_RESOURCE_FLAGS flags,
      D3D12_RESOURCE_STATES state) {
    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = kTextureWidth;
    desc.Height = kTextureHeight;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = sample_count;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = flags;

    ComPtr<ID3D12Resource> texture;
    const HRESULT hr = context_.device()->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr,
        IID_PPV_ARGS(texture.put()));
    EXPECT_EQ(hr, S_OK);
    return SUCCEEDED(hr) ? std::move(texture) : ComPtr<ID3D12Resource>{};
  }

  void ExpectWords(const std::vector<std::uint8_t> &bytes,
                   const std::array<UINT, kElementCount> &expected,
                   const char *chain) {
    ASSERT_EQ(bytes.size(), sizeof(expected)) << chain;
    for (UINT index = 0; index < expected.size(); ++index) {
      UINT actual = 0;
      std::memcpy(&actual, bytes.data() + index * sizeof(actual),
                  sizeof(actual));
      EXPECT_EQ(actual, expected[index]) << chain << ", element " << index;
    }
  }

  void ExpectSolidTexture(const TextureReadback &readback,
                          std::uint32_t expected, const char *chain) {
    ASSERT_EQ(readback.width, kTextureWidth) << chain;
    ASSERT_EQ(readback.height, kTextureHeight) << chain;
    ASSERT_GE(readback.row_pitch,
              static_cast<UINT64>(kTextureWidth * sizeof(expected)))
        << chain;
    const auto minimum_size =
        static_cast<std::size_t>(readback.row_pitch * (kTextureHeight - 1) +
                                 kTextureWidth * sizeof(expected));
    ASSERT_GE(readback.data.size(), minimum_size)
        << chain;
    for (UINT y = 0; y < kTextureHeight; ++y) {
      for (UINT x = 0; x < kTextureWidth; ++x) {
        std::uint32_t actual = 0;
        std::memcpy(&actual,
                    readback.data.data() + y * readback.row_pitch +
                        x * sizeof(actual),
                    sizeof(actual));
        EXPECT_EQ(actual, expected)
            << chain << ", pixel (" << x << ", " << y << ")";
      }
    }
  }

  void ExpectDeviceHealthy() {
    EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
  }

  D3D12TestContext context_;
};

TEST_F(LegacyProducerConsumerExecutionSpec,
       CopyToSrvTransitionPublishesEveryBufferWord) {
  std::array<UINT, kElementCount> source = {};
  std::array<UINT, kElementCount> expected = {};
  for (UINT index = 0; index < source.size(); ++index) {
    source[index] = 0x10203040u + index * 0x01020408u;
    expected[index] = source[index] ^ 0xa5a55a5au;
  }

  const auto consumer = CreateComputeProgram(R"(
    ByteAddressBuffer input : register(t0);
    RWByteAddressBuffer output : register(u0);

    [numthreads(8, 1, 1)]
    void main(uint3 id : SV_DispatchThreadID) {
      if (id.x < 32)
        output.Store(id.x * 4, input.Load(id.x * 4) ^ 0xa5a55a5au);
    }
  )",
                                             {D3D12_ROOT_PARAMETER_TYPE_SRV,
                                              D3D12_ROOT_PARAMETER_TYPE_UAV});
  auto upload = context_.CreateUploadBuffer(sizeof(source), source.data(),
                                             sizeof(source));
  auto intermediate = context_.CreateBuffer(
      sizeof(source), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  auto output = context_.CreateBuffer(
      sizeof(source), D3D12_HEAP_TYPE_DEFAULT,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  ASSERT_TRUE(consumer.root_signature);
  ASSERT_TRUE(consumer.pipeline);
  ASSERT_TRUE(upload);
  ASSERT_TRUE(intermediate);
  ASSERT_TRUE(output);

  context_.list()->CopyBufferRegion(intermediate.get(), 0, upload.get(), 0,
                                    sizeof(source));
  D3D12TestContext::Transition(
      context_.list(), intermediate.get(), D3D12_RESOURCE_STATE_COPY_DEST,
      D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  context_.list()->SetComputeRootSignature(consumer.root_signature.get());
  context_.list()->SetPipelineState(consumer.pipeline.get());
  context_.list()->SetComputeRootShaderResourceView(
      0, intermediate->GetGPUVirtualAddress());
  context_.list()->SetComputeRootUnorderedAccessView(
      1, output->GetGPUVirtualAddress());
  context_.list()->Dispatch(kElementCount / 8, 1, 1);
  D3D12TestContext::Transition(
      context_.list(), output.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_COPY_SOURCE);

  std::vector<std::uint8_t> bytes;
  ASSERT_EQ(context_.ReadbackBuffer(output.get(), sizeof(source), &bytes),
            S_OK);
  ExpectWords(bytes, expected, "Copy -> SRV");
  ExpectDeviceHealthy();
}

TEST_F(LegacyProducerConsumerExecutionSpec,
       UavToSrvTransitionPublishesProducerDispatch) {
  std::array<UINT, kElementCount> expected = {};
  for (UINT index = 0; index < expected.size(); ++index)
    expected[index] = (0x24680000u + index * 17u) ^ 0x00ff00ffu;

  const auto producer = CreateComputeProgram(R"(
    RWByteAddressBuffer output : register(u0);

    [numthreads(8, 1, 1)]
    void main(uint3 id : SV_DispatchThreadID) {
      if (id.x < 32)
        output.Store(id.x * 4, 0x24680000u + id.x * 17u);
    }
  )",
                                             {D3D12_ROOT_PARAMETER_TYPE_UAV});
  const auto consumer = CreateComputeProgram(R"(
    ByteAddressBuffer input : register(t0);
    RWByteAddressBuffer output : register(u0);

    [numthreads(8, 1, 1)]
    void main(uint3 id : SV_DispatchThreadID) {
      if (id.x < 32)
        output.Store(id.x * 4, input.Load(id.x * 4) ^ 0x00ff00ffu);
    }
  )",
                                             {D3D12_ROOT_PARAMETER_TYPE_SRV,
                                              D3D12_ROOT_PARAMETER_TYPE_UAV});
  auto intermediate = context_.CreateBuffer(
      sizeof(expected), D3D12_HEAP_TYPE_DEFAULT,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  auto output = context_.CreateBuffer(
      sizeof(expected), D3D12_HEAP_TYPE_DEFAULT,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  ASSERT_TRUE(producer.root_signature);
  ASSERT_TRUE(producer.pipeline);
  ASSERT_TRUE(consumer.root_signature);
  ASSERT_TRUE(consumer.pipeline);
  ASSERT_TRUE(intermediate);
  ASSERT_TRUE(output);

  context_.list()->SetComputeRootSignature(producer.root_signature.get());
  context_.list()->SetPipelineState(producer.pipeline.get());
  context_.list()->SetComputeRootUnorderedAccessView(
      0, intermediate->GetGPUVirtualAddress());
  context_.list()->Dispatch(kElementCount / 8, 1, 1);
  D3D12TestContext::Transition(
      context_.list(), intermediate.get(),
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  context_.list()->SetComputeRootSignature(consumer.root_signature.get());
  context_.list()->SetPipelineState(consumer.pipeline.get());
  context_.list()->SetComputeRootShaderResourceView(
      0, intermediate->GetGPUVirtualAddress());
  context_.list()->SetComputeRootUnorderedAccessView(
      1, output->GetGPUVirtualAddress());
  context_.list()->Dispatch(kElementCount / 8, 1, 1);
  D3D12TestContext::Transition(
      context_.list(), output.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_COPY_SOURCE);

  std::vector<std::uint8_t> bytes;
  ASSERT_EQ(context_.ReadbackBuffer(output.get(), sizeof(expected), &bytes),
            S_OK);
  ExpectWords(bytes, expected, "UAV -> SRV");
  ExpectDeviceHealthy();
}

TEST_F(LegacyProducerConsumerExecutionSpec,
       UavToCopyTransitionPublishesProducerDispatch) {
  std::array<UINT, kElementCount> expected = {};
  for (UINT index = 0; index < expected.size(); ++index)
    expected[index] = 0xc0010000u | index;

  const auto producer = CreateComputeProgram(R"(
    RWByteAddressBuffer output : register(u0);

    [numthreads(8, 1, 1)]
    void main(uint3 id : SV_DispatchThreadID) {
      if (id.x < 32)
        output.Store(id.x * 4, 0xc0010000u | id.x);
    }
  )",
                                             {D3D12_ROOT_PARAMETER_TYPE_UAV});
  auto intermediate = context_.CreateBuffer(
      sizeof(expected), D3D12_HEAP_TYPE_DEFAULT,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  ASSERT_TRUE(producer.root_signature);
  ASSERT_TRUE(producer.pipeline);
  ASSERT_TRUE(intermediate);

  context_.list()->SetComputeRootSignature(producer.root_signature.get());
  context_.list()->SetPipelineState(producer.pipeline.get());
  context_.list()->SetComputeRootUnorderedAccessView(
      0, intermediate->GetGPUVirtualAddress());
  context_.list()->Dispatch(kElementCount / 8, 1, 1);
  D3D12TestContext::Transition(
      context_.list(), intermediate.get(),
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_COPY_SOURCE);

  std::vector<std::uint8_t> bytes;
  ASSERT_EQ(
      context_.ReadbackBuffer(intermediate.get(), sizeof(expected), &bytes),
      S_OK);
  ExpectWords(bytes, expected, "UAV -> Copy");
  ExpectDeviceHealthy();
}

TEST_F(LegacyProducerConsumerExecutionSpec,
       RenderTargetToCopyTransitionPublishesEveryPixel) {
  auto target = CreateColorTexture(
      1, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
      D3D12_RESOURCE_STATE_RENDER_TARGET);
  auto rtv_heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
  ASSERT_TRUE(target);
  ASSERT_TRUE(rtv_heap);
  const auto rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
  context_.device()->CreateRenderTargetView(target.get(), nullptr, rtv);

  constexpr FLOAT kGreen[4] = {0.0f, 1.0f, 0.0f, 1.0f};
  context_.list()->ClearRenderTargetView(rtv, kGreen, 0, nullptr);
  D3D12TestContext::Transition(
      context_.list(), target.get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
      D3D12_RESOURCE_STATE_COPY_SOURCE);

  TextureReadback readback;
  ASSERT_EQ(context_.ReadbackTexture(target.get(), &readback), S_OK);
  ExpectSolidTexture(readback, 0xff00ff00u, "RTV -> Copy");
  ExpectDeviceHealthy();
}

TEST_F(LegacyProducerConsumerExecutionSpec,
       ResolveToCopyTransitionPublishesEveryResolvedPixel) {
  D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS quality = {};
  quality.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  quality.SampleCount = 4;
  ASSERT_EQ(context_.device()->CheckFeatureSupport(
                D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &quality,
                sizeof(quality)),
            S_OK);
  if (!quality.NumQualityLevels)
    GTEST_SKIP() << "4x MSAA is unavailable";

  auto source = CreateColorTexture(
      4, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
      D3D12_RESOURCE_STATE_RENDER_TARGET);
  auto destination = CreateColorTexture(
      1, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_RESOLVE_DEST);
  auto rtv_heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
  ASSERT_TRUE(source);
  ASSERT_TRUE(destination);
  ASSERT_TRUE(rtv_heap);
  const auto rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
  context_.device()->CreateRenderTargetView(source.get(), nullptr, rtv);

  constexpr FLOAT kBlue[4] = {0.0f, 0.0f, 1.0f, 1.0f};
  context_.list()->ClearRenderTargetView(rtv, kBlue, 0, nullptr);
  D3D12TestContext::Transition(
      context_.list(), source.get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
      D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
  context_.list()->ResolveSubresource(destination.get(), 0, source.get(), 0,
                                      DXGI_FORMAT_R8G8B8A8_UNORM);
  D3D12TestContext::Transition(
      context_.list(), destination.get(), D3D12_RESOURCE_STATE_RESOLVE_DEST,
      D3D12_RESOURCE_STATE_COPY_SOURCE);

  TextureReadback readback;
  ASSERT_EQ(context_.ReadbackTexture(destination.get(), &readback), S_OK);
  ExpectSolidTexture(readback, 0xffff0000u, "Resolve -> Copy");
  ExpectDeviceHealthy();
}

TEST_F(LegacyProducerConsumerExecutionSpec,
       UavBarrierThenTransitionPublishesArgumentsToExecuteIndirect) {
  const auto argument_producer = CreateComputeProgram(R"(
    RWByteAddressBuffer arguments : register(u0);

    [numthreads(1, 1, 1)]
    void main() {
      arguments.Store(0, 1);
      arguments.Store(4, 1);
      arguments.Store(8, 1);
    }
  )",
                                                      {D3D12_ROOT_PARAMETER_TYPE_UAV});
  const auto counter_consumer = CreateComputeProgram(R"(
    RWStructuredBuffer<uint> counter : register(u0);

    [numthreads(1, 1, 1)]
    void main() {
      uint ignored;
      InterlockedAdd(counter[0], 1, ignored);
    }
  )",
                                                      {D3D12_ROOT_PARAMETER_TYPE_UAV});
  D3D12_INDIRECT_ARGUMENT_DESC argument_desc = {};
  argument_desc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
  D3D12_COMMAND_SIGNATURE_DESC signature_desc = {};
  signature_desc.ByteStride = sizeof(D3D12_DISPATCH_ARGUMENTS);
  signature_desc.NumArgumentDescs = 1;
  signature_desc.pArgumentDescs = &argument_desc;
  ComPtr<ID3D12CommandSignature> signature;
  ASSERT_EQ(context_.device()->CreateCommandSignature(
                &signature_desc, nullptr, IID_PPV_ARGS(signature.put())),
            S_OK);

  constexpr std::array<UINT, 3> poison_arguments = {2, 2, 2};
  const UINT zero = 0;
  auto argument_poison = context_.CreateUploadBuffer(
      sizeof(poison_arguments), poison_arguments.data(),
      sizeof(poison_arguments));
  auto zero_upload =
      context_.CreateUploadBuffer(sizeof(zero), &zero, sizeof(zero));
  auto arguments = context_.CreateBuffer(
      sizeof(D3D12_DISPATCH_ARGUMENTS), D3D12_HEAP_TYPE_DEFAULT,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_COPY_DEST);
  auto counter = context_.CreateBuffer(
      sizeof(UINT), D3D12_HEAP_TYPE_DEFAULT,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(argument_producer.root_signature);
  ASSERT_TRUE(argument_producer.pipeline);
  ASSERT_TRUE(counter_consumer.root_signature);
  ASSERT_TRUE(counter_consumer.pipeline);
  ASSERT_TRUE(signature);
  ASSERT_TRUE(argument_poison);
  ASSERT_TRUE(zero_upload);
  ASSERT_TRUE(arguments);
  ASSERT_TRUE(counter);

  // A dropped producer must not accidentally consume stale {1,1,1}
  // arguments. Establish and verify {2,2,2} in an independent setup
  // submission; any missed producer store then executes 2, 4, or 8 consumer
  // thread groups instead of the expected one.
  context_.list()->CopyBufferRegion(arguments.get(), 0,
                                    argument_poison.get(), 0,
                                    sizeof(poison_arguments));
  D3D12TestContext::Transition(
      context_.list(), arguments.get(), D3D12_RESOURCE_STATE_COPY_DEST,
      D3D12_RESOURCE_STATE_COPY_SOURCE);
  std::vector<std::uint8_t> setup_bytes;
  ASSERT_EQ(context_.ReadbackBuffer(arguments.get(), sizeof(poison_arguments),
                                    &setup_bytes),
            S_OK);
  ASSERT_EQ(setup_bytes.size(), sizeof(poison_arguments));
  ASSERT_EQ(std::memcmp(setup_bytes.data(), poison_arguments.data(),
                        sizeof(poison_arguments)),
            0);
  ASSERT_EQ(context_.ResetCommandList(), S_OK);
  D3D12TestContext::Transition(
      context_.list(), arguments.get(), D3D12_RESOURCE_STATE_COPY_SOURCE,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

  context_.list()->CopyBufferRegion(counter.get(), 0, zero_upload.get(), 0,
                                    sizeof(zero));
  D3D12TestContext::Transition(
      context_.list(), counter.get(), D3D12_RESOURCE_STATE_COPY_DEST,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  context_.list()->SetComputeRootSignature(
      argument_producer.root_signature.get());
  context_.list()->SetPipelineState(argument_producer.pipeline.get());
  context_.list()->SetComputeRootUnorderedAccessView(
      0, arguments->GetGPUVirtualAddress());
  context_.list()->Dispatch(1, 1, 1);
  D3D12TestContext::UavBarrier(context_.list(), arguments.get());
  D3D12TestContext::Transition(
      context_.list(), arguments.get(),
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
  context_.list()->SetComputeRootSignature(
      counter_consumer.root_signature.get());
  context_.list()->SetPipelineState(counter_consumer.pipeline.get());
  context_.list()->SetComputeRootUnorderedAccessView(
      0, counter->GetGPUVirtualAddress());
  context_.list()->ExecuteIndirect(signature.get(), 1, arguments.get(), 0,
                                   nullptr, 0);
  D3D12TestContext::Transition(
      context_.list(), counter.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_COPY_SOURCE);

  std::vector<std::uint8_t> bytes;
  ASSERT_EQ(context_.ReadbackBuffer(counter.get(), sizeof(UINT), &bytes),
            S_OK);
  ASSERT_EQ(bytes.size(), sizeof(UINT));
  UINT actual = 0;
  std::memcpy(&actual, bytes.data(), sizeof(actual));
  EXPECT_EQ(actual, 1u);
  ExpectDeviceHealthy();
}

TEST_F(LegacyProducerConsumerExecutionSpec,
       ExpandedComputeResourceStateCapabilityMatchesExecution) {
  D3D12_FEATURE_DATA_D3D12_OPTIONS1 options = {};
  ASSERT_EQ(context_.device()->CheckFeatureSupport(
                D3D12_FEATURE_D3D12_OPTIONS1, &options, sizeof(options)),
            S_OK);
  ASSERT_TRUE(options.ExpandedComputeResourceStates);

  constexpr std::array<UINT, 2> expected = {0x13579bdfu, 0x2468ace0u};
  auto upload = context_.CreateUploadBuffer(sizeof(expected), expected.data(),
                                             sizeof(expected));
  auto cbv = context_.CreateBuffer(
      sizeof(UINT), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  auto indirect = context_.CreateBuffer(
      sizeof(UINT), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  auto readback = context_.CreateBuffer(
      sizeof(expected), D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(upload);
  ASSERT_TRUE(cbv);
  ASSERT_TRUE(indirect);
  ASSERT_TRUE(readback);

  D3D12_COMMAND_QUEUE_DESC queue_desc = {};
  queue_desc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
  ComPtr<ID3D12CommandQueue> queue;
  ASSERT_EQ(context_.device()->CreateCommandQueue(
                &queue_desc, IID_PPV_ARGS(queue.put())),
            S_OK);
  ComPtr<ID3D12CommandAllocator> allocator;
  ASSERT_EQ(context_.device()->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_COMPUTE,
                IID_PPV_ARGS(allocator.put())),
            S_OK);
  ComPtr<ID3D12GraphicsCommandList> list;
  ASSERT_EQ(context_.device()->CreateCommandList(
                0, D3D12_COMMAND_LIST_TYPE_COMPUTE, allocator.get(), nullptr,
                IID_PPV_ARGS(list.put())),
            S_OK);

  list->CopyBufferRegion(cbv.get(), 0, upload.get(), 0, sizeof(UINT));
  list->CopyBufferRegion(indirect.get(), 0, upload.get(), sizeof(UINT),
                         sizeof(UINT));
  D3D12TestContext::Transition(
      list.get(), cbv.get(), D3D12_RESOURCE_STATE_COPY_DEST,
      D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
  D3D12TestContext::Transition(
      list.get(), cbv.get(), D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
      D3D12_RESOURCE_STATE_COPY_SOURCE);
  D3D12TestContext::Transition(
      list.get(), indirect.get(), D3D12_RESOURCE_STATE_COPY_DEST,
      D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
  D3D12TestContext::Transition(
      list.get(), indirect.get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT,
      D3D12_RESOURCE_STATE_COPY_SOURCE);
  list->CopyBufferRegion(readback.get(), 0, cbv.get(), 0, sizeof(UINT));
  list->CopyBufferRegion(readback.get(), sizeof(UINT), indirect.get(), 0,
                         sizeof(UINT));
  ASSERT_EQ(list->Close(), S_OK);

  ComPtr<ID3D12Fence> fence;
  ASSERT_EQ(context_.device()->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                            IID_PPV_ARGS(fence.put())),
            S_OK);
  ID3D12CommandList *lists[] = {list.get()};
  queue->ExecuteCommandLists(1, lists);
  ASSERT_EQ(queue->Signal(fence.get(), 1), S_OK);
  ASSERT_EQ(context_.WaitForFence(fence.get(), 1), S_OK);

  void *mapped = nullptr;
  const D3D12_RANGE read_range = {0, sizeof(expected)};
  ASSERT_EQ(readback->Map(0, &read_range, &mapped), S_OK);
  std::array<UINT, 2> actual = {};
  std::memcpy(actual.data(), mapped, sizeof(actual));
  const D3D12_RANGE no_write = {};
  readback->Unmap(0, &no_write);
  EXPECT_EQ(actual, expected);
  ExpectDeviceHealthy();
}

} // namespace
