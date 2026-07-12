#include <dxmt_test.hpp>
#include <dxmt_test_com.hpp>
#include <dxmt_test_shader.hpp>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d10.h>
#include <dxgi.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>

extern "C" HRESULT WINAPI D3D10CoreCreateDevice(IDXGIFactory *factory,
                                                IDXGIAdapter *adapter,
                                                UINT flags,
                                                D3D_FEATURE_LEVEL feature_level,
                                                ID3D10Device **device);

namespace {

using dxmt::test::CompileShader;
using dxmt::test::ComPtr;

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

class D3D10PipelineSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_TRUE(HResultSucceeded(CreateDXGIFactory1(
        __uuidof(IDXGIFactory1), reinterpret_cast<void **>(factory_.put()))));
    ASSERT_TRUE(HResultSucceeded(factory_->EnumAdapters(0, adapter_.put())));
    ASSERT_TRUE(HResultSucceeded(
        D3D10CoreCreateDevice(factory_.get(), adapter_.get(), 0,
                              D3D_FEATURE_LEVEL_10_0, device_.put())));
  }

  ComPtr<IDXGIFactory1> factory_;
  ComPtr<IDXGIAdapter> adapter_;
  ComPtr<ID3D10Device> device_;
};

TEST_F(D3D10PipelineSpec, DrawsFullscreenTriangleIntoRenderTarget) {
  const auto vertex = CompileShader(kFullscreenVertexShader, "vs_4_0");
  const auto pixel = CompileShader(kSolidPixelShader, "ps_4_0");
  ASSERT_TRUE(HResultSucceeded(vertex.result)) << vertex.diagnostic_text();
  ASSERT_TRUE(HResultSucceeded(pixel.result)) << pixel.diagnostic_text();

  ComPtr<ID3D10VertexShader> vertex_shader;
  ComPtr<ID3D10PixelShader> pixel_shader;
  ASSERT_TRUE(HResultSucceeded(device_->CreateVertexShader(
      vertex.bytecode->GetBufferPointer(), vertex.bytecode->GetBufferSize(),
      vertex_shader.put())));
  ASSERT_TRUE(HResultSucceeded(device_->CreatePixelShader(
      pixel.bytecode->GetBufferPointer(), pixel.bytecode->GetBufferSize(),
      pixel_shader.put())));

  D3D10_TEXTURE2D_DESC target_desc = {};
  target_desc.Width = 16;
  target_desc.Height = 16;
  target_desc.MipLevels = 1;
  target_desc.ArraySize = 1;
  target_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  target_desc.SampleDesc.Count = 1;
  target_desc.Usage = D3D10_USAGE_DEFAULT;
  target_desc.BindFlags = D3D10_BIND_RENDER_TARGET;
  ComPtr<ID3D10Texture2D> target;
  ComPtr<ID3D10RenderTargetView> target_view;
  ASSERT_TRUE(HResultSucceeded(
      device_->CreateTexture2D(&target_desc, nullptr, target.put())));
  ASSERT_TRUE(HResultSucceeded(device_->CreateRenderTargetView(
      target.get(), nullptr, target_view.put())));

  D3D10_VIEWPORT viewport = {};
  viewport.Width = target_desc.Width;
  viewport.Height = target_desc.Height;
  viewport.MaxDepth = 1.0f;
  ID3D10RenderTargetView *target_views[] = {target_view.get()};
  device_->OMSetRenderTargets(1, target_views, nullptr);
  device_->RSSetViewports(1, &viewport);
  device_->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  device_->VSSetShader(vertex_shader.get());
  device_->PSSetShader(pixel_shader.get());
  device_->Draw(3, 0);
  device_->OMSetRenderTargets(0, nullptr, nullptr);

  D3D10_TEXTURE2D_DESC staging_desc = target_desc;
  staging_desc.Usage = D3D10_USAGE_STAGING;
  staging_desc.BindFlags = 0;
  staging_desc.CPUAccessFlags = D3D10_CPU_ACCESS_READ;
  ComPtr<ID3D10Texture2D> staging;
  ASSERT_TRUE(HResultSucceeded(
      device_->CreateTexture2D(&staging_desc, nullptr, staging.put())));
  device_->CopyResource(staging.get(), target.get());

  D3D10_MAPPED_TEXTURE2D mapped = {};
  ASSERT_TRUE(HResultSucceeded(staging->Map(0, D3D10_MAP_READ, 0, &mapped)));
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
  staging->Unmap(0);
}

TEST_F(D3D10PipelineSpec, CreatesStateObjectsWithRequestedDescriptions) {
  D3D10_RASTERIZER_DESC rasterizer_desc = {};
  rasterizer_desc.FillMode = D3D10_FILL_WIREFRAME;
  rasterizer_desc.CullMode = D3D10_CULL_FRONT;
  rasterizer_desc.FrontCounterClockwise = TRUE;
  rasterizer_desc.ScissorEnable = TRUE;
  ComPtr<ID3D10RasterizerState> rasterizer;
  ASSERT_TRUE(HResultSucceeded(
      device_->CreateRasterizerState(&rasterizer_desc, rasterizer.put())));
  D3D10_RASTERIZER_DESC actual_rasterizer = {};
  rasterizer->GetDesc(&actual_rasterizer);
  EXPECT_EQ(actual_rasterizer.FillMode, rasterizer_desc.FillMode);
  EXPECT_EQ(actual_rasterizer.CullMode, rasterizer_desc.CullMode);
  EXPECT_TRUE(actual_rasterizer.FrontCounterClockwise);
  EXPECT_TRUE(actual_rasterizer.ScissorEnable);

  D3D10_BLEND_DESC blend_desc = {};
  blend_desc.AlphaToCoverageEnable = TRUE;
  blend_desc.BlendEnable[0] = TRUE;
  blend_desc.SrcBlend = D3D10_BLEND_SRC_ALPHA;
  blend_desc.DestBlend = D3D10_BLEND_INV_SRC_ALPHA;
  blend_desc.BlendOp = D3D10_BLEND_OP_ADD;
  blend_desc.SrcBlendAlpha = D3D10_BLEND_ONE;
  blend_desc.DestBlendAlpha = D3D10_BLEND_ZERO;
  blend_desc.BlendOpAlpha = D3D10_BLEND_OP_ADD;
  blend_desc.RenderTargetWriteMask[0] = D3D10_COLOR_WRITE_ENABLE_ALL;
  ComPtr<ID3D10BlendState> blend;
  ASSERT_TRUE(
      HResultSucceeded(device_->CreateBlendState(&blend_desc, blend.put())));
  D3D10_BLEND_DESC actual_blend = {};
  blend->GetDesc(&actual_blend);
  EXPECT_TRUE(actual_blend.AlphaToCoverageEnable);
  EXPECT_TRUE(actual_blend.BlendEnable[0]);
  EXPECT_EQ(actual_blend.SrcBlend, D3D10_BLEND_SRC_ALPHA);

  D3D10_SAMPLER_DESC sampler_desc = {};
  sampler_desc.Filter = D3D10_FILTER_MIN_MAG_MIP_POINT;
  sampler_desc.AddressU = D3D10_TEXTURE_ADDRESS_BORDER;
  sampler_desc.AddressV = D3D10_TEXTURE_ADDRESS_MIRROR;
  sampler_desc.AddressW = D3D10_TEXTURE_ADDRESS_CLAMP;
  sampler_desc.MaxAnisotropy = 1;
  sampler_desc.ComparisonFunc = D3D10_COMPARISON_NEVER;
  sampler_desc.MaxLOD = D3D10_FLOAT32_MAX;
  sampler_desc.BorderColor[0] = 0.25f;
  ComPtr<ID3D10SamplerState> sampler;
  ASSERT_TRUE(HResultSucceeded(
      device_->CreateSamplerState(&sampler_desc, sampler.put())));
  D3D10_SAMPLER_DESC actual_sampler = {};
  sampler->GetDesc(&actual_sampler);
  EXPECT_EQ(actual_sampler.AddressU, D3D10_TEXTURE_ADDRESS_BORDER);
  EXPECT_EQ(actual_sampler.AddressV, D3D10_TEXTURE_ADDRESS_MIRROR);
  EXPECT_FLOAT_EQ(actual_sampler.BorderColor[0], 0.25f);
}

} // namespace
