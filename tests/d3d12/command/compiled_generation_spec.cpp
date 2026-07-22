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
  EXPECT_EQ(first.selected_typed_nodes, 0u);
  EXPECT_EQ(first.fallback_segments, 0u);
  EXPECT_EQ(first.state_delta_packets, 4u);
  EXPECT_EQ(first.zero_state_delta_packets, 3u);
  EXPECT_GT(first.immutable_state_reuses, 0u);
  EXPECT_EQ(first.compiled_barrier_ranges, 2u);
  EXPECT_EQ(first.compiled_barriers, 2u);
  EXPECT_EQ(first.compiled_resource_state_deltas, 1u);
  EXPECT_EQ(first.encoder_graph_node_count, 3u);
  EXPECT_EQ(first.compute_encoder_node_count, 1u);
  EXPECT_EQ(first.graphics_encoder_node_count, 0u);
  EXPECT_GT(first.encoder_graph_elided_state_records, 0u);

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
  // SM5 bytecode deliberately uses the compatibility packet encoder. The
  // Close plan still contains no typed nodes; each dispatch lowers directly
  // from its complete compiled packet without entering legacy record replay.
  EXPECT_EQ(submitted.replayed_compiled_packet_fallbacks, 4u);
  EXPECT_EQ(submitted.legacy_replay_records, 0u);
}

TEST_F(CompiledGenerationSpec, MergesComputeNodesAcrossElidedState) {
  D3D12_ROOT_PARAMETER root_parameter = {};
  root_parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  root_parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  root_parameter.Constants.Num32BitValues = 1;
  root_parameter.Constants.ShaderRegister = 0;
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  root_desc.NumParameters = 1;
  root_desc.pParameters = &root_parameter;
  auto root_signature = context_.CreateRootSignature(root_desc);
  ASSERT_TRUE(root_signature);
  const auto shader = dxmt::test::CompileShader(
      "[numthreads(1, 1, 1)] void main() {}", "cs_5_0");
  ASSERT_EQ(shader.result, S_OK) << shader.diagnostic_text();
  auto pipeline = context_.CreateComputePipeline(
      root_signature.get(),
      {shader.bytecode->GetBufferPointer(), shader.bytecode->GetBufferSize()});
  ASSERT_TRUE(pipeline);

  EnableStats(context_.list());
  context_.list()->SetPipelineState(pipeline.get());
  context_.list()->SetComputeRootSignature(root_signature.get());
  context_.list()->SetComputeRoot32BitConstant(0, 1, 0);
  context_.list()->Dispatch(1, 1, 1);
  // The constant changes packet state, but does not require an encoder
  // boundary. Close folds the setter into the next packet.
  context_.list()->SetComputeRoot32BitConstant(0, 2, 0);
  context_.list()->Dispatch(1, 1, 1);
  ASSERT_EQ(context_.list()->Close(), S_OK);

  const auto stats = ReadStats(context_.list());
  EXPECT_EQ(stats.compute_segments, 2u);
  EXPECT_EQ(stats.selected_compute_packets, 2u);
  EXPECT_EQ(stats.encoder_graph_node_count, 1u);
  EXPECT_EQ(stats.compute_encoder_node_count, 1u);
  EXPECT_GE(stats.encoder_graph_elided_state_records, 3u);

  ID3D12CommandList *lists[] = {context_.list()};
  context_.queue()->ExecuteCommandLists(1, lists);
  ASSERT_EQ(context_.SignalAndWait(), S_OK);
  const auto submitted = ReadStats(context_.list());
  EXPECT_EQ(submitted.legacy_replay_records, 0u);
}

TEST_F(CompiledGenerationSpec,
       KeepsGraphicsEncoderOpenAcrossStateAndEquivalentDescriptorChanges) {
  const D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  auto root_signature = context_.CreateRootSignature(root_desc);
  ASSERT_TRUE(root_signature);
  const auto pixel = dxmt::test::CompileShader(
      "float4 main() : SV_Target { return float4(1, 1, 1, 1); }",
      "ps_5_0");
  ASSERT_EQ(pixel.result, S_OK) << pixel.diagnostic_text();
  auto pipeline = context_.CreateGraphicsPipeline(
      root_signature.get(), DXGI_FORMAT_R8G8B8A8_UNORM,
      {pixel.bytecode->GetBufferPointer(), pixel.bytecode->GetBufferSize()});
  ASSERT_TRUE(pipeline);
  auto target = context_.CreateTexture2D(
      8, 8, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
      D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
      D3D12_RESOURCE_STATE_RENDER_TARGET);
  auto heap = context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2,
                                            false);
  ASSERT_TRUE(target);
  ASSERT_TRUE(heap);
  const auto first_rtv = heap->GetCPUDescriptorHandleForHeapStart();
  auto second_rtv = first_rtv;
  second_rtv.ptr += context_.device()->GetDescriptorHandleIncrementSize(
      D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
  context_.device()->CreateRenderTargetView(target.get(), nullptr, first_rtv);
  context_.device()->CreateRenderTargetView(target.get(), nullptr, second_rtv);

  EnableStats(context_.list());
  context_.list()->SetGraphicsRootSignature(root_signature.get());
  context_.list()->SetPipelineState(pipeline.get());
  context_.list()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  const D3D12_VIEWPORT viewport = {0, 0, 8, 8, 0, 1};
  const D3D12_RECT first_scissor = {0, 0, 4, 8};
  const D3D12_RECT second_scissor = {4, 0, 8, 8};
  context_.list()->RSSetViewports(1, &viewport);
  context_.list()->RSSetScissorRects(1, &first_scissor);
  context_.list()->OMSetRenderTargets(1, &first_rtv, FALSE, nullptr);
  context_.list()->DrawInstanced(3, 1, 0, 0);
  context_.list()->RSSetScissorRects(1, &second_scissor);
  context_.list()->OMSetRenderTargets(1, &second_rtv, FALSE, nullptr);
  context_.list()->DrawInstanced(3, 1, 0, 0);
  ASSERT_EQ(context_.list()->Close(), S_OK);

  const auto stats = ReadStats(context_.list());
  EXPECT_EQ(stats.graphics_segments, 2u);
  EXPECT_EQ(stats.selected_graphics_packets, 2u);
  EXPECT_EQ(stats.encoder_graph_node_count, 1u);
  EXPECT_EQ(stats.graphics_encoder_node_count, 1u);

  ID3D12CommandList *lists[] = {context_.list()};
  context_.queue()->ExecuteCommandLists(1, lists);
  ASSERT_EQ(context_.SignalAndWait(), S_OK);
  const auto submitted = ReadStats(context_.list());
  EXPECT_EQ(submitted.legacy_replay_records, 0u);
  EXPECT_EQ(submitted.encoder_attachment_materializations, 1u);
}

TEST_F(CompiledGenerationSpec, SplitsGraphicsEncodersForDifferentAttachments) {
  const D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  auto root_signature = context_.CreateRootSignature(root_desc);
  ASSERT_TRUE(root_signature);
  const auto pixel = dxmt::test::CompileShader(
      "float4 main() : SV_Target { return float4(1, 1, 1, 1); }",
      "ps_5_0");
  ASSERT_EQ(pixel.result, S_OK) << pixel.diagnostic_text();
  auto pipeline = context_.CreateGraphicsPipeline(
      root_signature.get(), DXGI_FORMAT_R8G8B8A8_UNORM,
      {pixel.bytecode->GetBufferPointer(), pixel.bytecode->GetBufferSize()});
  ASSERT_TRUE(pipeline);
  std::array<ComPtr<ID3D12Resource>, 2> targets = {
      context_.CreateTexture2D(8, 8, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
                               D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
                               D3D12_RESOURCE_STATE_RENDER_TARGET),
      context_.CreateTexture2D(8, 8, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
                               D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
                               D3D12_RESOURCE_STATE_RENDER_TARGET)};
  auto heap = context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2,
                                            false);
  ASSERT_TRUE(targets[0]);
  ASSERT_TRUE(targets[1]);
  ASSERT_TRUE(heap);
  std::array<D3D12_CPU_DESCRIPTOR_HANDLE, 2> rtvs = {
      heap->GetCPUDescriptorHandleForHeapStart(),
      heap->GetCPUDescriptorHandleForHeapStart()};
  rtvs[1].ptr += context_.device()->GetDescriptorHandleIncrementSize(
      D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
  for (UINT i = 0; i < targets.size(); ++i)
    context_.device()->CreateRenderTargetView(targets[i].get(), nullptr,
                                              rtvs[i]);

  EnableStats(context_.list());
  context_.list()->SetGraphicsRootSignature(root_signature.get());
  context_.list()->SetPipelineState(pipeline.get());
  context_.list()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  const D3D12_VIEWPORT viewport = {0, 0, 8, 8, 0, 1};
  const D3D12_RECT scissor = {0, 0, 8, 8};
  context_.list()->RSSetViewports(1, &viewport);
  context_.list()->RSSetScissorRects(1, &scissor);
  for (const auto rtv : rtvs) {
    context_.list()->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    context_.list()->DrawInstanced(3, 1, 0, 0);
  }
  ASSERT_EQ(context_.list()->Close(), S_OK);

  const auto stats = ReadStats(context_.list());
  EXPECT_EQ(stats.graphics_segments, 2u);
  EXPECT_EQ(stats.selected_graphics_packets, 2u);
  EXPECT_EQ(stats.encoder_graph_node_count, 2u);
  EXPECT_EQ(stats.graphics_encoder_node_count, 2u);

  ID3D12CommandList *lists[] = {context_.list()};
  context_.queue()->ExecuteCommandLists(1, lists);
  ASSERT_EQ(context_.SignalAndWait(), S_OK);
  EXPECT_EQ(ReadStats(context_.list()).encoder_attachment_materializations,
            2u);
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
