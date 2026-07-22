#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d11_test_context.hpp"

#include <array>
#include <cstdint>
#include <iomanip>
#include <vector>

// Public-D3D11 ClearState coverage. Twelve independently bindable state
// categories form every one of the 2^12 = 4096 pipeline subsets.

namespace {

using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

constexpr std::uint32_t kClearStateSubsetCaseCount = 4096;

const dxmt::test::LogicalCaseFamilyRegistration kClearStateSubsetCases(
    "D3D11ClearStateSubsetMatrixSpec."
    "RestoresDefaultsFrom4096PipelineSubsets",
    "D3D11.ClearState.Subset.", kClearStateSubsetCaseCount, 4,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "5_0", "Immediate",
      "ClearState,InputAssemblerState,ShaderState,ShaderResourceState,"
      "OutputMergerState,RasterizerState,StateGetters,ComReferenceState"},
     dxmt::test::kResourceTestCost,
     "test-local input layout, buffers, vertex shader, shader-resource view, "
     "sampler, unordered-access view, blend, depth-stencil and rasterizer "
     "state objects",
     "bind every subset of twelve independently selected pipeline-state "
     "categories, prove the pre-clear subset through public getters, call "
     "ClearState, then capture all categories again",
     "the pre-clear snapshot exactly matches the selected subset and every "
     "post-clear object and resource slot is null with documented default "
     "dynamic state and undefined topology",
     "logical ID, selected-case count, expected and observed pre-clear masks, "
     "post-clear mask, dynamic getter values, failing phase, and exact replay "
     "argument"});

const dxmt::test::TestCostRegistration
    kClearStateSubsetCost("D3D11ClearStateSubsetMatrixSpec."
                          "RestoresDefaultsFrom4096PipelineSubsets",
                          dxmt::test::kResourceTestCost);

enum StateBit : std::uint32_t {
  kInputLayoutBit = 1u << 0,
  kVertexBufferBit = 1u << 1,
  kIndexBufferBit = 1u << 2,
  kTopologyBit = 1u << 3,
  kVertexShaderBit = 1u << 4,
  kConstantBufferBit = 1u << 5,
  kShaderResourceBit = 1u << 6,
  kSamplerBit = 1u << 7,
  kUnorderedAccessBit = 1u << 8,
  kBlendStateBit = 1u << 9,
  kDepthStencilStateBit = 1u << 10,
  kRasterizerStateBit = 1u << 11,
};

struct PipelineObjects {
  ComPtr<ID3D11InputLayout> input_layout;
  ComPtr<ID3D11Buffer> vertex_buffer;
  ComPtr<ID3D11Buffer> index_buffer;
  ComPtr<ID3D11VertexShader> vertex_shader;
  ComPtr<ID3D11Buffer> constant_buffer;
  ComPtr<ID3D11ShaderResourceView> shader_resource;
  ComPtr<ID3D11SamplerState> sampler;
  ComPtr<ID3D11UnorderedAccessView> unordered_access;
  ComPtr<ID3D11BlendState> blend_state;
  ComPtr<ID3D11DepthStencilState> depth_stencil_state;
  ComPtr<ID3D11RasterizerState> rasterizer_state;
};

struct PipelineSnapshot {
  ComPtr<ID3D11InputLayout> input_layout;
  ComPtr<ID3D11Buffer> vertex_buffer;
  UINT vertex_stride = 0;
  UINT vertex_offset = 0;
  ComPtr<ID3D11Buffer> index_buffer;
  DXGI_FORMAT index_format = DXGI_FORMAT_UNKNOWN;
  UINT index_offset = 0;
  D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
  ComPtr<ID3D11VertexShader> vertex_shader;
  ComPtr<ID3D11Buffer> constant_buffer;
  ComPtr<ID3D11ShaderResourceView> shader_resource;
  ComPtr<ID3D11SamplerState> sampler;
  ComPtr<ID3D11UnorderedAccessView> unordered_access;
  ComPtr<ID3D11BlendState> blend_state;
  std::array<FLOAT, 4> blend_factor = {};
  UINT sample_mask = 0;
  ComPtr<ID3D11DepthStencilState> depth_stencil_state;
  UINT stencil_ref = 0;
  ComPtr<ID3D11RasterizerState> rasterizer_state;
};

constexpr UINT kVertexStride = 16;
constexpr UINT kVertexOffset = 4;
constexpr UINT kIndexOffset = 2;
constexpr D3D11_PRIMITIVE_TOPOLOGY kBoundTopology =
    D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
constexpr std::array<FLOAT, 4> kBlendFactor = {0.25f, 0.5f, 0.75f, 1.0f};
constexpr UINT kSampleMask = 0x13579bdfu;
constexpr UINT kStencilRef = 0x5au;
constexpr std::array<FLOAT, 4> kDefaultBlendFactor = {1.0f, 1.0f, 1.0f, 1.0f};

PipelineSnapshot CapturePipelineState(ID3D11DeviceContext *context) {
  PipelineSnapshot snapshot;
  context->IAGetInputLayout(snapshot.input_layout.put());
  context->IAGetVertexBuffers(0, 1, snapshot.vertex_buffer.put(),
                              &snapshot.vertex_stride, &snapshot.vertex_offset);
  context->IAGetIndexBuffer(snapshot.index_buffer.put(), &snapshot.index_format,
                            &snapshot.index_offset);
  context->IAGetPrimitiveTopology(&snapshot.topology);
  context->VSGetShader(snapshot.vertex_shader.put(), nullptr, nullptr);
  context->VSGetConstantBuffers(0, 1, snapshot.constant_buffer.put());
  context->PSGetShaderResources(0, 1, snapshot.shader_resource.put());
  context->PSGetSamplers(0, 1, snapshot.sampler.put());
  context->CSGetUnorderedAccessViews(0, 1, snapshot.unordered_access.put());
  context->OMGetBlendState(snapshot.blend_state.put(),
                           snapshot.blend_factor.data(), &snapshot.sample_mask);
  context->OMGetDepthStencilState(snapshot.depth_stencil_state.put(),
                                  &snapshot.stencil_ref);
  context->RSGetState(snapshot.rasterizer_state.put());
  return snapshot;
}

std::uint32_t ObservedObjectMask(const PipelineSnapshot &snapshot) {
  std::uint32_t mask = 0;
  if (snapshot.input_layout)
    mask |= kInputLayoutBit;
  if (snapshot.vertex_buffer)
    mask |= kVertexBufferBit;
  if (snapshot.index_buffer)
    mask |= kIndexBufferBit;
  if (snapshot.topology != D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED)
    mask |= kTopologyBit;
  if (snapshot.vertex_shader)
    mask |= kVertexShaderBit;
  if (snapshot.constant_buffer)
    mask |= kConstantBufferBit;
  if (snapshot.shader_resource)
    mask |= kShaderResourceBit;
  if (snapshot.sampler)
    mask |= kSamplerBit;
  if (snapshot.unordered_access)
    mask |= kUnorderedAccessBit;
  if (snapshot.blend_state)
    mask |= kBlendStateBit;
  if (snapshot.depth_stencil_state)
    mask |= kDepthStencilStateBit;
  if (snapshot.rasterizer_state)
    mask |= kRasterizerStateBit;
  return mask;
}

bool SnapshotMatchesMask(const PipelineSnapshot &snapshot,
                         const PipelineObjects &objects, std::uint32_t mask) {
  const auto selected = [mask](StateBit bit) { return (mask & bit) != 0; };
  const bool input_layout_matches =
      snapshot.input_layout.get() ==
      (selected(kInputLayoutBit) ? objects.input_layout.get() : nullptr);
  const bool vertex_buffer_matches =
      snapshot.vertex_buffer.get() == (selected(kVertexBufferBit)
                                           ? objects.vertex_buffer.get()
                                           : nullptr) &&
      snapshot.vertex_stride ==
          (selected(kVertexBufferBit) ? kVertexStride : 0u) &&
      snapshot.vertex_offset ==
          (selected(kVertexBufferBit) ? kVertexOffset : 0u);
  const bool index_buffer_matches =
      snapshot.index_buffer.get() ==
          (selected(kIndexBufferBit) ? objects.index_buffer.get() : nullptr) &&
      snapshot.index_format == (selected(kIndexBufferBit)
                                    ? DXGI_FORMAT_R16_UINT
                                    : DXGI_FORMAT_UNKNOWN) &&
      snapshot.index_offset == (selected(kIndexBufferBit) ? kIndexOffset : 0u);
  const bool topology_matches =
      snapshot.topology == (selected(kTopologyBit)
                                ? kBoundTopology
                                : D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED);
  const bool vertex_shader_matches =
      snapshot.vertex_shader.get() ==
      (selected(kVertexShaderBit) ? objects.vertex_shader.get() : nullptr);
  const bool constant_buffer_matches =
      snapshot.constant_buffer.get() ==
      (selected(kConstantBufferBit) ? objects.constant_buffer.get() : nullptr);
  const bool shader_resource_matches =
      snapshot.shader_resource.get() ==
      (selected(kShaderResourceBit) ? objects.shader_resource.get() : nullptr);
  const bool sampler_matches =
      snapshot.sampler.get() ==
      (selected(kSamplerBit) ? objects.sampler.get() : nullptr);
  const bool unordered_access_matches =
      snapshot.unordered_access.get() == (selected(kUnorderedAccessBit)
                                              ? objects.unordered_access.get()
                                              : nullptr);
  const bool blend_matches =
      snapshot.blend_state.get() ==
          (selected(kBlendStateBit) ? objects.blend_state.get() : nullptr) &&
      snapshot.blend_factor ==
          (selected(kBlendStateBit) ? kBlendFactor : kDefaultBlendFactor) &&
      snapshot.sample_mask ==
          (selected(kBlendStateBit) ? kSampleMask : ~UINT{0});
  const bool depth_stencil_matches =
      snapshot.depth_stencil_state.get() ==
          (selected(kDepthStencilStateBit) ? objects.depth_stencil_state.get()
                                           : nullptr) &&
      snapshot.stencil_ref ==
          (selected(kDepthStencilStateBit) ? kStencilRef : 0u);
  const bool rasterizer_matches =
      snapshot.rasterizer_state.get() == (selected(kRasterizerStateBit)
                                              ? objects.rasterizer_state.get()
                                              : nullptr);
  return input_layout_matches && vertex_buffer_matches &&
         index_buffer_matches && topology_matches && vertex_shader_matches &&
         constant_buffer_matches && shader_resource_matches &&
         sampler_matches && unordered_access_matches && blend_matches &&
         depth_stencil_matches && rasterizer_matches;
}

class D3D11ClearStateSubsetMatrixSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_NE(context_.context(), nullptr);
  }

  D3D11TestContext context_;
};

TEST_F(D3D11ClearStateSubsetMatrixSpec,
       RestoresDefaultsFrom4096PipelineSubsets) {
  std::vector<std::uint32_t> selected_cases;
  selected_cases.reserve(kClearStateSubsetCaseCount);
  for (std::uint32_t logical = 0; logical < kClearStateSubsetCaseCount;
       ++logical) {
    if (dxmt::test::LogicalCaseSelected(kClearStateSubsetCases.family(),
                                        logical))
      selected_cases.push_back(logical);
  }
  ASSERT_FALSE(selected_cases.empty());

  PipelineObjects objects;
  const auto vertex = CompileShader(
      "float4 main(float4 position : POSITION) : SV_Position { return "
      "position; }",
      "vs_5_0");
  ASSERT_EQ(vertex.result, S_OK) << vertex.diagnostic_text();
  ASSERT_EQ(context_.device()->CreateVertexShader(
                vertex.bytecode->GetBufferPointer(),
                vertex.bytecode->GetBufferSize(), nullptr,
                objects.vertex_shader.put()),
            S_OK);
  const D3D11_INPUT_ELEMENT_DESC element = {
      "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT,
      0,          0, D3D11_INPUT_PER_VERTEX_DATA,
      0};
  ASSERT_EQ(context_.device()->CreateInputLayout(
                &element, 1, vertex.bytecode->GetBufferPointer(),
                vertex.bytecode->GetBufferSize(), objects.input_layout.put()),
            S_OK);

  D3D11_BUFFER_DESC buffer_desc = {};
  buffer_desc.ByteWidth = 64;
  buffer_desc.Usage = D3D11_USAGE_DEFAULT;
  buffer_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  ASSERT_EQ(context_.device()->CreateBuffer(&buffer_desc, nullptr,
                                            objects.vertex_buffer.put()),
            S_OK);
  buffer_desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
  ASSERT_EQ(context_.device()->CreateBuffer(&buffer_desc, nullptr,
                                            objects.index_buffer.put()),
            S_OK);
  buffer_desc.ByteWidth = 16;
  buffer_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  ASSERT_EQ(context_.device()->CreateBuffer(&buffer_desc, nullptr,
                                            objects.constant_buffer.put()),
            S_OK);

  D3D11_TEXTURE2D_DESC texture_desc = {};
  texture_desc.Width = 1;
  texture_desc.Height = 1;
  texture_desc.MipLevels = 1;
  texture_desc.ArraySize = 1;
  texture_desc.Format = DXGI_FORMAT_R32_UINT;
  texture_desc.SampleDesc.Count = 1;
  texture_desc.Usage = D3D11_USAGE_DEFAULT;
  texture_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  ComPtr<ID3D11Texture2D> texture;
  ASSERT_EQ(
      context_.device()->CreateTexture2D(&texture_desc, nullptr, texture.put()),
      S_OK);
  ASSERT_EQ(context_.device()->CreateShaderResourceView(
                texture.get(), nullptr, objects.shader_resource.put()),
            S_OK);

  D3D11_SAMPLER_DESC sampler_desc = {};
  sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
  sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;
  ASSERT_EQ(context_.device()->CreateSamplerState(&sampler_desc,
                                                  objects.sampler.put()),
            S_OK);

  buffer_desc.ByteWidth = 16;
  buffer_desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
  buffer_desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
  buffer_desc.StructureByteStride = sizeof(std::uint32_t);
  ComPtr<ID3D11Buffer> unordered_buffer;
  ASSERT_EQ(context_.device()->CreateBuffer(&buffer_desc, nullptr,
                                            unordered_buffer.put()),
            S_OK);
  ASSERT_EQ(
      context_.device()->CreateUnorderedAccessView(
          unordered_buffer.get(), nullptr, objects.unordered_access.put()),
      S_OK);

  D3D11_BLEND_DESC blend_desc = {};
  blend_desc.RenderTarget[0].BlendEnable = TRUE;
  blend_desc.RenderTarget[0].SrcBlend = D3D11_BLEND_BLEND_FACTOR;
  blend_desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_BLEND_FACTOR;
  blend_desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
  blend_desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
  blend_desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
  blend_desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
  blend_desc.RenderTarget[0].RenderTargetWriteMask =
      D3D11_COLOR_WRITE_ENABLE_ALL;
  ASSERT_EQ(context_.device()->CreateBlendState(&blend_desc,
                                                objects.blend_state.put()),
            S_OK);

  D3D11_DEPTH_STENCIL_DESC depth_desc = {};
  depth_desc.DepthEnable = TRUE;
  depth_desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
  depth_desc.DepthFunc = D3D11_COMPARISON_LESS;
  depth_desc.StencilEnable = TRUE;
  depth_desc.StencilReadMask = D3D11_DEFAULT_STENCIL_READ_MASK;
  depth_desc.StencilWriteMask = D3D11_DEFAULT_STENCIL_WRITE_MASK;
  depth_desc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
  depth_desc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
  depth_desc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
  depth_desc.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
  depth_desc.BackFace = depth_desc.FrontFace;
  ASSERT_EQ(context_.device()->CreateDepthStencilState(
                &depth_desc, objects.depth_stencil_state.put()),
            S_OK);

  D3D11_RASTERIZER_DESC rasterizer_desc = {};
  rasterizer_desc.FillMode = D3D11_FILL_SOLID;
  rasterizer_desc.CullMode = D3D11_CULL_NONE;
  rasterizer_desc.DepthClipEnable = TRUE;
  ASSERT_EQ(context_.device()->CreateRasterizerState(
                &rasterizer_desc, objects.rasterizer_state.put()),
            S_OK);

  context_.context()->ClearState();
  RecordProperty("logical_cases_executed", selected_cases.size());
  RecordProperty("logical_case_prefix",
                 kClearStateSubsetCases.family().case_id_prefix);
  for (const std::uint32_t logical : selected_cases) {
    if (logical & kInputLayoutBit)
      context_.context()->IASetInputLayout(objects.input_layout.get());
    if (logical & kVertexBufferBit) {
      ID3D11Buffer *buffer = objects.vertex_buffer.get();
      const UINT stride = kVertexStride;
      const UINT offset = kVertexOffset;
      context_.context()->IASetVertexBuffers(0, 1, &buffer, &stride, &offset);
    }
    if (logical & kIndexBufferBit)
      context_.context()->IASetIndexBuffer(objects.index_buffer.get(),
                                           DXGI_FORMAT_R16_UINT, kIndexOffset);
    if (logical & kTopologyBit)
      context_.context()->IASetPrimitiveTopology(kBoundTopology);
    if (logical & kVertexShaderBit)
      context_.context()->VSSetShader(objects.vertex_shader.get(), nullptr, 0);
    if (logical & kConstantBufferBit) {
      ID3D11Buffer *buffer = objects.constant_buffer.get();
      context_.context()->VSSetConstantBuffers(0, 1, &buffer);
    }
    if (logical & kShaderResourceBit) {
      ID3D11ShaderResourceView *view = objects.shader_resource.get();
      context_.context()->PSSetShaderResources(0, 1, &view);
    }
    if (logical & kSamplerBit) {
      ID3D11SamplerState *sampler = objects.sampler.get();
      context_.context()->PSSetSamplers(0, 1, &sampler);
    }
    if (logical & kUnorderedAccessBit) {
      ID3D11UnorderedAccessView *view = objects.unordered_access.get();
      context_.context()->CSSetUnorderedAccessViews(0, 1, &view, nullptr);
    }
    if (logical & kBlendStateBit)
      context_.context()->OMSetBlendState(objects.blend_state.get(),
                                          kBlendFactor.data(), kSampleMask);
    if (logical & kDepthStencilStateBit)
      context_.context()->OMSetDepthStencilState(
          objects.depth_stencil_state.get(), kStencilRef);
    if (logical & kRasterizerStateBit)
      context_.context()->RSSetState(objects.rasterizer_state.get());

    const PipelineSnapshot before = CapturePipelineState(context_.context());
    const bool before_matches = SnapshotMatchesMask(before, objects, logical);
    context_.context()->ClearState();
    const PipelineSnapshot after = CapturePipelineState(context_.context());
    const bool after_matches = SnapshotMatchesMask(after, objects, 0);
    if (before_matches && after_matches)
      continue;

    const auto case_id =
        dxmt::test::LogicalCaseId(kClearStateSubsetCases.family(), logical);
    ADD_FAILURE()
        << "LogicalCaseId: " << case_id << '\n'
        << "Class: "
        << dxmt::test::TestClassName(
               kClearStateSubsetCases.family().traits.test_class)
        << '\n'
        << "Requirements: feature_level=11_0 shader_model=5_0 "
           "queue=Immediate capability=ClearState,PipelineStateGetters,"
           "ComReferenceState\n"
        << "ExecutionPath: "
        << dxmt::test::ExecutionPathName(
               kClearStateSubsetCases.family().traits.execution_path)
        << '\n'
        << "Parameters: logical=" << logical << " expected_mask=0x" << std::hex
        << logical << std::dec << " selected_cases=" << selected_cases.size()
        << '\n'
        << "Observed: before_matches=" << before_matches << " before_mask=0x"
        << std::hex << ObservedObjectMask(before)
        << " after_matches=" << after_matches << " after_mask=0x"
        << ObservedObjectMask(after) << std::dec
        << " before_vb_stride=" << before.vertex_stride
        << " before_vb_offset=" << before.vertex_offset
        << " before_index_format=" << before.index_format
        << " before_index_offset=" << before.index_offset
        << " before_topology=" << before.topology
        << " before_sample_mask=" << before.sample_mask
        << " before_stencil_ref=" << before.stencil_ref
        << " after_vb_stride=" << after.vertex_stride
        << " after_vb_offset=" << after.vertex_offset
        << " after_index_format=" << after.index_format
        << " after_index_offset=" << after.index_offset
        << " after_topology=" << after.topology
        << " after_sample_mask=" << after.sample_mask
        << " after_stencil_ref=" << after.stencil_ref << '\n'
        << "Replay: --dxmt-case-id=" << case_id;
    break;
  }

  context_.context()->ClearState();
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
