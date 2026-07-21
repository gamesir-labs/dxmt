#include <dxmt_test.hpp>

#include "../../../include/dxmt_d3d12_test_path.hpp"
#include "d3d12_test_context.hpp"
#include <dxmt_test_shader.hpp>
#include <array>
#include <atomic>
#include <thread>

namespace {

using dxmt::d3d12::test::ExecutionPathConfig;
using dxmt::d3d12::test::ExecutionPathStats;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

class CompiledGenerationSpec : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  static void EnableStats(ID3D12GraphicsCommandList *list) {
    const ExecutionPathConfig config = {};
    ASSERT_EQ(list->SetPrivateData(
                  dxmt::d3d12::test::kExecutionPathConfigGuid,
                  sizeof(config), &config),
              S_OK);
  }

  static ExecutionPathStats ReadStats(ID3D12GraphicsCommandList *list) {
    ExecutionPathStats stats = {};
    UINT size = sizeof(stats);
    EXPECT_EQ(list->GetPrivateData(
                  dxmt::d3d12::test::kExecutionPathStatsGuid, &size, &stats),
              S_OK);
    EXPECT_EQ(size, sizeof(stats));
    EXPECT_EQ(stats.struct_size, sizeof(stats));
    return stats;
  }

  D3D12TestContext context_;
};

TEST_F(CompiledGenerationSpec, ReusesImmutableStateAndMergesTypedRanges) {
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  auto root_signature = context_.CreateRootSignature(root_desc);
  ASSERT_TRUE(root_signature);
  const auto shader = dxmt::test::CompileShader(
      "[numthreads(1, 1, 1)] void main() {}", "cs_5_0");
  ASSERT_EQ(shader.result, S_OK) << shader.diagnostic_text();
  auto pipeline = context_.CreateComputePipeline(
      root_signature.get(),
      {shader.bytecode->GetBufferPointer(), shader.bytecode->GetBufferSize()});
  ASSERT_TRUE(pipeline);
  auto buffer = context_.CreateBuffer(
      256, D3D12_HEAP_TYPE_DEFAULT,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  ASSERT_TRUE(buffer);

  const auto record_workload = [&] {
    EnableStats(context_.list());
    context_.list()->SetPipelineState(pipeline.get());
    context_.list()->SetComputeRootSignature(root_signature.get());
    for (UINT i = 0; i < 4; ++i)
      context_.list()->Dispatch(1, 1, 1);
    D3D12TestContext::Transition(
        context_.list(), buffer.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12TestContext::Transition(
        context_.list(), buffer.get(), D3D12_RESOURCE_STATE_COPY_SOURCE,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    ASSERT_EQ(context_.list()->Close(), S_OK);
  };

  record_workload();

  const auto first = ReadStats(context_.list());
  EXPECT_NE(first.command_list_generation, 0u);
  EXPECT_EQ(first.unexpected_container_growths, 0u);
  EXPECT_EQ(first.work_record_count, 4u);
  EXPECT_EQ(first.compiled_work_record_count, 4u);
  EXPECT_EQ(first.compute_segments, 1u);
  EXPECT_EQ(first.selected_compute_packets, 4u);
  EXPECT_EQ(first.state_delta_packets, 4u);
  EXPECT_EQ(first.zero_state_delta_packets, 3u);
  EXPECT_GT(first.immutable_state_reuses, 0u);
  EXPECT_EQ(first.compiled_barrier_ranges, 2u);
  EXPECT_EQ(first.compiled_barriers, 2u);
  EXPECT_EQ(first.compiled_resource_state_deltas, 1u);

  // Warm every same-type block required by a complete generation before
  // asserting the steady-state high-water contract. Other tests in the same
  // process may leave differently sized blocks in the per-thread pool.
  ExecutionPathStats warmed = {};
  bool reached_high_water = false;
  for (UINT attempt = 0; attempt < 16; ++attempt) {
    ASSERT_EQ(context_.ResetCommandList(), S_OK);
    record_workload();
    warmed = ReadStats(context_.list());
    EXPECT_GT(warmed.command_list_generation, first.command_list_generation);
    EXPECT_EQ(warmed.unexpected_container_growths, 0u);
    if (!warmed.storage_allocation_events) {
      reached_high_water = true;
      break;
    }
  }
  ASSERT_TRUE(reached_high_water)
      << "nodes=" << warmed.node_storage_allocation_events
      << " state=" << warmed.state_storage_allocation_events
      << " access=" << warmed.access_storage_allocation_events;

  ASSERT_EQ(context_.ResetCommandList(), S_OK);
  record_workload();
  const auto steady = ReadStats(context_.list());
  EXPECT_GT(steady.command_list_generation, warmed.command_list_generation);
  EXPECT_EQ(steady.unexpected_container_growths, 0u);
  EXPECT_EQ(steady.storage_allocation_events, 0u)
      << "nodes=" << steady.node_storage_allocation_events
      << " state=" << steady.state_storage_allocation_events
      << " access=" << steady.access_storage_allocation_events;

  ID3D12CommandList *lists[] = {context_.list()};
  context_.queue()->ExecuteCommandLists(1, lists);
  ASSERT_EQ(context_.SignalAndWait(), S_OK);
  const auto submitted = ReadStats(context_.list());
  EXPECT_EQ(submitted.submitted_generation_shares, 1u);
  EXPECT_EQ(submitted.submitted_generation_deep_copies, 0u);
}

TEST_F(CompiledGenerationSpec, ClosesIndependentListsConcurrentlyWithoutLock) {
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  auto root_signature = context_.CreateRootSignature(root_desc);
  ASSERT_TRUE(root_signature);
  const auto shader = dxmt::test::CompileShader(
      "[numthreads(1, 1, 1)] void main() {}", "cs_5_0");
  ASSERT_EQ(shader.result, S_OK) << shader.diagnostic_text();
  auto pipeline = context_.CreateComputePipeline(
      root_signature.get(),
      {shader.bytecode->GetBufferPointer(), shader.bytecode->GetBufferSize()});
  ASSERT_TRUE(pipeline);

  constexpr size_t kThreadCount = 4;
  constexpr UINT kRounds = 8;
  std::atomic<bool> success = true;
  std::array<std::thread, kThreadCount> workers;
  for (auto &worker : workers) {
    worker = std::thread([&, device = context_.device()] {
      ComPtr<ID3D12CommandAllocator> allocator;
      ComPtr<ID3D12GraphicsCommandList> list;
      if (FAILED(device->CreateCommandAllocator(
              D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(allocator.put()))) ||
          FAILED(device->CreateCommandList(
              0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.get(), nullptr,
              IID_PPV_ARGS(list.put())))) {
        success.store(false, std::memory_order_relaxed);
        return;
      }

      for (UINT round = 0; round < kRounds; ++round) {
        const ExecutionPathConfig config = {};
        if (FAILED(list->SetPrivateData(
                dxmt::d3d12::test::kExecutionPathConfigGuid, sizeof(config),
                &config))) {
          success.store(false, std::memory_order_relaxed);
          return;
        }
        list->SetPipelineState(pipeline.get());
        list->SetComputeRootSignature(root_signature.get());
        for (UINT dispatch = 0; dispatch < 16; ++dispatch)
          list->Dispatch(1, 1, 1);
        if (FAILED(list->Close())) {
          success.store(false, std::memory_order_relaxed);
          return;
        }

        ExecutionPathStats stats = {};
        UINT size = sizeof(stats);
        if (FAILED(list->GetPrivateData(
                dxmt::d3d12::test::kExecutionPathStatsGuid, &size, &stats)) ||
            stats.unexpected_container_growths ||
            (round && stats.storage_allocation_events)) {
          success.store(false, std::memory_order_relaxed);
          return;
        }
        if (round + 1 == kRounds)
          break;
        if (FAILED(allocator->Reset()) ||
            FAILED(list->Reset(allocator.get(), nullptr))) {
          success.store(false, std::memory_order_relaxed);
          return;
        }
      }
    });
  }
  for (auto &worker : workers)
    worker.join();
  EXPECT_TRUE(success.load(std::memory_order_relaxed));
}

} // namespace
