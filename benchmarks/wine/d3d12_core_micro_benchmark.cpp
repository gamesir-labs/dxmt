#include <dxmt_benchmark.hpp>

#include "d3d12_test_context.hpp"
#include "shaders/runtime_test_shaders.hpp"

#include <chrono>
#include <cstring>
#include <cstdint>
#include <vector>

namespace {

using clock_type = std::chrono::steady_clock;
using dxmt::test::ClearBufferComputeShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

constexpr UINT kSmallCommandCount = 16;
constexpr UINT kLargeCommandCount = 4096;
constexpr UINT kDescriptorBufferElements = 4096;
// Wine's steady clock advances in roughly one-microsecond steps on the
// designated runner. Batch short workloads until the timed region contains at
// least this many API calls, while preserving each command-list workload's
// original shape.
constexpr UINT kMinimumTimedApiCalls = 256;
constexpr UINT kMinimumTimedCommandLists = 4;
constexpr UINT kMinimumTimedCopyLists = 16;
constexpr UINT kCloseSmallListBatchCount = 256;
constexpr UINT kEmptyListBatchCount = 64;
constexpr UINT kSignalBatchCount = 256;
constexpr UINT kSubMicroBatchCount = 4096;
constexpr UINT kAllocatorBatchCount = 4096;

double Seconds(clock_type::duration duration) {
  return std::chrono::duration<double>(duration).count();
}

UINT TimedBatchCount(UINT api_calls_per_workload) {
  return (kMinimumTimedApiCalls + api_calls_per_workload - 1) /
         api_calls_per_workload;
}

UINT TimedCommandListBatchCount(
    UINT commands_per_list,
    UINT minimum_list_count = kMinimumTimedCommandLists) {
  const UINT batch_count = TimedBatchCount(commands_per_list);
  return batch_count < minimum_list_count ? minimum_list_count : batch_count;
}

double PerWorkloadSeconds(clock_type::duration duration, UINT batch_count) {
  return Seconds(duration) / static_cast<double>(batch_count);
}

HRESULT ExecuteClosedList(D3D12TestContext &context,
                          ID3D12GraphicsCommandList *list) {
  ID3D12CommandList *lists[] = {list};
  context.queue()->ExecuteCommandLists(1, lists);
  return context.SignalAndWait();
}

bool CheckDeviceHealth(benchmark::State &state, D3D12TestContext &context) {
  if (context.device()->GetDeviceRemovedReason() != S_OK) {
    state.SkipWithError("D3D12 device health check failed");
    return false;
  }
  return true;
}

void PublishOperationCount(benchmark::State &state, const char *counter_name,
                           std::uint64_t operation_count,
                           double measured_seconds) {
  state.counters[counter_name] = static_cast<double>(operation_count);
  state.counters["ns_per_operation"] =
      operation_count ? measured_seconds * 1.0e9 / operation_count : 0.0;
  state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) *
                          static_cast<std::int64_t>(operation_count));
}

struct CommandListBatch {
  std::vector<ComPtr<ID3D12CommandAllocator>> allocators;
  std::vector<ComPtr<ID3D12GraphicsCommandList>> lists;
  std::vector<ID3D12CommandList *> submissions;
};

HRESULT CreateOpenCommandListBatch(D3D12TestContext &context, UINT list_count,
                                   CommandListBatch *batch) {
  batch->allocators.resize(list_count);
  batch->lists.resize(list_count);
  batch->submissions.resize(list_count);
  for (UINT index = 0; index < list_count; ++index) {
    HRESULT result = context.device()->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(batch->allocators[index].put()));
    if (FAILED(result))
      return result;
    result = context.device()->CreateCommandList(
        0, D3D12_COMMAND_LIST_TYPE_DIRECT, batch->allocators[index].get(),
        nullptr, IID_PPV_ARGS(batch->lists[index].put()));
    if (FAILED(result))
      return result;
    batch->submissions[index] = batch->lists[index].get();
  }
  return S_OK;
}

HRESULT CloseCommandListBatch(CommandListBatch &batch) {
  for (const auto &list : batch.lists) {
    const HRESULT result = list->Close();
    if (FAILED(result))
      return result;
  }
  return S_OK;
}

HRESULT SubmitCommandListBatch(D3D12TestContext &context,
                               const CommandListBatch &batch) {
  context.queue()->ExecuteCommandLists(
      static_cast<UINT>(batch.submissions.size()), batch.submissions.data());
  return context.SignalAndWait();
}

void RecordCopyCommands(ID3D12GraphicsCommandList *list,
                        ID3D12Resource *destination, ID3D12Resource *source,
                        UINT command_count) {
  for (UINT index = 0; index < command_count; ++index)
    list->CopyBufferRegion(destination, 0, source, 0, sizeof(std::uint32_t));
}

bool CreateCopyResources(benchmark::State &state, D3D12TestContext &context,
                         ComPtr<ID3D12Resource> *source,
                         ComPtr<ID3D12Resource> *destination) {
  const std::uint32_t source_value = 0x12345678u;
  *source = context.CreateUploadBuffer(sizeof(source_value), &source_value,
                                       sizeof(source_value));
  *destination = context.CreateBuffer(
      sizeof(source_value), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  if (!*source || !*destination) {
    state.SkipWithError("copy benchmark resource creation failed");
    return false;
  }
  return true;
}

bool RunCopyHealthPrecheck(benchmark::State &state, D3D12TestContext &context,
                           ID3D12Resource *source, ID3D12Resource *destination,
                           UINT command_count) {
  RecordCopyCommands(context.list(), destination, source, command_count);
  if (FAILED(context.ExecuteAndWait())) {
    state.SkipWithError("copy benchmark health precheck failed");
    return false;
  }
  return CheckDeviceHealth(state, context);
}

void BI_D3D12RecordEmptyList(benchmark::State &state) {
  D3D12TestContext context;
  if (FAILED(context.Initialize())) {
    state.SkipWithError("D3D12 initialization failed");
    return;
  }

  // Establish a completed allocator generation before measuring the normal
  // empty-list record lifecycle (Reset + Close).
  if (FAILED(context.ExecuteAndWait()) ||
      FAILED(context.allocator()->Reset()) ||
      FAILED(context.list()->Reset(context.allocator(), nullptr)) ||
      FAILED(context.list()->Close()) ||
      FAILED(ExecuteClosedList(context, context.list())) ||
      !CheckDeviceHealth(state, context)) {
    if (!state.skipped())
      state.SkipWithError("empty command-list health precheck failed");
    return;
  }

  CommandListBatch batch;
  if (FAILED(
          CreateOpenCommandListBatch(context, kEmptyListBatchCount, &batch)) ||
      FAILED(CloseCommandListBatch(batch)) ||
      FAILED(SubmitCommandListBatch(context, batch))) {
    state.SkipWithError("empty command-list batch setup failed");
    return;
  }

  double measured_seconds = 0.0;
  for (auto _ : state) {
    for (const auto &allocator : batch.allocators) {
      if (FAILED(allocator->Reset())) {
        state.SkipWithError("command allocator reset failed");
        return;
      }
    }
    const auto begin = clock_type::now();
    HRESULT result = S_OK;
    for (UINT index = 0; index < kEmptyListBatchCount; ++index) {
      result =
          batch.lists[index]->Reset(batch.allocators[index].get(), nullptr);
      if (SUCCEEDED(result))
        result = batch.lists[index]->Close();
      if (FAILED(result))
        break;
    }
    const auto end = clock_type::now();
    if (FAILED(result)) {
      state.SkipWithError("empty command-list recording failed");
      return;
    }
    measured_seconds = PerWorkloadSeconds(end - begin, kEmptyListBatchCount);
    state.SetIterationTime(measured_seconds);
    if (FAILED(SubmitCommandListBatch(context, batch))) {
      state.SkipWithError("empty command-list submission failed");
      return;
    }
  }
  if (!CheckDeviceHealth(state, context))
    return;
  PublishOperationCount(state, "lists", 1, measured_seconds);
}

struct DispatchResources {
  ComPtr<ID3D12Resource> output;
  ComPtr<ID3D12DescriptorHeap> heap;
  ComPtr<ID3D12RootSignature> root_signature;
  ComPtr<ID3D12PipelineState> pipeline;
};

bool CreateDispatchResources(benchmark::State &state, D3D12TestContext &context,
                             DispatchResources *resources) {
  resources->output =
      context.CreateBuffer(64 * sizeof(std::uint32_t), D3D12_HEAP_TYPE_DEFAULT,
                           D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                           D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  resources->heap = context.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true);

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
  resources->root_signature = context.CreateRootSignature(root_desc);
  if (resources->root_signature) {
    resources->pipeline = context.CreateComputePipeline(
        resources->root_signature.get(), ClearBufferComputeShader());
  }
  if (!resources->output || !resources->heap || !resources->root_signature ||
      !resources->pipeline) {
    state.SkipWithError("dispatch benchmark setup failed");
    return false;
  }

  D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
  uav.Format = DXGI_FORMAT_R32_UINT;
  uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav.Buffer.NumElements = 64;
  context.device()->CreateUnorderedAccessView(
      resources->output.get(), nullptr, &uav,
      resources->heap->GetCPUDescriptorHandleForHeapStart());
  return CheckDeviceHealth(state, context);
}

void BindDispatchResources(ID3D12GraphicsCommandList *list,
                           const DispatchResources &resources) {
  ID3D12DescriptorHeap *heaps[] = {resources.heap.get()};
  list->SetDescriptorHeaps(1, heaps);
  list->SetPipelineState(resources.pipeline.get());
  list->SetComputeRootSignature(resources.root_signature.get());
  list->SetComputeRoot32BitConstant(0, 0x89abcdefu, 0);
  list->SetComputeRootDescriptorTable(
      1, resources.heap->GetGPUDescriptorHandleForHeapStart());
}

void BI_D3D12RecordDispatch(benchmark::State &state) {
  D3D12TestContext context;
  if (FAILED(context.Initialize())) {
    state.SkipWithError("D3D12 initialization failed");
    return;
  }
  DispatchResources resources;
  if (!CreateDispatchResources(state, context, &resources))
    return;

  BindDispatchResources(context.list(), resources);
  context.list()->Dispatch(1, 1, 1);
  if (FAILED(context.ExecuteAndWait()) || !CheckDeviceHealth(state, context)) {
    if (!state.skipped())
      state.SkipWithError("dispatch benchmark health precheck failed");
    return;
  }

  const auto command_count = static_cast<UINT>(state.range(0));
  const UINT batch_count = TimedCommandListBatchCount(command_count);
  CommandListBatch batch;
  if (FAILED(CreateOpenCommandListBatch(context, batch_count, &batch))) {
    state.SkipWithError("dispatch command-list batch setup failed");
    return;
  }
  for (const auto &list : batch.lists)
    BindDispatchResources(list.get(), resources);

  double measured_seconds = 0.0;
  for (auto _ : state) {
    const auto begin = clock_type::now();
    for (const auto &list : batch.lists) {
      for (UINT index = 0; index < command_count; ++index)
        list->Dispatch(1, 1, 1);
    }
    const auto end = clock_type::now();
    measured_seconds = PerWorkloadSeconds(end - begin, batch_count);
    state.SetIterationTime(measured_seconds);
    if (FAILED(CloseCommandListBatch(batch)) ||
        FAILED(SubmitCommandListBatch(context, batch))) {
      state.SkipWithError("dispatch benchmark execution failed");
      return;
    }
  }
  if (!CheckDeviceHealth(state, context))
    return;
  PublishOperationCount(state, "commands", command_count, measured_seconds);
}

void BI_D3D12RecordCopyBuffer(benchmark::State &state) {
  D3D12TestContext context;
  if (FAILED(context.Initialize())) {
    state.SkipWithError("D3D12 initialization failed");
    return;
  }
  ComPtr<ID3D12Resource> source;
  ComPtr<ID3D12Resource> destination;
  if (!CreateCopyResources(state, context, &source, &destination) ||
      !RunCopyHealthPrecheck(state, context, source.get(), destination.get(),
                             1))
    return;

  const auto command_count = static_cast<UINT>(state.range(0));
  const UINT batch_count =
      TimedCommandListBatchCount(command_count, kMinimumTimedCopyLists);
  CommandListBatch batch;
  if (FAILED(CreateOpenCommandListBatch(context, batch_count, &batch))) {
    state.SkipWithError("copy command-list batch setup failed");
    return;
  }

  double measured_seconds = 0.0;
  for (auto _ : state) {
    const auto begin = clock_type::now();
    for (const auto &list : batch.lists) {
      RecordCopyCommands(list.get(), destination.get(), source.get(),
                         command_count);
    }
    const auto end = clock_type::now();
    measured_seconds = PerWorkloadSeconds(end - begin, batch_count);
    state.SetIterationTime(measured_seconds);
    if (FAILED(CloseCommandListBatch(batch)) ||
        FAILED(SubmitCommandListBatch(context, batch))) {
      state.SkipWithError("copy command-list execution failed");
      return;
    }
  }
  if (!CheckDeviceHealth(state, context))
    return;
  PublishOperationCount(state, "commands", command_count, measured_seconds);
}

void RunCloseListBenchmark(benchmark::State &state, UINT command_count) {
  D3D12TestContext context;
  if (FAILED(context.Initialize())) {
    state.SkipWithError("D3D12 initialization failed");
    return;
  }
  ComPtr<ID3D12Resource> source;
  ComPtr<ID3D12Resource> destination;
  if (!CreateCopyResources(state, context, &source, &destination) ||
      !RunCopyHealthPrecheck(state, context, source.get(), destination.get(),
                             command_count))
    return;

  const UINT batch_count = command_count == kSmallCommandCount
                               ? kCloseSmallListBatchCount
                               : TimedCommandListBatchCount(command_count);
  CommandListBatch batch;
  if (FAILED(CreateOpenCommandListBatch(context, batch_count, &batch))) {
    state.SkipWithError("close command-list batch setup failed");
    return;
  }
  for (const auto &list : batch.lists) {
    RecordCopyCommands(list.get(), destination.get(), source.get(),
                       command_count);
  }

  double measured_seconds = 0.0;
  for (auto _ : state) {
    const auto begin = clock_type::now();
    const HRESULT result = CloseCommandListBatch(batch);
    const auto end = clock_type::now();
    if (FAILED(result)) {
      state.SkipWithError("command-list close failed");
      return;
    }
    measured_seconds = PerWorkloadSeconds(end - begin, batch_count);
    state.SetIterationTime(measured_seconds);
    if (FAILED(SubmitCommandListBatch(context, batch))) {
      state.SkipWithError("closed command-list execution failed");
      return;
    }
  }
  if (!CheckDeviceHealth(state, context))
    return;
  PublishOperationCount(state, "commands", command_count, measured_seconds);
}

void BI_D3D12CloseSmallList(benchmark::State &state) {
  RunCloseListBenchmark(state, kSmallCommandCount);
}

void BI_D3D12CloseLargeList(benchmark::State &state) {
  RunCloseListBenchmark(state, kLargeCommandCount);
}

void BI_D3D12SignalOnly(benchmark::State &state) {
  D3D12TestContext context;
  if (FAILED(context.Initialize())) {
    state.SkipWithError("D3D12 initialization failed");
    return;
  }
  ComPtr<ID3D12Fence> fence;
  if (FAILED(context.device()->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                           IID_PPV_ARGS(fence.put())))) {
    state.SkipWithError("signal benchmark fence creation failed");
    return;
  }

  UINT64 fence_value = 1;
  if (FAILED(context.queue()->Signal(fence.get(), fence_value)) ||
      FAILED(context.WaitForFence(fence.get(), fence_value)) ||
      !CheckDeviceHealth(state, context)) {
    if (!state.skipped())
      state.SkipWithError("signal benchmark health precheck failed");
    return;
  }

  double measured_seconds = 0.0;
  for (auto _ : state) {
    const auto begin = clock_type::now();
    HRESULT result = S_OK;
    for (UINT index = 0; index < kSignalBatchCount; ++index) {
      ++fence_value;
      result = context.queue()->Signal(fence.get(), fence_value);
      if (FAILED(result))
        break;
    }
    const auto end = clock_type::now();
    if (FAILED(result)) {
      state.SkipWithError("queue signal failed");
      return;
    }
    measured_seconds = PerWorkloadSeconds(end - begin, kSignalBatchCount);
    state.SetIterationTime(measured_seconds);
    if (FAILED(context.WaitForFence(fence.get(), fence_value))) {
      state.SkipWithError("queue signal completion wait failed");
      return;
    }
  }
  if (!CheckDeviceHealth(state, context))
    return;
  PublishOperationCount(state, "signals", 1, measured_seconds);
}

void BI_D3D12WaitAlreadyComplete(benchmark::State &state) {
  D3D12TestContext context;
  if (FAILED(context.Initialize())) {
    state.SkipWithError("D3D12 initialization failed");
    return;
  }
  constexpr UINT64 kCompletedValue = 1;
  ComPtr<ID3D12Fence> fence;
  if (FAILED(context.device()->CreateFence(
          kCompletedValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence.put()))) ||
      fence->GetCompletedValue() < kCompletedValue ||
      FAILED(context.WaitForFence(fence.get(), kCompletedValue))) {
    state.SkipWithError("completed-fence wait health precheck failed");
    return;
  }

  double measured_seconds = 0.0;
  for (auto _ : state) {
    const auto begin = clock_type::now();
    HRESULT result = S_OK;
    for (UINT index = 0; index < kSubMicroBatchCount; ++index) {
      result = context.WaitForFence(fence.get(), kCompletedValue);
      if (FAILED(result))
        break;
    }
    const auto end = clock_type::now();
    if (FAILED(result)) {
      state.SkipWithError("completed-fence wait failed");
      return;
    }
    measured_seconds = PerWorkloadSeconds(end - begin, kSubMicroBatchCount);
    state.SetIterationTime(measured_seconds);
  }
  if (!CheckDeviceHealth(state, context))
    return;
  PublishOperationCount(state, "waits", 1, measured_seconds);
}

void BI_D3D12AllocatorReuse(benchmark::State &state) {
  D3D12TestContext context;
  if (FAILED(context.Initialize())) {
    state.SkipWithError("D3D12 initialization failed");
    return;
  }

  // Every allocator is reset only after its previous list has completed. The
  // independent allocator batch preserves one real reuse per normalized
  // workload instead of repeatedly resetting an already-empty allocator.
  if (FAILED(context.ExecuteAndWait()) ||
      FAILED(context.allocator()->Reset()) ||
      FAILED(context.list()->Reset(context.allocator(), nullptr)) ||
      FAILED(context.list()->Close()) ||
      FAILED(ExecuteClosedList(context, context.list())) ||
      !CheckDeviceHealth(state, context)) {
    if (!state.skipped())
      state.SkipWithError("allocator reuse health precheck failed");
    return;
  }

  CommandListBatch batch;
  if (FAILED(
          CreateOpenCommandListBatch(context, kAllocatorBatchCount, &batch)) ||
      FAILED(CloseCommandListBatch(batch)) ||
      FAILED(SubmitCommandListBatch(context, batch))) {
    state.SkipWithError("allocator reuse batch setup failed");
    return;
  }

  double measured_seconds = 0.0;
  for (auto _ : state) {
    const auto begin = clock_type::now();
    HRESULT reset_result = S_OK;
    for (const auto &allocator : batch.allocators) {
      reset_result = allocator->Reset();
      if (FAILED(reset_result))
        break;
    }
    const auto end = clock_type::now();
    if (FAILED(reset_result)) {
      state.SkipWithError("command allocator reuse failed");
      return;
    }
    measured_seconds = PerWorkloadSeconds(end - begin, kAllocatorBatchCount);
    state.SetIterationTime(measured_seconds);

    for (UINT index = 0; index < kAllocatorBatchCount; ++index) {
      if (FAILED(batch.lists[index]->Reset(batch.allocators[index].get(),
                                           nullptr)) ||
          FAILED(batch.lists[index]->Close())) {
        state.SkipWithError("allocator reuse validation recording failed");
        return;
      }
    }
    if (FAILED(SubmitCommandListBatch(context, batch))) {
      state.SkipWithError("allocator reuse validation submission failed");
      return;
    }
  }
  if (!CheckDeviceHealth(state, context))
    return;
  PublishOperationCount(state, "reuses", 1, measured_seconds);
}

D3D12_SHADER_RESOURCE_VIEW_DESC
RawBufferSrv(UINT first_element = 0,
             UINT element_count = kDescriptorBufferElements) {
  D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
  srv.Format = DXGI_FORMAT_R32_TYPELESS;
  srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
  srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv.Buffer.FirstElement = first_element;
  srv.Buffer.NumElements = element_count;
  srv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
  return srv;
}

D3D12_UNORDERED_ACCESS_VIEW_DESC RawBufferUav() {
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
  uav.Format = DXGI_FORMAT_R32_TYPELESS;
  uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav.Buffer.NumElements = kDescriptorBufferElements;
  uav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
  return uav;
}

void RunCreateDescriptorBenchmark(benchmark::State &state, bool create_uav) {
  D3D12TestContext context;
  if (FAILED(context.Initialize())) {
    state.SkipWithError("D3D12 initialization failed");
    return;
  }
  const auto descriptor_count = static_cast<UINT>(state.range(0));
  const UINT batch_count = TimedBatchCount(descriptor_count);
  const UINT timed_descriptor_count = descriptor_count * batch_count;
  const D3D12_RESOURCE_FLAGS flags =
      create_uav ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
                 : D3D12_RESOURCE_FLAG_NONE;
  auto buffer = context.CreateBuffer(
      static_cast<UINT64>(kDescriptorBufferElements) * sizeof(std::uint32_t),
      D3D12_HEAP_TYPE_DEFAULT, flags, D3D12_RESOURCE_STATE_COMMON);
  auto heap = context.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, timed_descriptor_count, false);
  auto precheck_heap = context.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, false);
  if (!buffer || !heap || !precheck_heap) {
    state.SkipWithError("descriptor creation benchmark setup failed");
    return;
  }

  const auto srv = RawBufferSrv();
  const auto uav = RawBufferUav();
  const auto precheck_handle =
      precheck_heap->GetCPUDescriptorHandleForHeapStart();
  if (create_uav)
    context.device()->CreateUnorderedAccessView(buffer.get(), nullptr, &uav,
                                                precheck_handle);
  else
    context.device()->CreateShaderResourceView(buffer.get(), &srv,
                                               precheck_handle);
  if (!CheckDeviceHealth(state, context))
    return;

  const UINT increment = context.device()->GetDescriptorHandleIncrementSize(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  double measured_seconds = 0.0;
  for (auto _ : state) {
    const auto begin = clock_type::now();
    for (UINT batch_index = 0; batch_index < batch_count; ++batch_index) {
      auto handle = heap->GetCPUDescriptorHandleForHeapStart();
      handle.ptr +=
          static_cast<SIZE_T>(batch_index) * descriptor_count * increment;
      for (UINT index = 0; index < descriptor_count; ++index) {
        if (create_uav) {
          context.device()->CreateUnorderedAccessView(buffer.get(), nullptr,
                                                      &uav, handle);
        } else {
          context.device()->CreateShaderResourceView(buffer.get(), &srv,
                                                     handle);
        }
        handle.ptr += increment;
      }
    }
    const auto end = clock_type::now();
    measured_seconds = PerWorkloadSeconds(end - begin, batch_count);
    state.SetIterationTime(measured_seconds);
  }
  if (!CheckDeviceHealth(state, context))
    return;
  PublishOperationCount(state, "descriptors", descriptor_count,
                        measured_seconds);
}

void BI_D3D12CreateSrv(benchmark::State &state) {
  RunCreateDescriptorBenchmark(state, false);
}

void BI_D3D12CreateUav(benchmark::State &state) {
  RunCreateDescriptorBenchmark(state, true);
}

void BI_D3D12CopySingleDescriptor(benchmark::State &state) {
  D3D12TestContext context;
  if (FAILED(context.Initialize())) {
    state.SkipWithError("D3D12 initialization failed");
    return;
  }
  const auto descriptor_count = static_cast<UINT>(state.range(0));
  const UINT batch_count = TimedBatchCount(descriptor_count);
  const UINT timed_descriptor_count = descriptor_count * batch_count;
  auto buffer = context.CreateBuffer(
      static_cast<UINT64>(kDescriptorBufferElements) * sizeof(std::uint32_t),
      D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COMMON);
  auto source = context.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, false);
  auto destination = context.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, timed_descriptor_count, false);
  auto precheck_destination = context.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, false);
  if (!buffer || !source || !destination || !precheck_destination) {
    state.SkipWithError("single descriptor copy setup failed");
    return;
  }

  const auto srv = RawBufferSrv();
  const auto source_handle = source->GetCPUDescriptorHandleForHeapStart();
  context.device()->CreateShaderResourceView(buffer.get(), &srv, source_handle);
  context.device()->CopyDescriptorsSimple(
      1, precheck_destination->GetCPUDescriptorHandleForHeapStart(),
      source_handle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  if (!CheckDeviceHealth(state, context))
    return;

  const UINT increment = context.device()->GetDescriptorHandleIncrementSize(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  double measured_seconds = 0.0;
  for (auto _ : state) {
    const auto begin = clock_type::now();
    for (UINT batch_index = 0; batch_index < batch_count; ++batch_index) {
      auto destination_handle =
          destination->GetCPUDescriptorHandleForHeapStart();
      destination_handle.ptr +=
          static_cast<SIZE_T>(batch_index) * descriptor_count * increment;
      for (UINT index = 0; index < descriptor_count; ++index) {
        context.device()->CopyDescriptorsSimple(
            1, destination_handle, source_handle,
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        destination_handle.ptr += increment;
      }
    }
    const auto end = clock_type::now();
    measured_seconds = PerWorkloadSeconds(end - begin, batch_count);
    state.SetIterationTime(measured_seconds);
  }
  if (!CheckDeviceHealth(state, context))
    return;
  PublishOperationCount(state, "descriptors", descriptor_count,
                        measured_seconds);
}

void BI_D3D12OverwriteSameSlot(benchmark::State &state) {
  D3D12TestContext context;
  if (FAILED(context.Initialize())) {
    state.SkipWithError("D3D12 initialization failed");
    return;
  }
  const auto descriptor_count = static_cast<UINT>(state.range(0));
  auto buffer = context.CreateBuffer(
      static_cast<UINT64>(kDescriptorBufferElements) * sizeof(std::uint32_t),
      D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COMMON);
  auto heap = context.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, false);
  if (!buffer || !heap) {
    state.SkipWithError("descriptor overwrite setup failed");
    return;
  }

  D3D12_SHADER_RESOURCE_VIEW_DESC descriptors[2] = {RawBufferSrv(0, 1),
                                                    RawBufferSrv(1, 1)};
  const auto handle = heap->GetCPUDescriptorHandleForHeapStart();
  context.device()->CreateShaderResourceView(buffer.get(), &descriptors[0],
                                             handle);
  if (!CheckDeviceHealth(state, context))
    return;

  const UINT batch_count = TimedBatchCount(descriptor_count);
  double measured_seconds = 0.0;
  for (auto _ : state) {
    const auto begin = clock_type::now();
    for (UINT batch_index = 0; batch_index < batch_count; ++batch_index) {
      for (UINT index = 0; index < descriptor_count; ++index) {
        const auto &descriptor = descriptors[index & 1u];
        context.device()->CreateShaderResourceView(buffer.get(), &descriptor,
                                                   handle);
      }
    }
    const auto end = clock_type::now();
    measured_seconds = PerWorkloadSeconds(end - begin, batch_count);
    state.SetIterationTime(measured_seconds);
  }
  if (!CheckDeviceHealth(state, context))
    return;
  PublishOperationCount(state, "descriptors", descriptor_count,
                        measured_seconds);
}

void BI_D3D12RecordBarrierBatch(benchmark::State &state) {
  D3D12TestContext context;
  if (FAILED(context.Initialize())) {
    state.SkipWithError("D3D12 initialization failed");
    return;
  }
  auto buffer = context.CreateBuffer(4096, D3D12_HEAP_TYPE_DEFAULT,
                                     D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                     D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  if (!buffer) {
    state.SkipWithError("buffer creation failed");
    return;
  }
  const auto barrier_count = static_cast<UINT>(state.range(0));
  for (auto _ : state) {
    const auto begin = clock_type::now();
    for (UINT index = 0; index < barrier_count; ++index)
      D3D12TestContext::UavBarrier(context.list(), buffer.get());
    const HRESULT close_result = context.list()->Close();
    const auto end = clock_type::now();
    if (FAILED(close_result)) {
      state.SkipWithError("command-list close failed");
      return;
    }
    state.SetIterationTime(Seconds(end - begin));
    if (FAILED(context.ResetCommandList())) {
      state.SkipWithError("command-list reset failed");
      return;
    }
  }
  state.counters["commands"] = barrier_count;
}

void BI_D3D12ExecuteClosedLists(benchmark::State &state) {
  D3D12TestContext context;
  if (FAILED(context.Initialize())) {
    state.SkipWithError("D3D12 initialization failed");
    return;
  }
  const auto list_count = static_cast<UINT>(state.range(0));
  std::vector<ComPtr<ID3D12CommandAllocator>> allocators(list_count);
  std::vector<ComPtr<ID3D12GraphicsCommandList>> lists(list_count);
  std::vector<ID3D12CommandList *> submissions(list_count);
  for (UINT index = 0; index < list_count; ++index) {
    if (FAILED(context.device()->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(allocators[index].put()))) ||
        FAILED(context.device()->CreateCommandList(
            0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocators[index].get(), nullptr,
            IID_PPV_ARGS(lists[index].put()))) ||
        FAILED(lists[index]->Close())) {
      state.SkipWithError("closed command-list creation failed");
      return;
    }
    submissions[index] = lists[index].get();
  }
  for (auto _ : state) {
    const auto begin = clock_type::now();
    context.queue()->ExecuteCommandLists(list_count, submissions.data());
    const HRESULT result = context.SignalAndWait();
    const auto end = clock_type::now();
    if (FAILED(result)) {
      state.SkipWithError("queue submission failed");
      return;
    }
    state.SetIterationTime(Seconds(end - begin));
  }
  state.counters["lists"] = list_count;
}

void BI_D3D12CopyDescriptorRange(benchmark::State &state) {
  D3D12TestContext context;
  if (FAILED(context.Initialize())) {
    state.SkipWithError("D3D12 initialization failed");
    return;
  }
  const auto descriptor_count = static_cast<UINT>(state.range(0));
  const UINT batch_count = TimedBatchCount(descriptor_count);
  const UINT timed_descriptor_count = descriptor_count * batch_count;
  auto source = context.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, descriptor_count, false);
  auto destination = context.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, timed_descriptor_count, false);
  auto validation_source = context.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, descriptor_count, false);
  auto validation_destination = context.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, descriptor_count, true);
  auto buffer = context.CreateBuffer(4096, D3D12_HEAP_TYPE_DEFAULT,
                                     D3D12_RESOURCE_FLAG_NONE,
                                     D3D12_RESOURCE_STATE_COMMON);
  constexpr UINT kValidationElementCount = 64;
  constexpr UINT kValidationSentinel = 0x13579bdfu;
  constexpr UINT kValidationExpected = 0x2468ace0u;
  constexpr UINT64 kValidationSize =
      kValidationElementCount * sizeof(std::uint32_t);
  auto validation_output = context.CreateBuffer(
      kValidationSize, D3D12_HEAP_TYPE_DEFAULT,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  auto poison_output = context.CreateBuffer(
      kValidationSize, D3D12_HEAP_TYPE_DEFAULT,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

  D3D12_DESCRIPTOR_RANGE validation_range = {};
  validation_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  validation_range.NumDescriptors = 1;
  D3D12_ROOT_PARAMETER validation_parameters[2] = {};
  validation_parameters[0].ParameterType =
      D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  validation_parameters[0].Constants.Num32BitValues = 1;
  validation_parameters[1].ParameterType =
      D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  validation_parameters[1].DescriptorTable.NumDescriptorRanges = 1;
  validation_parameters[1].DescriptorTable.pDescriptorRanges =
      &validation_range;
  D3D12_ROOT_SIGNATURE_DESC validation_root_desc = {};
  validation_root_desc.NumParameters = 2;
  validation_root_desc.pParameters = validation_parameters;
  auto validation_root = context.CreateRootSignature(validation_root_desc);
  ComPtr<ID3D12PipelineState> validation_pipeline;
  if (validation_root) {
    validation_pipeline = context.CreateComputePipeline(
        validation_root.get(), ClearBufferComputeShader());
  }
  if (!source || !destination || !validation_source ||
      !validation_destination || !buffer || !validation_output ||
      !poison_output || !validation_root || !validation_pipeline) {
    state.SkipWithError("descriptor benchmark setup failed");
    return;
  }

  D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
  srv.Format = DXGI_FORMAT_R32_TYPELESS;
  srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
  srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv.Buffer.NumElements = 1024;
  srv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
  for (UINT index = 0; index < descriptor_count; ++index) {
    context.device()->CreateShaderResourceView(
        buffer.get(), &srv, context.CpuDescriptorHandle(source.get(), index));
  }
  D3D12_UNORDERED_ACCESS_VIEW_DESC validation_uav = {};
  validation_uav.Format = DXGI_FORMAT_R32_UINT;
  validation_uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  validation_uav.Buffer.NumElements = kValidationElementCount;

  const auto validation_cpu =
      context.CpuDescriptorHandle(validation_source.get(), 0);
  const auto validation_gpu =
      context.GpuDescriptorHandle(validation_destination.get(), 0);
  ID3D12DescriptorHeap *validation_heaps[] = {validation_destination.get()};

  context.device()->CreateUnorderedAccessView(
      poison_output.get(), nullptr, &validation_uav, validation_cpu);
  context.device()->CreateUnorderedAccessView(
      poison_output.get(), nullptr, &validation_uav,
      context.CpuDescriptorHandle(validation_destination.get(), 0));
  context.list()->SetDescriptorHeaps(1, validation_heaps);
  const UINT clear_values[4] = {kValidationSentinel, kValidationSentinel,
                                kValidationSentinel, kValidationSentinel};
  context.list()->ClearUnorderedAccessViewUint(
      validation_gpu, validation_cpu, poison_output.get(), clear_values, 0,
      nullptr);
  if (FAILED(context.ExecuteAndWait()) || FAILED(context.ResetCommandList())) {
    state.SkipWithError("descriptor range poison clear failed");
    return;
  }

  for (UINT index = 0; index < descriptor_count; ++index) {
    context.device()->CreateUnorderedAccessView(
        validation_output.get(), nullptr, &validation_uav,
        context.CpuDescriptorHandle(validation_source.get(), index));
  }
  context.device()->CreateUnorderedAccessView(
      validation_output.get(), nullptr, &validation_uav,
      context.CpuDescriptorHandle(validation_destination.get(), 0));
  context.list()->SetDescriptorHeaps(1, validation_heaps);
  context.list()->ClearUnorderedAccessViewUint(
      validation_gpu, validation_cpu, validation_output.get(), clear_values, 0,
      nullptr);
  if (FAILED(context.ExecuteAndWait()) || FAILED(context.ResetCommandList())) {
    state.SkipWithError("descriptor range validation clear failed");
    return;
  }

  for (UINT index = 0; index < descriptor_count; ++index) {
    context.device()->CreateUnorderedAccessView(
        poison_output.get(), nullptr, &validation_uav,
        context.CpuDescriptorHandle(validation_destination.get(), index));
  }
  context.device()->CopyDescriptorsSimple(
      descriptor_count,
      validation_destination->GetCPUDescriptorHandleForHeapStart(),
      validation_source->GetCPUDescriptorHandleForHeapStart(),
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  context.list()->SetDescriptorHeaps(1, validation_heaps);
  context.list()->SetPipelineState(validation_pipeline.get());
  context.list()->SetComputeRootSignature(validation_root.get());
  context.list()->SetComputeRoot32BitConstant(0, kValidationExpected, 0);
  for (UINT index = 0; index < descriptor_count; ++index) {
    context.list()->SetComputeRootDescriptorTable(
        1, context.GpuDescriptorHandle(validation_destination.get(), index));
    context.list()->Dispatch(1, 1, 1);
  }
  D3D12TestContext::Transition(
      context.list(), validation_output.get(),
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_COPY_SOURCE);
  D3D12TestContext::Transition(
      context.list(), poison_output.get(),
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_COPY_SOURCE);
  std::vector<std::uint8_t> validation_bytes;
  if (FAILED(context.ReadbackBuffer(validation_output.get(), kValidationSize,
                                    &validation_bytes)) ||
      validation_bytes.size() != kValidationSize) {
    state.SkipWithError("descriptor range validation readback failed");
    return;
  }
  for (UINT index = 0; index < kValidationElementCount; ++index) {
    std::uint32_t actual = 0;
    std::memcpy(&actual,
                validation_bytes.data() + index * sizeof(std::uint32_t),
                sizeof(actual));
    if (actual != kValidationExpected) {
      state.SkipWithError("descriptor range validation produced wrong data");
      return;
    }
  }
  if (FAILED(context.ResetCommandList())) {
    state.SkipWithError("descriptor range validation reset failed");
    return;
  }
  std::vector<std::uint8_t> poison_bytes;
  if (FAILED(context.ReadbackBuffer(poison_output.get(), kValidationSize,
                                    &poison_bytes)) ||
      poison_bytes.size() != kValidationSize) {
    state.SkipWithError("descriptor range poison readback failed");
    return;
  }
  for (UINT index = 0; index < kValidationElementCount; ++index) {
    std::uint32_t actual = 0;
    std::memcpy(&actual,
                poison_bytes.data() + index * sizeof(std::uint32_t),
                sizeof(actual));
    if (actual != kValidationSentinel) {
      state.SkipWithError("descriptor range copied a poison descriptor");
      return;
    }
  }
  if (!CheckDeviceHealth(state, context))
    return;

  const UINT increment = context.device()->GetDescriptorHandleIncrementSize(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  for (auto _ : state) {
    const auto begin = clock_type::now();
    for (UINT batch_index = 0; batch_index < batch_count; ++batch_index) {
      auto destination_handle =
          destination->GetCPUDescriptorHandleForHeapStart();
      destination_handle.ptr +=
          static_cast<SIZE_T>(batch_index) * descriptor_count * increment;
      context.device()->CopyDescriptorsSimple(
          descriptor_count, destination_handle,
          source->GetCPUDescriptorHandleForHeapStart(),
          D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }
    const auto end = clock_type::now();
    state.SetIterationTime(PerWorkloadSeconds(end - begin, batch_count));
  }
  if (!CheckDeviceHealth(state, context))
    return;
  state.counters["descriptors"] = descriptor_count;
}

BENCHMARK(BI_D3D12RecordEmptyList)->Iterations(1)->UseManualTime();

BENCHMARK(BI_D3D12RecordDispatch)
    ->Arg(1)
    ->Arg(16)
    ->Arg(32)
    ->Arg(256)
    ->Arg(4096)
    ->ArgName("commands")
    ->Iterations(1)
    ->UseManualTime();

BENCHMARK(BI_D3D12RecordCopyBuffer)
    ->Arg(1)
    ->Arg(16)
    ->Arg(32)
    ->Arg(256)
    ->Arg(4096)
    ->ArgName("commands")
    ->Iterations(1)
    ->UseManualTime();

BENCHMARK(BI_D3D12CloseSmallList)->Iterations(1)->UseManualTime();

BENCHMARK(BI_D3D12CloseLargeList)->Iterations(1)->UseManualTime();

BENCHMARK(BI_D3D12SignalOnly)->Iterations(1)->UseManualTime();

BENCHMARK(BI_D3D12WaitAlreadyComplete)->Iterations(1)->UseManualTime();

BENCHMARK(BI_D3D12AllocatorReuse)->Iterations(1)->UseManualTime();

BENCHMARK(BI_D3D12CreateSrv)
    ->Arg(1)
    ->Arg(16)
    ->Arg(32)
    ->Arg(256)
    ->Arg(4096)
    ->ArgName("descriptors")
    ->Iterations(1)
    ->UseManualTime();

BENCHMARK(BI_D3D12CreateUav)
    ->Arg(1)
    ->Arg(16)
    ->Arg(32)
    ->Arg(256)
    ->Arg(4096)
    ->ArgName("descriptors")
    ->Iterations(1)
    ->UseManualTime();

BENCHMARK(BI_D3D12CopySingleDescriptor)
    ->Arg(1)
    ->Arg(16)
    ->Arg(32)
    ->Arg(256)
    ->Arg(4096)
    ->ArgName("descriptors")
    ->Iterations(1)
    ->UseManualTime();

BENCHMARK(BI_D3D12OverwriteSameSlot)
    ->Arg(1)
    ->Arg(16)
    ->Arg(32)
    ->Arg(256)
    ->Arg(4096)
    ->ArgName("descriptors")
    ->Iterations(1)
    ->UseManualTime();

BENCHMARK(BI_D3D12RecordBarrierBatch)
    ->Arg(1)
    ->Arg(16)
    ->Arg(31)
    ->Arg(32)
    ->Arg(33)
    ->Arg(256)
    ->ArgName("barriers")
    ->Iterations(1)
    ->UseManualTime();

BENCHMARK(BI_D3D12ExecuteClosedLists)
    ->Arg(1)
    ->Arg(16)
    ->Arg(64)
    ->ArgName("lists")
    ->Iterations(1)
    ->UseManualTime();

BENCHMARK(BI_D3D12CopyDescriptorRange)
    ->Arg(1)
    ->Arg(16)
    ->Arg(32)
    ->Arg(256)
    ->Arg(4096)
    ->ArgName("descriptors")
    ->Iterations(1)
    ->UseManualTime();

} // namespace
