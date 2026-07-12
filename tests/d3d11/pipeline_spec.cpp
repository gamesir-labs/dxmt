#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d11_test_context.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>

namespace {

using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

constexpr std::string_view kFullscreenVertexShader = R"(
float4 main(uint vertex_id : SV_VertexID) : SV_Position {
  float2 position = float2((vertex_id << 1) & 2, vertex_id & 2);
  return float4(position * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}
)";

constexpr std::string_view kSolidPixelShader = R"(
float4 main() : SV_Target {
  return float4(0.25, 0.5, 0.75, 1.0);
}
)";

constexpr std::string_view kTextureComputeShader = R"(
RWTexture2D<uint> output : register(u0);

[numthreads(4, 4, 1)]
void main(uint3 thread_id : SV_DispatchThreadID) {
  output[thread_id.xy] = thread_id.y * 4 + thread_id.x + 7;
}
)";

::testing::AssertionResult HResultSucceeded(HRESULT hr) {
  if (SUCCEEDED(hr))
    return ::testing::AssertionSuccess();
  return ::testing::AssertionFailure()
         << "HRESULT failed: 0x" << std::hex << static_cast<unsigned long>(hr);
}

bool ColorMatches(uint32_t actual, uint32_t expected, unsigned tolerance) {
  for (unsigned shift = 0; shift < 32; shift += 8) {
    const int actual_channel = (actual >> shift) & 0xff;
    const int expected_channel = (expected >> shift) & 0xff;
    if (std::abs(actual_channel - expected_channel) >
        static_cast<int>(tolerance))
      return false;
  }
  return true;
}

class D3D11PipelineSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_TRUE(HResultSucceeded(context_.Initialize()));
  }

  D3D11TestContext context_;
};

TEST_F(D3D11PipelineSpec, DrawsFullscreenTriangleIntoRenderTarget) {
  const auto vertex = CompileShader(kFullscreenVertexShader, "vs_5_0");
  const auto pixel = CompileShader(kSolidPixelShader, "ps_5_0");
  ASSERT_TRUE(HResultSucceeded(vertex.result)) << vertex.diagnostic_text();
  ASSERT_TRUE(HResultSucceeded(pixel.result)) << pixel.diagnostic_text();

  ComPtr<ID3D11VertexShader> vertex_shader;
  ComPtr<ID3D11PixelShader> pixel_shader;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateVertexShader(
      vertex.bytecode->GetBufferPointer(), vertex.bytecode->GetBufferSize(),
      nullptr, vertex_shader.put())));
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreatePixelShader(
      pixel.bytecode->GetBufferPointer(), pixel.bytecode->GetBufferSize(),
      nullptr, pixel_shader.put())));

  D3D11_TEXTURE2D_DESC target_desc = {};
  target_desc.Width = 16;
  target_desc.Height = 16;
  target_desc.MipLevels = 1;
  target_desc.ArraySize = 1;
  target_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  target_desc.SampleDesc.Count = 1;
  target_desc.Usage = D3D11_USAGE_DEFAULT;
  target_desc.BindFlags = D3D11_BIND_RENDER_TARGET;
  ComPtr<ID3D11Texture2D> target;
  ComPtr<ID3D11RenderTargetView> target_view;
  ASSERT_TRUE(HResultSucceeded(
      context_.device()->CreateTexture2D(&target_desc, nullptr, target.put())));
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateRenderTargetView(
      target.get(), nullptr, target_view.put())));

  D3D11_VIEWPORT viewport = {};
  viewport.Width = static_cast<float>(target_desc.Width);
  viewport.Height = static_cast<float>(target_desc.Height);
  viewport.MaxDepth = 1.0f;
  ID3D11RenderTargetView *target_views[] = {target_view.get()};
  context_.context()->OMSetRenderTargets(1, target_views, nullptr);
  context_.context()->RSSetViewports(1, &viewport);
  context_.context()->IASetPrimitiveTopology(
      D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context_.context()->VSSetShader(vertex_shader.get(), nullptr, 0);
  context_.context()->PSSetShader(pixel_shader.get(), nullptr, 0);
  context_.context()->Draw(3, 0);
  context_.context()->OMSetRenderTargets(0, nullptr, nullptr);

  D3D11_TEXTURE2D_DESC staging_desc = target_desc;
  staging_desc.Usage = D3D11_USAGE_STAGING;
  staging_desc.BindFlags = 0;
  staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Texture2D> staging;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateTexture2D(
      &staging_desc, nullptr, staging.put())));
  context_.context()->CopyResource(staging.get(), target.get());

  D3D11_MAPPED_SUBRESOURCE mapped = {};
  ASSERT_TRUE(HResultSucceeded(
      context_.context()->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped)));
  constexpr uint32_t expected = 0xffbf8040;
  for (UINT y = 0; y < target_desc.Height; ++y) {
    for (UINT x = 0; x < target_desc.Width; ++x) {
      uint32_t actual = 0;
      std::memcpy(&actual,
                  static_cast<const uint8_t *>(mapped.pData) +
                      y * mapped.RowPitch + x * sizeof(actual),
                  sizeof(actual));
      ASSERT_TRUE(ColorMatches(actual, expected, 2))
          << "pixel (" << x << ", " << y << ") was 0x" << std::hex << actual;
    }
  }
  context_.context()->Unmap(staging.get(), 0);
}

TEST_F(D3D11PipelineSpec, ReportsSamplesForAnOcclusionQuery) {
  const auto vertex = CompileShader(kFullscreenVertexShader, "vs_5_0");
  const auto pixel = CompileShader(kSolidPixelShader, "ps_5_0");
  ASSERT_TRUE(HResultSucceeded(vertex.result)) << vertex.diagnostic_text();
  ASSERT_TRUE(HResultSucceeded(pixel.result)) << pixel.diagnostic_text();
  ComPtr<ID3D11VertexShader> vertex_shader;
  ComPtr<ID3D11PixelShader> pixel_shader;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateVertexShader(
      vertex.bytecode->GetBufferPointer(), vertex.bytecode->GetBufferSize(),
      nullptr, vertex_shader.put())));
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreatePixelShader(
      pixel.bytecode->GetBufferPointer(), pixel.bytecode->GetBufferSize(),
      nullptr, pixel_shader.put())));

  D3D11_TEXTURE2D_DESC target_desc = {};
  target_desc.Width = 8;
  target_desc.Height = 8;
  target_desc.MipLevels = 1;
  target_desc.ArraySize = 1;
  target_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  target_desc.SampleDesc.Count = 1;
  target_desc.Usage = D3D11_USAGE_DEFAULT;
  target_desc.BindFlags = D3D11_BIND_RENDER_TARGET;
  ComPtr<ID3D11Texture2D> target;
  ComPtr<ID3D11RenderTargetView> target_view;
  ASSERT_TRUE(HResultSucceeded(
      context_.device()->CreateTexture2D(&target_desc, nullptr, target.put())));
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateRenderTargetView(
      target.get(), nullptr, target_view.put())));

  D3D11_QUERY_DESC query_desc = {};
  query_desc.Query = D3D11_QUERY_OCCLUSION;
  ComPtr<ID3D11Query> query;
  ASSERT_TRUE(HResultSucceeded(
      context_.device()->CreateQuery(&query_desc, query.put())));
  D3D11_VIEWPORT viewport = {};
  viewport.Width = static_cast<float>(target_desc.Width);
  viewport.Height = static_cast<float>(target_desc.Height);
  viewport.MaxDepth = 1.0f;
  ID3D11RenderTargetView *target_views[] = {target_view.get()};
  context_.context()->OMSetRenderTargets(1, target_views, nullptr);
  context_.context()->RSSetViewports(1, &viewport);
  context_.context()->IASetPrimitiveTopology(
      D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context_.context()->VSSetShader(vertex_shader.get(), nullptr, 0);
  context_.context()->PSSetShader(pixel_shader.get(), nullptr, 0);
  context_.context()->Begin(query.get());
  context_.context()->Draw(3, 0);
  context_.context()->End(query.get());
  context_.context()->OMSetRenderTargets(0, nullptr, nullptr);
  context_.context()->Flush();

  UINT64 samples = 0;
  HRESULT data_hr = S_FALSE;
  for (UINT attempt = 0; attempt < 100 && data_hr == S_FALSE; ++attempt) {
    data_hr =
        context_.context()->GetData(query.get(), &samples, sizeof(samples), 0);
    if (data_hr == S_FALSE)
      Sleep(1);
  }
  EXPECT_EQ(data_hr, S_OK);
  EXPECT_GT(samples, 0u);
}

TEST_F(D3D11PipelineSpec, ReadsCompletedTimestampQuery) {
  D3D11_QUERY_DESC query_desc = {};
  query_desc.Query = D3D11_QUERY_TIMESTAMP;
  ComPtr<ID3D11Query> query;
  ASSERT_TRUE(HResultSucceeded(
      context_.device()->CreateQuery(&query_desc, query.put())));
  context_.context()->End(query.get());
  context_.context()->Flush();

  UINT64 timestamp = 0;
  HRESULT data_hr = S_FALSE;
  for (UINT attempt = 0; attempt < 100 && data_hr == S_FALSE; ++attempt) {
    data_hr = context_.context()->GetData(query.get(), &timestamp,
                                          sizeof(timestamp), 0);
    if (data_hr == S_FALSE)
      Sleep(1);
  }
  EXPECT_EQ(data_hr, S_OK);
  EXPECT_GT(timestamp, 0ull);
}

TEST_F(D3D11PipelineSpec, DispatchesComputeShaderIntoStorageTexture) {
  const auto compute = CompileShader(kTextureComputeShader, "cs_5_0");
  ASSERT_TRUE(HResultSucceeded(compute.result)) << compute.diagnostic_text();
  ComPtr<ID3D11ComputeShader> compute_shader;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateComputeShader(
      compute.bytecode->GetBufferPointer(), compute.bytecode->GetBufferSize(),
      nullptr, compute_shader.put())));

  D3D11_TEXTURE2D_DESC texture_desc = {};
  texture_desc.Width = 4;
  texture_desc.Height = 4;
  texture_desc.MipLevels = 1;
  texture_desc.ArraySize = 1;
  texture_desc.Format = DXGI_FORMAT_R32_UINT;
  texture_desc.SampleDesc.Count = 1;
  texture_desc.Usage = D3D11_USAGE_DEFAULT;
  texture_desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
  ComPtr<ID3D11Texture2D> texture;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateTexture2D(
      &texture_desc, nullptr, texture.put())));

  D3D11_UNORDERED_ACCESS_VIEW_DESC view_desc = {};
  view_desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
  view_desc.Format = texture_desc.Format;
  ComPtr<ID3D11UnorderedAccessView> view;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateUnorderedAccessView(
      texture.get(), &view_desc, view.put())));
  ID3D11UnorderedAccessView *views[] = {view.get()};
  context_.context()->CSSetShader(compute_shader.get(), nullptr, 0);
  context_.context()->CSSetUnorderedAccessViews(0, 1, views, nullptr);
  context_.context()->Dispatch(1, 1, 1);
  context_.context()->CSSetUnorderedAccessViews(0, 0, nullptr, nullptr);

  D3D11_TEXTURE2D_DESC staging_desc = texture_desc;
  staging_desc.Usage = D3D11_USAGE_STAGING;
  staging_desc.BindFlags = 0;
  staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Texture2D> staging;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateTexture2D(
      &staging_desc, nullptr, staging.put())));
  context_.context()->CopyResource(staging.get(), texture.get());

  D3D11_MAPPED_SUBRESOURCE mapped = {};
  ASSERT_TRUE(HResultSucceeded(
      context_.context()->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped)));
  for (UINT y = 0; y < texture_desc.Height; ++y) {
    const auto *row = reinterpret_cast<const uint32_t *>(
        static_cast<const uint8_t *>(mapped.pData) + y * mapped.RowPitch);
    for (UINT x = 0; x < texture_desc.Width; ++x)
      EXPECT_EQ(row[x], y * texture_desc.Width + x + 7)
          << "texel (" << x << ", " << y << ')';
  }
  context_.context()->Unmap(staging.get(), 0);
}

TEST_F(D3D11PipelineSpec, ExecutesDeferredRenderTargetClear) {
  D3D11_TEXTURE2D_DESC texture_desc = {};
  texture_desc.Width = 8;
  texture_desc.Height = 8;
  texture_desc.MipLevels = 1;
  texture_desc.ArraySize = 1;
  texture_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  texture_desc.SampleDesc.Count = 1;
  texture_desc.Usage = D3D11_USAGE_DEFAULT;
  texture_desc.BindFlags = D3D11_BIND_RENDER_TARGET;
  ComPtr<ID3D11Texture2D> texture;
  ComPtr<ID3D11RenderTargetView> view;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateTexture2D(
      &texture_desc, nullptr, texture.put())));
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateRenderTargetView(
      texture.get(), nullptr, view.put())));

  ComPtr<ID3D11DeviceContext> deferred;
  ASSERT_TRUE(HResultSucceeded(
      context_.device()->CreateDeferredContext(0, deferred.put())));
  constexpr float clear_color[4] = {0.125f, 0.25f, 0.5f, 1.0f};
  deferred->ClearRenderTargetView(view.get(), clear_color);
  ComPtr<ID3D11CommandList> command_list;
  ASSERT_TRUE(
      HResultSucceeded(deferred->FinishCommandList(FALSE, command_list.put())));
  context_.context()->ExecuteCommandList(command_list.get(), FALSE);

  D3D11_TEXTURE2D_DESC staging_desc = texture_desc;
  staging_desc.Usage = D3D11_USAGE_STAGING;
  staging_desc.BindFlags = 0;
  staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Texture2D> staging;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateTexture2D(
      &staging_desc, nullptr, staging.put())));
  context_.context()->CopyResource(staging.get(), texture.get());

  D3D11_MAPPED_SUBRESOURCE mapped = {};
  ASSERT_TRUE(HResultSucceeded(
      context_.context()->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped)));
  constexpr uint32_t expected = 0xff804020;
  for (UINT y = 0; y < texture_desc.Height; ++y) {
    const auto *row = reinterpret_cast<const uint32_t *>(
        static_cast<const uint8_t *>(mapped.pData) + y * mapped.RowPitch);
    for (UINT x = 0; x < texture_desc.Width; ++x)
      EXPECT_TRUE(ColorMatches(row[x], expected, 1));
  }
  context_.context()->Unmap(staging.get(), 0);
}

TEST_F(D3D11PipelineSpec, CreatesStateObjectsWithRequestedDescriptions) {
  D3D11_RASTERIZER_DESC rasterizer_desc = {};
  rasterizer_desc.FillMode = D3D11_FILL_WIREFRAME;
  rasterizer_desc.CullMode = D3D11_CULL_FRONT;
  rasterizer_desc.FrontCounterClockwise = TRUE;
  rasterizer_desc.ScissorEnable = TRUE;
  ComPtr<ID3D11RasterizerState> rasterizer;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateRasterizerState(
      &rasterizer_desc, rasterizer.put())));
  D3D11_RASTERIZER_DESC actual_rasterizer = {};
  rasterizer->GetDesc(&actual_rasterizer);
  EXPECT_EQ(actual_rasterizer.FillMode, rasterizer_desc.FillMode);
  EXPECT_EQ(actual_rasterizer.CullMode, rasterizer_desc.CullMode);
  EXPECT_TRUE(actual_rasterizer.FrontCounterClockwise);
  EXPECT_TRUE(actual_rasterizer.ScissorEnable);

  D3D11_BLEND_DESC blend_desc = {};
  blend_desc.AlphaToCoverageEnable = TRUE;
  blend_desc.IndependentBlendEnable = TRUE;
  blend_desc.RenderTarget[0].BlendEnable = TRUE;
  blend_desc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
  blend_desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
  blend_desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
  blend_desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
  blend_desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
  blend_desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
  blend_desc.RenderTarget[0].RenderTargetWriteMask =
      D3D11_COLOR_WRITE_ENABLE_ALL;
  ComPtr<ID3D11BlendState> blend;
  ASSERT_TRUE(HResultSucceeded(
      context_.device()->CreateBlendState(&blend_desc, blend.put())));
  D3D11_BLEND_DESC actual_blend = {};
  blend->GetDesc(&actual_blend);
  EXPECT_TRUE(actual_blend.AlphaToCoverageEnable);
  EXPECT_TRUE(actual_blend.IndependentBlendEnable);
  EXPECT_EQ(actual_blend.RenderTarget[0].SrcBlend, D3D11_BLEND_SRC_ALPHA);

  D3D11_DEPTH_STENCIL_DESC depth_desc = {};
  depth_desc.DepthEnable = TRUE;
  depth_desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
  depth_desc.DepthFunc = D3D11_COMPARISON_GREATER_EQUAL;
  depth_desc.StencilEnable = TRUE;
  depth_desc.StencilReadMask = 0x3f;
  depth_desc.StencilWriteMask = 0x7f;
  ComPtr<ID3D11DepthStencilState> depth;
  ASSERT_TRUE(HResultSucceeded(
      context_.device()->CreateDepthStencilState(&depth_desc, depth.put())));
  D3D11_DEPTH_STENCIL_DESC actual_depth = {};
  depth->GetDesc(&actual_depth);
  EXPECT_EQ(actual_depth.DepthFunc, D3D11_COMPARISON_GREATER_EQUAL);
  EXPECT_EQ(actual_depth.StencilReadMask, 0x3f);
  EXPECT_EQ(actual_depth.StencilWriteMask, 0x7f);

  D3D11_SAMPLER_DESC sampler_desc = {};
  sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
  sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_BORDER;
  sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_MIRROR;
  sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampler_desc.MaxAnisotropy = 1;
  sampler_desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
  sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;
  sampler_desc.BorderColor[0] = 0.25f;
  ComPtr<ID3D11SamplerState> sampler;
  ASSERT_TRUE(HResultSucceeded(
      context_.device()->CreateSamplerState(&sampler_desc, sampler.put())));
  D3D11_SAMPLER_DESC actual_sampler = {};
  sampler->GetDesc(&actual_sampler);
  EXPECT_EQ(actual_sampler.AddressU, D3D11_TEXTURE_ADDRESS_BORDER);
  EXPECT_EQ(actual_sampler.AddressV, D3D11_TEXTURE_ADDRESS_MIRROR);
  EXPECT_FLOAT_EQ(actual_sampler.BorderColor[0], 0.25f);
}

TEST_F(D3D11PipelineSpec, CreatesCompatibleViewsOfTypelessDepthTexture) {
  D3D11_TEXTURE2D_DESC texture_desc = {};
  texture_desc.Width = 8;
  texture_desc.Height = 8;
  texture_desc.MipLevels = 1;
  texture_desc.ArraySize = 1;
  texture_desc.Format = DXGI_FORMAT_R24G8_TYPELESS;
  texture_desc.SampleDesc.Count = 1;
  texture_desc.Usage = D3D11_USAGE_DEFAULT;
  texture_desc.BindFlags =
      D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
  ComPtr<ID3D11Texture2D> texture;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateTexture2D(
      &texture_desc, nullptr, texture.put())));

  D3D11_DEPTH_STENCIL_VIEW_DESC depth_desc = {};
  depth_desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
  depth_desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
  ComPtr<ID3D11DepthStencilView> depth_view;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateDepthStencilView(
      texture.get(), &depth_desc, depth_view.put())));

  D3D11_SHADER_RESOURCE_VIEW_DESC resource_desc = {};
  resource_desc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
  resource_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
  resource_desc.Texture2D.MipLevels = 1;
  ComPtr<ID3D11ShaderResourceView> resource_view;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateShaderResourceView(
      texture.get(), &resource_desc, resource_view.put())));

  D3D11_DEPTH_STENCIL_VIEW_DESC actual_depth = {};
  D3D11_SHADER_RESOURCE_VIEW_DESC actual_resource = {};
  depth_view->GetDesc(&actual_depth);
  resource_view->GetDesc(&actual_resource);
  EXPECT_EQ(actual_depth.Format, depth_desc.Format);
  EXPECT_EQ(actual_resource.Format, resource_desc.Format);
}

} // namespace
