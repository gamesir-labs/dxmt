#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"
#include "shaders/runtime_test_shaders.hpp"

#include <array>
#include <bit>
#include <cstring>
#include <optional>
#include <vector>

namespace {

using dxmt::test::ColorsMatch;
using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;
using dxmt::test::TextureReadback;

enum class GraphicsResetState {
  PipelineState,
  RootSignature,
  RootArguments,
  VertexBuffers,
  IndexBuffer,
  PrimitiveTopology,
  RenderTargets,
  Viewports,
  Scissors,
  BlendFactor,
  StencilRef,
  Predication,
  ActiveQuery,
  RenderPass,
};

class GraphicsResetStateSpec
    : public ::testing::TestWithParam<GraphicsResetState> {
protected:
  void SetUp() override {
    ASSERT_TRUE(SUCCEEDED(context_.Initialize()));

    auto vertex_shader = CompileShader(R"(
      struct Input { float2 position : POSITION; };
      float4 main(Input input) : SV_Position {
        return float4(input.position, 0.0, 1.0);
      })",
                                       "vs_5_0");
    ASSERT_TRUE(SUCCEEDED(vertex_shader.result))
        << vertex_shader.diagnostic_text();
    vertex_shader_ = std::move(vertex_shader.bytecode);
    auto pixel_shader = CompileShader(R"(
      cbuffer Color : register(b0) { float value; };
      float4 main() : SV_Target { return value.xxxx; })",
                                      "ps_5_0");
    ASSERT_TRUE(SUCCEEDED(pixel_shader.result))
        << pixel_shader.diagnostic_text();
    pixel_shader_ = std::move(pixel_shader.bytecode);

    D3D12_ROOT_PARAMETER parameter = {};
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameter.Constants.ShaderRegister = 0;
    parameter.Constants.Num32BitValues = 1;
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC root_desc = {};
    root_desc.NumParameters = 1;
    root_desc.pParameters = &parameter;
    root_desc.Flags =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    root_signature_ = context_.CreateRootSignature(root_desc);
    ASSERT_TRUE(root_signature_);

    D3D12_INPUT_ELEMENT_DESC input = {};
    input.SemanticName = "POSITION";
    input.Format = DXGI_FORMAT_R32G32_FLOAT;
    input.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline_desc = {};
    pipeline_desc.pRootSignature = root_signature_.get();
    pipeline_desc.VS = {vertex_shader_->GetBufferPointer(),
                        vertex_shader_->GetBufferSize()};
    pipeline_desc.PS = {pixel_shader_->GetBufferPointer(),
                        pixel_shader_->GetBufferSize()};
    pipeline_desc.BlendState.RenderTarget[0].BlendEnable = TRUE;
    pipeline_desc.BlendState.RenderTarget[0].SrcBlend =
        D3D12_BLEND_BLEND_FACTOR;
    pipeline_desc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
    pipeline_desc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    pipeline_desc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    pipeline_desc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
    pipeline_desc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    pipeline_desc.BlendState.RenderTarget[0].RenderTargetWriteMask =
        D3D12_COLOR_WRITE_ENABLE_ALL;
    pipeline_desc.SampleMask = UINT_MAX;
    pipeline_desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pipeline_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pipeline_desc.RasterizerState.DepthClipEnable = TRUE;
    pipeline_desc.DepthStencilState.DepthEnable = FALSE;
    pipeline_desc.DepthStencilState.StencilEnable = TRUE;
    pipeline_desc.DepthStencilState.StencilReadMask =
        D3D12_DEFAULT_STENCIL_READ_MASK;
    pipeline_desc.DepthStencilState.StencilWriteMask =
        D3D12_DEFAULT_STENCIL_WRITE_MASK;
    pipeline_desc.DepthStencilState.FrontFace.StencilFailOp =
        D3D12_STENCIL_OP_KEEP;
    pipeline_desc.DepthStencilState.FrontFace.StencilDepthFailOp =
        D3D12_STENCIL_OP_KEEP;
    pipeline_desc.DepthStencilState.FrontFace.StencilPassOp =
        D3D12_STENCIL_OP_KEEP;
    pipeline_desc.DepthStencilState.FrontFace.StencilFunc =
        D3D12_COMPARISON_FUNC_EQUAL;
    pipeline_desc.DepthStencilState.BackFace =
        pipeline_desc.DepthStencilState.FrontFace;
    pipeline_desc.InputLayout = {&input, 1};
    pipeline_desc.PrimitiveTopologyType =
        D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pipeline_desc.NumRenderTargets = 1;
    pipeline_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pipeline_desc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    pipeline_desc.SampleDesc.Count = 1;
    ASSERT_TRUE(SUCCEEDED(context_.device()->CreateGraphicsPipelineState(
        &pipeline_desc, IID_PPV_ARGS(pipeline_.put()))));

    render_target_ =
        context_.CreateTexture2D(kSize, kSize, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
                                 D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
                                 D3D12_RESOURCE_STATE_RENDER_TARGET);
    depth_stencil_ =
        context_.CreateTexture2D(kSize, kSize, 1, DXGI_FORMAT_D24_UNORM_S8_UINT,
                                 D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL,
                                 D3D12_RESOURCE_STATE_DEPTH_WRITE);
    rtv_heap_ =
        context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
    dsv_heap_ =
        context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false);
    ASSERT_TRUE(render_target_);
    ASSERT_TRUE(depth_stencil_);
    ASSERT_TRUE(rtv_heap_);
    ASSERT_TRUE(dsv_heap_);
    rtv_ = rtv_heap_->GetCPUDescriptorHandleForHeapStart();
    dsv_ = dsv_heap_->GetCPUDescriptorHandleForHeapStart();
    context_.device()->CreateRenderTargetView(render_target_.get(), nullptr,
                                              rtv_);
    context_.device()->CreateDepthStencilView(depth_stencil_.get(), nullptr,
                                              dsv_);

    constexpr std::array<float, 6> vertices = {-1.0f, -1.0f, -1.0f,
                                               3.0f,  3.0f,  -1.0f};
    constexpr std::array<std::uint16_t, 3> indices = {0, 1, 2};
    vertex_buffer_ = context_.CreateUploadBuffer(
        sizeof(vertices), vertices.data(), sizeof(vertices));
    index_buffer_ = context_.CreateUploadBuffer(sizeof(indices), indices.data(),
                                                sizeof(indices));
    const UINT64 predicate = 1;
    predicate_ = context_.CreateUploadBuffer(sizeof(predicate), &predicate,
                                             sizeof(predicate));
    ASSERT_TRUE(vertex_buffer_);
    ASSERT_TRUE(index_buffer_);
    ASSERT_TRUE(predicate_);
    vertex_view_ = {vertex_buffer_->GetGPUVirtualAddress(), sizeof(vertices),
                    2 * sizeof(float)};
    index_view_ = {index_buffer_->GetGPUVirtualAddress(), sizeof(indices),
                   DXGI_FORMAT_R16_UINT};

    D3D12_QUERY_HEAP_DESC query_desc = {};
    query_desc.Type = D3D12_QUERY_HEAP_TYPE_OCCLUSION;
    query_desc.Count = 1;
    ASSERT_TRUE(SUCCEEDED(context_.device()->CreateQueryHeap(
        &query_desc, IID_PPV_ARGS(query_heap_.put()))));
    query_result_ = context_.CreateBuffer(
        sizeof(UINT64), D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE,
        D3D12_RESOURCE_STATE_COPY_DEST);
    ASSERT_TRUE(query_result_);
    ASSERT_TRUE(
        SUCCEEDED(context_.list()->QueryInterface(IID_PPV_ARGS(list4_.put()))));

    viewport_ = {
        0.0f, 0.0f, static_cast<float>(kSize), static_cast<float>(kSize),
        0.0f, 1.0f};
    scissor_ = {0, 0, kSize, kSize};
  }

  void BindGraphicsState(std::optional<GraphicsResetState> omitted,
                         bool priming) {
    auto *list = context_.list();
    if (omitted != GraphicsResetState::PipelineState)
      list->SetPipelineState(pipeline_.get());
    if (omitted != GraphicsResetState::RootSignature)
      list->SetGraphicsRootSignature(root_signature_.get());
    if (omitted != GraphicsResetState::RootArguments)
      list->SetGraphicsRoot32BitConstant(0, std::bit_cast<UINT>(1.0f), 0);
    if (omitted != GraphicsResetState::VertexBuffers)
      list->IASetVertexBuffers(0, 1, &vertex_view_);
    if (omitted != GraphicsResetState::IndexBuffer)
      list->IASetIndexBuffer(&index_view_);
    if (omitted != GraphicsResetState::PrimitiveTopology)
      list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    if (omitted != GraphicsResetState::RenderTargets &&
        omitted != GraphicsResetState::RenderPass)
      list->OMSetRenderTargets(1, &rtv_, FALSE, &dsv_);
    if (omitted != GraphicsResetState::Viewports)
      list->RSSetViewports(1, &viewport_);
    if (omitted != GraphicsResetState::Scissors) {
      const D3D12_RECT rect = priming ? D3D12_RECT{} : scissor_;
      list->RSSetScissorRects(1, &rect);
    }
    if (omitted != GraphicsResetState::BlendFactor) {
      const FLOAT factor = priming ? 0.0f : 1.0f;
      const FLOAT factors[4] = {factor, factor, factor, factor};
      list->OMSetBlendFactor(factors);
    }
    if (omitted != GraphicsResetState::StencilRef)
      list->OMSetStencilRef(priming ? 1 : 0);
    if (omitted != GraphicsResetState::Predication) {
      list->SetPredication(priming ? predicate_.get() : nullptr, 0,
                           D3D12_PREDICATION_OP_EQUAL_ZERO);
    }
  }

  void BeginRenderPass() {
    D3D12_RENDER_PASS_RENDER_TARGET_DESC target = {};
    target.cpuDescriptor = rtv_;
    target.BeginningAccess.Type =
        D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_PRESERVE;
    target.EndingAccess.Type = D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_PRESERVE;
    D3D12_RENDER_PASS_DEPTH_STENCIL_DESC depth = {};
    depth.cpuDescriptor = dsv_;
    depth.DepthBeginningAccess.Type =
        D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_PRESERVE;
    depth.DepthEndingAccess.Type =
        D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_PRESERVE;
    depth.StencilBeginningAccess.Type =
        D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_PRESERVE;
    depth.StencilEndingAccess.Type =
        D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_PRESERVE;
    list4_->BeginRenderPass(1, &target, &depth, D3D12_RENDER_PASS_FLAG_NONE);
  }

  void PrimeAndReset(GraphicsResetState state) {
    BindGraphicsState(std::nullopt, true);
    if (state == GraphicsResetState::ActiveQuery)
      context_.list()->BeginQuery(query_heap_.get(), D3D12_QUERY_TYPE_OCCLUSION,
                                  0);
    if (state == GraphicsResetState::RenderPass)
      BeginRenderPass();
    ASSERT_EQ(context_.list()->Close(), S_OK);
    ASSERT_EQ(context_.list()->Reset(context_.allocator(), nullptr), S_OK);
  }

  void ExpectResetState(GraphicsResetState state) {
    PrimeAndReset(state);
    const FLOAT clear[4] = {};
    context_.list()->ClearRenderTargetView(rtv_, clear, 0, nullptr);
    context_.list()->ClearDepthStencilView(dsv_, D3D12_CLEAR_FLAG_STENCIL, 1.0f,
                                           0, 0, nullptr);
    BindGraphicsState(state, false);
    if (state == GraphicsResetState::ActiveQuery)
      context_.list()->BeginQuery(query_heap_.get(), D3D12_QUERY_TYPE_OCCLUSION,
                                  0);
    if (state == GraphicsResetState::RenderPass)
      BeginRenderPass();

    if (state == GraphicsResetState::IndexBuffer)
      context_.list()->DrawIndexedInstanced(3, 1, 0, 0, 0);
    else
      context_.list()->DrawInstanced(3, 1, 0, 0);

    if (state == GraphicsResetState::ActiveQuery) {
      context_.list()->EndQuery(query_heap_.get(), D3D12_QUERY_TYPE_OCCLUSION,
                                0);
      context_.list()->ResolveQueryData(query_heap_.get(),
                                        D3D12_QUERY_TYPE_OCCLUSION, 0, 1,
                                        query_result_.get(), 0);
    }
    if (state == GraphicsResetState::RenderPass)
      list4_->EndRenderPass();
    D3D12TestContext::Transition(context_.list(), render_target_.get(),
                                 D3D12_RESOURCE_STATE_RENDER_TARGET,
                                 D3D12_RESOURCE_STATE_COPY_SOURCE);

    TextureReadback readback;
    ASSERT_TRUE(
        SUCCEEDED(context_.ReadbackTexture(render_target_.get(), &readback)));
    const bool should_draw = state == GraphicsResetState::Scissors ||
                             state == GraphicsResetState::BlendFactor ||
                             state == GraphicsResetState::StencilRef ||
                             state == GraphicsResetState::Predication ||
                             state == GraphicsResetState::ActiveQuery ||
                             state == GraphicsResetState::RenderPass;
    const std::uint32_t expected = should_draw ? 0xffffffffu : 0u;
    for (UINT y = 0; y < kSize; ++y) {
      for (UINT x = 0; x < kSize; ++x) {
        std::uint32_t actual = 0;
        std::memcpy(&actual,
                    readback.data.data() + y * readback.row_pitch +
                        x * sizeof(actual),
                    sizeof(actual));
        EXPECT_TRUE(ColorsMatch(actual, expected, 1))
            << "pixel (" << x << ", " << y << ") actual=0x" << std::hex
            << actual << " expected=0x" << expected;
      }
    }

    if (state == GraphicsResetState::ActiveQuery) {
      void *mapping = nullptr;
      D3D12_RANGE range = {0, sizeof(UINT64)};
      ASSERT_TRUE(SUCCEEDED(query_result_->Map(0, &range, &mapping)));
      EXPECT_GT(*static_cast<const UINT64 *>(mapping), 0u);
      const D3D12_RANGE written = {0, 0};
      query_result_->Unmap(0, &written);
    }
  }

public:
  static const char *
  Name(const ::testing::TestParamInfo<GraphicsResetState> &info) {
    switch (info.param) {
    case GraphicsResetState::PipelineState:
      return "PipelineState";
    case GraphicsResetState::RootSignature:
      return "GraphicsRootSignature";
    case GraphicsResetState::RootArguments:
      return "GraphicsRootArguments";
    case GraphicsResetState::VertexBuffers:
      return "VertexBuffers";
    case GraphicsResetState::IndexBuffer:
      return "IndexBuffer";
    case GraphicsResetState::PrimitiveTopology:
      return "PrimitiveTopology";
    case GraphicsResetState::RenderTargets:
      return "RenderTargets";
    case GraphicsResetState::Viewports:
      return "Viewports";
    case GraphicsResetState::Scissors:
      return "Scissors";
    case GraphicsResetState::BlendFactor:
      return "BlendFactor";
    case GraphicsResetState::StencilRef:
      return "StencilRef";
    case GraphicsResetState::Predication:
      return "Predication";
    case GraphicsResetState::ActiveQuery:
      return "ActiveQueryState";
    case GraphicsResetState::RenderPass:
      return "RenderPassState";
    }
    return "Unknown";
  }

protected:
  static constexpr UINT kSize = 8;
  D3D12TestContext context_;
  ComPtr<ID3DBlob> vertex_shader_;
  ComPtr<ID3DBlob> pixel_shader_;
  ComPtr<ID3D12RootSignature> root_signature_;
  ComPtr<ID3D12PipelineState> pipeline_;
  ComPtr<ID3D12Resource> render_target_;
  ComPtr<ID3D12Resource> depth_stencil_;
  ComPtr<ID3D12DescriptorHeap> rtv_heap_;
  ComPtr<ID3D12DescriptorHeap> dsv_heap_;
  ComPtr<ID3D12Resource> vertex_buffer_;
  ComPtr<ID3D12Resource> index_buffer_;
  ComPtr<ID3D12Resource> predicate_;
  ComPtr<ID3D12QueryHeap> query_heap_;
  ComPtr<ID3D12Resource> query_result_;
  ComPtr<ID3D12GraphicsCommandList4> list4_;
  D3D12_CPU_DESCRIPTOR_HANDLE rtv_ = {};
  D3D12_CPU_DESCRIPTOR_HANDLE dsv_ = {};
  D3D12_VERTEX_BUFFER_VIEW vertex_view_ = {};
  D3D12_INDEX_BUFFER_VIEW index_view_ = {};
  D3D12_VIEWPORT viewport_ = {};
  D3D12_RECT scissor_ = {};
};

TEST_P(GraphicsResetStateSpec, ResetClearsBoundState) {
  ExpectResetState(GetParam());
}

INSTANTIATE_TEST_SUITE_P(
    GraphicsState, GraphicsResetStateSpec,
    ::testing::Values(
        GraphicsResetState::PipelineState, GraphicsResetState::RootSignature,
        GraphicsResetState::RootArguments, GraphicsResetState::VertexBuffers,
        GraphicsResetState::IndexBuffer, GraphicsResetState::PrimitiveTopology,
        GraphicsResetState::RenderTargets, GraphicsResetState::Viewports,
        GraphicsResetState::Scissors, GraphicsResetState::BlendFactor,
        GraphicsResetState::StencilRef, GraphicsResetState::Predication,
        GraphicsResetState::ActiveQuery, GraphicsResetState::RenderPass),
    GraphicsResetStateSpec::Name);

enum class ComputeResetState {
  RootSignature,
  RootArguments,
  DescriptorHeaps,
};

class ComputeResetStateSpec
    : public ::testing::TestWithParam<ComputeResetState> {
protected:
  void SetUp() override {
    ASSERT_TRUE(SUCCEEDED(context_.Initialize()));
    D3D12_DESCRIPTOR_RANGE range = {};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    range.NumDescriptors = 1;
    D3D12_ROOT_PARAMETER parameters[2] = {};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[0].Constants.ShaderRegister = 0;
    parameters[0].Constants.Num32BitValues = 1;
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[1].DescriptorTable.NumDescriptorRanges = 1;
    parameters[1].DescriptorTable.pDescriptorRanges = &range;
    D3D12_ROOT_SIGNATURE_DESC desc = {};
    desc.NumParameters = 2;
    desc.pParameters = parameters;
    root_signature_ = context_.CreateRootSignature(desc);
    pipeline_ = context_.CreateComputePipeline(
        root_signature_.get(), dxmt::test::ClearBufferComputeShader());
    output_ = context_.CreateBuffer(64 * sizeof(UINT), D3D12_HEAP_TYPE_DEFAULT,
                                    D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    heap_ = context_.CreateDescriptorHeap(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true);
    ASSERT_TRUE(root_signature_);
    ASSERT_TRUE(pipeline_);
    ASSERT_TRUE(output_);
    ASSERT_TRUE(heap_);
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
    uav.Format = DXGI_FORMAT_R32_UINT;
    uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uav.Buffer.NumElements = 64;
    context_.device()->CreateUnorderedAccessView(
        output_.get(), nullptr, &uav,
        heap_->GetCPUDescriptorHandleForHeapStart());

    ID3D12DescriptorHeap *heaps[] = {heap_.get()};
    context_.list()->SetDescriptorHeaps(1, heaps);
    const UINT clear[4] = {};
    context_.list()->ClearUnorderedAccessViewUint(
        heap_->GetGPUDescriptorHandleForHeapStart(),
        heap_->GetCPUDescriptorHandleForHeapStart(), output_.get(), clear, 0,
        nullptr);
    ASSERT_TRUE(SUCCEEDED(context_.ExecuteAndWait()));
    ASSERT_TRUE(SUCCEEDED(context_.ResetCommandList()));
  }

  void Bind(std::optional<ComputeResetState> omitted) {
    if (omitted != ComputeResetState::DescriptorHeaps) {
      ID3D12DescriptorHeap *heaps[] = {heap_.get()};
      context_.list()->SetDescriptorHeaps(1, heaps);
    }
    context_.list()->SetPipelineState(pipeline_.get());
    if (omitted != ComputeResetState::RootSignature)
      context_.list()->SetComputeRootSignature(root_signature_.get());
    if (omitted != ComputeResetState::RootArguments) {
      context_.list()->SetComputeRoot32BitConstant(0, 0x12345678, 0);
      context_.list()->SetComputeRootDescriptorTable(
          1, heap_->GetGPUDescriptorHandleForHeapStart());
    }
  }

public:
  static const char *
  Name(const ::testing::TestParamInfo<ComputeResetState> &info) {
    switch (info.param) {
    case ComputeResetState::RootSignature:
      return "ComputeRootSignature";
    case ComputeResetState::RootArguments:
      return "ComputeRootArguments";
    case ComputeResetState::DescriptorHeaps:
      return "DescriptorHeaps";
    }
    return "Unknown";
  }

protected:
  D3D12TestContext context_;
  ComPtr<ID3D12RootSignature> root_signature_;
  ComPtr<ID3D12PipelineState> pipeline_;
  ComPtr<ID3D12Resource> output_;
  ComPtr<ID3D12DescriptorHeap> heap_;
};

TEST_P(ComputeResetStateSpec, ResetClearsBoundState) {
  Bind(std::nullopt);
  ASSERT_EQ(context_.list()->Close(), S_OK);
  ASSERT_EQ(context_.list()->Reset(context_.allocator(), nullptr), S_OK);

  Bind(GetParam());
  context_.list()->Dispatch(1, 1, 1);
  D3D12TestContext::Transition(context_.list(), output_.get(),
                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
  std::vector<std::uint8_t> bytes;
  ASSERT_TRUE(SUCCEEDED(
      context_.ReadbackBuffer(output_.get(), 64 * sizeof(UINT), &bytes)));
  ASSERT_EQ(bytes.size(), 64 * sizeof(UINT));
  for (std::size_t offset = 0; offset < bytes.size(); offset += sizeof(UINT)) {
    UINT value = 0;
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    EXPECT_EQ(value, 0u) << "element " << offset / sizeof(UINT);
  }
}

INSTANTIATE_TEST_SUITE_P(ComputeState, ComputeResetStateSpec,
                         ::testing::Values(ComputeResetState::RootSignature,
                                           ComputeResetState::RootArguments,
                                           ComputeResetState::DescriptorHeaps),
                         ComputeResetStateSpec::Name);

} // namespace
