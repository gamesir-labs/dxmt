#include <dxmt_benchmark.hpp>

#include "d3d12_test_context.hpp"

#include <chrono>
#include <cstdint>
#include <vector>

namespace {

using clock_type = std::chrono::steady_clock;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

double Seconds(clock_type::duration duration) {
  return std::chrono::duration<double>(duration).count();
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
  auto source = context.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, descriptor_count, false);
  auto destination = context.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, descriptor_count, false);
  auto buffer = context.CreateBuffer(4096, D3D12_HEAP_TYPE_DEFAULT,
                                     D3D12_RESOURCE_FLAG_NONE,
                                     D3D12_RESOURCE_STATE_COMMON);
  if (!source || !destination || !buffer) {
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
  for (auto _ : state) {
    const auto begin = clock_type::now();
    context.device()->CopyDescriptorsSimple(
        descriptor_count, destination->GetCPUDescriptorHandleForHeapStart(),
        source->GetCPUDescriptorHandleForHeapStart(),
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    const auto end = clock_type::now();
    state.SetIterationTime(Seconds(end - begin));
  }
  state.counters["descriptors"] = descriptor_count;
}

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
    ->Arg(256)
    ->Arg(4096)
    ->ArgName("descriptors")
    ->Iterations(1)
    ->UseManualTime();

} // namespace
