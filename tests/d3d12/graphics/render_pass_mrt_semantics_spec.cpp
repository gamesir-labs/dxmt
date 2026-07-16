#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

namespace {

using dxmt::test::ColorsMatch;
using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;
using dxmt::test::TextureReadback;

constexpr UINT kWidth = 12;
constexpr UINT kHeight = 8;
constexpr DXGI_FORMAT kColorFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

std::uint32_t PixelAt(const TextureReadback &readback, UINT x, UINT y) {
  std::uint32_t pixel = 0;
  std::memcpy(&pixel,
              readback.data.data() + y * readback.row_pitch +
                  x * sizeof(pixel),
              sizeof(pixel));
  return pixel;
}

class RenderPassMrtSemanticsSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_EQ(context_.list()->QueryInterface(IID_PPV_ARGS(list4_.put())), S_OK);

    D3D12_ROOT_SIGNATURE_DESC empty_desc = {};
    empty_desc.Flags =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    empty_root_signature_ = context_.CreateRootSignature(empty_desc);
    ASSERT_TRUE(empty_root_signature_);

    auto vertex = CompileShader(R"(
      float4 main(uint id : SV_VertexID) : SV_Position {
        const float2 positions[3] = {
          float2(-1.0, -1.0), float2(-1.0, 3.0), float2(3.0, -1.0)
        };
        return float4(positions[id], 0.5, 1.0);
      })",
                                "vs_5_0");
    ASSERT_EQ(vertex.result, S_OK) << vertex.diagnostic_text();
    vertex_shader_ = std::move(vertex.bytecode);

    auto mrt = CompileShader(R"(
      struct Output {
        float4 target0 : SV_Target0;
        float4 target1 : SV_Target1;
      };
      Output main() {
        Output output;
        output.target0 = float4(1.0, 0.0, 0.0, 1.0);
        output.target1 = float4(0.0, 1.0, 0.0, 1.0);
        return output;
      })",
                             "ps_5_0");
    ASSERT_EQ(mrt.result, S_OK) << mrt.diagnostic_text();
    mrt_pixel_shader_ = std::move(mrt.bytecode);
  }

  ComPtr<ID3D12Resource> CreateTarget() {
    return context_.CreateTexture2D(
        kWidth, kHeight, 1, kColorFormat,
        D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
  }

  ComPtr<ID3D12PipelineState>
  CreateMrtPipeline(ID3D12RootSignature *root_signature, ID3DBlob *pixel,
                    UINT target_count) {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
    desc.pRootSignature = root_signature;
    desc.VS = {vertex_shader_->GetBufferPointer(),
               vertex_shader_->GetBufferSize()};
    desc.PS = {pixel->GetBufferPointer(), pixel->GetBufferSize()};
    desc.BlendState.IndependentBlendEnable = TRUE;
    for (UINT i = 0; i < target_count; ++i) {
      desc.BlendState.RenderTarget[i].RenderTargetWriteMask =
          D3D12_COLOR_WRITE_ENABLE_ALL;
      desc.RTVFormats[i] = kColorFormat;
    }
    desc.SampleMask = std::numeric_limits<UINT>::max();
    desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    desc.RasterizerState.DepthClipEnable = TRUE;
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets = target_count;
    desc.SampleDesc.Count = 1;
    ComPtr<ID3D12PipelineState> pipeline;
    EXPECT_EQ(context_.device()->CreateGraphicsPipelineState(
                  &desc, IID_PPV_ARGS(pipeline.put())),
              S_OK);
    return pipeline;
  }

  void SetDrawState(ID3D12RootSignature *root_signature,
                    ID3D12PipelineState *pipeline,
                    const D3D12_RECT &scissor) {
    context_.list()->SetGraphicsRootSignature(root_signature);
    context_.list()->SetPipelineState(pipeline);
    context_.list()->IASetPrimitiveTopology(
        D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    const D3D12_VIEWPORT viewport = {
        0.0f, 0.0f, float(kWidth), float(kHeight), 0.0f, 1.0f};
    context_.list()->RSSetViewports(1, &viewport);
    context_.list()->RSSetScissorRects(1, &scissor);
  }

  D3D12_RENDER_PASS_RENDER_TARGET_DESC
  PassTarget(D3D12_CPU_DESCRIPTOR_HANDLE rtv,
             const std::array<FLOAT, 4> &clear,
             D3D12_RENDER_PASS_ENDING_ACCESS_TYPE ending =
                 D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_PRESERVE) {
    D3D12_RENDER_PASS_RENDER_TARGET_DESC desc = {};
    desc.cpuDescriptor = rtv;
    desc.BeginningAccess.Type =
        D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR;
    desc.BeginningAccess.Clear.ClearValue.Format = kColorFormat;
    std::copy(clear.begin(), clear.end(),
              desc.BeginningAccess.Clear.ClearValue.Color);
    desc.EndingAccess.Type = ending;
    return desc;
  }

  std::vector<TextureReadback>
  ReadTargets(const std::vector<ID3D12Resource *> &targets) {
    for (auto *target : targets) {
      D3D12TestContext::Transition(context_.list(), target,
                                   D3D12_RESOURCE_STATE_RENDER_TARGET,
                                   D3D12_RESOURCE_STATE_COPY_SOURCE);
    }
    std::vector<TextureReadback> result(targets.size());
    for (UINT i = 0; i < targets.size(); ++i) {
      EXPECT_EQ(context_.ReadbackTexture(targets[i], &result[i]), S_OK);
      if (i + 1 < targets.size()) {
        EXPECT_EQ(context_.ResetCommandList(), S_OK);
      }
    }
    return result;
  }

  D3D12TestContext context_;
  ComPtr<ID3D12GraphicsCommandList4> list4_;
  ComPtr<ID3D12RootSignature> empty_root_signature_;
  ComPtr<ID3DBlob> vertex_shader_;
  ComPtr<ID3DBlob> mrt_pixel_shader_;
};

TEST_F(RenderPassMrtSemanticsSpec,
       MultipleColorAttachmentsClearAndReceiveIndependentOutputs) {
  std::array<ComPtr<ID3D12Resource>, 2> targets = {CreateTarget(),
                                                  CreateTarget()};
  auto heap = context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2,
                                             false);
  auto pipeline = CreateMrtPipeline(empty_root_signature_.get(),
                                    mrt_pixel_shader_.get(), 2);
  ASSERT_TRUE(targets[0]);
  ASSERT_TRUE(targets[1]);
  ASSERT_TRUE(heap);
  ASSERT_TRUE(pipeline);
  std::array<D3D12_CPU_DESCRIPTOR_HANDLE, 2> rtvs = {
      context_.CpuDescriptorHandle(heap.get(), 0),
      context_.CpuDescriptorHandle(heap.get(), 1)};
  context_.device()->CreateRenderTargetView(targets[0].get(), nullptr, rtvs[0]);
  context_.device()->CreateRenderTargetView(targets[1].get(), nullptr, rtvs[1]);

  const std::array<FLOAT, 4> blue = {0.0f, 0.0f, 1.0f, 1.0f};
  const std::array<FLOAT, 4> yellow = {1.0f, 1.0f, 0.0f, 1.0f};
  std::array<D3D12_RENDER_PASS_RENDER_TARGET_DESC, 2> pass_targets = {
      PassTarget(rtvs[0], blue), PassTarget(rtvs[1], yellow)};
  list4_->BeginRenderPass(2, pass_targets.data(), nullptr,
                          D3D12_RENDER_PASS_FLAG_NONE);
  const D3D12_RECT left_half = {0, 0, LONG(kWidth / 2), LONG(kHeight)};
  SetDrawState(empty_root_signature_.get(), pipeline.get(), left_half);
  context_.list()->DrawInstanced(3, 1, 0, 0);
  list4_->EndRenderPass();

  const auto readbacks =
      ReadTargets({targets[0].get(), targets[1].get()});
  ASSERT_EQ(readbacks.size(), 2u);
  for (UINT y = 0; y < kHeight; ++y) {
    for (UINT x = 0; x < kWidth; ++x) {
      const bool drawn = x < kWidth / 2;
      EXPECT_TRUE(ColorsMatch(PixelAt(readbacks[0], x, y),
                              drawn ? 0xff0000ffu : 0xffff0000u, 1));
      EXPECT_TRUE(ColorsMatch(PixelAt(readbacks[1], x, y),
                              drawn ? 0xff00ff00u : 0xff00ffffu, 1));
    }
  }
}

TEST_F(RenderPassMrtSemanticsSpec,
       MultiAttachmentPassMatchesExplicitClearAndTargetBinding) {
  std::array<ComPtr<ID3D12Resource>, 4> targets = {
      CreateTarget(), CreateTarget(), CreateTarget(), CreateTarget()};
  auto heap = context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 4,
                                             false);
  auto pipeline = CreateMrtPipeline(empty_root_signature_.get(),
                                    mrt_pixel_shader_.get(), 2);
  ASSERT_TRUE(heap);
  ASSERT_TRUE(pipeline);
  std::array<D3D12_CPU_DESCRIPTOR_HANDLE, 4> rtvs;
  for (UINT i = 0; i < targets.size(); ++i) {
    ASSERT_TRUE(targets[i]);
    rtvs[i] = context_.CpuDescriptorHandle(heap.get(), i);
    context_.device()->CreateRenderTargetView(targets[i].get(), nullptr,
                                              rtvs[i]);
  }
  const std::array<FLOAT, 4> background0 = {0.125f, 0.25f, 0.5f, 1.0f};
  const std::array<FLOAT, 4> background1 = {0.75f, 0.25f, 0.125f, 1.0f};
  const D3D12_RECT middle = {LONG(kWidth / 4), 0, LONG(3 * kWidth / 4),
                             LONG(kHeight)};

  std::array<D3D12_RENDER_PASS_RENDER_TARGET_DESC, 2> pass_targets = {
      PassTarget(rtvs[0], background0), PassTarget(rtvs[1], background1)};
  list4_->BeginRenderPass(2, pass_targets.data(), nullptr,
                          D3D12_RENDER_PASS_FLAG_NONE);
  SetDrawState(empty_root_signature_.get(), pipeline.get(), middle);
  context_.list()->DrawInstanced(3, 1, 0, 0);
  list4_->EndRenderPass();

  context_.list()->ClearRenderTargetView(rtvs[2], background0.data(), 0,
                                         nullptr);
  context_.list()->ClearRenderTargetView(rtvs[3], background1.data(), 0,
                                         nullptr);
  const std::array<D3D12_CPU_DESCRIPTOR_HANDLE, 2> flat_rtvs = {rtvs[2],
                                                               rtvs[3]};
  context_.list()->OMSetRenderTargets(2, flat_rtvs.data(), FALSE, nullptr);
  SetDrawState(empty_root_signature_.get(), pipeline.get(), middle);
  context_.list()->DrawInstanced(3, 1, 0, 0);

  const auto readbacks = ReadTargets({targets[0].get(), targets[1].get(),
                                      targets[2].get(), targets[3].get()});
  ASSERT_EQ(readbacks.size(), 4u);
  for (UINT y = 0; y < kHeight; ++y) {
    for (UINT x = 0; x < kWidth; ++x) {
      EXPECT_EQ(PixelAt(readbacks[0], x, y), PixelAt(readbacks[2], x, y))
          << "target 0 mismatch at (" << x << ", " << y << ")";
      EXPECT_EQ(PixelAt(readbacks[1], x, y), PixelAt(readbacks[3], x, y))
          << "target 1 mismatch at (" << x << ", " << y << ")";
    }
  }
}

TEST_F(RenderPassMrtSemanticsSpec,
       EndingDiscardAllowsACompleteSubsequentOverwrite) {
  auto target = CreateTarget();
  auto heap = context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1,
                                             false);
  ASSERT_TRUE(target);
  ASSERT_TRUE(heap);
  const auto rtv = heap->GetCPUDescriptorHandleForHeapStart();
  context_.device()->CreateRenderTargetView(target.get(), nullptr, rtv);
  const std::array<FLOAT, 4> old_color = {1.0f, 0.0f, 1.0f, 1.0f};
  const auto pass =
      PassTarget(rtv, old_color, D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_DISCARD);
  list4_->BeginRenderPass(1, &pass, nullptr, D3D12_RENDER_PASS_FLAG_NONE);
  list4_->EndRenderPass();

  constexpr FLOAT kReplacement[4] = {0.0f, 0.5f, 0.25f, 1.0f};
  context_.list()->ClearRenderTargetView(rtv, kReplacement, 0, nullptr);
  const auto readbacks = ReadTargets({target.get()});
  for (UINT y = 0; y < kHeight; ++y) {
    for (UINT x = 0; x < kWidth; ++x) {
      EXPECT_TRUE(ColorsMatch(PixelAt(readbacks[0], x, y), 0xff408000u, 2));
    }
  }
}

TEST_F(RenderPassMrtSemanticsSpec,
       AllowUavWritesPassExecutesPixelShaderAtomics) {
  D3D12_DESCRIPTOR_RANGE range = {};
  range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  range.NumDescriptors = 1;
  // Pixel UAV registers overlap output-merger slots. With one bound render
  // target the first legal UAV register is u1.
  range.BaseShaderRegister = 1;
  range.OffsetInDescriptorsFromTableStart =
      D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
  D3D12_ROOT_PARAMETER parameter = {};
  parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameter.DescriptorTable.NumDescriptorRanges = 1;
  parameter.DescriptorTable.pDescriptorRanges = &range;
  parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  root_desc.NumParameters = 1;
  root_desc.pParameters = &parameter;
  root_desc.Flags =
      D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
  auto uav_root_signature = context_.CreateRootSignature(root_desc);
  ASSERT_TRUE(uav_root_signature);

  auto pixel = CompileShader(R"(
    RWStructuredBuffer<uint> counter : register(u1);
    float4 main() : SV_Target {
      uint original;
      InterlockedAdd(counter[0], 1, original);
      return float4(0.0, 0.0, 1.0, 1.0);
    })",
                             "ps_5_0");
  ASSERT_EQ(pixel.result, S_OK) << pixel.diagnostic_text();
  auto pipeline =
      CreateMrtPipeline(uav_root_signature.get(), pixel.bytecode.get(), 1);
  auto target = CreateTarget();
  auto counter = context_.CreateBuffer(
      sizeof(std::uint32_t), D3D12_HEAP_TYPE_DEFAULT,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  auto rtv_heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
  auto uav_heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true);
  ASSERT_TRUE(pipeline);
  ASSERT_TRUE(target);
  ASSERT_TRUE(counter);
  ASSERT_TRUE(rtv_heap);
  ASSERT_TRUE(uav_heap);
  const auto rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
  const auto uav_cpu = uav_heap->GetCPUDescriptorHandleForHeapStart();
  const auto uav_gpu = uav_heap->GetGPUDescriptorHandleForHeapStart();
  context_.device()->CreateRenderTargetView(target.get(), nullptr, rtv);
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
  uav_desc.Format = DXGI_FORMAT_UNKNOWN;
  uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav_desc.Buffer.NumElements = 1;
  uav_desc.Buffer.StructureByteStride = sizeof(std::uint32_t);
  context_.device()->CreateUnorderedAccessView(counter.get(), nullptr,
                                               &uav_desc, uav_cpu);
  ID3D12DescriptorHeap *descriptor_heaps[] = {uav_heap.get()};
  context_.list()->SetDescriptorHeaps(1, descriptor_heaps);
  constexpr UINT kZero[4] = {};
  context_.list()->ClearUnorderedAccessViewUint(uav_gpu, uav_cpu, counter.get(),
                                                kZero, 0, nullptr);

  const std::array<FLOAT, 4> black = {};
  const auto pass = PassTarget(rtv, black);
  list4_->BeginRenderPass(1, &pass, nullptr,
                          D3D12_RENDER_PASS_FLAG_ALLOW_UAV_WRITES);
  SetDrawState(uav_root_signature.get(), pipeline.get(),
               {0, 0, LONG(kWidth), LONG(kHeight)});
  context_.list()->SetGraphicsRootDescriptorTable(0, uav_gpu);
  context_.list()->DrawInstanced(3, 1, 0, 0);
  list4_->EndRenderPass();
  D3D12TestContext::UavBarrier(context_.list(), counter.get());
  D3D12TestContext::Transition(context_.list(), counter.get(),
                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
  std::vector<std::uint8_t> bytes;
  ASSERT_EQ(context_.ReadbackBuffer(counter.get(), sizeof(std::uint32_t),
                                    &bytes),
            S_OK);
  ASSERT_EQ(bytes.size(), sizeof(std::uint32_t));
  std::uint32_t value = 0;
  std::memcpy(&value, bytes.data(), sizeof(value));
  EXPECT_EQ(value, kWidth * kHeight);
}

} // namespace
