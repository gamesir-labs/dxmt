#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;
using dxmt::test::TextureReadback;

class TextureViewDimensionSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);

    D3D12_DESCRIPTOR_RANGE srv_ranges[2] = {};
    srv_ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srv_ranges[0].NumDescriptors = 1;
    srv_ranges[0].BaseShaderRegister = 0;
    srv_ranges[0].OffsetInDescriptorsFromTableStart = 0;
    srv_ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    srv_ranges[1].NumDescriptors = 1;
    srv_ranges[1].BaseShaderRegister = 0;
    srv_ranges[1].OffsetInDescriptorsFromTableStart = 1;
    D3D12_ROOT_PARAMETER srv_parameter = {};
    srv_parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    srv_parameter.DescriptorTable.NumDescriptorRanges = 2;
    srv_parameter.DescriptorTable.pDescriptorRanges = srv_ranges;
    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MaxAnisotropy = 1;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_ROOT_SIGNATURE_DESC srv_root_desc = {};
    srv_root_desc.NumParameters = 1;
    srv_root_desc.pParameters = &srv_parameter;
    srv_root_desc.NumStaticSamplers = 1;
    srv_root_desc.pStaticSamplers = &sampler;
    srv_root_ = context_.CreateRootSignature(srv_root_desc);
    ASSERT_TRUE(srv_root_);

    D3D12_DESCRIPTOR_RANGE uav_range = {};
    uav_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    uav_range.NumDescriptors = 1;
    uav_range.BaseShaderRegister = 0;
    D3D12_ROOT_PARAMETER uav_parameter = {};
    uav_parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    uav_parameter.DescriptorTable.NumDescriptorRanges = 1;
    uav_parameter.DescriptorTable.pDescriptorRanges = &uav_range;
    D3D12_ROOT_SIGNATURE_DESC uav_root_desc = {};
    uav_root_desc.NumParameters = 1;
    uav_root_desc.pParameters = &uav_parameter;
    uav_root_ = context_.CreateRootSignature(uav_root_desc);
    ASSERT_TRUE(uav_root_);
  }

  ComPtr<ID3D12Resource>
  CreateTexture(D3D12_RESOURCE_DIMENSION dimension, UINT64 width, UINT height,
                UINT16 depth_or_array_size, DXGI_FORMAT format,
                D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state,
                UINT sample_count = 1) {
    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = dimension;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = depth_or_array_size;
    desc.MipLevels = 1;
    desc.Format = format;
    desc.SampleDesc.Count = sample_count;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = flags;
    ComPtr<ID3D12Resource> texture;
    EXPECT_EQ(context_.device()->CreateCommittedResource(
                  &heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr,
                  IID_PPV_ARGS(texture.put())),
              S_OK);
    return texture;
  }

  ComPtr<ID3D12PipelineState>
  CompilePipeline(ID3D12RootSignature *root, const char *source) {
    const auto shader = CompileShader(source, "cs_5_0");
    EXPECT_EQ(shader.result, S_OK) << shader.diagnostic_text();
    if (shader.result != S_OK)
      return {};
    const D3D12_SHADER_BYTECODE bytecode = {
        shader.bytecode->GetBufferPointer(), shader.bytecode->GetBufferSize()};
    auto pipeline = context_.CreateComputePipeline(root, bytecode);
    EXPECT_TRUE(pipeline);
    return pipeline;
  }

  void ExecuteSrv(ID3D12Resource *texture, D3D12_RESOURCE_STATES before,
                  const D3D12_SHADER_RESOURCE_VIEW_DESC &srv,
                  ID3D12PipelineState *pipeline,
                  const std::array<UINT, 4> &expected) {
    constexpr std::array<UINT, 4> poison = {
        0xcdcdcdcdu, 0xcdcdcdcdu, 0xcdcdcdcdu, 0xcdcdcdcdu};
    auto initial = context_.CreateUploadBuffer(sizeof(poison), poison.data(),
                                                sizeof(poison));
    auto output = context_.CreateBuffer(
        sizeof(expected), D3D12_HEAP_TYPE_DEFAULT,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_DEST);
    auto heap = context_.CreateDescriptorHeap(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, true);
    ASSERT_TRUE(initial);
    ASSERT_TRUE(output);
    ASSERT_TRUE(heap);
    context_.list()->CopyBufferRegion(output.get(), 0, initial.get(), 0,
                                      sizeof(poison));
    context_.device()->CreateShaderResourceView(
        texture, &srv, context_.CpuDescriptorHandle(heap.get(), 0));
    D3D12_UNORDERED_ACCESS_VIEW_DESC output_uav = {};
    output_uav.Format = DXGI_FORMAT_R32_TYPELESS;
    output_uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    output_uav.Buffer.NumElements = expected.size();
    output_uav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
    context_.device()->CreateUnorderedAccessView(
        output.get(), nullptr, &output_uav,
        context_.CpuDescriptorHandle(heap.get(), 1));

    D3D12TestContext::Transition(
        context_.list(), texture, before,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    D3D12TestContext::Transition(
        context_.list(), output.get(), D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    ID3D12DescriptorHeap *heaps[] = {heap.get()};
    context_.list()->SetDescriptorHeaps(1, heaps);
    context_.list()->SetComputeRootSignature(srv_root_.get());
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
    std::array<UINT, 4> actual = {};
    std::memcpy(actual.data(), bytes.data(), sizeof(actual));
    EXPECT_EQ(actual, expected);
  }

  void UploadUintSubresources(ID3D12Resource *texture, UINT subresource_count,
                              UINT width, UINT height, UINT depth,
                              UINT base_value) {
    std::vector<UINT> values(width * height * depth);
    for (UINT subresource = 0; subresource < subresource_count; ++subresource) {
      for (UINT i = 0; i < values.size(); ++i)
        values[i] = base_value + subresource * 0x100u + i;
      ASSERT_EQ(context_.UploadTextureAndReset(
                    texture, values.data(), width * sizeof(UINT),
                    width * height * sizeof(UINT), subresource),
                S_OK)
          << "subresource=" << subresource;
    }
  }

  void UploadZeroSubresources(ID3D12Resource *texture, UINT subresource_count,
                              UINT width, UINT height, UINT depth) {
    const std::vector<UINT> zeros(width * height * depth, 0);
    for (UINT subresource = 0; subresource < subresource_count; ++subresource) {
      ASSERT_EQ(context_.UploadTextureAndReset(
                    texture, zeros.data(), width * sizeof(UINT),
                    width * height * sizeof(UINT), subresource),
                S_OK)
          << "subresource=" << subresource;
    }
  }

  void DispatchUav(ID3D12Resource *texture,
                   const D3D12_UNORDERED_ACCESS_VIEW_DESC &uav,
                   ID3D12PipelineState *pipeline) {
    auto heap = context_.CreateDescriptorHeap(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true);
    ASSERT_TRUE(heap);
    context_.device()->CreateUnorderedAccessView(
        texture, nullptr, &uav, heap->GetCPUDescriptorHandleForHeapStart());
    D3D12TestContext::Transition(
        context_.list(), texture, D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    ID3D12DescriptorHeap *heaps[] = {heap.get()};
    context_.list()->SetDescriptorHeaps(1, heaps);
    context_.list()->SetComputeRootSignature(uav_root_.get());
    context_.list()->SetPipelineState(pipeline);
    context_.list()->SetComputeRootDescriptorTable(
        0, heap->GetGPUDescriptorHandleForHeapStart());
    context_.list()->Dispatch(1, 1, 1);
    D3D12TestContext::Transition(
        context_.list(), texture, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_SOURCE);
    ASSERT_EQ(context_.ExecuteAndWait(), S_OK);
    ASSERT_EQ(context_.ResetCommandList(), S_OK);
  }

  UINT ReadUint(ID3D12Resource *texture, UINT subresource, UINT x, UINT y,
                UINT z = 0) {
    TextureReadback readback;
    EXPECT_EQ(context_.ReadbackTexture(texture, &readback, subresource), S_OK);
    if (readback.data.empty())
      return 0;
    const std::size_t offset =
        std::size_t(z) * readback.row_pitch * readback.height +
        std::size_t(y) * readback.row_pitch + std::size_t(x) * sizeof(UINT);
    EXPECT_LE(offset + sizeof(UINT), readback.data.size());
    UINT value = 0;
    if (offset + sizeof(UINT) <= readback.data.size())
      std::memcpy(&value, readback.data.data() + offset, sizeof(value));
    EXPECT_EQ(context_.ResetCommandList(), S_OK);
    return value;
  }

  bool SupportsFourSampleR32Float() {
    D3D12_FEATURE_DATA_FORMAT_SUPPORT support = {};
    support.Format = DXGI_FORMAT_R32_FLOAT;
    if (context_.device()->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT,
                                                &support,
                                                sizeof(support)) != S_OK)
      return false;
    const D3D12_FORMAT_SUPPORT1 required =
        D3D12_FORMAT_SUPPORT1_TEXTURE2D |
        D3D12_FORMAT_SUPPORT1_SHADER_LOAD |
        D3D12_FORMAT_SUPPORT1_RENDER_TARGET |
        D3D12_FORMAT_SUPPORT1_MULTISAMPLE_RENDERTARGET |
        D3D12_FORMAT_SUPPORT1_MULTISAMPLE_LOAD;
    if ((support.Support1 & required) != required)
      return false;
    D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS quality = {};
    quality.Format = DXGI_FORMAT_R32_FLOAT;
    quality.SampleCount = 4;
    return context_.device()->CheckFeatureSupport(
               D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &quality,
               sizeof(quality)) == S_OK &&
           quality.NumQualityLevels != 0;
  }

  D3D12TestContext context_;
  ComPtr<ID3D12RootSignature> srv_root_;
  ComPtr<ID3D12RootSignature> uav_root_;
};

TEST_F(TextureViewDimensionSpec, Texture1DArraySelectsViewRelativeSlices) {
  auto texture = CreateTexture(
      D3D12_RESOURCE_DIMENSION_TEXTURE1D, 4, 1, 3, DXGI_FORMAT_R32_UINT,
      D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(texture);
  ASSERT_NO_FATAL_FAILURE(UploadUintSubresources(texture.get(), 3, 4, 1, 1,
                                                  0x1000u));
  auto pipeline = CompilePipeline(srv_root_.get(), R"(
    Texture1DArray<uint> input : register(t0);
    RWByteAddressBuffer output : register(u0);
    [numthreads(1, 1, 1)] void main() {
      output.Store4(0, uint4(input.Load(int3(2, 0, 0)),
                            input.Load(int3(1, 1, 0)), 0x1da, 0x1db));
    }
  )");
  ASSERT_TRUE(pipeline);
  D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
  srv.Format = DXGI_FORMAT_R32_UINT;
  srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1DARRAY;
  srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv.Texture1DArray.MipLevels = 1;
  srv.Texture1DArray.FirstArraySlice = 1;
  srv.Texture1DArray.ArraySize = 2;
  ExecuteSrv(texture.get(), D3D12_RESOURCE_STATE_COPY_DEST, srv,
             pipeline.get(), {0x1102u, 0x1201u, 0x1dau, 0x1dbu});
}

TEST_F(TextureViewDimensionSpec, Texture2DArraySelectsViewRelativeSlices) {
  auto texture = CreateTexture(
      D3D12_RESOURCE_DIMENSION_TEXTURE2D, 3, 2, 3, DXGI_FORMAT_R32_UINT,
      D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(texture);
  ASSERT_NO_FATAL_FAILURE(UploadUintSubresources(texture.get(), 3, 3, 2, 1,
                                                  0x2000u));
  auto pipeline = CompilePipeline(srv_root_.get(), R"(
    Texture2DArray<uint> input : register(t0);
    RWByteAddressBuffer output : register(u0);
    [numthreads(1, 1, 1)] void main() {
      output.Store4(0, uint4(input.Load(int4(2, 1, 0, 0)),
                            input.Load(int4(1, 0, 1, 0)), 0x2da, 0x2db));
    }
  )");
  ASSERT_TRUE(pipeline);
  D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
  srv.Format = DXGI_FORMAT_R32_UINT;
  srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
  srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv.Texture2DArray.MipLevels = 1;
  srv.Texture2DArray.FirstArraySlice = 1;
  srv.Texture2DArray.ArraySize = 2;
  ExecuteSrv(texture.get(), D3D12_RESOURCE_STATE_COPY_DEST, srv,
             pipeline.get(), {0x2105u, 0x2201u, 0x2dau, 0x2dbu});
}

TEST_F(TextureViewDimensionSpec, TextureCubeArraySelectsWholeCubeRange) {
  auto texture = CreateTexture(
      D3D12_RESOURCE_DIMENSION_TEXTURE2D, 2, 2, 18, DXGI_FORMAT_R32_FLOAT,
      D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(texture);
  for (UINT face = 0; face < 18; ++face) {
    const std::array<float, 4> values = {
        float(100 + face), float(100 + face), float(100 + face),
        float(100 + face)};
    ASSERT_EQ(context_.UploadTextureAndReset(
                  texture.get(), values.data(), 2 * sizeof(float),
                  4 * sizeof(float), face),
              S_OK)
        << "face=" << face;
  }
  auto pipeline = CompilePipeline(srv_root_.get(), R"(
    TextureCubeArray<float> input : register(t0);
    SamplerState point_sampler : register(s0);
    RWByteAddressBuffer output : register(u0);
    [numthreads(1, 1, 1)] void main() {
      float first = input.SampleLevel(point_sampler, float4(0, 0, 1, 0), 0);
      float second = input.SampleLevel(point_sampler, float4(0, 0, 1, 1), 0);
      output.Store4(0, uint4(asuint(first), asuint(second), 0xcaba, 0xcabb));
    }
  )");
  ASSERT_TRUE(pipeline);
  D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
  srv.Format = DXGI_FORMAT_R32_FLOAT;
  srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
  srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv.TextureCubeArray.MipLevels = 1;
  srv.TextureCubeArray.First2DArrayFace = 6;
  srv.TextureCubeArray.NumCubes = 2;
  UINT first = 0;
  UINT second = 0;
  const float first_float = 110.0f;
  const float second_float = 116.0f;
  std::memcpy(&first, &first_float, sizeof(first));
  std::memcpy(&second, &second_float, sizeof(second));
  ExecuteSrv(texture.get(), D3D12_RESOURCE_STATE_COPY_DEST, srv,
             pipeline.get(), {first, second, 0xcabau, 0xcabbu});
}

TEST_F(TextureViewDimensionSpec, Texture2DMSLoadsDeclaredSample) {
  if (!SupportsFourSampleR32Float())
    GTEST_SKIP() << "4x R32_FLOAT render-target shader loads are unsupported";
  auto texture = CreateTexture(
      D3D12_RESOURCE_DIMENSION_TEXTURE2D, 2, 2, 1, DXGI_FORMAT_R32_FLOAT,
      D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
      D3D12_RESOURCE_STATE_RENDER_TARGET, 4);
  auto rtv_heap =
      context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
  ASSERT_TRUE(texture);
  ASSERT_TRUE(rtv_heap);
  D3D12_RENDER_TARGET_VIEW_DESC rtv = {};
  rtv.Format = DXGI_FORMAT_R32_FLOAT;
  rtv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMS;
  context_.device()->CreateRenderTargetView(
      texture.get(), &rtv, rtv_heap->GetCPUDescriptorHandleForHeapStart());
  constexpr FLOAT clear[4] = {5.0f, 0.0f, 0.0f, 0.0f};
  context_.list()->ClearRenderTargetView(
      rtv_heap->GetCPUDescriptorHandleForHeapStart(), clear, 0, nullptr);
  auto pipeline = CompilePipeline(srv_root_.get(), R"(
    Texture2DMS<float> input : register(t0);
    RWByteAddressBuffer output : register(u0);
    [numthreads(1, 1, 1)] void main() {
      output.Store4(0, uint4(asuint(input.Load(int2(0, 0), 0)),
                            asuint(input.Load(int2(1, 1), 3)), 0x2d5, 0x2d6));
    }
  )");
  ASSERT_TRUE(pipeline);
  D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
  srv.Format = DXGI_FORMAT_R32_FLOAT;
  srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
  srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  ExecuteSrv(texture.get(), D3D12_RESOURCE_STATE_RENDER_TARGET, srv,
             pipeline.get(), {0x40a00000u, 0x40a00000u, 0x2d5u, 0x2d6u});
}

TEST_F(TextureViewDimensionSpec, Texture2DMSArraySelectsViewRelativeSlices) {
  if (!SupportsFourSampleR32Float())
    GTEST_SKIP() << "4x R32_FLOAT render-target shader loads are unsupported";
  auto texture = CreateTexture(
      D3D12_RESOURCE_DIMENSION_TEXTURE2D, 2, 2, 3, DXGI_FORMAT_R32_FLOAT,
      D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
      D3D12_RESOURCE_STATE_RENDER_TARGET, 4);
  auto rtv_heap =
      context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
  ASSERT_TRUE(texture);
  ASSERT_TRUE(rtv_heap);
  for (UINT slice = 0; slice < 3; ++slice) {
    D3D12_RENDER_TARGET_VIEW_DESC rtv = {};
    rtv.Format = DXGI_FORMAT_R32_FLOAT;
    rtv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY;
    rtv.Texture2DMSArray.FirstArraySlice = slice;
    rtv.Texture2DMSArray.ArraySize = 1;
    const auto handle = rtv_heap->GetCPUDescriptorHandleForHeapStart();
    context_.device()->CreateRenderTargetView(texture.get(), &rtv, handle);
    const FLOAT clear[4] = {float(7 + slice), 0.0f, 0.0f, 0.0f};
    context_.list()->ClearRenderTargetView(handle, clear, 0, nullptr);
  }
  auto pipeline = CompilePipeline(srv_root_.get(), R"(
    Texture2DMSArray<float> input : register(t0);
    RWByteAddressBuffer output : register(u0);
    [numthreads(1, 1, 1)] void main() {
      output.Store4(0, uint4(asuint(input.Load(int3(0, 0, 0), 0)),
                            asuint(input.Load(int3(1, 1, 1), 3)), 0x2a5, 0x2a6));
    }
  )");
  ASSERT_TRUE(pipeline);
  D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
  srv.Format = DXGI_FORMAT_R32_FLOAT;
  srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY;
  srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv.Texture2DMSArray.FirstArraySlice = 1;
  srv.Texture2DMSArray.ArraySize = 2;
  ExecuteSrv(texture.get(), D3D12_RESOURCE_STATE_RENDER_TARGET, srv,
             pipeline.get(), {0x41000000u, 0x41100000u, 0x2a5u, 0x2a6u});
}

TEST_F(TextureViewDimensionSpec, Texture1DUavWritesSelectedTexel) {
  auto texture = CreateTexture(
      D3D12_RESOURCE_DIMENSION_TEXTURE1D, 4, 1, 1, DXGI_FORMAT_R32_UINT,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(texture);
  ASSERT_NO_FATAL_FAILURE(
      UploadZeroSubresources(texture.get(), 1, 4, 1, 1));
  auto pipeline = CompilePipeline(uav_root_.get(), R"(
    RWTexture1D<uint> target : register(u0);
    [numthreads(1, 1, 1)] void main() { target[2] = 0x1d1d0001; }
  )");
  ASSERT_TRUE(pipeline);
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
  uav.Format = DXGI_FORMAT_R32_UINT;
  uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1D;
  DispatchUav(texture.get(), uav, pipeline.get());
  EXPECT_EQ(ReadUint(texture.get(), 0, 2, 0), 0x1d1d0001u);
}

TEST_F(TextureViewDimensionSpec, Texture2DUavWritesSelectedTexel) {
  auto texture = CreateTexture(
      D3D12_RESOURCE_DIMENSION_TEXTURE2D, 3, 2, 1, DXGI_FORMAT_R32_UINT,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(texture);
  ASSERT_NO_FATAL_FAILURE(
      UploadZeroSubresources(texture.get(), 1, 3, 2, 1));
  auto pipeline = CompilePipeline(uav_root_.get(), R"(
    RWTexture2D<uint> target : register(u0);
    [numthreads(1, 1, 1)] void main() { target[uint2(2, 1)] = 0x2d2d0002; }
  )");
  ASSERT_TRUE(pipeline);
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
  uav.Format = DXGI_FORMAT_R32_UINT;
  uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
  DispatchUav(texture.get(), uav, pipeline.get());
  EXPECT_EQ(ReadUint(texture.get(), 0, 2, 1), 0x2d2d0002u);
}

TEST_F(TextureViewDimensionSpec, Texture2DArrayUavSelectsViewRelativeSlices) {
  auto texture = CreateTexture(
      D3D12_RESOURCE_DIMENSION_TEXTURE2D, 2, 2, 3, DXGI_FORMAT_R32_UINT,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(texture);
  ASSERT_NO_FATAL_FAILURE(
      UploadZeroSubresources(texture.get(), 3, 2, 2, 1));
  auto pipeline = CompilePipeline(uav_root_.get(), R"(
    RWTexture2DArray<uint> target : register(u0);
    [numthreads(1, 1, 1)] void main() {
      target[uint3(1, 0, 0)] = 0x2da00001;
      target[uint3(0, 1, 1)] = 0x2da00002;
    }
  )");
  ASSERT_TRUE(pipeline);
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
  uav.Format = DXGI_FORMAT_R32_UINT;
  uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
  uav.Texture2DArray.FirstArraySlice = 1;
  uav.Texture2DArray.ArraySize = 2;
  DispatchUav(texture.get(), uav, pipeline.get());
  EXPECT_EQ(ReadUint(texture.get(), 0, 1, 0), 0u);
  EXPECT_EQ(ReadUint(texture.get(), 1, 1, 0), 0x2da00001u);
  EXPECT_EQ(ReadUint(texture.get(), 2, 0, 1), 0x2da00002u);
}

TEST_F(TextureViewDimensionSpec, Texture3DUavWritesFirstAndLastDepthSlices) {
  auto texture = CreateTexture(
      D3D12_RESOURCE_DIMENSION_TEXTURE3D, 2, 2, 4, DXGI_FORMAT_R32_UINT,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(texture);
  ASSERT_NO_FATAL_FAILURE(
      UploadZeroSubresources(texture.get(), 1, 2, 2, 4));
  auto pipeline = CompilePipeline(uav_root_.get(), R"(
    RWTexture3D<uint> target : register(u0);
    [numthreads(1, 1, 1)] void main() {
      target[uint3(1, 0, 0)] = 0x3d300001;
      target[uint3(0, 1, 3)] = 0x3d300004;
    }
  )");
  ASSERT_TRUE(pipeline);
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
  uav.Format = DXGI_FORMAT_R32_UINT;
  uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
  uav.Texture3D.WSize = 4;
  DispatchUav(texture.get(), uav, pipeline.get());
  EXPECT_EQ(ReadUint(texture.get(), 0, 1, 0, 0), 0x3d300001u);
  EXPECT_EQ(ReadUint(texture.get(), 0, 0, 1, 3), 0x3d300004u);
}

} // namespace
