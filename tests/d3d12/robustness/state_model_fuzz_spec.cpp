#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>

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

} // namespace
