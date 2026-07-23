#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <cstdint>

// Public D3D11 texture-view format contracts. A null view description inherits
// the resource format, which cannot produce a valid view for typeless textures.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

class D3D11TextureViewFormatContractSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_NE(context_.context(), nullptr);

    D3D11_TEXTURE2D_DESC texture_desc = {};
    texture_desc.Width = 8;
    texture_desc.Height = 8;
    texture_desc.MipLevels = 1;
    texture_desc.ArraySize = 1;
    texture_desc.Format = DXGI_FORMAT_R8G8B8A8_TYPELESS;
    texture_desc.SampleDesc.Count = 1;
    texture_desc.Usage = D3D11_USAGE_DEFAULT;
    texture_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE |
                             D3D11_BIND_RENDER_TARGET |
                             D3D11_BIND_UNORDERED_ACCESS;
    ASSERT_EQ(context_.device()->CreateTexture2D(&texture_desc, nullptr,
                                                 texture_.put()),
              S_OK);
    ASSERT_NE(texture_.get(), nullptr);
  }

  D3D11TestContext context_;
  ComPtr<ID3D11Texture2D> texture_;
};

TEST_F(D3D11TextureViewFormatContractSpec,
       ShaderResourceViewsRequireTypedFormats) {
  ID3D11ShaderResourceView *view = reinterpret_cast<ID3D11ShaderResourceView *>(
      static_cast<std::uintptr_t>(1));
  EXPECT_EQ(context_.device()->CreateShaderResourceView(texture_.get(), nullptr,
                                                        &view),
            E_INVALIDARG);
  EXPECT_EQ(view, nullptr);

  D3D11_SHADER_RESOURCE_VIEW_DESC desc = {};
  desc.Format = DXGI_FORMAT_R8G8B8A8_TYPELESS;
  desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
  desc.Texture2D.MostDetailedMip = 0;
  desc.Texture2D.MipLevels = 1;
  view = reinterpret_cast<ID3D11ShaderResourceView *>(
      static_cast<std::uintptr_t>(1));
  EXPECT_EQ(
      context_.device()->CreateShaderResourceView(texture_.get(), &desc, &view),
      E_INVALIDARG);
  EXPECT_EQ(view, nullptr);

  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  ComPtr<ID3D11ShaderResourceView> typed_view;
  ASSERT_EQ(context_.device()->CreateShaderResourceView(texture_.get(), &desc,
                                                        typed_view.put()),
            S_OK);
  D3D11_SHADER_RESOURCE_VIEW_DESC actual = {};
  typed_view->GetDesc(&actual);
  EXPECT_EQ(actual.Format, DXGI_FORMAT_R8G8B8A8_UNORM);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11TextureViewFormatContractSpec,
       RenderTargetViewsRequireTypedFormats) {
  ID3D11RenderTargetView *view = reinterpret_cast<ID3D11RenderTargetView *>(
      static_cast<std::uintptr_t>(1));
  EXPECT_EQ(
      context_.device()->CreateRenderTargetView(texture_.get(), nullptr, &view),
      E_INVALIDARG);
  EXPECT_EQ(view, nullptr);

  D3D11_RENDER_TARGET_VIEW_DESC desc = {};
  desc.Format = DXGI_FORMAT_R8G8B8A8_TYPELESS;
  desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
  desc.Texture2D.MipSlice = 0;
  view = reinterpret_cast<ID3D11RenderTargetView *>(
      static_cast<std::uintptr_t>(1));
  EXPECT_EQ(
      context_.device()->CreateRenderTargetView(texture_.get(), &desc, &view),
      E_INVALIDARG);
  EXPECT_EQ(view, nullptr);

  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  ComPtr<ID3D11RenderTargetView> typed_view;
  ASSERT_EQ(context_.device()->CreateRenderTargetView(texture_.get(), &desc,
                                                      typed_view.put()),
            S_OK);
  D3D11_RENDER_TARGET_VIEW_DESC actual = {};
  typed_view->GetDesc(&actual);
  EXPECT_EQ(actual.Format, DXGI_FORMAT_R8G8B8A8_UNORM);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11TextureViewFormatContractSpec,
       UnorderedAccessViewsRequireTypedFormats) {
  ID3D11UnorderedAccessView *view =
      reinterpret_cast<ID3D11UnorderedAccessView *>(
          static_cast<std::uintptr_t>(1));
  EXPECT_EQ(context_.device()->CreateUnorderedAccessView(texture_.get(),
                                                         nullptr, &view),
            E_INVALIDARG);
  EXPECT_EQ(view, nullptr);

  D3D11_UNORDERED_ACCESS_VIEW_DESC desc = {};
  desc.Format = DXGI_FORMAT_R8G8B8A8_TYPELESS;
  desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
  desc.Texture2D.MipSlice = 0;
  view = reinterpret_cast<ID3D11UnorderedAccessView *>(
      static_cast<std::uintptr_t>(1));
  EXPECT_EQ(context_.device()->CreateUnorderedAccessView(texture_.get(), &desc,
                                                         &view),
            E_INVALIDARG);
  EXPECT_EQ(view, nullptr);

  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  ComPtr<ID3D11UnorderedAccessView> typed_view;
  ASSERT_EQ(context_.device()->CreateUnorderedAccessView(texture_.get(), &desc,
                                                         typed_view.put()),
            S_OK);
  D3D11_UNORDERED_ACCESS_VIEW_DESC actual = {};
  typed_view->GetDesc(&actual);
  EXPECT_EQ(actual.Format, DXGI_FORMAT_R8G8B8A8_UNORM);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
