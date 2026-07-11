#include <dxmt_benchmark.hpp>

#include "d3d12_test_context.hpp"
#include "shaders/runtime_test_shaders.hpp"

#include <cstdint>
#include <cstring>
#include <fstream>
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
constexpr unsigned int kFh4BlitPasses = 810;
constexpr unsigned int kFh4BarrierPasses = 121;
constexpr unsigned int kFh4BaselineFenceEntries = 1861;

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

struct CommandBufferDiagnosticTotals {
  std::uint64_t input_encoders = 0;
  std::uint64_t encoded_encoders = 0;
  std::uint64_t encoded_blit = 0;
  std::uint64_t barrier_only = 0;
  std::uint64_t fence_waits = 0;
  std::uint64_t fence_updates = 0;
};

IntegrationError RunFh4SynchronizationScenario(
    CommandBufferDiagnosticTotals *totals) {
  std::ostringstream marker_name;
  marker_name << "C:\\dxmt-fh4-sync-" << GetCurrentProcessId() << "-"
              << GetTickCount64() << ".txt";
  const auto marker_path = marker_name.str();
  std::ofstream(marker_path, std::ios::trunc).close();
  struct MarkerGuard {
    explicit MarkerGuard(std::string marker_path)
        : path(std::move(marker_path)) {
      SetEnvironmentVariableA("DXMT_TEST_COMMAND_BUFFER_DIAGNOSTIC_MARKER",
                              path.c_str());
    }
    ~MarkerGuard() {
      Disable();
      DeleteFileA(path.c_str());
    }
    void Disable() {
      if (!enabled)
        return;
      SetEnvironmentVariableA("DXMT_TEST_COMMAND_BUFFER_DIAGNOSTIC_MARKER",
                              nullptr);
      enabled = false;
    }
    std::string path;
    bool enabled = true;
  } marker_guard(marker_path);

  D3D12TestContext context;
  if (auto error = HResultError("D3D12 context initialization",
                                context.Initialize()))
    return error;

  const std::array<std::uint32_t, 64> expected = [] {
    std::array<std::uint32_t, 64> values = {};
    for (std::uint32_t i = 0; i < values.size(); i++)
      values[i] = 0x9e370000u + i;
    return values;
  }();
  auto upload = context.CreateUploadBuffer(
      sizeof(expected), expected.data(), sizeof(expected));
  auto barrier_resource = context.CreateBuffer(
      sizeof(expected), D3D12_HEAP_TYPE_DEFAULT,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  if (!upload || !barrier_resource)
    return "failed to create FH4 synchronization resources";

  std::vector<ComPtr<ID3D12Resource>> destinations;
  std::vector<ComPtr<ID3D12CommandAllocator>> allocators;
  std::vector<ComPtr<ID3D12GraphicsCommandList>> command_lists;
  std::vector<ID3D12CommandList *> submit_lists;
  destinations.reserve(kFh4BlitPasses);
  allocators.reserve(kFh4BlitPasses + kFh4BarrierPasses);
  command_lists.reserve(kFh4BlitPasses + kFh4BarrierPasses);
  submit_lists.reserve(kFh4BlitPasses + kFh4BarrierPasses);

  auto append_list = [&](bool barrier_only,
                         ID3D12Resource *destination) -> IntegrationError {
    ComPtr<ID3D12CommandAllocator> allocator;
    HRESULT hr = context.device()->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator),
        reinterpret_cast<void **>(allocator.put()));
    if (auto error = HResultError("FH4 allocator creation", hr))
      return error;
    ComPtr<ID3D12GraphicsCommandList> list;
    hr = context.device()->CreateCommandList(
        0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.get(), nullptr,
        __uuidof(ID3D12GraphicsCommandList),
        reinterpret_cast<void **>(list.put()));
    if (auto error = HResultError("FH4 command-list creation", hr))
      return error;
    if (barrier_only)
      D3D12TestContext::UavBarrier(list.get(), barrier_resource.get());
    else
      list->CopyBufferRegion(destination, 0, upload.get(), 0,
                             sizeof(expected));
    if (auto error = HResultError("FH4 command-list close", list->Close()))
      return error;
    submit_lists.push_back(list.get());
    allocators.push_back(std::move(allocator));
    command_lists.push_back(std::move(list));
    return std::nullopt;
  };

  for (unsigned int i = 0; i < kFh4BlitPasses; i++) {
    auto destination = context.CreateBuffer(
        sizeof(expected), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
        D3D12_RESOURCE_STATE_COPY_DEST);
    if (!destination)
      return "failed to create FH4 blit destination";
    auto *destination_ptr = destination.get();
    destinations.push_back(std::move(destination));
    if (auto error = append_list(false, destination_ptr))
      return error;
    if (((i + 1) * kFh4BarrierPasses) / kFh4BlitPasses !=
        (i * kFh4BarrierPasses) / kFh4BlitPasses) {
      if (auto error = append_list(true, nullptr))
        return error;
    }
  }

  context.queue()->ExecuteCommandLists(
      static_cast<UINT>(submit_lists.size()), submit_lists.data());
  if (auto error = HResultError("FH4 synchronization queue completion",
                                context.SignalAndWait()))
    return error;
  marker_guard.Disable();

  std::ifstream marker(marker_path);
  CommandBufferDiagnosticTotals measured = {};
  CommandBufferDiagnosticTotals line = {};
  while (marker >> line.input_encoders >> line.encoded_encoders >>
         line.encoded_blit >> line.barrier_only >> line.fence_waits >>
         line.fence_updates) {
    measured.input_encoders += line.input_encoders;
    measured.encoded_encoders += line.encoded_encoders;
    measured.encoded_blit += line.encoded_blit;
    measured.barrier_only += line.barrier_only;
    measured.fence_waits += line.fence_waits;
    measured.fence_updates += line.fence_updates;
  }
  if (totals)
    *totals = measured;
  if (measured.barrier_only)
    return "FH4 synchronization workload encoded standalone barriers";
  if (measured.fence_waits + measured.fence_updates >
      kFh4BaselineFenceEntries / 4)
    return "FH4 synchronization workload did not reduce fence entries by 75 percent";

  if (auto error = HResultError("FH4 empty command-list close",
                                context.list()->Close()))
    return error;
  if (auto error = HResultError("FH4 readback list reset",
                                context.ResetCommandList()))
    return error;
  D3D12TestContext::Transition(
      context.list(), destinations.back().get(),
      D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
  std::vector<std::uint8_t> readback;
  if (auto error = HResultError(
          "FH4 synchronization readback",
          context.ReadbackBuffer(destinations.back().get(), sizeof(expected),
                                 &readback)))
    return error;
  if (readback.size() != sizeof(expected) ||
      std::memcmp(readback.data(), expected.data(), sizeof(expected)) != 0)
    return "FH4 synchronization readback mismatch";
  return std::nullopt;
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

void BI_D3D12Fh4SynchronizationAmplification(benchmark::State &state) {
  for (auto _ : state) {
    CommandBufferDiagnosticTotals totals = {};
    if (IntegrationError error = RunFh4SynchronizationScenario(&totals)) {
      state.SkipWithError(error->c_str());
      return;
    }
    state.counters["logical_blit"] = kFh4BlitPasses;
    state.counters["logical_barrier"] = kFh4BarrierPasses;
    state.counters["input_encoders"] = totals.input_encoders;
    state.counters["encoded_encoders"] = totals.encoded_encoders;
    state.counters["encoded_blit"] = totals.encoded_blit;
    state.counters["barrier_only"] = totals.barrier_only;
    state.counters["fence_edges"] =
        totals.fence_waits + totals.fence_updates;
  }
  state.SetItemsProcessed(state.iterations() * kFh4BlitPasses);
}

void BI_D3D12ResourceInitializerLifetime(benchmark::State &state) {
  RunIntegrationBenchmark(
      state, RunInitializerLifetimeScenario,
      kInitializerBatches * kTexturesPerInitializerBatch);
}

BENCHMARK(BI_D3D12MixedEncoderBarrierOnly)->Iterations(1)->UseRealTime();
BENCHMARK(BI_D3D12MixedEncoderCopyTransitions)->Iterations(1)->UseRealTime();
BENCHMARK(BI_D3D12FenceOnlyBarrierChain)->Iterations(1)->UseRealTime();
BENCHMARK(BI_D3D12Fh4SynchronizationAmplification)
    ->Iterations(1)
    ->UseRealTime();
BENCHMARK(BI_D3D12ResourceInitializerLifetime)->Iterations(1)->UseRealTime();

} // namespace
