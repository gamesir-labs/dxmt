#include <dxmt_benchmark.hpp>

#include "d3d12_test_context.hpp"
#include "shaders/runtime_test_shaders.hpp"

#include <cstdint>
#include <cstring>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {

using dxmt::test::ClearBufferComputeShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

constexpr unsigned int kMixedEncoderIterations = 384;
constexpr unsigned int kBarrierCount = 384;
constexpr unsigned int kInitializerBatches = 1152;
constexpr unsigned int kTexturesPerInitializerBatch = 4;

using IntegrationError = std::optional<std::string>;

IntegrationError HResultError(const char *operation, HRESULT hr) {
  if (SUCCEEDED(hr))
    return std::nullopt;

  std::ostringstream message;
  message << operation << " failed with HRESULT 0x" << std::hex
          << static_cast<unsigned long>(hr);
  return message.str();
}

IntegrationError RunMixedEncoderScenario(bool copy_between_dispatches) {
  D3D12TestContext context;
  if (auto error = HResultError("D3D12 context initialization",
                                context.Initialize()))
    return error;

  D3D12_ROOT_PARAMETER parameters[2] = {};
  parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
  parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  parameters[0].Descriptor.ShaderRegister = 0;
  parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  parameters[1].Constants.ShaderRegister = 0;
  parameters[1].Constants.Num32BitValues = 1;
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  root_desc.NumParameters = 2;
  root_desc.pParameters = parameters;

  ComPtr<ID3D12RootSignature> root_signature =
      context.CreateRootSignature(root_desc);
  if (!root_signature)
    return "failed to create mixed-encoder root signature";
  ComPtr<ID3D12PipelineState> pipeline = context.CreateComputePipeline(
      root_signature.get(), ClearBufferComputeShader());
  if (!pipeline)
    return "failed to create mixed-encoder compute pipeline";

  ComPtr<ID3D12Resource> source = context.CreateBuffer(
      64 * sizeof(std::uint32_t), D3D12_HEAP_TYPE_DEFAULT,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  ComPtr<ID3D12Resource> destination = context.CreateBuffer(
      64 * sizeof(std::uint32_t), D3D12_HEAP_TYPE_DEFAULT,
      D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
  if (!source || !destination)
    return "failed to create mixed-encoder buffers";

  context.list()->SetComputeRootSignature(root_signature.get());
  context.list()->SetPipelineState(pipeline.get());
  context.list()->SetComputeRootUnorderedAccessView(
      0, source->GetGPUVirtualAddress());
  for (unsigned int i = 0; i < kMixedEncoderIterations; ++i) {
    context.list()->SetComputeRoot32BitConstant(1, i + 1, 0);
    context.list()->Dispatch(1, 1, 1);
    if (!copy_between_dispatches) {
      D3D12TestContext::UavBarrier(context.list(), source.get());
      continue;
    }

    D3D12TestContext::Transition(
        context.list(), source.get(),
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_SOURCE);
    context.list()->CopyBufferRegion(destination.get(), 0, source.get(), 0,
                                     64 * sizeof(std::uint32_t));
    D3D12TestContext::Transition(
        context.list(), source.get(), D3D12_RESOURCE_STATE_COPY_SOURCE,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  }

  if (auto error = HResultError("mixed-encoder command-list close",
                                context.list()->Close()))
    return error;
  ID3D12CommandList *lists[] = {context.list()};
  context.queue()->ExecuteCommandLists(1, lists);
  if (auto error = HResultError("mixed-encoder queue completion",
                                context.SignalAndWait()))
    return error;
  if (auto error = HResultError("mixed-encoder command-list reset",
                                context.ResetCommandList()))
    return error;

  ID3D12Resource *readback_source = nullptr;
  if (copy_between_dispatches) {
    D3D12TestContext::Transition(
        context.list(), destination.get(), D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_COPY_SOURCE);
    readback_source = destination.get();
  } else {
    D3D12TestContext::Transition(
        context.list(), source.get(),
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_SOURCE);
    readback_source = source.get();
  }

  std::vector<std::uint8_t> readback;
  if (auto error = HResultError(
          "mixed-encoder buffer readback",
          context.ReadbackBuffer(readback_source,
                                 64 * sizeof(std::uint32_t), &readback)))
    return error;

  for (unsigned int i = 0; i < 64; ++i) {
    std::uint32_t actual = 0;
    std::memcpy(&actual, readback.data() + i * sizeof(actual), sizeof(actual));
    if (actual != kMixedEncoderIterations) {
      std::ostringstream message;
      message << "mixed-encoder element " << i << " was " << actual
              << ", expected " << kMixedEncoderIterations;
      return message.str();
    }
  }
  return std::nullopt;
}

IntegrationError RunBarrierChainScenario() {
  D3D12TestContext context;
  if (auto error = HResultError("D3D12 context initialization",
                                context.Initialize()))
    return error;
  ComPtr<ID3D12Resource> resource = context.CreateBuffer(
      4096, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  if (!resource)
    return "failed to create barrier-chain resource";

  std::vector<ComPtr<ID3D12CommandAllocator>> allocators;
  std::vector<ComPtr<ID3D12GraphicsCommandList>> command_lists;
  std::vector<ID3D12CommandList *> submit_lists;
  allocators.reserve(kBarrierCount - 1);
  command_lists.reserve(kBarrierCount - 1);
  submit_lists.reserve(kBarrierCount);

  D3D12_RESOURCE_STATES before = D3D12_RESOURCE_STATE_COPY_DEST;
  D3D12_RESOURCE_STATES after = D3D12_RESOURCE_STATE_COPY_SOURCE;
  for (unsigned int i = 0; i < kBarrierCount; ++i) {
    ID3D12GraphicsCommandList *list = nullptr;
    if (i == 0) {
      list = context.list();
    } else {
      ComPtr<ID3D12CommandAllocator> allocator;
      HRESULT hr = context.device()->CreateCommandAllocator(
          D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator),
          reinterpret_cast<void **>(allocator.put()));
      if (auto error = HResultError("barrier-chain allocator creation", hr))
        return error;

      ComPtr<ID3D12GraphicsCommandList> command_list;
      hr = context.device()->CreateCommandList(
          0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.get(), nullptr,
          __uuidof(ID3D12GraphicsCommandList),
          reinterpret_cast<void **>(command_list.put()));
      if (auto error = HResultError("barrier-chain command-list creation", hr))
        return error;
      list = command_list.get();
      allocators.push_back(std::move(allocator));
      command_lists.push_back(std::move(command_list));
    }

    D3D12TestContext::Transition(list, resource.get(), before, after);
    if (auto error = HResultError("barrier-chain command-list close",
                                  list->Close()))
      return error;
    submit_lists.push_back(list);
    before = after;
    after = after == D3D12_RESOURCE_STATE_COPY_SOURCE
                ? D3D12_RESOURCE_STATE_COPY_DEST
                : D3D12_RESOURCE_STATE_COPY_SOURCE;
  }

  context.queue()->ExecuteCommandLists(
      static_cast<UINT>(submit_lists.size()), submit_lists.data());
  return HResultError("barrier-chain queue completion", context.SignalAndWait());
}

IntegrationError RunInitializerLifetimeScenario() {
  D3D12TestContext context;
  if (auto error = HResultError("D3D12 context initialization",
                                context.Initialize()))
    return error;
  if (auto error = HResultError("initializer command-list close",
                                context.list()->Close()))
    return error;

  ComPtr<ID3D12Fence> fence;
  HRESULT hr = context.device()->CreateFence(
      0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence),
      reinterpret_cast<void **>(fence.put()));
  if (auto error = HResultError("initializer fence creation", hr))
    return error;

  ID3D12CommandList *lists[] = {context.list()};
  UINT64 submitted_batches = 0;
  for (unsigned int i = 0; i < kInitializerBatches; ++i) {
    for (unsigned int j = 0; j < kTexturesPerInitializerBatch; ++j) {
      ComPtr<ID3D12Resource> texture = context.CreateTexture2D(
          64, 64, 1, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_NONE,
          D3D12_RESOURCE_STATE_COMMON);
      if (!texture)
        return "failed to create initializer-lifetime texture";
    }

    context.queue()->ExecuteCommandLists(1, lists);
    submitted_batches = i + 1;
    hr = context.queue()->Signal(fence.get(), submitted_batches);
    if (auto error = HResultError("initializer batch signal", hr))
      return error;
  }

  return HResultError("initializer final fence wait",
                      context.WaitForFence(fence.get(), submitted_batches));
}

template <typename Scenario>
void RunIntegrationBenchmark(benchmark::State &state, Scenario scenario,
                             std::int64_t item_count) {
  for (auto _ : state) {
    if (IntegrationError error = scenario()) {
      state.SkipWithError(error->c_str());
      return;
    }
  }
  state.SetItemsProcessed(state.iterations() * item_count);
}

void BI_D3D12MixedEncoderBarrierOnly(benchmark::State &state) {
  RunIntegrationBenchmark(
      state, [] { return RunMixedEncoderScenario(false); },
      kMixedEncoderIterations);
}

void BI_D3D12MixedEncoderCopyTransitions(benchmark::State &state) {
  RunIntegrationBenchmark(
      state, [] { return RunMixedEncoderScenario(true); },
      kMixedEncoderIterations);
}

void BI_D3D12FenceOnlyBarrierChain(benchmark::State &state) {
  RunIntegrationBenchmark(state, RunBarrierChainScenario, kBarrierCount);
}

void BI_D3D12ResourceInitializerLifetime(benchmark::State &state) {
  RunIntegrationBenchmark(
      state, RunInitializerLifetimeScenario,
      kInitializerBatches * kTexturesPerInitializerBatch);
}

BENCHMARK(BI_D3D12MixedEncoderBarrierOnly)->Iterations(1)->UseRealTime();
BENCHMARK(BI_D3D12MixedEncoderCopyTransitions)->Iterations(1)->UseRealTime();
BENCHMARK(BI_D3D12FenceOnlyBarrierChain)->Iterations(1)->UseRealTime();
BENCHMARK(BI_D3D12ResourceInitializerLifetime)->Iterations(1)->UseRealTime();

} // namespace
