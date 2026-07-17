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

struct PlanarCase {
  DXGI_FORMAT format;
  DXGI_FORMAT plane0_format;
  DXGI_FORMAT plane1_format;
  const char *name;
};

constexpr std::array kPlanarCases = {
    PlanarCase{DXGI_FORMAT_NV12, DXGI_FORMAT_R8_TYPELESS,
               DXGI_FORMAT_R8G8_TYPELESS, "NV12"},
    PlanarCase{DXGI_FORMAT_P010, DXGI_FORMAT_R16_TYPELESS,
               DXGI_FORMAT_R16G16_TYPELESS, "P010"},
    PlanarCase{DXGI_FORMAT_P016, DXGI_FORMAT_R16_TYPELESS,
               DXGI_FORMAT_R16G16_TYPELESS, "P016"},
};

class PlanarFormatSpec : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  D3D12_FEATURE_DATA_FORMAT_SUPPORT Support(DXGI_FORMAT format) {
    D3D12_FEATURE_DATA_FORMAT_SUPPORT support = {};
    support.Format = format;
    EXPECT_EQ(context_.device()->CheckFeatureSupport(
                  D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support)),
              S_OK);
    return support;
  }

  D3D12_RESOURCE_DESC Desc(DXGI_FORMAT format) {
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = 8;
    desc.Height = 4;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    return desc;
  }

  ComPtr<ID3D12Resource> CreateTexture(DXGI_FORMAT format) {
    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    const auto desc = Desc(format);
    ComPtr<ID3D12Resource> texture;
    EXPECT_EQ(context_.device()->CreateCommittedResource(
                  &heap, D3D12_HEAP_FLAG_NONE, &desc,
                  D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                  IID_PPV_ARGS(texture.put())),
              S_OK);
    return texture;
  }

  void TransitionSubresource(ID3D12Resource *resource, UINT subresource,
                             D3D12_RESOURCE_STATES before,
                             D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.Subresource = subresource;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    context_.list()->ResourceBarrier(1, &barrier);
  }

  D3D12TestContext context_;
};

TEST_F(PlanarFormatSpec, AdvertisedVideoFormatsExposeTwoPlaneFootprints) {
  UINT executed = 0;
  for (const auto &test : kPlanarCases) {
    SCOPED_TRACE(test.name);
    if (!(Support(test.format).Support1 & D3D12_FORMAT_SUPPORT1_TEXTURE2D))
      continue;

    D3D12_FEATURE_DATA_FORMAT_INFO info = {};
    info.Format = test.format;
    ASSERT_EQ(context_.device()->CheckFeatureSupport(D3D12_FEATURE_FORMAT_INFO,
                                                     &info, sizeof(info)),
              S_OK);
    EXPECT_EQ(info.PlaneCount, 2u);

    const auto desc = Desc(test.format);
    std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT, 2> footprints = {};
    std::array<UINT, 2> rows = {};
    std::array<UINT64, 2> row_sizes = {};
    UINT64 total_size = 0;
    context_.device()->GetCopyableFootprints(&desc, 0, footprints.size(), 0,
                                             footprints.data(), rows.data(),
                                             row_sizes.data(), &total_size);
    EXPECT_EQ(footprints[0].Footprint.Format, test.plane0_format);
    EXPECT_EQ(footprints[0].Footprint.Width, 8u);
    EXPECT_EQ(footprints[0].Footprint.Height, 4u);
    EXPECT_EQ(footprints[1].Footprint.Format, test.plane1_format);
    EXPECT_EQ(footprints[1].Footprint.Width, 4u);
    EXPECT_EQ(footprints[1].Footprint.Height, 2u);
    EXPECT_EQ(rows[0], 4u);
    EXPECT_EQ(rows[1], 2u);
    EXPECT_GT(row_sizes[0], 0u);
    EXPECT_GT(row_sizes[1], 0u);
    EXPECT_EQ(footprints[0].Offset % D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT,
              0u);
    EXPECT_EQ(footprints[1].Offset % D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT,
              0u);
    EXPECT_GT(total_size, footprints[1].Offset);
    ++executed;
  }
  EXPECT_GT(executed, 0u);
}

TEST_F(PlanarFormatSpec, PlaneCopiesRoundTripWithIndependentSubresourceState) {
  UINT executed = 0;
  for (const auto &test : kPlanarCases) {
    SCOPED_TRACE(test.name);
    if (!(Support(test.format).Support1 & D3D12_FORMAT_SUPPORT1_TEXTURE2D))
      continue;
    auto texture = CreateTexture(test.format);
    ASSERT_TRUE(texture);
    const auto desc = texture->GetDesc();

    std::array<UINT, 2> rows = {};
    std::array<UINT64, 2> row_sizes = {};
    context_.device()->GetCopyableFootprints(
        &desc, 0, 2, 0, nullptr, rows.data(), row_sizes.data(), nullptr);
    std::array<std::vector<std::uint8_t>, 2> expected;
    for (UINT plane = 0; plane < 2; ++plane) {
      expected[plane].resize(
          static_cast<std::size_t>(rows[plane] * row_sizes[plane]));
      for (std::size_t index = 0; index < expected[plane].size(); ++index)
        expected[plane][index] = static_cast<std::uint8_t>(
            0x21u + plane * 0x40u + (index * 17u) % 0x3du);
      ASSERT_EQ(context_.UploadTextureAndReset(
                    texture.get(), expected[plane].data(), row_sizes[plane],
                    expected[plane].size(), plane),
                S_OK);
    }

    for (UINT plane = 0; plane < 2; ++plane) {
      TransitionSubresource(texture.get(), plane,
                            D3D12_RESOURCE_STATE_COPY_DEST,
                            D3D12_RESOURCE_STATE_COPY_SOURCE);
      TextureReadback readback;
      ASSERT_EQ(context_.ReadbackTexture(texture.get(), &readback, plane),
                S_OK);
      for (UINT row = 0; row < rows[plane]; ++row) {
        EXPECT_EQ(std::memcmp(readback.data.data() + row * readback.row_pitch,
                              expected[plane].data() + row * row_sizes[plane],
                              static_cast<std::size_t>(row_sizes[plane])),
                  0)
            << "plane=" << plane << " row=" << row;
      }
      ASSERT_EQ(context_.ResetCommandList(), S_OK);
    }
    ++executed;
  }
  EXPECT_GT(executed, 0u);
}

TEST_F(PlanarFormatSpec, Nv12PlaneSliceSrvsReadLumaAndChroma) {
  if (!(Support(DXGI_FORMAT_NV12).Support1 & D3D12_FORMAT_SUPPORT1_TEXTURE2D))
    GTEST_SKIP() << "NV12 textures are unsupported";

  auto texture = CreateTexture(DXGI_FORMAT_NV12);
  ASSERT_TRUE(texture);
  const auto desc = texture->GetDesc();
  std::array<UINT, 2> rows = {};
  std::array<UINT64, 2> row_sizes = {};
  context_.device()->GetCopyableFootprints(&desc, 0, 2, 0, nullptr, rows.data(),
                                           row_sizes.data(), nullptr);
  std::vector<std::uint8_t> luma(rows[0] * row_sizes[0], 0x11);
  std::vector<std::uint8_t> chroma(rows[1] * row_sizes[1], 0);
  for (UINT row = 0; row < rows[1]; ++row) {
    for (UINT64 column = 0; column < row_sizes[1]; column += 2) {
      chroma[row * row_sizes[1] + column] = 0x33;
      chroma[row * row_sizes[1] + column + 1] = 0x55;
    }
  }
  ASSERT_EQ(context_.UploadTextureAndReset(texture.get(), luma.data(),
                                           row_sizes[0], luma.size(), 0),
            S_OK);
  ASSERT_EQ(context_.UploadTextureAndReset(texture.get(), chroma.data(),
                                           row_sizes[1], chroma.size(), 1),
            S_OK);

  const auto shader = CompileShader(R"(
    Texture2D<float4> luma : register(t0);
    Texture2D<float4> chroma : register(t1);
    RWByteAddressBuffer output : register(u0);
    [numthreads(1, 1, 1)]
    void main() {
      float4 y = luma.Load(int3(0, 0, 0));
      float4 uv = chroma.Load(int3(0, 0, 0));
      output.Store(0, (uint)round(y.x * 255.0));
      output.Store(4, (uint)round(uv.x * 255.0));
      output.Store(8, (uint)round(uv.y * 255.0));
    }
  )",
                                    "cs_5_0");
  ASSERT_EQ(shader.result, S_OK) << shader.diagnostic_text();

  D3D12_DESCRIPTOR_RANGE ranges[2] = {};
  ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  ranges[0].NumDescriptors = 2;
  ranges[0].OffsetInDescriptorsFromTableStart = 0;
  ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  ranges[1].NumDescriptors = 1;
  ranges[1].OffsetInDescriptorsFromTableStart = 2;
  D3D12_ROOT_PARAMETER parameter = {};
  parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameter.DescriptorTable.NumDescriptorRanges = 2;
  parameter.DescriptorTable.pDescriptorRanges = ranges;
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  root_desc.NumParameters = 1;
  root_desc.pParameters = &parameter;
  auto root = context_.CreateRootSignature(root_desc);
  ASSERT_TRUE(root);
  const D3D12_SHADER_BYTECODE bytecode = {shader.bytecode->GetBufferPointer(),
                                          shader.bytecode->GetBufferSize()};
  auto pipeline = context_.CreateComputePipeline(root.get(), bytecode);
  ASSERT_TRUE(pipeline);

  auto output =
      context_.CreateBuffer(3 * sizeof(UINT), D3D12_HEAP_TYPE_DEFAULT,
                            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  auto heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 3, true);
  ASSERT_TRUE(output);
  ASSERT_TRUE(heap);
  D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
  srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv.Texture2D.MipLevels = 1;
  srv.Format = DXGI_FORMAT_R8_UNORM;
  srv.Texture2D.PlaneSlice = 0;
  context_.device()->CreateShaderResourceView(
      texture.get(), &srv, context_.CpuDescriptorHandle(heap.get(), 0));
  srv.Format = DXGI_FORMAT_R8G8_UNORM;
  srv.Texture2D.PlaneSlice = 1;
  context_.device()->CreateShaderResourceView(
      texture.get(), &srv, context_.CpuDescriptorHandle(heap.get(), 1));
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
  uav.Format = DXGI_FORMAT_R32_TYPELESS;
  uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav.Buffer.NumElements = 3;
  uav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
  context_.device()->CreateUnorderedAccessView(
      output.get(), nullptr, &uav, context_.CpuDescriptorHandle(heap.get(), 2));

  D3D12TestContext::Transition(context_.list(), texture.get(),
                               D3D12_RESOURCE_STATE_COPY_DEST,
                               D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  ID3D12DescriptorHeap *heaps[] = {heap.get()};
  context_.list()->SetDescriptorHeaps(1, heaps);
  context_.list()->SetComputeRootSignature(root.get());
  context_.list()->SetPipelineState(pipeline.get());
  context_.list()->SetComputeRootDescriptorTable(
      0, heap->GetGPUDescriptorHandleForHeapStart());
  context_.list()->Dispatch(1, 1, 1);
  D3D12TestContext::Transition(context_.list(), output.get(),
                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
  std::vector<std::uint8_t> bytes;
  ASSERT_EQ(context_.ReadbackBuffer(output.get(), 3 * sizeof(UINT), &bytes),
            S_OK);
  ASSERT_EQ(bytes.size(), 3 * sizeof(UINT));
  std::array<UINT, 3> actual = {};
  std::memcpy(actual.data(), bytes.data(), bytes.size());
  EXPECT_EQ(actual, (std::array<UINT, 3>{0x11, 0x33, 0x55}));
}

} // namespace
