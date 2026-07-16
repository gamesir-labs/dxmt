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

} // namespace
