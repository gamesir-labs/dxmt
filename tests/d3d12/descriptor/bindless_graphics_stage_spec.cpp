#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"

#include <array>
#include <cstring>
#include <limits>

namespace {

using dxmt::test::ColorsMatch;
using dxmt::test::ComPtr;
using dxmt::test::CompileShader;
using dxmt::test::D3D12TestContext;
using dxmt::test::ShaderCompilation;
using dxmt::test::TextureReadback;

constexpr char kPointVertexShader[] = R"(
struct Output {
  float4 position : SV_Position;
};

Output main() {
  Output output;
  output.position = float4(0.0, 0.0, 0.0, 1.0);
  return output;
}
)";

constexpr char kPointVertexShaderReadsDescriptor[] = R"(
StructuredBuffer<float4> stage_value : register(t0);

struct Output {
  float4 position : SV_Position;
  float4 color : COLOR0;
};

Output main() {
  Output output;
  output.position = float4(0.0, 0.0, 0.0, 1.0);
  output.color = stage_value[0];
  return output;
}
)";

constexpr char kGeometryShader[] = R"(
StructuredBuffer<float4> stage_value : register(t0);

struct VertexOutput {
  float4 position : SV_Position;
};

struct GeometryOutput {
  float4 position : SV_Position;
  float4 color : COLOR0;
};

[maxvertexcount(3)]
void main(point VertexOutput input[1], inout TriangleStream<GeometryOutput> stream) {
  const float2 positions[3] = {
    float2(-1.0, -1.0),
    float2(-1.0,  3.0),
    float2( 3.0, -1.0)
  };
  GeometryOutput output;
  output.color = stage_value[0];
  [unroll]
  for (uint index = 0; index < 3; ++index) {
    output.position = float4(positions[index], 0.0, 1.0);
    stream.Append(output);
  }
}
)";

constexpr char kGeometryShaderConstantGreen[] = R"(
struct VertexOutput {
  float4 position : SV_Position;
};

struct GeometryOutput {
  float4 position : SV_Position;
  float4 color : COLOR0;
};

[maxvertexcount(3)]
void main(point VertexOutput input[1], inout TriangleStream<GeometryOutput> stream) {
  const float2 positions[3] = {
    float2(-1.0, -1.0),
    float2(-1.0,  3.0),
    float2( 3.0, -1.0)
  };
  GeometryOutput output;
  output.color = float4(0.0, 1.0, 0.0, 1.0);
  [unroll]
  for (uint index = 0; index < 3; ++index) {
    output.position = float4(positions[index], 0.0, 1.0);
    stream.Append(output);
  }
}
)";

constexpr char kGeometryShaderReadsVertexColor[] = R"(
struct VertexOutput {
  float4 position : SV_Position;
  float4 color : COLOR0;
};

struct GeometryOutput {
  float4 position : SV_Position;
  float4 color : COLOR0;
};

[maxvertexcount(3)]
void main(point VertexOutput input[1], inout TriangleStream<GeometryOutput> stream) {
  const float2 positions[3] = {
    float2(-1.0, -1.0),
    float2(-1.0,  3.0),
    float2( 3.0, -1.0)
  };
  GeometryOutput output;
  output.color = input[0].color;
  [unroll]
  for (uint index = 0; index < 3; ++index) {
    output.position = float4(positions[index], 0.0, 1.0);
    stream.Append(output);
  }
}
)";

constexpr char kPatchVertexShader[] = R"(
struct ControlPoint {
  float4 position : POSITION;
  float4 color : COLOR0;
};

ControlPoint main(uint vertex_id : SV_VertexID) {
  const float2 positions[3] = {
    float2(-1.0, -1.0),
    float2(-1.0,  3.0),
    float2( 3.0, -1.0)
  };
  ControlPoint output;
  output.position = float4(positions[vertex_id], 0.0, 1.0);
  output.color = 0.0;
  return output;
}
)";

constexpr char kPatchVertexShaderReadsDescriptor[] = R"(
StructuredBuffer<float4> stage_value : register(t0);

struct ControlPoint {
  float4 position : POSITION;
  float4 color : COLOR0;
};

ControlPoint main(uint vertex_id : SV_VertexID) {
  const float2 positions[3] = {
    float2(-1.0, -1.0),
    float2(-1.0,  3.0),
    float2( 3.0, -1.0)
  };
  ControlPoint output;
  output.position = float4(positions[vertex_id], 0.0, 1.0);
  output.color = stage_value[0];
  return output;
}
)";

constexpr char kHullShaderReadsDescriptor[] = R"(
StructuredBuffer<float4> stage_value : register(t0);

struct ControlPoint {
  float4 position : POSITION;
  float4 color : COLOR0;
};

struct PatchConstants {
  float edge[3] : SV_TessFactor;
  float inside : SV_InsideTessFactor;
};

PatchConstants PatchConstantFunction(InputPatch<ControlPoint, 3> patch) {
  PatchConstants output;
  output.edge[0] = 1.0;
  output.edge[1] = 1.0;
  output.edge[2] = 1.0;
  output.inside = 1.0;
  return output;
}

[domain("tri")]
[partitioning("integer")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(3)]
[patchconstantfunc("PatchConstantFunction")]
ControlPoint main(InputPatch<ControlPoint, 3> patch,
                  uint control_point_id : SV_OutputControlPointID) {
  ControlPoint output = patch[control_point_id];
  output.color = stage_value[0];
  return output;
}
)";

constexpr char kHullShaderPassThrough[] = R"(
struct ControlPoint {
  float4 position : POSITION;
  float4 color : COLOR0;
};

struct PatchConstants {
  float edge[3] : SV_TessFactor;
  float inside : SV_InsideTessFactor;
};

PatchConstants PatchConstantFunction(InputPatch<ControlPoint, 3> patch) {
  PatchConstants output;
  output.edge[0] = 1.0;
  output.edge[1] = 1.0;
  output.edge[2] = 1.0;
  output.inside = 1.0;
  return output;
}

[domain("tri")]
[partitioning("integer")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(3)]
[patchconstantfunc("PatchConstantFunction")]
ControlPoint main(InputPatch<ControlPoint, 3> patch,
                  uint control_point_id : SV_OutputControlPointID) {
  return patch[control_point_id];
}
)";

constexpr char kHullShaderAddsDescriptor[] = R"(
StructuredBuffer<float4> stage_value : register(t0);

struct ControlPoint {
  float4 position : POSITION;
  float4 color : COLOR0;
};

struct PatchConstants {
  float edge[3] : SV_TessFactor;
  float inside : SV_InsideTessFactor;
};

PatchConstants PatchConstantFunction(InputPatch<ControlPoint, 3> patch) {
  PatchConstants output;
  output.edge[0] = 1.0;
  output.edge[1] = 1.0;
  output.edge[2] = 1.0;
  output.inside = 1.0;
  return output;
}

[domain("tri")]
[partitioning("integer")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(3)]
[patchconstantfunc("PatchConstantFunction")]
ControlPoint main(InputPatch<ControlPoint, 3> patch,
                  uint control_point_id : SV_OutputControlPointID) {
  ControlPoint output = patch[control_point_id];
  output.color += stage_value[0];
  output.color.a = 1.0;
  return output;
}
)";

constexpr char kDomainShaderReadsHullColor[] = R"(
struct ControlPoint {
  float4 position : POSITION;
  float4 color : COLOR0;
};

struct PatchConstants {
  float edge[3] : SV_TessFactor;
  float inside : SV_InsideTessFactor;
};

struct DomainOutput {
  float4 position : SV_Position;
  float4 color : COLOR0;
};

[domain("tri")]
DomainOutput main(PatchConstants constants,
                  const OutputPatch<ControlPoint, 3> patch,
                  float3 barycentric : SV_DomainLocation) {
  DomainOutput output;
  output.position = patch[0].position * barycentric.x +
                    patch[1].position * barycentric.y +
                    patch[2].position * barycentric.z;
  output.color = patch[0].color * barycentric.x +
                 patch[1].color * barycentric.y +
                 patch[2].color * barycentric.z;
  return output;
}
)";

constexpr char kDomainShaderReadsDescriptor[] = R"(
StructuredBuffer<float4> stage_value : register(t0);

struct ControlPoint {
  float4 position : POSITION;
  float4 color : COLOR0;
};

struct PatchConstants {
  float edge[3] : SV_TessFactor;
  float inside : SV_InsideTessFactor;
};

struct DomainOutput {
  float4 position : SV_Position;
  float4 color : COLOR0;
};

[domain("tri")]
DomainOutput main(PatchConstants constants,
                  const OutputPatch<ControlPoint, 3> patch,
                  float3 barycentric : SV_DomainLocation) {
  DomainOutput output;
  output.position = patch[0].position * barycentric.x +
                    patch[1].position * barycentric.y +
                    patch[2].position * barycentric.z;
  output.color = stage_value[0];
  return output;
}
)";

constexpr char kDomainShaderConstantGreen[] = R"(
struct ControlPoint {
  float4 position : POSITION;
  float4 color : COLOR0;
};

struct PatchConstants {
  float edge[3] : SV_TessFactor;
  float inside : SV_InsideTessFactor;
};

struct DomainOutput {
  float4 position : SV_Position;
  float4 color : COLOR0;
};

[domain("tri")]
DomainOutput main(PatchConstants constants,
                  const OutputPatch<ControlPoint, 3> patch,
                  float3 barycentric : SV_DomainLocation) {
  DomainOutput output;
  output.position = patch[0].position * barycentric.x +
                    patch[1].position * barycentric.y +
                    patch[2].position * barycentric.z;
  output.color = float4(0.0, 1.0, 0.0, 1.0);
  return output;
}
)";

constexpr char kColorPixelShader[] = R"(
float4 main(float4 position : SV_Position, float4 color : COLOR0) : SV_Target {
  return color;
}
)";

struct StageTestResources {
  ComPtr<ID3D12RootSignature> root_signature;
  ComPtr<ID3D12DescriptorHeap> descriptor_heap;
  ComPtr<ID3D12Resource> stage_value;
  ComPtr<ID3D12Resource> second_stage_value;
  ComPtr<ID3D12Resource> render_target;
  ComPtr<ID3D12DescriptorHeap> rtv_heap;
  D3D12_CPU_DESCRIPTOR_HANDLE rtv = {};
};

D3D12_SHADER_BYTECODE Bytecode(const ShaderCompilation &shader) {
  return {shader.bytecode->GetBufferPointer(), shader.bytecode->GetBufferSize()};
}

StageTestResources CreateDualObjectStageTestResources(
    D3D12TestContext &context) {
  StageTestResources resources;

  std::array<D3D12_DESCRIPTOR_RANGE, 2> ranges = {};
  std::array<D3D12_ROOT_PARAMETER, 2> parameters = {};
  const std::array<D3D12_SHADER_VISIBILITY, 2> visibilities = {
      D3D12_SHADER_VISIBILITY_VERTEX, D3D12_SHADER_VISIBILITY_HULL};
  for (UINT i = 0; i < parameters.size(); ++i) {
    ranges[i].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[i].NumDescriptors = 1;
    ranges[i].BaseShaderRegister = 0;
    ranges[i].OffsetInDescriptorsFromTableStart = 0;
    parameters[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[i].DescriptorTable.NumDescriptorRanges = 1;
    parameters[i].DescriptorTable.pDescriptorRanges = &ranges[i];
    parameters[i].ShaderVisibility = visibilities[i];
  }
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  root_desc.NumParameters = parameters.size();
  root_desc.pParameters = parameters.data();
  resources.root_signature = context.CreateRootSignature(root_desc);

  resources.descriptor_heap = context.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, true);
  const std::array<float, 4> vertex_value = {0.25f, 0.0f, 0.0f, 1.0f};
  const std::array<float, 4> hull_value = {0.0f, 0.75f, 0.0f, 0.0f};
  resources.stage_value = context.CreateUploadBuffer(
      sizeof(vertex_value), vertex_value.data(), sizeof(vertex_value));
  resources.second_stage_value = context.CreateUploadBuffer(
      sizeof(hull_value), hull_value.data(), sizeof(hull_value));
  if (resources.descriptor_heap && resources.stage_value &&
      resources.second_stage_value) {
    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
    srv_desc.Format = DXGI_FORMAT_UNKNOWN;
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv_desc.Buffer.NumElements = 1;
    srv_desc.Buffer.StructureByteStride = sizeof(vertex_value);
    auto cpu = resources.descriptor_heap->GetCPUDescriptorHandleForHeapStart();
    context.device()->CreateShaderResourceView(resources.stage_value.get(),
                                               &srv_desc, cpu);
    cpu.ptr += context.device()->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    context.device()->CreateShaderResourceView(
        resources.second_stage_value.get(), &srv_desc, cpu);
  }

  resources.render_target = context.CreateTexture2D(
      32, 32, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
      D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
      D3D12_RESOURCE_STATE_RENDER_TARGET);
  resources.rtv_heap = context.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
  if (resources.render_target && resources.rtv_heap) {
    resources.rtv = resources.rtv_heap->GetCPUDescriptorHandleForHeapStart();
    context.device()->CreateRenderTargetView(resources.render_target.get(),
                                             nullptr, resources.rtv);
  }
  return resources;
}

void ExpectShaderCompiled(const ShaderCompilation &shader) {
  ASSERT_TRUE(SUCCEEDED(shader.result)) << shader.diagnostic_text();
  ASSERT_TRUE(shader.bytecode);
}

StageTestResources CreateStageTestResources(D3D12TestContext &context,
                                            D3D12_SHADER_VISIBILITY visibility) {
  StageTestResources resources;

  D3D12_DESCRIPTOR_RANGE range = {};
  range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  range.NumDescriptors = 1;
  range.BaseShaderRegister = 0;
  range.OffsetInDescriptorsFromTableStart = 0;
  D3D12_ROOT_PARAMETER parameter = {};
  parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameter.DescriptorTable.NumDescriptorRanges = 1;
  parameter.DescriptorTable.pDescriptorRanges = &range;
  parameter.ShaderVisibility = visibility;
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  root_desc.NumParameters = 1;
  root_desc.pParameters = &parameter;
  resources.root_signature = context.CreateRootSignature(root_desc);

  resources.descriptor_heap = context.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true);
  const std::array<float, 4> green = {0.0f, 1.0f, 0.0f, 1.0f};
  resources.stage_value =
      context.CreateUploadBuffer(sizeof(green), green.data(), sizeof(green));
  if (resources.descriptor_heap && resources.stage_value) {
    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
    srv_desc.Format = DXGI_FORMAT_UNKNOWN;
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv_desc.Buffer.NumElements = 1;
    srv_desc.Buffer.StructureByteStride = sizeof(green);
    context.device()->CreateShaderResourceView(
        resources.stage_value.get(), &srv_desc,
        resources.descriptor_heap->GetCPUDescriptorHandleForHeapStart());
  }

  resources.render_target = context.CreateTexture2D(
      32, 32, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
      D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
      D3D12_RESOURCE_STATE_RENDER_TARGET);
  resources.rtv_heap = context.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
  if (resources.render_target && resources.rtv_heap) {
    resources.rtv = resources.rtv_heap->GetCPUDescriptorHandleForHeapStart();
    context.device()->CreateRenderTargetView(resources.render_target.get(),
                                             nullptr, resources.rtv);
  }
  return resources;
}

ComPtr<ID3D12PipelineState> CreatePipeline(
    D3D12TestContext &context, ID3D12RootSignature *root_signature,
    const ShaderCompilation &vertex, const ShaderCompilation &pixel,
    const ShaderCompilation *geometry, const ShaderCompilation *hull,
    const ShaderCompilation *domain, D3D12_PRIMITIVE_TOPOLOGY_TYPE topology) {
  D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
  desc.pRootSignature = root_signature;
  desc.VS = Bytecode(vertex);
  desc.PS = Bytecode(pixel);
  if (geometry)
    desc.GS = Bytecode(*geometry);
  if (hull)
    desc.HS = Bytecode(*hull);
  if (domain)
    desc.DS = Bytecode(*domain);
  desc.BlendState.RenderTarget[0].RenderTargetWriteMask =
      D3D12_COLOR_WRITE_ENABLE_ALL;
  desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
  desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  desc.SampleMask = std::numeric_limits<UINT>::max();
  desc.PrimitiveTopologyType = topology;
  desc.NumRenderTargets = 1;
  desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;

  ComPtr<ID3D12PipelineState> pipeline;
  const HRESULT hr = context.device()->CreateGraphicsPipelineState(
      &desc, IID_PPV_ARGS(pipeline.put()));
  return SUCCEEDED(hr) ? std::move(pipeline) : ComPtr<ID3D12PipelineState>();
}

void RecordAndExpectColor(D3D12TestContext &context,
                          const StageTestResources &resources,
                          ID3D12PipelineState *pipeline,
                          D3D12_PRIMITIVE_TOPOLOGY topology,
                          UINT vertex_count,
                          std::uint32_t expected = 0xff00ff00u,
                          UINT root_table_count = 1) {
  const D3D12_VIEWPORT viewport = {0.0f, 0.0f, 32.0f, 32.0f, 0.0f, 1.0f};
  const D3D12_RECT scissor = {0, 0, 32, 32};
  const float clear[4] = {};
  context.list()->ClearRenderTargetView(resources.rtv, clear, 0, nullptr);
  context.list()->OMSetRenderTargets(1, &resources.rtv, FALSE, nullptr);
  context.list()->RSSetViewports(1, &viewport);
  context.list()->RSSetScissorRects(1, &scissor);
  context.list()->SetGraphicsRootSignature(resources.root_signature.get());
  context.list()->SetPipelineState(pipeline);
  ID3D12DescriptorHeap *heaps[] = {resources.descriptor_heap.get()};
  context.list()->SetDescriptorHeaps(1, heaps);
  auto gpu = resources.descriptor_heap->GetGPUDescriptorHandleForHeapStart();
  const UINT descriptor_stride = context.device()->GetDescriptorHandleIncrementSize(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  for (UINT i = 0; i < root_table_count; ++i) {
    context.list()->SetGraphicsRootDescriptorTable(i, gpu);
    gpu.ptr += descriptor_stride;
  }
  context.list()->IASetPrimitiveTopology(topology);
  context.list()->DrawInstanced(vertex_count, 1, 0, 0);
  D3D12TestContext::Transition(
      context.list(), resources.render_target.get(),
      D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

  TextureReadback readback;
  ASSERT_TRUE(SUCCEEDED(
      context.ReadbackTexture(resources.render_target.get(), &readback)));
  ASSERT_EQ(readback.width, 32u);
  ASSERT_EQ(readback.height, 32u);
  const UINT x = readback.width / 2;
  const UINT y = readback.height / 2;
  std::uint32_t actual = 0;
  std::memcpy(&actual,
              readback.data.data() + y * readback.row_pitch +
                  x * sizeof(actual),
              sizeof(actual));
  EXPECT_TRUE(ColorsMatch(actual, expected, 1))
      << "center pixel was 0x" << std::hex << actual
      << ", expected 0x" << expected;
}

class D3D12BindlessGraphicsStageSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_TRUE(SUCCEEDED(context_.Initialize()));
  }

  D3D12TestContext context_;
};

TEST_F(D3D12BindlessGraphicsStageSpec,
       RendersGeometryPipelineWithoutStageDescriptor) {
  const auto vertex = CompileShader(kPointVertexShader, "vs_5_0");
  const auto geometry = CompileShader(kGeometryShaderConstantGreen, "gs_5_0");
  const auto pixel = CompileShader(kColorPixelShader, "ps_5_0");
  ExpectShaderCompiled(vertex);
  ExpectShaderCompiled(geometry);
  ExpectShaderCompiled(pixel);

  auto resources =
      CreateStageTestResources(context_, D3D12_SHADER_VISIBILITY_GEOMETRY);
  ASSERT_TRUE(resources.root_signature);
  ASSERT_TRUE(resources.descriptor_heap);
  ASSERT_TRUE(resources.stage_value);
  ASSERT_TRUE(resources.render_target);
  ASSERT_TRUE(resources.rtv_heap);
  auto pipeline = CreatePipeline(
      context_, resources.root_signature.get(), vertex, pixel, &geometry,
      nullptr, nullptr, D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT);
  ASSERT_TRUE(pipeline);

  RecordAndExpectColor(context_, resources, pipeline.get(),
                       D3D_PRIMITIVE_TOPOLOGY_POINTLIST, 1);
}

TEST_F(D3D12BindlessGraphicsStageSpec,
       RendersTessellationPipelineWithoutStageDescriptor) {
  const auto vertex = CompileShader(kPatchVertexShader, "vs_5_0");
  const auto hull = CompileShader(kHullShaderPassThrough, "hs_5_0");
  const auto domain = CompileShader(kDomainShaderConstantGreen, "ds_5_0");
  const auto pixel = CompileShader(kColorPixelShader, "ps_5_0");
  ExpectShaderCompiled(vertex);
  ExpectShaderCompiled(hull);
  ExpectShaderCompiled(domain);
  ExpectShaderCompiled(pixel);

  auto resources =
      CreateStageTestResources(context_, D3D12_SHADER_VISIBILITY_DOMAIN);
  ASSERT_TRUE(resources.root_signature);
  ASSERT_TRUE(resources.descriptor_heap);
  ASSERT_TRUE(resources.stage_value);
  ASSERT_TRUE(resources.render_target);
  ASSERT_TRUE(resources.rtv_heap);
  auto pipeline = CreatePipeline(
      context_, resources.root_signature.get(), vertex, pixel, nullptr, &hull,
      &domain, D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH);
  ASSERT_TRUE(pipeline);

  RecordAndExpectColor(context_, resources, pipeline.get(),
                       D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST, 3);
}

TEST_F(D3D12BindlessGraphicsStageSpec,
       BindsGeometryDescriptorTableToMeshStage) {
  const auto vertex = CompileShader(kPointVertexShader, "vs_5_0");
  const auto geometry = CompileShader(kGeometryShader, "gs_5_0");
  const auto pixel = CompileShader(kColorPixelShader, "ps_5_0");
  ExpectShaderCompiled(vertex);
  ExpectShaderCompiled(geometry);
  ExpectShaderCompiled(pixel);

  auto resources =
      CreateStageTestResources(context_, D3D12_SHADER_VISIBILITY_GEOMETRY);
  ASSERT_TRUE(resources.root_signature);
  ASSERT_TRUE(resources.descriptor_heap);
  ASSERT_TRUE(resources.stage_value);
  ASSERT_TRUE(resources.render_target);
  ASSERT_TRUE(resources.rtv_heap);
  auto pipeline = CreatePipeline(
      context_, resources.root_signature.get(), vertex, pixel, &geometry,
      nullptr, nullptr, D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT);
  ASSERT_TRUE(pipeline);

  RecordAndExpectColor(context_, resources, pipeline.get(),
                       D3D_PRIMITIVE_TOPOLOGY_POINTLIST, 1);
}

TEST_F(D3D12BindlessGraphicsStageSpec,
       BindsGeometryVertexDescriptorTableToObjectStage) {
  const auto vertex =
      CompileShader(kPointVertexShaderReadsDescriptor, "vs_5_0");
  const auto geometry =
      CompileShader(kGeometryShaderReadsVertexColor, "gs_5_0");
  const auto pixel = CompileShader(kColorPixelShader, "ps_5_0");
  ExpectShaderCompiled(vertex);
  ExpectShaderCompiled(geometry);
  ExpectShaderCompiled(pixel);

  auto resources =
      CreateStageTestResources(context_, D3D12_SHADER_VISIBILITY_VERTEX);
  ASSERT_TRUE(resources.root_signature);
  ASSERT_TRUE(resources.descriptor_heap);
  ASSERT_TRUE(resources.stage_value);
  ASSERT_TRUE(resources.render_target);
  ASSERT_TRUE(resources.rtv_heap);
  auto pipeline = CreatePipeline(
      context_, resources.root_signature.get(), vertex, pixel, &geometry,
      nullptr, nullptr, D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT);
  ASSERT_TRUE(pipeline);

  RecordAndExpectColor(context_, resources, pipeline.get(),
                       D3D_PRIMITIVE_TOPOLOGY_POINTLIST, 1);
}

TEST_F(D3D12BindlessGraphicsStageSpec,
       BindsHullDescriptorTableToObjectStage) {
  const auto vertex = CompileShader(kPatchVertexShader, "vs_5_0");
  const auto hull = CompileShader(kHullShaderReadsDescriptor, "hs_5_0");
  const auto domain = CompileShader(kDomainShaderReadsHullColor, "ds_5_0");
  const auto pixel = CompileShader(kColorPixelShader, "ps_5_0");
  ExpectShaderCompiled(vertex);
  ExpectShaderCompiled(hull);
  ExpectShaderCompiled(domain);
  ExpectShaderCompiled(pixel);

  auto resources =
      CreateStageTestResources(context_, D3D12_SHADER_VISIBILITY_HULL);
  ASSERT_TRUE(resources.root_signature);
  ASSERT_TRUE(resources.descriptor_heap);
  ASSERT_TRUE(resources.stage_value);
  ASSERT_TRUE(resources.render_target);
  ASSERT_TRUE(resources.rtv_heap);
  auto pipeline = CreatePipeline(
      context_, resources.root_signature.get(), vertex, pixel, nullptr, &hull,
      &domain, D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH);
  ASSERT_TRUE(pipeline);

  RecordAndExpectColor(context_, resources, pipeline.get(),
                       D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST, 3);
}

TEST_F(D3D12BindlessGraphicsStageSpec,
       BindsDomainDescriptorTableToMeshStage) {
  const auto vertex = CompileShader(kPatchVertexShader, "vs_5_0");
  const auto hull = CompileShader(kHullShaderPassThrough, "hs_5_0");
  const auto domain = CompileShader(kDomainShaderReadsDescriptor, "ds_5_0");
  const auto pixel = CompileShader(kColorPixelShader, "ps_5_0");
  ExpectShaderCompiled(vertex);
  ExpectShaderCompiled(hull);
  ExpectShaderCompiled(domain);
  ExpectShaderCompiled(pixel);

  auto resources =
      CreateStageTestResources(context_, D3D12_SHADER_VISIBILITY_DOMAIN);
  ASSERT_TRUE(resources.root_signature);
  ASSERT_TRUE(resources.descriptor_heap);
  ASSERT_TRUE(resources.stage_value);
  ASSERT_TRUE(resources.render_target);
  ASSERT_TRUE(resources.rtv_heap);
  auto pipeline = CreatePipeline(
      context_, resources.root_signature.get(), vertex, pixel, nullptr, &hull,
      &domain, D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH);
  ASSERT_TRUE(pipeline);

  RecordAndExpectColor(context_, resources, pipeline.get(),
                       D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST, 3);
}

TEST_F(D3D12BindlessGraphicsStageSpec,
       BindsTessellationVertexDescriptorTableToObjectStage) {
  const auto vertex =
      CompileShader(kPatchVertexShaderReadsDescriptor, "vs_5_0");
  const auto hull = CompileShader(kHullShaderPassThrough, "hs_5_0");
  const auto domain = CompileShader(kDomainShaderReadsHullColor, "ds_5_0");
  const auto pixel = CompileShader(kColorPixelShader, "ps_5_0");
  ExpectShaderCompiled(vertex);
  ExpectShaderCompiled(hull);
  ExpectShaderCompiled(domain);
  ExpectShaderCompiled(pixel);

  auto resources =
      CreateStageTestResources(context_, D3D12_SHADER_VISIBILITY_VERTEX);
  ASSERT_TRUE(resources.root_signature);
  ASSERT_TRUE(resources.descriptor_heap);
  ASSERT_TRUE(resources.stage_value);
  ASSERT_TRUE(resources.render_target);
  ASSERT_TRUE(resources.rtv_heap);
  auto pipeline = CreatePipeline(
      context_, resources.root_signature.get(), vertex, pixel, nullptr, &hull,
      &domain, D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH);
  ASSERT_TRUE(pipeline);

  RecordAndExpectColor(context_, resources, pipeline.get(),
                       D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST, 3);
}

TEST_F(D3D12BindlessGraphicsStageSpec,
       KeepsVertexAndHullTablesDistinctInObjectStage) {
  const auto vertex =
      CompileShader(kPatchVertexShaderReadsDescriptor, "vs_5_0");
  const auto hull = CompileShader(kHullShaderAddsDescriptor, "hs_5_0");
  const auto domain = CompileShader(kDomainShaderReadsHullColor, "ds_5_0");
  const auto pixel = CompileShader(kColorPixelShader, "ps_5_0");
  ExpectShaderCompiled(vertex);
  ExpectShaderCompiled(hull);
  ExpectShaderCompiled(domain);
  ExpectShaderCompiled(pixel);

  auto resources = CreateDualObjectStageTestResources(context_);
  ASSERT_TRUE(resources.root_signature);
  ASSERT_TRUE(resources.descriptor_heap);
  ASSERT_TRUE(resources.stage_value);
  ASSERT_TRUE(resources.second_stage_value);
  ASSERT_TRUE(resources.render_target);
  ASSERT_TRUE(resources.rtv_heap);
  auto pipeline = CreatePipeline(
      context_, resources.root_signature.get(), vertex, pixel, nullptr, &hull,
      &domain, D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH);
  ASSERT_TRUE(pipeline);

  RecordAndExpectColor(
      context_, resources, pipeline.get(),
      D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST, 3, 0xff00bf40u, 2);
}

} // namespace
