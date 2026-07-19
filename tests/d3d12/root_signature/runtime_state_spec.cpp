#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"
#include "shaders/runtime_test_shaders.hpp"

#include <array>
#include <cstring>
#include <vector>

namespace {

using dxmt::test::ClearBufferComputeShader;
using dxmt::test::ColorsMatch;
using dxmt::test::ComPtr;
using dxmt::test::CompileShader;
using dxmt::test::CreateIsolatedD3D12Device;
using dxmt::test::D3D12TestContext;
using dxmt::test::TextureReadback;

struct RootStateResources {
  ComPtr<ID3D12RootSignature> signature;
  ComPtr<ID3D12PipelineState> pipeline;
  ComPtr<ID3D12Resource> output;
  ComPtr<ID3D12DescriptorHeap> heap;
};

class RootSignatureRuntimeStateSpec : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_TRUE(SUCCEEDED(context_.Initialize())); }

  ComPtr<ID3D12RootSignature> CreateSignature(D3D12TestContext &context) {
    D3D12_DESCRIPTOR_RANGE range = {};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    range.NumDescriptors = 1;
    D3D12_ROOT_PARAMETER parameters[2] = {};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[0].Constants.Num32BitValues = 1;
    parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[1].DescriptorTable.NumDescriptorRanges = 1;
    parameters[1].DescriptorTable.pDescriptorRanges = &range;
    parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_ROOT_SIGNATURE_DESC desc = {};
    desc.NumParameters = 2;
    desc.pParameters = parameters;
    return context.CreateRootSignature(desc);
  }

  ComPtr<ID3D12RootSignature> CreateSignature() {
    return CreateSignature(context_);
  }

  ComPtr<ID3D12RootSignature>
  CreateGraphicsSignature(D3D12TestContext &context) {
    D3D12_ROOT_PARAMETER parameter = {};
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameter.Constants.Num32BitValues = 4;
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC desc = {};
    desc.NumParameters = 1;
    desc.pParameters = &parameter;
    return context.CreateRootSignature(desc);
  }

  RootStateResources CreateResources() {
    RootStateResources resources;
    resources.signature = CreateSignature();
    if (!resources.signature)
      return resources;
    resources.pipeline = context_.CreateComputePipeline(
        resources.signature.get(), ClearBufferComputeShader());
    resources.output = context_.CreateBuffer(
        64 * sizeof(std::uint32_t), D3D12_HEAP_TYPE_DEFAULT,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    resources.heap = context_.CreateDescriptorHeap(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true);
    if (!resources.output || !resources.heap)
      return resources;

    D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
    uav.Format = DXGI_FORMAT_R32_UINT;
    uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uav.Buffer.NumElements = 64;
    context_.device()->CreateUnorderedAccessView(
        resources.output.get(), nullptr, &uav,
        resources.heap->GetCPUDescriptorHandleForHeapStart());
    return resources;
  }

  void BindComputeState(const RootStateResources &resources,
                        std::uint32_t value) {
    ID3D12DescriptorHeap *heaps[] = {resources.heap.get()};
    context_.list()->SetDescriptorHeaps(1, heaps);
    context_.list()->SetPipelineState(resources.pipeline.get());
    context_.list()->SetComputeRootSignature(resources.signature.get());
    context_.list()->SetComputeRoot32BitConstant(0, value, 0);
    context_.list()->SetComputeRootDescriptorTable(
        1, resources.heap->GetGPUDescriptorHandleForHeapStart());
  }

  void ExpectDispatchOutput(const RootStateResources &resources,
                            std::uint32_t expected) {
    context_.list()->Dispatch(1, 1, 1);
    D3D12TestContext::Transition(
        context_.list(), resources.output.get(),
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_SOURCE);
    std::vector<std::uint8_t> readback;
    ASSERT_TRUE(SUCCEEDED(context_.ReadbackBuffer(
        resources.output.get(), 64 * sizeof(expected), &readback)));
    ASSERT_EQ(readback.size(), 64 * sizeof(expected));
    for (std::size_t index = 0; index < 64; ++index) {
      std::uint32_t actual = 0;
      std::memcpy(&actual, readback.data() + index * sizeof(actual),
                  sizeof(actual));
      EXPECT_EQ(actual, expected) << "element " << index;
    }
  }

  D3D12TestContext context_;
};

TEST_F(RootSignatureRuntimeStateSpec,
       SameComputeRootSignaturePreservesComputeArguments) {
  auto resources = CreateResources();
  ASSERT_TRUE(resources.signature);
  ASSERT_TRUE(resources.pipeline);
  ASSERT_TRUE(resources.output);
  ASSERT_TRUE(resources.heap);
  constexpr std::uint32_t expected = 0x13579bdf;
  BindComputeState(resources, expected);

  context_.list()->SetComputeRootSignature(resources.signature.get());

  ExpectDispatchOutput(resources, expected);
}

TEST_F(RootSignatureRuntimeStateSpec,
       DifferentComputeRootSignatureAllowsAllArgumentsToBeRebound) {
  auto resources = CreateResources();
  auto replacement = CreateSignature();
  ASSERT_TRUE(resources.signature);
  ASSERT_TRUE(resources.pipeline);
  ASSERT_TRUE(resources.output);
  ASSERT_TRUE(resources.heap);
  ASSERT_TRUE(replacement);
  constexpr std::uint32_t expected = 0x2468ace0;
  BindComputeState(resources, expected);

  context_.list()->SetComputeRootSignature(replacement.get());
  context_.list()->SetComputeRoot32BitConstant(0, expected, 0);
  context_.list()->SetComputeRootDescriptorTable(
      1, resources.heap->GetGPUDescriptorHandleForHeapStart());

  ExpectDispatchOutput(resources, expected);
}

TEST_F(RootSignatureRuntimeStateSpec,
       DifferentComputeRootSignatureStalesComputeArguments) {
  auto resources = CreateResources();
  auto replacement = CreateSignature();
  ASSERT_TRUE(resources.signature);
  ASSERT_TRUE(resources.pipeline);
  ASSERT_TRUE(resources.output);
  ASSERT_TRUE(resources.heap);
  ASSERT_TRUE(replacement);
  ASSERT_NE(resources.signature.get(), replacement.get());

  // Clear the UAV so a stale dispatch cannot leave residual non-zero values.
  ID3D12DescriptorHeap *heaps[] = {resources.heap.get()};
  context_.list()->SetDescriptorHeaps(1, heaps);
  const UINT clear[4] = {};
  context_.list()->ClearUnorderedAccessViewUint(
      resources.heap->GetGPUDescriptorHandleForHeapStart(),
      resources.heap->GetCPUDescriptorHandleForHeapStart(),
      resources.output.get(), clear, 0, nullptr);
  D3D12TestContext::UavBarrier(context_.list(), resources.output.get());

  constexpr std::uint32_t sentinel = 0x2468ace0;
  BindComputeState(resources, sentinel);
  // A different root-signature object invalidates every previously set root
  // argument, including descriptor tables and 32-bit constants.
  context_.list()->SetComputeRootSignature(replacement.get());
  context_.list()->Dispatch(1, 1, 1);
  D3D12TestContext::Transition(
      context_.list(), resources.output.get(),
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_COPY_SOURCE);
  std::vector<std::uint8_t> readback;
  ASSERT_TRUE(SUCCEEDED(context_.ReadbackBuffer(
      resources.output.get(), 64 * sizeof(std::uint32_t), &readback)));
  ASSERT_EQ(readback.size(), 64 * sizeof(std::uint32_t));
  for (std::size_t index = 0; index < 64; ++index) {
    std::uint32_t actual = 0;
    std::memcpy(&actual, readback.data() + index * sizeof(actual),
                sizeof(actual));
    EXPECT_EQ(actual, 0u) << "element " << index;
  }
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(RootSignatureRuntimeStateSpec,
       GraphicsRootChangeDoesNotAffectComputeRootState) {
  auto resources = CreateResources();
  auto graphics_signature = CreateSignature();
  ASSERT_TRUE(resources.signature);
  ASSERT_TRUE(resources.pipeline);
  ASSERT_TRUE(resources.output);
  ASSERT_TRUE(resources.heap);
  ASSERT_TRUE(graphics_signature);
  constexpr std::uint32_t expected = 0x89abcdef;
  BindComputeState(resources, expected);

  context_.list()->SetGraphicsRootSignature(graphics_signature.get());

  ExpectDispatchOutput(resources, expected);
}

TEST_F(RootSignatureRuntimeStateSpec,
       ForeignPipelineUpdatesPreserveBoundComputeState) {
  auto foreign_device = CreateIsolatedD3D12Device();
  ASSERT_TRUE(foreign_device);
  D3D12TestContext foreign_context;
  ASSERT_EQ(foreign_context.Initialize(foreign_device.get()), S_OK);
  auto foreign_signature = CreateSignature(foreign_context);
  ASSERT_TRUE(foreign_signature);
  auto foreign_pipeline = foreign_context.CreateComputePipeline(
      foreign_signature.get(), ClearBufferComputeShader());
  ASSERT_TRUE(foreign_pipeline);

  for (const bool clear_state : {false, true}) {
    SCOPED_TRACE(clear_state ? "ClearState" : "SetPipelineState");
    auto resources = CreateResources();
    ASSERT_TRUE(resources.signature);
    ASSERT_TRUE(resources.pipeline);
    ASSERT_TRUE(resources.output);
    ASSERT_TRUE(resources.heap);
    constexpr std::uint32_t expected = 0xc001d00d;
    BindComputeState(resources, expected);

    if (clear_state)
      context_.list()->ClearState(foreign_pipeline.get());
    else
      context_.list()->SetPipelineState(foreign_pipeline.get());

    ExpectDispatchOutput(resources, expected);
    ASSERT_EQ(context_.ResetCommandList(), S_OK);
  }
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(RootSignatureRuntimeStateSpec,
       ForeignComputeRootSignaturePreservesArguments) {
  auto resources = CreateResources();
  auto foreign_device = CreateIsolatedD3D12Device();
  ASSERT_TRUE(resources.signature);
  ASSERT_TRUE(resources.pipeline);
  ASSERT_TRUE(resources.output);
  ASSERT_TRUE(resources.heap);
  ASSERT_TRUE(foreign_device);
  D3D12TestContext foreign_context;
  ASSERT_EQ(foreign_context.Initialize(foreign_device.get()), S_OK);
  auto foreign_signature = CreateSignature(foreign_context);
  ASSERT_TRUE(foreign_signature);
  constexpr std::uint32_t expected = 0xf00dcafe;
  BindComputeState(resources, expected);

  context_.list()->SetComputeRootSignature(foreign_signature.get());

  ExpectDispatchOutput(resources, expected);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(RootSignatureRuntimeStateSpec,
       ForeignGraphicsRootSignaturePreservesArguments) {
  auto local_signature = CreateGraphicsSignature(context_);
  auto foreign_device = CreateIsolatedD3D12Device();
  ASSERT_TRUE(local_signature);
  ASSERT_TRUE(foreign_device);
  D3D12TestContext foreign_context;
  ASSERT_EQ(foreign_context.Initialize(foreign_device.get()), S_OK);
  auto foreign_signature = CreateGraphicsSignature(foreign_context);
  ASSERT_TRUE(foreign_signature);

  const auto pixel = CompileShader(R"(
    cbuffer Color : register(b0) { float4 color; };
    float4 main() : SV_Target { return color; }
  )", "ps_5_0");
  ASSERT_EQ(pixel.result, S_OK) << pixel.diagnostic_text();
  const D3D12_SHADER_BYTECODE bytecode = {pixel.bytecode->GetBufferPointer(),
                                          pixel.bytecode->GetBufferSize()};
  auto pipeline = context_.CreateGraphicsPipeline(
      local_signature.get(), DXGI_FORMAT_R8G8B8A8_UNORM, bytecode);
  auto target = context_.CreateTexture2D(
      1, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
      D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
      D3D12_RESOURCE_STATE_RENDER_TARGET);
  auto heap = context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1,
                                            false);
  ASSERT_TRUE(pipeline);
  ASSERT_TRUE(target);
  ASSERT_TRUE(heap);
  const auto rtv = heap->GetCPUDescriptorHandleForHeapStart();
  context_.device()->CreateRenderTargetView(target.get(), nullptr, rtv);
  const D3D12_VIEWPORT viewport = {0, 0, 1, 1, 0, 1};
  const D3D12_RECT scissor = {0, 0, 1, 1};
  constexpr std::array<float, 4> color = {1.0f, 0.0f, 0.0f, 1.0f};
  context_.list()->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
  context_.list()->SetPipelineState(pipeline.get());
  context_.list()->SetGraphicsRootSignature(local_signature.get());
  context_.list()->SetGraphicsRoot32BitConstants(
      0, static_cast<UINT>(color.size()), color.data(), 0);
  context_.list()->SetGraphicsRootSignature(foreign_signature.get());
  context_.list()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context_.list()->RSSetViewports(1, &viewport);
  context_.list()->RSSetScissorRects(1, &scissor);
  context_.list()->DrawInstanced(3, 1, 0, 0);
  D3D12TestContext::Transition(
      context_.list(), target.get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
      D3D12_RESOURCE_STATE_COPY_SOURCE);

  TextureReadback readback;
  ASSERT_EQ(context_.ReadbackTexture(target.get(), &readback), S_OK);
  ASSERT_GE(readback.data.size(), sizeof(std::uint32_t));
  std::uint32_t actual = 0;
  std::memcpy(&actual, readback.data.data(), sizeof(actual));
  EXPECT_TRUE(ColorsMatch(actual, 0xff0000ffu, 1));
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(RootSignatureRuntimeStateSpec,
       DifferentGraphicsRootSignatureStalesGraphicsArguments) {
  auto local_signature = CreateGraphicsSignature(context_);
  auto replacement = CreateGraphicsSignature(context_);
  ASSERT_TRUE(local_signature);
  ASSERT_TRUE(replacement);
  ASSERT_NE(local_signature.get(), replacement.get());

  const auto pixel = CompileShader(R"(
    cbuffer Color : register(b0) { float4 color; };
    float4 main() : SV_Target { return color; }
  )", "ps_5_0");
  ASSERT_EQ(pixel.result, S_OK) << pixel.diagnostic_text();
  const D3D12_SHADER_BYTECODE bytecode = {pixel.bytecode->GetBufferPointer(),
                                          pixel.bytecode->GetBufferSize()};
  auto pipeline = context_.CreateGraphicsPipeline(
      local_signature.get(), DXGI_FORMAT_R8G8B8A8_UNORM, bytecode);
  auto target = context_.CreateTexture2D(
      1, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
      D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
      D3D12_RESOURCE_STATE_RENDER_TARGET);
  auto heap = context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1,
                                            false);
  ASSERT_TRUE(pipeline);
  ASSERT_TRUE(target);
  ASSERT_TRUE(heap);
  const auto rtv = heap->GetCPUDescriptorHandleForHeapStart();
  context_.device()->CreateRenderTargetView(target.get(), nullptr, rtv);
  const D3D12_VIEWPORT viewport = {0, 0, 1, 1, 0, 1};
  const D3D12_RECT scissor = {0, 0, 1, 1};
  constexpr std::array<float, 4> clear_color = {0.0f, 0.0f, 0.0f, 1.0f};
  constexpr std::array<float, 4> draw_color = {1.0f, 0.0f, 0.0f, 1.0f};
  context_.list()->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
  context_.list()->ClearRenderTargetView(rtv, clear_color.data(), 0, nullptr);
  context_.list()->SetPipelineState(pipeline.get());
  context_.list()->SetGraphicsRootSignature(local_signature.get());
  context_.list()->SetGraphicsRoot32BitConstants(
      0, static_cast<UINT>(draw_color.size()), draw_color.data(), 0);
  // Same-device, different root-signature object: root arguments go stale.
  context_.list()->SetGraphicsRootSignature(replacement.get());
  context_.list()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context_.list()->RSSetViewports(1, &viewport);
  context_.list()->RSSetScissorRects(1, &scissor);
  context_.list()->DrawInstanced(3, 1, 0, 0);
  D3D12TestContext::Transition(
      context_.list(), target.get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
      D3D12_RESOURCE_STATE_COPY_SOURCE);

  TextureReadback readback;
  ASSERT_EQ(context_.ReadbackTexture(target.get(), &readback), S_OK);
  ASSERT_GE(readback.data.size(), sizeof(std::uint32_t));
  std::uint32_t actual = 0;
  std::memcpy(&actual, readback.data.data(), sizeof(actual));
  // Stale constants must not produce the previously bound red color.
  EXPECT_FALSE(ColorsMatch(actual, 0xff0000ffu, 1));
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
