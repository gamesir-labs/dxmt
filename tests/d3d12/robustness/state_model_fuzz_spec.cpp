#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

std::uint32_t NextStateRandom(std::uint32_t &state) {
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return state;
}

class D3D12LegalCommandStateMachineFuzzSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);

    D3D12_ROOT_PARAMETER parameter = {};
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameter.Constants.Num32BitValues = 1;
    D3D12_ROOT_SIGNATURE_DESC root_desc = {};
    root_desc.NumParameters = 1;
    root_desc.pParameters = &parameter;
    root_ = context_.CreateRootSignature(root_desc);
    heap_ = context_.CreateDescriptorHeap(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 4, true);
    ASSERT_TRUE(root_);
    ASSERT_TRUE(heap_);
    const auto pixel = CompileShader(
        "float4 main() : SV_Target { return float4(0.25, 0.5, 0.75, 1); }",
        "ps_5_0");
    ASSERT_EQ(pixel.result, S_OK) << pixel.diagnostic_text();
    const D3D12_SHADER_BYTECODE bytecode = {pixel.bytecode->GetBufferPointer(),
                                            pixel.bytecode->GetBufferSize()};
    graphics_pipeline_ = context_.CreateGraphicsPipeline(
        root_.get(), DXGI_FORMAT_R8G8B8A8_UNORM, bytecode);
    ASSERT_TRUE(graphics_pipeline_);
  }

  D3D12TestContext context_;
  ComPtr<ID3D12RootSignature> root_;
  ComPtr<ID3D12DescriptorHeap> heap_;
  ComPtr<ID3D12PipelineState> graphics_pipeline_;
};

TEST_F(D3D12LegalCommandStateMachineFuzzSpec,
       LegalModelMatchesAcross256FixedSeeds) {
  std::uint32_t first_seed = 0x6d2b79f5u;
  std::size_t seed_count = 256;
  if (const char *replay_seed = std::getenv("DXMT_D3D12_STATE_FUZZ_SEED")) {
    char *end = nullptr;
    const auto parsed = std::strtoul(replay_seed, &end, 0);
    ASSERT_NE(end, replay_seed) << "invalid DXMT_D3D12_STATE_FUZZ_SEED";
    ASSERT_EQ(*end, '\0') << "invalid DXMT_D3D12_STATE_FUZZ_SEED";
    first_seed = static_cast<std::uint32_t>(parsed);
    seed_count = 1;
  }

  for (std::size_t seed_index = 0; seed_index < seed_count; ++seed_index) {
    const std::uint32_t seed =
        first_seed + static_cast<std::uint32_t>(seed_index * 0x9e3779b9u);
    SCOPED_TRACE(::testing::Message() << "seed=0x" << std::hex << seed);
    std::uint32_t random = seed;
    const UINT value = NextStateRandom(random) | 1u;
    auto upload =
        context_.CreateUploadBuffer(sizeof(value), &value, sizeof(value));
    auto scratch = context_.CreateBuffer(sizeof(value), D3D12_HEAP_TYPE_DEFAULT,
                                         D3D12_RESOURCE_FLAG_NONE,
                                         D3D12_RESOURCE_STATE_COPY_DEST);
    auto readback = context_.CreateBuffer(
        sizeof(value), D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE,
        D3D12_RESOURCE_STATE_COPY_DEST);
    auto target =
        context_.CreateTexture2D(4, 4, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
                                 D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
                                 D3D12_RESOURCE_STATE_RENDER_TARGET);
    auto rtv_heap =
        context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
    ASSERT_TRUE(upload);
    ASSERT_TRUE(scratch);
    ASSERT_TRUE(readback);
    ASSERT_TRUE(target);
    ASSERT_TRUE(rtv_heap);
    const auto rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
    context_.device()->CreateRenderTargetView(target.get(), nullptr, rtv);

    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    ASSERT_EQ(
        context_.device()->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(allocator.put())),
        S_OK);
    ASSERT_EQ(context_.device()->CreateCommandList(
                  0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.get(), nullptr,
                  IID_PPV_ARGS(list.put())),
              S_OK);

    bool recording = true;
    bool submitted = false;
    bool completed = false;
    bool graphics_pipeline_bound = false;
    bool graphics_state_bound = false;
    D3D12_RESOURCE_STATES scratch_state = D3D12_RESOURCE_STATE_COPY_DEST;
    D3D12_RESOURCE_STATES committed_scratch_state = scratch_state;
    const std::size_t operation_count = 5 + NextStateRandom(random) % 76;
    for (std::size_t operation = 0; operation < operation_count; ++operation) {
      switch (NextStateRandom(random) % 12) {
      case 0:
        if (recording && scratch_state == D3D12_RESOURCE_STATE_COPY_DEST)
          list->CopyBufferRegion(scratch.get(), 0, upload.get(), 0,
                                 sizeof(value));
        break;
      case 1:
        if (recording) {
          const auto next_state =
              scratch_state == D3D12_RESOURCE_STATE_COPY_DEST
                  ? D3D12_RESOURCE_STATE_COPY_SOURCE
                  : D3D12_RESOURCE_STATE_COPY_DEST;
          D3D12TestContext::Transition(list.get(), scratch.get(), scratch_state,
                                       next_state);
          scratch_state = next_state;
        }
        break;
      case 2:
        if (recording) {
          list->SetComputeRootSignature(root_.get());
          list->SetComputeRoot32BitConstant(0, value, 0);
        }
        break;
      case 3:
        if (recording) {
          ID3D12DescriptorHeap *heaps[] = {heap_.get()};
          list->SetDescriptorHeaps(1, heaps);
        }
        break;
      case 4:
        if (recording) {
          ASSERT_EQ(list->Close(), S_OK);
          recording = false;
          submitted = false;
          completed = false;
        }
        break;
      case 5:
        if (!recording && !submitted) {
          ID3D12CommandList *lists[] = {list.get()};
          context_.queue()->ExecuteCommandLists(1, lists);
          submitted = true;
          committed_scratch_state = scratch_state;
        }
        break;
      case 6:
        if (submitted && !completed) {
          ASSERT_EQ(context_.SignalAndWait(), S_OK);
          completed = true;
        }
        break;
      case 7:
        if (!recording && (!submitted || completed)) {
          if (!submitted)
            scratch_state = committed_scratch_state;
          ASSERT_EQ(allocator->Reset(), S_OK);
          ASSERT_EQ(list->Reset(allocator.get(), nullptr), S_OK);
          recording = true;
          submitted = false;
          completed = false;
          graphics_pipeline_bound = false;
          graphics_state_bound = false;
        }
        break;
      case 8:
        if (recording) {
          list->SetGraphicsRootSignature(root_.get());
          list->SetPipelineState(graphics_pipeline_.get());
          graphics_pipeline_bound = true;
        }
        break;
      case 9:
        if (recording) {
          const D3D12_VIEWPORT viewport = {0, 0, 4, 4, 0, 1};
          const D3D12_RECT scissor = {0, 0, 4, 4};
          list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
          list->RSSetViewports(1, &viewport);
          list->RSSetScissorRects(1, &scissor);
          list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
          graphics_state_bound = true;
        }
        break;
      case 10:
        if (recording && graphics_pipeline_bound && graphics_state_bound)
          list->DrawInstanced(3, 1, 0, 0);
        break;
      case 11:
        if (recording) {
          const FLOAT color[4] = {float((value >> 0) & 0xffu) / 255.0f,
                                  float((value >> 8) & 0xffu) / 255.0f,
                                  float((value >> 16) & 0xffu) / 255.0f, 1.0f};
          list->ClearRenderTargetView(rtv, color, 0, nullptr);
        }
        break;
      }
    }

    if (!recording) {
      if (submitted && !completed) {
        ASSERT_EQ(context_.SignalAndWait(), S_OK);
      }
      if (!submitted)
        scratch_state = committed_scratch_state;
      ASSERT_EQ(allocator->Reset(), S_OK);
      ASSERT_EQ(list->Reset(allocator.get(), nullptr), S_OK);
      recording = true;
    }
    if (scratch_state == D3D12_RESOURCE_STATE_COPY_SOURCE) {
      D3D12TestContext::Transition(list.get(), scratch.get(), scratch_state,
                                   D3D12_RESOURCE_STATE_COPY_DEST);
      scratch_state = D3D12_RESOURCE_STATE_COPY_DEST;
    }
    list->CopyBufferRegion(scratch.get(), 0, upload.get(), 0, sizeof(value));
    D3D12TestContext::Transition(list.get(), scratch.get(), scratch_state,
                                 D3D12_RESOURCE_STATE_COPY_SOURCE);
    list->CopyBufferRegion(readback.get(), 0, scratch.get(), 0, sizeof(value));
    ASSERT_EQ(list->Close(), S_OK);
    ID3D12CommandList *lists[] = {list.get()};
    context_.queue()->ExecuteCommandLists(1, lists);
    ASSERT_EQ(context_.SignalAndWait(), S_OK);

    UINT *mapping = nullptr;
    const D3D12_RANGE read_range = {0, sizeof(value)};
    ASSERT_EQ(
        readback->Map(0, &read_range, reinterpret_cast<void **>(&mapping)),
        S_OK);
    EXPECT_EQ(*mapping, value);
    const D3D12_RANGE no_write = {0, 0};
    readback->Unmap(0, &no_write);
  }
}

TEST_F(D3D12LegalCommandStateMachineFuzzSpec,
       MultiListTwoQueueDispatchDescriptorAndSubresourceModel) {
  constexpr UINT kQueueCount = 2;
  constexpr UINT kListsPerQueue = 2;
  constexpr UINT kListCount = kQueueCount * kListsPerQueue;
  constexpr UINT kOutputsPerQueue = 2;
  constexpr UINT kMipCount = 2;

  const auto compute = CompileShader(R"(
    cbuffer Parameters : register(b0) { uint value; };
    RWBuffer<uint> output : register(u0);
    [numthreads(1, 1, 1)]
    void main() { output[0] = value; }
  )",
                                     "cs_5_0");
  ASSERT_EQ(compute.result, S_OK) << compute.diagnostic_text();

  D3D12_DESCRIPTOR_RANGE range = {};
  range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  range.NumDescriptors = 1;
  range.OffsetInDescriptorsFromTableStart = 0;
  std::array<D3D12_ROOT_PARAMETER, 2> parameters = {};
  parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  parameters[0].Constants.ShaderRegister = 0;
  parameters[0].Constants.Num32BitValues = 1;
  parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[1].DescriptorTable.NumDescriptorRanges = 1;
  parameters[1].DescriptorTable.pDescriptorRanges = &range;
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  root_desc.NumParameters = static_cast<UINT>(parameters.size());
  root_desc.pParameters = parameters.data();
  auto dispatch_root = context_.CreateRootSignature(root_desc);
  ASSERT_TRUE(dispatch_root);
  const D3D12_SHADER_BYTECODE bytecode = {
      compute.bytecode->GetBufferPointer(), compute.bytecode->GetBufferSize()};
  auto dispatch_pipeline =
      context_.CreateComputePipeline(dispatch_root.get(), bytecode);
  ASSERT_TRUE(dispatch_pipeline);

  std::array<ComPtr<ID3D12CommandQueue>, kQueueCount> queues;
  std::array<ComPtr<ID3D12Fence>, kQueueCount> fences;
  std::array<UINT64, kQueueCount> fence_values = {};
  for (UINT queue_index = 0; queue_index < kQueueCount; ++queue_index) {
    D3D12_COMMAND_QUEUE_DESC queue_desc = {};
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ASSERT_EQ(context_.device()->CreateCommandQueue(
                  &queue_desc, IID_PPV_ARGS(queues[queue_index].put())),
              S_OK);
    ASSERT_EQ(context_.device()->CreateFence(
                  0, D3D12_FENCE_FLAG_NONE,
                  IID_PPV_ARGS(fences[queue_index].put())),
              S_OK);
  }

  std::array<ComPtr<ID3D12CommandAllocator>, kListCount> allocators;
  std::array<ComPtr<ID3D12GraphicsCommandList>, kListCount> lists;
  for (UINT list_index = 0; list_index < kListCount; ++list_index) {
    ASSERT_EQ(context_.device()->CreateCommandAllocator(
                  D3D12_COMMAND_LIST_TYPE_DIRECT,
                  IID_PPV_ARGS(allocators[list_index].put())),
              S_OK);
    ASSERT_EQ(context_.device()->CreateCommandList(
                  0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                  allocators[list_index].get(), nullptr,
                  IID_PPV_ARGS(lists[list_index].put())),
              S_OK);
    ASSERT_EQ(lists[list_index]->Close(), S_OK);
  }

  std::uint32_t first_seed = 0x8f7011eeu;
  std::size_t seed_count = 32;
  if (const char *replay_seed =
          std::getenv("DXMT_D3D12_STATE_FUZZ_SEED")) {
    char *end = nullptr;
    const auto parsed = std::strtoul(replay_seed, &end, 0);
    ASSERT_NE(end, replay_seed) << "invalid DXMT_D3D12_STATE_FUZZ_SEED";
    ASSERT_EQ(*end, '\0') << "invalid DXMT_D3D12_STATE_FUZZ_SEED";
    first_seed = static_cast<std::uint32_t>(parsed);
    seed_count = 1;
  }

  for (std::size_t seed_index = 0; seed_index < seed_count; ++seed_index) {
    const std::uint32_t seed =
        first_seed + static_cast<std::uint32_t>(seed_index * 0x9e3779b9u);
    SCOPED_TRACE(::testing::Message()
                 << "replay with DXMT_D3D12_STATE_FUZZ_SEED=0x" << std::hex
                 << seed);
    std::uint32_t random = seed;

    std::array<ComPtr<ID3D12DescriptorHeap>, kQueueCount> descriptor_heaps;
    std::array<std::array<ComPtr<ID3D12Resource>, kOutputsPerQueue>,
               kQueueCount>
        outputs;
    std::array<ComPtr<ID3D12Resource>, kQueueCount> textures;
    std::array<std::array<D3D12_RESOURCE_STATES, kMipCount>, kQueueCount>
        mip_states;
    std::array<std::array<UINT, kOutputsPerQueue>, kQueueCount>
        descriptor_resources = {};
    std::array<std::array<UINT, kOutputsPerQueue>, kQueueCount>
        descriptor_generations = {};
    std::array<std::array<std::uint32_t, kOutputsPerQueue>, kQueueCount>
        expected_outputs = {};

    D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
    uav.Format = DXGI_FORMAT_R32_UINT;
    uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uav.Buffer.NumElements = 1;
    for (UINT queue_index = 0; queue_index < kQueueCount; ++queue_index) {
      descriptor_heaps[queue_index] = context_.CreateDescriptorHeap(
          D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kOutputsPerQueue, true);
      textures[queue_index] = context_.CreateTexture2D(
          8, 8, kMipCount, DXGI_FORMAT_R8G8B8A8_UNORM,
          D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
      ASSERT_TRUE(descriptor_heaps[queue_index]);
      ASSERT_TRUE(textures[queue_index]);
      mip_states[queue_index].fill(D3D12_RESOURCE_STATE_COPY_DEST);
      for (UINT output_index = 0; output_index < kOutputsPerQueue;
           ++output_index) {
        outputs[queue_index][output_index] = context_.CreateBuffer(
            sizeof(std::uint32_t), D3D12_HEAP_TYPE_DEFAULT,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        ASSERT_TRUE(outputs[queue_index][output_index]);
        descriptor_resources[queue_index][output_index] = output_index;
        context_.device()->CreateUnorderedAccessView(
            outputs[queue_index][output_index].get(), nullptr, &uav,
            context_.CpuDescriptorHandle(descriptor_heaps[queue_index].get(),
                                         output_index));
      }
    }

    const UINT operation_count = 12 + NextStateRandom(random) % 13;
    for (UINT operation = 0; operation < operation_count; ++operation) {
      const UINT worker = operation < kListCount
                              ? operation
                              : NextStateRandom(random) % kListCount;
      const UINT queue_index = worker / kListsPerQueue;
      const UINT other_queue = queue_index ^ 1u;
      const UINT list_index = worker;

      // Descriptor storage may be consumed by an earlier submission. Waiting
      // for this queue makes each CPU overwrite generation unambiguous.
      ASSERT_EQ(context_.WaitForFence(fences[queue_index].get(),
                                      fence_values[queue_index]),
                S_OK);
      ASSERT_EQ(allocators[list_index]->Reset(), S_OK);
      ASSERT_EQ(lists[list_index]->Reset(allocators[list_index].get(),
                                         dispatch_pipeline.get()),
                S_OK);

      const UINT descriptor_slot = operation < kListCount
                                       ? operation % kOutputsPerQueue
                                       : NextStateRandom(random) %
                                             kOutputsPerQueue;
      const UINT descriptor_resource = operation < kListCount
                                           ? operation % kOutputsPerQueue
                                           : NextStateRandom(random) %
                                                 kOutputsPerQueue;
      ++descriptor_generations[queue_index][descriptor_slot];
      descriptor_resources[queue_index][descriptor_slot] =
          descriptor_resource;
      context_.device()->CreateUnorderedAccessView(
          outputs[queue_index][descriptor_resource].get(), nullptr, &uav,
          context_.CpuDescriptorHandle(descriptor_heaps[queue_index].get(),
                                       descriptor_slot));

      const UINT subresource = operation < kListCount
                                   ? operation % kMipCount
                                   : NextStateRandom(random) % kMipCount;
      const auto before = mip_states[queue_index][subresource];
      const auto after = before == D3D12_RESOURCE_STATE_COPY_DEST
                             ? D3D12_RESOURCE_STATE_COPY_SOURCE
                             : D3D12_RESOURCE_STATE_COPY_DEST;
      D3D12_RESOURCE_BARRIER barrier = {};
      barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      barrier.Transition.pResource = textures[queue_index].get();
      barrier.Transition.Subresource = subresource;
      barrier.Transition.StateBefore = before;
      barrier.Transition.StateAfter = after;
      lists[list_index]->ResourceBarrier(1, &barrier);
      mip_states[queue_index][subresource] = after;

      const std::uint32_t value =
          seed ^ (0x45d9f3bu * (operation + 1)) ^
          (descriptor_generations[queue_index][descriptor_slot] << 24) ^
          (queue_index << 20) ^ (descriptor_slot << 16);
      ID3D12DescriptorHeap *heaps[] = {
          descriptor_heaps[queue_index].get()};
      lists[list_index]->SetDescriptorHeaps(1, heaps);
      lists[list_index]->SetComputeRootSignature(dispatch_root.get());
      lists[list_index]->SetComputeRoot32BitConstant(0, value, 0);
      lists[list_index]->SetComputeRootDescriptorTable(
          1, context_.GpuDescriptorHandle(descriptor_heaps[queue_index].get(),
                                          descriptor_slot));
      lists[list_index]->Dispatch(1, 1, 1);
      ASSERT_EQ(lists[list_index]->Close(), S_OK);

      if (fence_values[other_queue] && (NextStateRandom(random) & 1u)) {
        ASSERT_EQ(queues[queue_index]->Wait(fences[other_queue].get(),
                                            fence_values[other_queue]),
                  S_OK);
      }
      ID3D12CommandList *submission[] = {lists[list_index].get()};
      queues[queue_index]->ExecuteCommandLists(1, submission);
      ++fence_values[queue_index];
      ASSERT_EQ(queues[queue_index]->Signal(fences[queue_index].get(),
                                            fence_values[queue_index]),
                S_OK);
      expected_outputs[queue_index]
                      [descriptor_resources[queue_index][descriptor_slot]] =
                          value;
    }

    for (UINT queue_index = 0; queue_index < kQueueCount; ++queue_index) {
      ASSERT_EQ(context_.WaitForFence(fences[queue_index].get(),
                                      fence_values[queue_index]),
                S_OK);
    }

    constexpr UINT64 kReadbackSize =
        kQueueCount * kOutputsPerQueue * sizeof(std::uint32_t);
    auto readback = context_.CreateBuffer(
        kReadbackSize, D3D12_HEAP_TYPE_READBACK,
        D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
    ASSERT_TRUE(readback);
    UINT output_offset = 0;
    for (UINT queue_index = 0; queue_index < kQueueCount; ++queue_index) {
      for (UINT output_index = 0; output_index < kOutputsPerQueue;
           ++output_index) {
        D3D12TestContext::Transition(
            context_.list(), outputs[queue_index][output_index].get(),
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_COPY_SOURCE);
        context_.list()->CopyBufferRegion(
            readback.get(), output_offset * sizeof(std::uint32_t),
            outputs[queue_index][output_index].get(), 0,
            sizeof(std::uint32_t));
        ++output_offset;
      }
    }
    ASSERT_EQ(context_.ExecuteAndWait(), S_OK);

    void *mapping = nullptr;
    const D3D12_RANGE read_range = {0, static_cast<SIZE_T>(kReadbackSize)};
    ASSERT_EQ(readback->Map(0, &read_range, &mapping), S_OK);
    ASSERT_NE(mapping, nullptr);
    const auto *actual = static_cast<const std::uint32_t *>(mapping);
    output_offset = 0;
    for (UINT queue_index = 0; queue_index < kQueueCount; ++queue_index) {
      for (UINT output_index = 0; output_index < kOutputsPerQueue;
           ++output_index) {
        EXPECT_EQ(actual[output_offset],
                  expected_outputs[queue_index][output_index])
            << "queue=" << queue_index << " output=" << output_index;
        ++output_offset;
      }
    }
    const D3D12_RANGE no_write = {0, 0};
    readback->Unmap(0, &no_write);
    EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
    ASSERT_EQ(context_.ResetCommandList(), S_OK);
  }
}

} // namespace
