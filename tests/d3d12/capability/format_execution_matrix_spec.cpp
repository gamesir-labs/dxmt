#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;
using dxmt::test::TextureReadback;

enum class ShaderValueType {
  Float,
  Uint,
  Sint,
};

struct FormatLoadCase {
  DXGI_FORMAT format;
  ShaderValueType type;
  const char *name;
};

class FormatExecutionMatrixSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    D3D12_DESCRIPTOR_RANGE ranges[2] = {};
    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[0].NumDescriptors = 1;
    ranges[0].OffsetInDescriptorsFromTableStart = 0;
    ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    ranges[1].NumDescriptors = 1;
    ranges[1].OffsetInDescriptorsFromTableStart = 1;
    D3D12_ROOT_PARAMETER parameter = {};
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameter.DescriptorTable.NumDescriptorRanges = 2;
    parameter.DescriptorTable.pDescriptorRanges = ranges;
    D3D12_ROOT_SIGNATURE_DESC root_desc = {};
    root_desc.NumParameters = 1;
    root_desc.pParameters = &parameter;
    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MaxAnisotropy = 1;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_desc.NumStaticSamplers = 1;
    root_desc.pStaticSamplers = &sampler;
    root_ = context_.CreateRootSignature(root_desc);
    ASSERT_TRUE(root_);

    CreatePipeline("float4", ShaderValueType::Float);
    CreatePipeline("uint4", ShaderValueType::Uint);
    CreatePipeline("int4", ShaderValueType::Sint);
  }

  void CreatePipeline(const char *texture_type, ShaderValueType type) {
    const std::string source =
        std::string("Texture2D<") + texture_type + R"(> input : register(t0);
          RWByteAddressBuffer output : register(u0);
          [numthreads(1, 1, 1)]
          void main() {
            output.Store4(0, asuint(input.Load(int3(0, 0, 0))));
          }
        )";
    const auto shader = CompileShader(source.c_str(), "cs_5_0");
    ASSERT_EQ(shader.result, S_OK) << shader.diagnostic_text();
    const D3D12_SHADER_BYTECODE bytecode = {
        shader.bytecode->GetBufferPointer(), shader.bytecode->GetBufferSize()};
    Pipeline(type) = context_.CreateComputePipeline(root_.get(), bytecode);
    ASSERT_TRUE(Pipeline(type));
  }

  ComPtr<ID3D12PipelineState> &Pipeline(ShaderValueType type) {
    switch (type) {
    case ShaderValueType::Float:
      return float_pipeline_;
    case ShaderValueType::Uint:
      return uint_pipeline_;
    case ShaderValueType::Sint:
    default:
      return sint_pipeline_;
    }
  }

  D3D12_FEATURE_DATA_FORMAT_SUPPORT Support(DXGI_FORMAT format) {
    D3D12_FEATURE_DATA_FORMAT_SUPPORT support = {};
    support.Format = format;
    EXPECT_EQ(context_.device()->CheckFeatureSupport(
                  D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support)),
              S_OK);
    return support;
  }

  ComPtr<ID3D12Resource> CreateTexture(DXGI_FORMAT format) {
    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = 4;
    desc.Height = 4;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    ComPtr<ID3D12Resource> texture;
    const HRESULT hr = context_.device()->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr, IID_PPV_ARGS(texture.put()));
    EXPECT_EQ(hr, S_OK);
    return texture;
  }

  ComPtr<ID3D12Resource>
  CreateTexture(D3D12_RESOURCE_DIMENSION dimension, UINT64 width, UINT height,
                UINT16 depth_or_array_size, DXGI_FORMAT format) {
    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = dimension;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = depth_or_array_size;
    desc.MipLevels = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    ComPtr<ID3D12Resource> texture;
    const HRESULT hr = context_.device()->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr, IID_PPV_ARGS(texture.put()));
    EXPECT_EQ(hr, S_OK);
    return texture;
  }

  ComPtr<ID3D12PipelineState> CompilePipeline(const char *source) {
    const auto shader = CompileShader(source, "cs_5_0");
    if (shader.result != S_OK) {
      ADD_FAILURE() << shader.diagnostic_text();
      return {};
    }
    const D3D12_SHADER_BYTECODE bytecode = {
        shader.bytecode->GetBufferPointer(), shader.bytecode->GetBufferSize()};
    return context_.CreateComputePipeline(root_.get(), bytecode);
  }

  ComPtr<ID3D12PipelineState>
  CompileGraphicsPipeline(DXGI_FORMAT render_target_format, UINT sample_count,
                          DXGI_FORMAT depth_stencil_format =
                              DXGI_FORMAT_UNKNOWN) {
    const auto vertex = CompileShader(R"(
      float4 main(uint vertex_id : SV_VertexID) : SV_Position {
        const float2 positions[3] = {
          float2(-1.0, -1.0), float2(-1.0, 3.0), float2(3.0, -1.0)
        };
        return float4(positions[vertex_id], 0.25, 1.0);
      }
    )",
                                      "vs_5_0");
    if (vertex.result != S_OK) {
      ADD_FAILURE() << vertex.diagnostic_text();
      return {};
    }
    const auto pixel = CompileShader(R"(
      float4 main() : SV_Target {
        return float4(0.25, 0.5, 0.75, 1.0);
      }
    )",
                                     "ps_5_0");
    if (pixel.result != S_OK) {
      ADD_FAILURE() << pixel.diagnostic_text();
      return {};
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
    desc.pRootSignature = root_.get();
    desc.VS = {vertex.bytecode->GetBufferPointer(),
               vertex.bytecode->GetBufferSize()};
    desc.PS = {pixel.bytecode->GetBufferPointer(),
               pixel.bytecode->GetBufferSize()};
    desc.BlendState.RenderTarget[0].RenderTargetWriteMask =
        D3D12_COLOR_WRITE_ENABLE_ALL;
    desc.SampleMask = UINT_MAX;
    desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    desc.RasterizerState.DepthClipEnable = TRUE;
    if (depth_stencil_format != DXGI_FORMAT_UNKNOWN) {
      desc.DepthStencilState.DepthEnable = TRUE;
      desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
      desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
      desc.DSVFormat = depth_stencil_format;
    }
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = render_target_format;
    desc.SampleDesc.Count = sample_count;
    ComPtr<ID3D12PipelineState> pipeline;
    const HRESULT hr = context_.device()->CreateGraphicsPipelineState(
        &desc, IID_PPV_ARGS(pipeline.put()));
    EXPECT_EQ(hr, S_OK);
    return pipeline;
  }

  ComPtr<ID3D12Resource>
  CreateTexture2D(DXGI_FORMAT format, D3D12_RESOURCE_FLAGS flags,
                  D3D12_RESOURCE_STATES state, UINT sample_count = 1) {
    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = 4;
    desc.Height = 4;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = format;
    desc.SampleDesc.Count = sample_count;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = flags;
    ComPtr<ID3D12Resource> texture;
    const HRESULT hr = context_.device()->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr,
        IID_PPV_ARGS(texture.put()));
    EXPECT_EQ(hr, S_OK);
    return texture;
  }

  void DrawFullscreen(ID3D12PipelineState *pipeline,
                      D3D12_CPU_DESCRIPTOR_HANDLE render_target,
                      const D3D12_CPU_DESCRIPTOR_HANDLE *depth_stencil =
                          nullptr) {
    context_.list()->OMSetRenderTargets(1, &render_target, FALSE,
                                        depth_stencil);
    context_.list()->SetGraphicsRootSignature(root_.get());
    context_.list()->SetPipelineState(pipeline);
    context_.list()->IASetPrimitiveTopology(
        D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    const D3D12_VIEWPORT viewport = {0.0f, 0.0f, 4.0f, 4.0f, 0.0f, 1.0f};
    const D3D12_RECT scissor = {0, 0, 4, 4};
    context_.list()->RSSetViewports(1, &viewport);
    context_.list()->RSSetScissorRects(1, &scissor);
    context_.list()->DrawInstanced(3, 1, 0, 0);
  }

  template <typename T>
  void ExecuteTypedUav(DXGI_FORMAT format,
                       const std::array<T, 4> &initial,
                       const std::array<T, 4> &expected) {
    auto pipeline = CompilePipeline(R"(
      RWBuffer<uint> target : register(u0);
      [numthreads(1, 1, 1)] void main() {
        uint first = target[0];
        uint fourth = target[3];
        target[1] = first + 7;
        target[2] = fourth + 3;
      }
    )");
    ASSERT_TRUE(pipeline);
    auto upload = context_.CreateUploadBuffer(sizeof(initial), initial.data(),
                                               sizeof(initial));
    auto target = context_.CreateBuffer(
        sizeof(initial), D3D12_HEAP_TYPE_DEFAULT,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_DEST);
    auto heap = context_.CreateDescriptorHeap(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, true);
    ASSERT_TRUE(upload);
    ASSERT_TRUE(target);
    ASSERT_TRUE(heap);
    context_.list()->CopyBufferRegion(target.get(), 0, upload.get(), 0,
                                      sizeof(initial));

    D3D12_SHADER_RESOURCE_VIEW_DESC unused_srv = {};
    unused_srv.Format = DXGI_FORMAT_R32_TYPELESS;
    unused_srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    unused_srv.Shader4ComponentMapping =
        D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    unused_srv.Buffer.NumElements = sizeof(initial) / sizeof(std::uint32_t);
    unused_srv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
    context_.device()->CreateShaderResourceView(
        target.get(), &unused_srv, context_.CpuDescriptorHandle(heap.get(), 0));
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
    uav.Format = format;
    uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uav.Buffer.NumElements = initial.size();
    context_.device()->CreateUnorderedAccessView(
        target.get(), nullptr, &uav,
        context_.CpuDescriptorHandle(heap.get(), 1));

    D3D12TestContext::Transition(
        context_.list(), target.get(), D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    ID3D12DescriptorHeap *heaps[] = {heap.get()};
    context_.list()->SetDescriptorHeaps(1, heaps);
    context_.list()->SetComputeRootSignature(root_.get());
    context_.list()->SetPipelineState(pipeline.get());
    context_.list()->SetComputeRootDescriptorTable(
        0, heap->GetGPUDescriptorHandleForHeapStart());
    context_.list()->Dispatch(1, 1, 1);
    D3D12TestContext::Transition(
        context_.list(), target.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_SOURCE);

    std::vector<std::uint8_t> bytes;
    ASSERT_EQ(context_.ReadbackBuffer(target.get(), sizeof(expected), &bytes),
              S_OK);
    ASSERT_EQ(bytes.size(), sizeof(expected));
    std::array<T, 4> actual = {};
    std::memcpy(actual.data(), bytes.data(), sizeof(actual));
    EXPECT_EQ(actual, expected);
  }

  void ExecuteAndExpect(
      ID3D12Resource *input, D3D12_RESOURCE_STATES input_state,
      const D3D12_SHADER_RESOURCE_VIEW_DESC &srv,
      ID3D12PipelineState *pipeline,
      const std::array<std::uint32_t, 4> &expected,
      const D3D12_SHADER_RESOURCE_VIEW_DESC *invalid_before_valid = nullptr) {
    constexpr std::array<std::uint32_t, 4> sentinel = {
        0xcdcdcdcdu, 0xcdcdcdcdu, 0xcdcdcdcdu, 0xcdcdcdcdu};
    auto output = context_.CreateBuffer(
        sizeof(expected), D3D12_HEAP_TYPE_DEFAULT,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_DEST);
    auto initial = context_.CreateUploadBuffer(sizeof(sentinel), sentinel.data(),
                                               sizeof(sentinel));
    auto heap = context_.CreateDescriptorHeap(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, true);
    ASSERT_TRUE(input);
    ASSERT_TRUE(pipeline);
    ASSERT_TRUE(output);
    ASSERT_TRUE(initial);
    ASSERT_TRUE(heap);

    context_.list()->CopyBufferRegion(output.get(), 0, initial.get(), 0,
                                      sizeof(sentinel));
    const auto srv_handle = context_.CpuDescriptorHandle(heap.get(), 0);
    if (invalid_before_valid) {
      context_.device()->CreateShaderResourceView(input, invalid_before_valid,
                                                  srv_handle);
    }
    context_.device()->CreateShaderResourceView(input, &srv, srv_handle);
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
    uav.Format = DXGI_FORMAT_R32_TYPELESS;
    uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uav.Buffer.NumElements = expected.size();
    uav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
    context_.device()->CreateUnorderedAccessView(
        output.get(), nullptr, &uav,
        context_.CpuDescriptorHandle(heap.get(), 1));

    D3D12TestContext::Transition(
        context_.list(), input, input_state,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    D3D12TestContext::Transition(
        context_.list(), output.get(), D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    ID3D12DescriptorHeap *heaps[] = {heap.get()};
    context_.list()->SetDescriptorHeaps(1, heaps);
    context_.list()->SetComputeRootSignature(root_.get());
    context_.list()->SetPipelineState(pipeline);
    context_.list()->SetComputeRootDescriptorTable(
        0, heap->GetGPUDescriptorHandleForHeapStart());
    context_.list()->Dispatch(1, 1, 1);
    D3D12TestContext::Transition(
        context_.list(), output.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_SOURCE);

    std::vector<std::uint8_t> bytes;
    ASSERT_EQ(context_.ReadbackBuffer(output.get(), sizeof(expected), &bytes),
              S_OK);
    ASSERT_EQ(bytes.size(), sizeof(expected));
    std::array<std::uint32_t, 4> actual = {};
    std::memcpy(actual.data(), bytes.data(), sizeof(actual));
    EXPECT_EQ(actual, expected);
    EXPECT_NE(actual, sentinel);
  }

  bool ExecuteLoad(const FormatLoadCase &test) {
    auto texture = CreateTexture(test.format);
    if (!texture)
      return false;
    const auto texture_desc = texture->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
    UINT rows = 0;
    UINT64 row_size = 0;
    UINT64 total_size = 0;
    context_.device()->GetCopyableFootprints(&texture_desc, 0, 1, 0,
                                             &footprint, &rows, &row_size,
                                             &total_size);
    std::vector<std::uint8_t> zero_data(
        static_cast<std::size_t>(row_size) * rows *
            footprint.Footprint.Depth,
        0);
    EXPECT_EQ(context_.UploadTextureAndReset(
                  texture.get(), zero_data.data(), row_size,
                  static_cast<UINT64>(zero_data.size())),
              S_OK);

    constexpr std::array<std::uint32_t, 4> sentinel = {
        0xcdcdcdcdu, 0xcdcdcdcdu, 0xcdcdcdcdu, 0xcdcdcdcdu};
    auto output = context_.CreateBuffer(
        sizeof(sentinel), D3D12_HEAP_TYPE_DEFAULT,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_DEST);
    auto initial = context_.CreateUploadBuffer(sizeof(sentinel),
                                               sentinel.data(),
                                               sizeof(sentinel));
    auto heap = context_.CreateDescriptorHeap(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, true);
    EXPECT_TRUE(output);
    EXPECT_TRUE(initial);
    EXPECT_TRUE(heap);
    if (!output || !initial || !heap)
      return false;
    context_.list()->CopyBufferRegion(output.get(), 0, initial.get(), 0,
                                      sizeof(sentinel));

    D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
    srv.Format = test.format;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels = 1;
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
    uav.Format = DXGI_FORMAT_R32_TYPELESS;
    uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uav.Buffer.NumElements = 4;
    uav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
    context_.device()->CreateShaderResourceView(
        texture.get(), &srv, context_.CpuDescriptorHandle(heap.get(), 0));
    context_.device()->CreateUnorderedAccessView(
        output.get(), nullptr, &uav,
        context_.CpuDescriptorHandle(heap.get(), 1));

    D3D12TestContext::Transition(
        context_.list(), texture.get(), D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    D3D12TestContext::Transition(
        context_.list(), output.get(), D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    ID3D12DescriptorHeap *heaps[] = {heap.get()};
    context_.list()->SetDescriptorHeaps(1, heaps);
    context_.list()->SetComputeRootSignature(root_.get());
    context_.list()->SetPipelineState(Pipeline(test.type).get());
    context_.list()->SetComputeRootDescriptorTable(
        0, heap->GetGPUDescriptorHandleForHeapStart());
    context_.list()->Dispatch(1, 1, 1);
    D3D12TestContext::Transition(
        context_.list(), output.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_SOURCE);
    std::vector<std::uint8_t> bytes;
    EXPECT_EQ(context_.ReadbackBuffer(output.get(), sizeof(sentinel), &bytes),
              S_OK);
    if (bytes.size() < sizeof(sentinel))
      return false;
    std::array<std::uint32_t, 4> actual = {};
    std::memcpy(actual.data(), bytes.data(), sizeof(actual));
    EXPECT_EQ(actual[0], 0u);
    EXPECT_NE(actual, sentinel);
    EXPECT_EQ(context_.ResetCommandList(), S_OK);
    return true;
  }

  D3D12TestContext context_;
  ComPtr<ID3D12RootSignature> root_;
  ComPtr<ID3D12PipelineState> float_pipeline_;
  ComPtr<ID3D12PipelineState> uint_pipeline_;
  ComPtr<ID3D12PipelineState> sint_pipeline_;
};

TEST_F(FormatExecutionMatrixSpec,
       AdvertisedTextureLoadFormatsCreateViewExecuteAndReadBack) {
  constexpr std::array cases = {
      FormatLoadCase{DXGI_FORMAT_R8_SNORM, ShaderValueType::Float, "R8Snorm"},
      FormatLoadCase{DXGI_FORMAT_R8G8_SNORM, ShaderValueType::Float,
                     "R8G8Snorm"},
      FormatLoadCase{DXGI_FORMAT_R16G16_UNORM, ShaderValueType::Float,
                     "R16G16Unorm"},
      FormatLoadCase{DXGI_FORMAT_R16G16_SNORM, ShaderValueType::Float,
                     "R16G16Snorm"},
      FormatLoadCase{DXGI_FORMAT_R8G8B8A8_SNORM, ShaderValueType::Float,
                     "Rgba8Snorm"},
      FormatLoadCase{DXGI_FORMAT_B8G8R8X8_UNORM, ShaderValueType::Float,
                     "Bgra8XUnorm"},
      FormatLoadCase{DXGI_FORMAT_R9G9B9E5_SHAREDEXP, ShaderValueType::Float,
                     "Rgb9e5"},
      FormatLoadCase{DXGI_FORMAT_R16G16B16A16_UNORM, ShaderValueType::Float,
                     "Rgba16Unorm"},
      FormatLoadCase{DXGI_FORMAT_BC1_UNORM, ShaderValueType::Float, "Bc1"},
      FormatLoadCase{DXGI_FORMAT_BC3_UNORM, ShaderValueType::Float, "Bc3"},
      FormatLoadCase{DXGI_FORMAT_BC5_UNORM, ShaderValueType::Float, "Bc5"},
      FormatLoadCase{DXGI_FORMAT_BC7_UNORM, ShaderValueType::Float, "Bc7"},
      FormatLoadCase{DXGI_FORMAT_R8G8_UINT, ShaderValueType::Uint,
                     "R8G8Uint"},
      FormatLoadCase{DXGI_FORMAT_R16_UINT, ShaderValueType::Uint, "R16Uint"},
      FormatLoadCase{DXGI_FORMAT_R16G16_UINT, ShaderValueType::Uint,
                     "R16G16Uint"},
      FormatLoadCase{DXGI_FORMAT_R16G16B16A16_UINT, ShaderValueType::Uint,
                     "Rgba16Uint"},
      FormatLoadCase{DXGI_FORMAT_R8_SINT, ShaderValueType::Sint, "R8Sint"},
      FormatLoadCase{DXGI_FORMAT_R8G8_SINT, ShaderValueType::Sint,
                     "R8G8Sint"},
      FormatLoadCase{DXGI_FORMAT_R16G16_SINT, ShaderValueType::Sint,
                     "R16G16Sint"},
      FormatLoadCase{DXGI_FORMAT_R16G16B16A16_SINT, ShaderValueType::Sint,
                     "Rgba16Sint"},
  };
  constexpr D3D12_FORMAT_SUPPORT1 required =
      D3D12_FORMAT_SUPPORT1_TEXTURE2D | D3D12_FORMAT_SUPPORT1_SHADER_LOAD;
  UINT executed = 0;
  UINT skipped = 0;
  for (const auto &test : cases) {
    SCOPED_TRACE(test.name);
    const auto support = Support(test.format);
    if ((support.Support1 & required) != required) {
      ++skipped;
      continue;
    }
    ASSERT_TRUE(ExecuteLoad(test));
    ++executed;
  }
  EXPECT_GT(executed, 0u);
  EXPECT_EQ(executed + skipped, cases.size());
}

TEST_F(FormatExecutionMatrixSpec,
       AdvertisedTexture1DLoadCreatesViewExecutesAndReadsBack) {
  constexpr DXGI_FORMAT format = DXGI_FORMAT_R32_UINT;
  constexpr D3D12_FORMAT_SUPPORT1 required =
      D3D12_FORMAT_SUPPORT1_TEXTURE1D | D3D12_FORMAT_SUPPORT1_SHADER_LOAD;
  const auto support = Support(format);
  if ((support.Support1 & required) != required)
    GTEST_SKIP() << "R32_UINT Texture1D shader loads are not advertised";

  auto pipeline = CompilePipeline(R"(
    Texture1D<uint> input : register(t0);
    RWByteAddressBuffer output : register(u0);
    [numthreads(1, 1, 1)] void main() {
      uint width, levels;
      input.GetDimensions(0, width, levels);
      output.Store4(0, uint4(input.Load(int2(2, 0)), width, levels, 0x1d));
    }
  )");
  ASSERT_TRUE(pipeline);
  auto texture = CreateTexture(D3D12_RESOURCE_DIMENSION_TEXTURE1D, 4, 1, 1,
                               format);
  ASSERT_TRUE(texture);
  constexpr std::array<std::uint32_t, 4> texels = {3, 5, 7, 11};
  ASSERT_EQ(context_.UploadTextureAndReset(texture.get(), texels.data(),
                                           sizeof(texels), sizeof(texels)),
            S_OK);

  D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
  srv.Format = format;
  srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1D;
  srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv.Texture1D.MipLevels = 1;
  ExecuteAndExpect(texture.get(), D3D12_RESOURCE_STATE_COPY_DEST, srv,
                   pipeline.get(), {7, 4, 1, 0x1d});
}

TEST_F(FormatExecutionMatrixSpec,
       AdvertisedTexture3DLoadCreatesViewExecutesAndReadsBack) {
  constexpr DXGI_FORMAT format = DXGI_FORMAT_R32_UINT;
  constexpr D3D12_FORMAT_SUPPORT1 required =
      D3D12_FORMAT_SUPPORT1_TEXTURE3D | D3D12_FORMAT_SUPPORT1_SHADER_LOAD;
  const auto support = Support(format);
  if ((support.Support1 & required) != required)
    GTEST_SKIP() << "R32_UINT Texture3D shader loads are not advertised";

  auto pipeline = CompilePipeline(R"(
    Texture3D<uint> input : register(t0);
    RWByteAddressBuffer output : register(u0);
    [numthreads(1, 1, 1)] void main() {
      uint width, height, depth, levels;
      input.GetDimensions(0, width, height, depth, levels);
      output.Store4(0, uint4(input.Load(int4(1, 0, 1, 0)), width, height,
                                  depth));
    }
  )");
  ASSERT_TRUE(pipeline);
  auto texture = CreateTexture(D3D12_RESOURCE_DIMENSION_TEXTURE3D, 2, 2, 2,
                               format);
  ASSERT_TRUE(texture);
  constexpr std::array<std::uint32_t, 8> texels = {10, 11, 12, 13,
                                                   20, 21, 22, 23};
  ASSERT_EQ(context_.UploadTextureAndReset(
                texture.get(), texels.data(), 2 * sizeof(std::uint32_t),
                4 * sizeof(std::uint32_t)),
            S_OK);

  D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
  srv.Format = format;
  srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
  srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv.Texture3D.MipLevels = 1;
  ExecuteAndExpect(texture.get(), D3D12_RESOURCE_STATE_COPY_DEST, srv,
                   pipeline.get(), {21, 2, 2, 2});
}

TEST_F(FormatExecutionMatrixSpec,
       AdvertisedTypedBufferLoadCreatesViewExecutesAndReadsBack) {
  constexpr DXGI_FORMAT format = DXGI_FORMAT_R32_UINT;
  constexpr D3D12_FORMAT_SUPPORT1 required =
      D3D12_FORMAT_SUPPORT1_BUFFER | D3D12_FORMAT_SUPPORT1_SHADER_LOAD;
  const auto support = Support(format);
  if ((support.Support1 & required) != required)
    GTEST_SKIP() << "R32_UINT typed buffer shader loads are not advertised";

  auto pipeline = CompilePipeline(R"(
    Buffer<uint> input : register(t0);
    RWByteAddressBuffer output : register(u0);
    [numthreads(1, 1, 1)] void main() {
      output.Store4(0, uint4(input[2], 4, input[0], input[3]));
    }
  )");
  ASSERT_TRUE(pipeline);
  constexpr std::array<std::uint32_t, 4> values = {
      0x10203040u, 0x11223344u, 0x55667788u, 0xa5a55a5au};
  auto upload =
      context_.CreateUploadBuffer(sizeof(values), values.data(), sizeof(values));
  auto buffer = context_.CreateBuffer(
      sizeof(values), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(upload);
  ASSERT_TRUE(buffer);
  context_.list()->CopyBufferRegion(buffer.get(), 0, upload.get(), 0,
                                    sizeof(values));

  D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
  srv.Format = format;
  srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
  srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv.Buffer.NumElements = values.size();
  ExecuteAndExpect(buffer.get(), D3D12_RESOURCE_STATE_COPY_DEST, srv,
                   pipeline.get(),
                   {0x55667788u, 4, 0x10203040u, 0xa5a55a5au});
}

TEST_F(FormatExecutionMatrixSpec,
       AdvertisedR8UintTypedUavLoadsStoresAndReadsBack) {
  constexpr DXGI_FORMAT format = DXGI_FORMAT_R8_UINT;
  const auto support = Support(format);
  constexpr D3D12_FORMAT_SUPPORT1 required1 =
      D3D12_FORMAT_SUPPORT1_BUFFER |
      D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW;
  constexpr D3D12_FORMAT_SUPPORT2 required2 =
      D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD |
      D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE;
  if ((support.Support1 & required1) != required1 ||
      (support.Support2 & required2) != required2)
    GTEST_SKIP() << "R8_UINT typed UAV load/store is not advertised";

  ExecuteTypedUav<std::uint8_t>(format, {12, 2, 3, 40}, {12, 19, 43, 40});
}

TEST_F(FormatExecutionMatrixSpec,
       AdvertisedR16UintTypedUavLoadsStoresAndReadsBack) {
  constexpr DXGI_FORMAT format = DXGI_FORMAT_R16_UINT;
  const auto support = Support(format);
  constexpr D3D12_FORMAT_SUPPORT1 required1 =
      D3D12_FORMAT_SUPPORT1_BUFFER |
      D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW;
  constexpr D3D12_FORMAT_SUPPORT2 required2 =
      D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD |
      D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE;
  if ((support.Support1 & required1) != required1 ||
      (support.Support2 & required2) != required2)
    GTEST_SKIP() << "R16_UINT typed UAV load/store is not advertised";

  ExecuteTypedUav<std::uint16_t>(format, {1000, 2, 3, 4000},
                                 {1000, 1007, 4003, 4000});
}

TEST_F(FormatExecutionMatrixSpec,
       AdvertisedR32UintTypedUavLoadsStoresAndReadsBack) {
  constexpr DXGI_FORMAT format = DXGI_FORMAT_R32_UINT;
  const auto support = Support(format);
  constexpr D3D12_FORMAT_SUPPORT1 required1 =
      D3D12_FORMAT_SUPPORT1_BUFFER |
      D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW;
  constexpr D3D12_FORMAT_SUPPORT2 required2 =
      D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD |
      D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE;
  if ((support.Support1 & required1) != required1 ||
      (support.Support2 & required2) != required2)
    GTEST_SKIP() << "R32_UINT typed UAV load/store is not advertised";

  ExecuteTypedUav<std::uint32_t>(
      format, {0x10203040u, 2, 3, 0x55667700u},
      {0x10203040u, 0x10203047u, 0x55667703u, 0x55667700u});
}

TEST_F(FormatExecutionMatrixSpec,
       AdvertisedMsaaRenderTargetDrawsResolvesAndReadsBack) {
  constexpr DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
  const auto support = Support(format);
  constexpr D3D12_FORMAT_SUPPORT1 required =
      D3D12_FORMAT_SUPPORT1_TEXTURE2D |
      D3D12_FORMAT_SUPPORT1_RENDER_TARGET |
      D3D12_FORMAT_SUPPORT1_MULTISAMPLE_RENDERTARGET |
      D3D12_FORMAT_SUPPORT1_MULTISAMPLE_RESOLVE;
  if ((support.Support1 & required) != required)
    GTEST_SKIP() << "R8G8B8A8_UNORM MSAA resolve is not advertised";

  D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS quality = {};
  quality.Format = format;
  quality.SampleCount = 4;
  ASSERT_EQ(context_.device()->CheckFeatureSupport(
                D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &quality,
                sizeof(quality)),
            S_OK);
  if (!quality.NumQualityLevels)
    GTEST_SKIP() << "4x R8G8B8A8_UNORM MSAA is not advertised";

  auto pipeline = CompileGraphicsPipeline(format, 4);
  auto source = CreateTexture2D(
      format, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
      D3D12_RESOURCE_STATE_RENDER_TARGET, 4);
  auto destination = CreateTexture2D(format, D3D12_RESOURCE_FLAG_NONE,
                                     D3D12_RESOURCE_STATE_RESOLVE_DEST);
  auto heap =
      context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
  ASSERT_TRUE(pipeline);
  ASSERT_TRUE(source);
  ASSERT_TRUE(destination);
  ASSERT_TRUE(heap);
  const auto rtv = heap->GetCPUDescriptorHandleForHeapStart();
  context_.device()->CreateRenderTargetView(source.get(), nullptr, rtv);
  constexpr FLOAT black[4] = {};
  context_.list()->ClearRenderTargetView(rtv, black, 0, nullptr);
  DrawFullscreen(pipeline.get(), rtv);
  D3D12TestContext::Transition(
      context_.list(), source.get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
      D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
  context_.list()->ResolveSubresource(destination.get(), 0, source.get(), 0,
                                      format);
  D3D12TestContext::Transition(
      context_.list(), destination.get(), D3D12_RESOURCE_STATE_RESOLVE_DEST,
      D3D12_RESOURCE_STATE_COPY_SOURCE);

  TextureReadback readback;
  ASSERT_EQ(context_.ReadbackTexture(destination.get(), &readback), S_OK);
  for (UINT y = 0; y < readback.height; ++y) {
    for (UINT x = 0; x < readback.width; ++x) {
      const auto *pixel = readback.data.data() + y * readback.row_pitch + x * 4;
      EXPECT_NEAR(pixel[0], 64, 1) << "red at (" << x << ", " << y << ")";
      EXPECT_NEAR(pixel[1], 128, 1) << "green at (" << x << ", " << y
                                    << ")";
      EXPECT_NEAR(pixel[2], 191, 1) << "blue at (" << x << ", " << y << ")";
      EXPECT_EQ(pixel[3], 255) << "alpha at (" << x << ", " << y << ")";
    }
  }
}

TEST_F(FormatExecutionMatrixSpec,
       AdvertisedRgba16FloatRenderTargetDrawsAndReadsBack) {
  constexpr DXGI_FORMAT format = DXGI_FORMAT_R16G16B16A16_FLOAT;
  const auto support = Support(format);
  constexpr D3D12_FORMAT_SUPPORT1 required =
      D3D12_FORMAT_SUPPORT1_TEXTURE2D |
      D3D12_FORMAT_SUPPORT1_RENDER_TARGET;
  if ((support.Support1 & required) != required)
    GTEST_SKIP() << "R16G16B16A16_FLOAT render targets are not advertised";

  auto pipeline = CompileGraphicsPipeline(format, 1);
  auto target = CreateTexture2D(format,
                                D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
                                D3D12_RESOURCE_STATE_RENDER_TARGET);
  auto heap =
      context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
  ASSERT_TRUE(pipeline);
  ASSERT_TRUE(target);
  ASSERT_TRUE(heap);
  const auto rtv = heap->GetCPUDescriptorHandleForHeapStart();
  D3D12_RENDER_TARGET_VIEW_DESC view = {};
  view.Format = format;
  view.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
  context_.device()->CreateRenderTargetView(target.get(), &view, rtv);
  constexpr FLOAT black[4] = {};
  context_.list()->ClearRenderTargetView(rtv, black, 0, nullptr);
  DrawFullscreen(pipeline.get(), rtv);
  D3D12TestContext::Transition(
      context_.list(), target.get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
      D3D12_RESOURCE_STATE_COPY_SOURCE);

  TextureReadback readback;
  ASSERT_EQ(context_.ReadbackTexture(target.get(), &readback), S_OK);
  constexpr std::array<std::uint16_t, 4> expected = {
      0x3400, 0x3800, 0x3a00, 0x3c00};
  for (UINT y = 0; y < readback.height; ++y) {
    for (UINT x = 0; x < readback.width; ++x) {
      std::array<std::uint16_t, 4> actual = {};
      std::memcpy(actual.data(),
                  readback.data.data() + y * readback.row_pitch + x * 8,
                  sizeof(actual));
      EXPECT_EQ(actual, expected) << "pixel (" << x << ", " << y << ")";
    }
  }
}

TEST_F(FormatExecutionMatrixSpec,
       AdvertisedD32DepthTargetTestsWritesAndReadsBack) {
  constexpr DXGI_FORMAT color_format = DXGI_FORMAT_R8G8B8A8_UNORM;
  constexpr DXGI_FORMAT depth_format = DXGI_FORMAT_D32_FLOAT;
  const auto color_support = Support(color_format);
  const auto depth_support = Support(depth_format);
  constexpr D3D12_FORMAT_SUPPORT1 color_required =
      D3D12_FORMAT_SUPPORT1_TEXTURE2D |
      D3D12_FORMAT_SUPPORT1_RENDER_TARGET;
  constexpr D3D12_FORMAT_SUPPORT1 depth_required =
      D3D12_FORMAT_SUPPORT1_TEXTURE2D |
      D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL;
  if ((color_support.Support1 & color_required) != color_required ||
      (depth_support.Support1 & depth_required) != depth_required)
    GTEST_SKIP() << "R8G8B8A8_UNORM RTV or D32_FLOAT DSV is not advertised";

  auto pipeline = CompileGraphicsPipeline(color_format, 1, depth_format);
  auto color = CreateTexture2D(color_format,
                               D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
                               D3D12_RESOURCE_STATE_RENDER_TARGET);
  auto depth = CreateTexture2D(depth_format,
                               D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL,
                               D3D12_RESOURCE_STATE_DEPTH_WRITE);
  auto rtv_heap =
      context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
  auto dsv_heap =
      context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false);
  ASSERT_TRUE(pipeline);
  ASSERT_TRUE(color);
  ASSERT_TRUE(depth);
  ASSERT_TRUE(rtv_heap);
  ASSERT_TRUE(dsv_heap);
  const auto rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
  const auto dsv = dsv_heap->GetCPUDescriptorHandleForHeapStart();
  context_.device()->CreateRenderTargetView(color.get(), nullptr, rtv);
  D3D12_DEPTH_STENCIL_VIEW_DESC view = {};
  view.Format = depth_format;
  view.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
  context_.device()->CreateDepthStencilView(depth.get(), &view, dsv);
  constexpr FLOAT black[4] = {};
  context_.list()->ClearRenderTargetView(rtv, black, 0, nullptr);
  context_.list()->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0,
                                         0, nullptr);
  DrawFullscreen(pipeline.get(), rtv, &dsv);
  D3D12TestContext::Transition(
      context_.list(), color.get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
      D3D12_RESOURCE_STATE_COPY_SOURCE);
  D3D12TestContext::Transition(
      context_.list(), depth.get(), D3D12_RESOURCE_STATE_DEPTH_WRITE,
      D3D12_RESOURCE_STATE_COPY_SOURCE);

  TextureReadback color_readback;
  ASSERT_EQ(context_.ReadbackTexture(color.get(), &color_readback), S_OK);
  for (UINT y = 0; y < color_readback.height; ++y) {
    for (UINT x = 0; x < color_readback.width; ++x) {
      const auto *pixel = color_readback.data.data() +
                          y * color_readback.row_pitch + x * 4;
      EXPECT_NEAR(pixel[0], 64, 1);
      EXPECT_NEAR(pixel[1], 128, 1);
      EXPECT_NEAR(pixel[2], 191, 1);
      EXPECT_EQ(pixel[3], 255);
    }
  }
  ASSERT_EQ(context_.ResetCommandList(), S_OK);
  TextureReadback depth_readback;
  ASSERT_EQ(context_.ReadbackTexture(depth.get(), &depth_readback), S_OK);
  for (UINT y = 0; y < depth_readback.height; ++y) {
    for (UINT x = 0; x < depth_readback.width; ++x) {
      float actual = 0.0f;
      std::memcpy(&actual,
                  depth_readback.data.data() + y * depth_readback.row_pitch +
                      x * sizeof(actual),
                  sizeof(actual));
      EXPECT_NEAR(actual, 0.25f, 1.0e-6f)
          << "depth at (" << x << ", " << y << ")";
    }
  }
}

TEST_F(FormatExecutionMatrixSpec,
       AdvertisedTextureCubeSampleCreatesViewExecutesAndReadsBack) {
  constexpr DXGI_FORMAT format = DXGI_FORMAT_R32_FLOAT;
  constexpr D3D12_FORMAT_SUPPORT1 required =
      D3D12_FORMAT_SUPPORT1_TEXTURECUBE |
      D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE;
  const auto support = Support(format);
  if ((support.Support1 & required) != required)
    GTEST_SKIP() << "R32_FLOAT TextureCube sampling is not advertised";

  auto pipeline = CompilePipeline(R"(
    TextureCube<float> input : register(t0);
    SamplerState point_sampler : register(s0);
    RWByteAddressBuffer output : register(u0);
    [numthreads(1, 1, 1)] void main() {
      uint width, height, levels;
      input.GetDimensions(0, width, height, levels);
      float value = input.SampleLevel(point_sampler, float3(0, 0, 1), 0);
      output.Store4(0, uint4(asuint(value), width, height, levels));
    }
  )");
  ASSERT_TRUE(pipeline);
  auto texture = CreateTexture(D3D12_RESOURCE_DIMENSION_TEXTURE2D, 2, 2, 6,
                               format);
  ASSERT_TRUE(texture);
  for (UINT face = 0; face < 6; ++face) {
    const std::array<float, 4> texels = {
        static_cast<float>(face + 1), static_cast<float>(face + 1),
        static_cast<float>(face + 1), static_cast<float>(face + 1)};
    ASSERT_EQ(context_.UploadTextureAndReset(
                  texture.get(), texels.data(), 2 * sizeof(float),
                  4 * sizeof(float), face),
              S_OK)
        << "face=" << face;
  }

  D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
  srv.Format = format;
  srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
  srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv.TextureCube.MipLevels = 1;
  ExecuteAndExpect(texture.get(), D3D12_RESOURCE_STATE_COPY_DEST, srv,
                   pipeline.get(), {0x40a00000u, 2, 2, 1});
}

TEST_F(FormatExecutionMatrixSpec,
       InvalidViewDoesNotMutateResourceAndValidRewriteExecutes) {
  constexpr DXGI_FORMAT format = DXGI_FORMAT_R32_UINT;
  constexpr D3D12_FORMAT_SUPPORT1 required =
      D3D12_FORMAT_SUPPORT1_TEXTURE2D | D3D12_FORMAT_SUPPORT1_SHADER_LOAD;
  const auto support = Support(format);
  if ((support.Support1 & required) != required)
    GTEST_SKIP() << "R32_UINT Texture2D shader loads are not advertised";

  auto texture = CreateTexture(format);
  ASSERT_TRUE(texture);
  std::array<std::uint32_t, 16> texels = {};
  texels[0] = 0x12345678u;
  ASSERT_EQ(context_.UploadTextureAndReset(
                texture.get(), texels.data(), 4 * sizeof(std::uint32_t),
                texels.size() * sizeof(std::uint32_t)),
            S_OK);

  D3D12_SHADER_RESOURCE_VIEW_DESC valid = {};
  valid.Format = format;
  valid.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  valid.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  valid.Texture2D.MipLevels = 1;
  D3D12_SHADER_RESOURCE_VIEW_DESC invalid = {};
  invalid.Format = format;
  invalid.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
  invalid.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  invalid.Buffer.NumElements = texels.size();
  ExecuteAndExpect(texture.get(), D3D12_RESOURCE_STATE_COPY_DEST, valid,
                   Pipeline(ShaderValueType::Uint).get(),
                   {0x12345678u, 0, 0, 1}, &invalid);
}

TEST_F(FormatExecutionMatrixSpec,
       UnadvertisedMsaaConfigurationRejectsAndClearsOutput) {
  constexpr DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
  const auto support = Support(format);
  if (!(support.Support1 & D3D12_FORMAT_SUPPORT1_RENDER_TARGET))
    GTEST_SKIP() << "R8G8B8A8_UNORM render targets are not advertised";

  UINT unsupported_sample_count = 0;
  for (const UINT sample_count : {2u, 4u, 8u, 16u}) {
    D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS quality = {};
    quality.Format = format;
    quality.SampleCount = sample_count;
    ASSERT_EQ(context_.device()->CheckFeatureSupport(
                  D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &quality,
                  sizeof(quality)),
              S_OK);
    if (!quality.NumQualityLevels) {
      unsupported_sample_count = sample_count;
      break;
    }
  }
  if (!unsupported_sample_count)
    GTEST_SKIP() << "all tested MSAA sample counts are advertised";

  D3D12_HEAP_PROPERTIES heap = {};
  heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width = 4;
  desc.Height = 4;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.Format = format;
  desc.SampleDesc.Count = unsupported_sample_count;
  desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  void *output = reinterpret_cast<void *>(std::uintptr_t{1});
  EXPECT_TRUE(FAILED(context_.device()->CreateCommittedResource(
      &heap, D3D12_HEAP_FLAG_NONE, &desc,
      D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, __uuidof(ID3D12Resource),
      &output)));
  EXPECT_EQ(output, nullptr);
}

TEST_F(FormatExecutionMatrixSpec,
       UnadvertisedTextureFormatRejectsCreationAndClearsOutput) {
  constexpr DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
  const auto support = Support(format);
  ASSERT_EQ(support.Support1 & D3D12_FORMAT_SUPPORT1_TEXTURE1D, 0u);

  D3D12_HEAP_PROPERTIES heap = {};
  heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE1D;
  desc.Width = 4;
  desc.Height = 1;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.Format = format;
  desc.SampleDesc.Count = 1;
  void *output = reinterpret_cast<void *>(std::uintptr_t{1});
  EXPECT_EQ(context_.device()->CreateCommittedResource(
                &heap, D3D12_HEAP_FLAG_NONE, &desc,
                D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                __uuidof(ID3D12Resource), &output),
            E_INVALIDARG);
  EXPECT_EQ(output, nullptr);
}

} // namespace
