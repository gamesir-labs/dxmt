#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d11_test_context.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>

namespace {

using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

constexpr UINT kSize = 8;
constexpr uint32_t kBlack = 0x00000000u;
constexpr uint32_t kGreen = 0xff00ff00u;

constexpr std::string_view kDepthVertexShader = R"(
cbuffer DepthConstants : register(b0) {
  float depth;
  float3 _pad;
};
float4 main(uint vertex_id : SV_VertexID) : SV_Position {
  float2 positions[3] = {
      float2(-1.0, -1.0),
      float2(-1.0, 3.0),
      float2(3.0, -1.0),
  };
  return float4(positions[vertex_id], depth, 1.0);
}
)";

constexpr std::string_view kGreenPixelShader = R"(
float4 main() : SV_Target {
  return float4(0.0, 1.0, 0.0, 1.0);
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

HRESULT ReadTexturePixel(ID3D11Device *device, ID3D11DeviceContext *context,
                         ID3D11Texture2D *texture, UINT x, UINT y,
                         uint32_t *pixel) {
  if (!device || !context || !texture || !pixel)
    return E_INVALIDARG;
  D3D11_TEXTURE2D_DESC desc = {};
  texture->GetDesc(&desc);
  if (x >= desc.Width || y >= desc.Height)
    return E_INVALIDARG;
  desc.Usage = D3D11_USAGE_STAGING;
  desc.BindFlags = 0;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  desc.MiscFlags = 0;
  ComPtr<ID3D11Texture2D> staging;
  HRESULT hr = device->CreateTexture2D(&desc, nullptr, staging.put());
  if (FAILED(hr))
    return hr;
  context->CopyResource(staging.get(), texture);
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  hr = context->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped);
  if (FAILED(hr))
    return hr;
  std::memcpy(pixel,
              static_cast<const uint8_t *>(mapped.pData) + y * mapped.RowPitch +
                  x * sizeof(*pixel),
              sizeof(*pixel));
  context->Unmap(staging.get(), 0);
  return S_OK;
}

struct DepthCompareCase {
  D3D11_COMPARISON_FUNC function;
  float incoming;
  float stored;
  bool passes;
  const char *name;
};

struct StencilCompareCase {
  D3D11_COMPARISON_FUNC function;
  UINT8 reference;
  UINT8 stored;
  bool passes;
  const char *name;
};

struct StencilOperationCase {
  D3D11_STENCIL_OP operation;
  D3D11_COMPARISON_FUNC function;
  UINT8 initial;
  UINT8 reference;
  UINT8 expected_for_equal;
  bool expect_green;
  const char *name;
};

struct DepthWriteCase {
  BOOL depth_write_enable;
  bool second_draw_passes;
  const char *name;
};

class DepthStencilMatrixFixture : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_TRUE(HResultSucceeded(context_.Initialize()));

    const auto vertex = CompileShader(kDepthVertexShader, "vs_5_0");
    const auto pixel = CompileShader(kGreenPixelShader, "ps_5_0");
    ASSERT_TRUE(HResultSucceeded(vertex.result)) << vertex.diagnostic_text();
    ASSERT_TRUE(HResultSucceeded(pixel.result)) << pixel.diagnostic_text();
    ASSERT_TRUE(HResultSucceeded(context_.device()->CreateVertexShader(
        vertex.bytecode->GetBufferPointer(), vertex.bytecode->GetBufferSize(),
        nullptr, vertex_shader_.put())));
    ASSERT_TRUE(HResultSucceeded(context_.device()->CreatePixelShader(
        pixel.bytecode->GetBufferPointer(), pixel.bytecode->GetBufferSize(),
        nullptr, pixel_shader_.put())));

    D3D11_BUFFER_DESC constant_desc = {};
    constant_desc.ByteWidth = 16;
    constant_desc.Usage = D3D11_USAGE_DEFAULT;
    constant_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    ASSERT_TRUE(HResultSucceeded(context_.device()->CreateBuffer(
        &constant_desc, nullptr, depth_constants_.put())));

    D3D11_TEXTURE2D_DESC target_desc = {};
    target_desc.Width = kSize;
    target_desc.Height = kSize;
    target_desc.MipLevels = 1;
    target_desc.ArraySize = 1;
    target_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    target_desc.SampleDesc.Count = 1;
    target_desc.Usage = D3D11_USAGE_DEFAULT;
    target_desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    ASSERT_TRUE(HResultSucceeded(context_.device()->CreateTexture2D(
        &target_desc, nullptr, color_target_.put())));
    ASSERT_TRUE(HResultSucceeded(context_.device()->CreateRenderTargetView(
        color_target_.get(), nullptr, color_rtv_.put())));

    ASSERT_TRUE(HResultSucceeded(CreateDepthResources()));

    D3D11_RASTERIZER_DESC rasterizer_desc = {};
    rasterizer_desc.FillMode = D3D11_FILL_SOLID;
    rasterizer_desc.CullMode = D3D11_CULL_NONE;
    rasterizer_desc.DepthClipEnable = TRUE;
    ASSERT_TRUE(HResultSucceeded(context_.device()->CreateRasterizerState(
        &rasterizer_desc, rasterizer_.put())));

    D3D11_BLEND_DESC write_all = {};
    write_all.RenderTarget[0].RenderTargetWriteMask =
        D3D11_COLOR_WRITE_ENABLE_ALL;
    ASSERT_TRUE(HResultSucceeded(context_.device()->CreateBlendState(
        &write_all, color_write_all_.put())));

    D3D11_BLEND_DESC write_none = {};
    write_none.RenderTarget[0].RenderTargetWriteMask = 0;
    ASSERT_TRUE(HResultSucceeded(context_.device()->CreateBlendState(
        &write_none, color_write_none_.put())));
  }

  HRESULT CreateDepthResources() {
    constexpr std::array candidates = {
        std::pair{DXGI_FORMAT_D24_UNORM_S8_UINT, true},
        std::pair{DXGI_FORMAT_D32_FLOAT_S8X24_UINT, true},
        std::pair{DXGI_FORMAT_D16_UNORM, false},
    };
    for (const auto &[format, has_stencil] : candidates) {
      D3D11_TEXTURE2D_DESC depth_desc = {};
      depth_desc.Width = kSize;
      depth_desc.Height = kSize;
      depth_desc.MipLevels = 1;
      depth_desc.ArraySize = 1;
      depth_desc.Format = format;
      depth_desc.SampleDesc.Count = 1;
      depth_desc.Usage = D3D11_USAGE_DEFAULT;
      depth_desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
      ComPtr<ID3D11Texture2D> depth_texture;
      HRESULT hr = context_.device()->CreateTexture2D(&depth_desc, nullptr,
                                                     depth_texture.put());
      if (FAILED(hr) || !depth_texture)
        continue;
      ComPtr<ID3D11DepthStencilView> depth_view;
      hr = context_.device()->CreateDepthStencilView(
          depth_texture.get(), nullptr, depth_view.put());
      if (FAILED(hr) || !depth_view)
        continue;
      depth_texture_ = std::move(depth_texture);
      depth_dsv_ = std::move(depth_view);
      depth_format_ = format;
      has_stencil_ = has_stencil;
      return S_OK;
    }
    return E_FAIL;
  }

  ComPtr<ID3D11DepthStencilState>
  CreateDepthStencilState(const D3D11_DEPTH_STENCIL_DESC &desc) {
    ComPtr<ID3D11DepthStencilState> state;
    EXPECT_TRUE(HResultSucceeded(
        context_.device()->CreateDepthStencilState(&desc, state.put())));
    return state;
  }

  static D3D11_DEPTH_STENCILOP_DESC
  StencilFace(D3D11_COMPARISON_FUNC function,
              D3D11_STENCIL_OP pass = D3D11_STENCIL_OP_KEEP) {
    D3D11_DEPTH_STENCILOP_DESC face = {};
    face.StencilFailOp = D3D11_STENCIL_OP_KEEP;
    face.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
    face.StencilPassOp = pass;
    face.StencilFunc = function;
    return face;
  }

  void Clear(float depth, UINT8 stencil) {
    const FLOAT black[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    context_.context()->ClearRenderTargetView(color_rtv_.get(), black);
    const UINT clear_flags = has_stencil_
                                 ? (D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL)
                                 : D3D11_CLEAR_DEPTH;
    context_.context()->ClearDepthStencilView(depth_dsv_.get(), clear_flags,
                                              depth, stencil);
  }

  void SetDepthConstant(float depth) {
    struct Constants {
      float depth;
      float pad[3];
    } constants = {depth, {0.0f, 0.0f, 0.0f}};
    context_.context()->UpdateSubresource(depth_constants_.get(), 0, nullptr,
                                          &constants, 0, 0);
  }

  void Draw(ID3D11DepthStencilState *depth_state, float depth,
            UINT stencil_ref, ID3D11BlendState *blend_state) {
    SetDepthConstant(depth);
    ID3D11RenderTargetView *targets[] = {color_rtv_.get()};
    ID3D11Buffer *constants[] = {depth_constants_.get()};
    const D3D11_VIEWPORT viewport = {
        0.0f, 0.0f, static_cast<float>(kSize), static_cast<float>(kSize),
        0.0f, 1.0f};
    const FLOAT blend_factor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    context_.context()->OMSetRenderTargets(1, targets, depth_dsv_.get());
    context_.context()->OMSetDepthStencilState(depth_state, stencil_ref);
    context_.context()->OMSetBlendState(blend_state, blend_factor, 0xffffffff);
    context_.context()->RSSetState(rasterizer_.get());
    context_.context()->RSSetViewports(1, &viewport);
    context_.context()->IASetPrimitiveTopology(
        D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_.context()->VSSetShader(vertex_shader_.get(), nullptr, 0);
    context_.context()->VSSetConstantBuffers(0, 1, constants);
    context_.context()->PSSetShader(pixel_shader_.get(), nullptr, 0);
    context_.context()->Draw(3, 0);
    context_.context()->OMSetRenderTargets(0, nullptr, nullptr);
  }

  void ExpectCenterColor(bool green) {
    uint32_t pixel = 0;
    ASSERT_TRUE(HResultSucceeded(ReadTexturePixel(
        context_.device(), context_.context(), color_target_.get(), kSize / 2,
        kSize / 2, &pixel)));
    const uint32_t expected = green ? kGreen : kBlack;
    EXPECT_TRUE(ColorMatches(pixel, expected, 2))
        << "center pixel was 0x" << std::hex << pixel << " expected 0x"
        << expected;
  }

  void RequireStencil() {
    if (!has_stencil_) {
      GTEST_SKIP() << "depth format " << static_cast<int>(depth_format_)
                   << " has no stencil plane";
    }
  }

  D3D11TestContext context_;
  ComPtr<ID3D11VertexShader> vertex_shader_;
  ComPtr<ID3D11PixelShader> pixel_shader_;
  ComPtr<ID3D11Buffer> depth_constants_;
  ComPtr<ID3D11Texture2D> color_target_;
  ComPtr<ID3D11RenderTargetView> color_rtv_;
  ComPtr<ID3D11Texture2D> depth_texture_;
  ComPtr<ID3D11DepthStencilView> depth_dsv_;
  ComPtr<ID3D11RasterizerState> rasterizer_;
  ComPtr<ID3D11BlendState> color_write_all_;
  ComPtr<ID3D11BlendState> color_write_none_;
  DXGI_FORMAT depth_format_ = DXGI_FORMAT_UNKNOWN;
  bool has_stencil_ = false;
};

class DepthCompareMatrixSpec
    : public DepthStencilMatrixFixture,
      public ::testing::WithParamInterface<DepthCompareCase> {
public:
  static std::string
  Name(const ::testing::TestParamInfo<DepthCompareCase> &info) {
    return info.param.name;
  }
};

TEST_P(DepthCompareMatrixSpec, CompareFunctionMatrix) {
  const auto &test_case = GetParam();
  D3D11_DEPTH_STENCIL_DESC desc = {};
  desc.DepthEnable = TRUE;
  desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
  desc.DepthFunc = test_case.function;
  desc.StencilEnable = FALSE;
  auto state = CreateDepthStencilState(desc);
  ASSERT_TRUE(state);

  Clear(test_case.stored, 0);
  Draw(state.get(), test_case.incoming, 0, color_write_all_.get());
  ExpectCenterColor(test_case.passes);
}

INSTANTIATE_TEST_SUITE_P(
    Functions, DepthCompareMatrixSpec,
    ::testing::Values(
        DepthCompareCase{D3D11_COMPARISON_NEVER, 0.25f, 0.5f, false, "Never"},
        DepthCompareCase{D3D11_COMPARISON_LESS, 0.25f, 0.5f, true, "LessPass"},
        DepthCompareCase{D3D11_COMPARISON_LESS, 0.75f, 0.5f, false, "LessFail"},
        DepthCompareCase{D3D11_COMPARISON_EQUAL, 0.5f, 0.5f, true, "EqualPass"},
        DepthCompareCase{D3D11_COMPARISON_EQUAL, 0.25f, 0.5f, false,
                         "EqualFail"},
        DepthCompareCase{D3D11_COMPARISON_LESS_EQUAL, 0.5f, 0.5f, true,
                         "LessEqualPass"},
        DepthCompareCase{D3D11_COMPARISON_LESS_EQUAL, 0.75f, 0.5f, false,
                         "LessEqualFail"},
        DepthCompareCase{D3D11_COMPARISON_GREATER, 0.75f, 0.5f, true,
                         "GreaterPass"},
        DepthCompareCase{D3D11_COMPARISON_GREATER, 0.25f, 0.5f, false,
                         "GreaterFail"},
        DepthCompareCase{D3D11_COMPARISON_NOT_EQUAL, 0.25f, 0.5f, true,
                         "NotEqualPass"},
        DepthCompareCase{D3D11_COMPARISON_NOT_EQUAL, 0.5f, 0.5f, false,
                         "NotEqualFail"},
        DepthCompareCase{D3D11_COMPARISON_GREATER_EQUAL, 0.5f, 0.5f, true,
                         "GreaterEqualPass"},
        DepthCompareCase{D3D11_COMPARISON_GREATER_EQUAL, 0.25f, 0.5f, false,
                         "GreaterEqualFail"},
        DepthCompareCase{D3D11_COMPARISON_ALWAYS, 0.75f, 0.5f, true, "Always"}),
    DepthCompareMatrixSpec::Name);

class StencilCompareMatrixSpec
    : public DepthStencilMatrixFixture,
      public ::testing::WithParamInterface<StencilCompareCase> {
public:
  static std::string
  Name(const ::testing::TestParamInfo<StencilCompareCase> &info) {
    return info.param.name;
  }
};

TEST_P(StencilCompareMatrixSpec, CompareFunctionMatrix) {
  RequireStencil();
  const auto &test_case = GetParam();
  D3D11_DEPTH_STENCIL_DESC desc = {};
  desc.DepthEnable = FALSE;
  desc.StencilEnable = TRUE;
  desc.StencilReadMask = D3D11_DEFAULT_STENCIL_READ_MASK;
  desc.StencilWriteMask = 0;
  desc.FrontFace = StencilFace(test_case.function);
  desc.BackFace = desc.FrontFace;
  auto state = CreateDepthStencilState(desc);
  ASSERT_TRUE(state);

  Clear(1.0f, test_case.stored);
  Draw(state.get(), 0.0f, test_case.reference, color_write_all_.get());
  ExpectCenterColor(test_case.passes);
}

INSTANTIATE_TEST_SUITE_P(
    Functions, StencilCompareMatrixSpec,
    ::testing::Values(
        StencilCompareCase{D3D11_COMPARISON_EQUAL, 3, 3, true, "EqualPass"},
        StencilCompareCase{D3D11_COMPARISON_EQUAL, 3, 2, false, "EqualFail"},
        StencilCompareCase{D3D11_COMPARISON_NOT_EQUAL, 2, 3, true,
                           "NotEqualPass"},
        StencilCompareCase{D3D11_COMPARISON_NOT_EQUAL, 3, 3, false,
                           "NotEqualFail"},
        StencilCompareCase{D3D11_COMPARISON_LESS, 2, 3, true, "LessPass"},
        StencilCompareCase{D3D11_COMPARISON_LESS, 3, 2, false, "LessFail"},
        StencilCompareCase{D3D11_COMPARISON_ALWAYS, 3, 2, true, "Always"},
        StencilCompareCase{D3D11_COMPARISON_NEVER, 3, 3, false, "Never"}),
    StencilCompareMatrixSpec::Name);

class StencilOperationMatrixSpec
    : public DepthStencilMatrixFixture,
      public ::testing::WithParamInterface<StencilOperationCase> {
public:
  static std::string
  Name(const ::testing::TestParamInfo<StencilOperationCase> &info) {
    return info.param.name;
  }
};

// First draw applies a stencil pass op without color writes. Second draw
// validates the resulting stencil value with EQUAL (or NOT_EQUAL) and green.
TEST_P(StencilOperationMatrixSpec, PassOpAndRefMatrix) {
  RequireStencil();
  const auto &test_case = GetParam();

  D3D11_DEPTH_STENCIL_DESC operation_desc = {};
  operation_desc.DepthEnable = FALSE;
  operation_desc.StencilEnable = TRUE;
  operation_desc.StencilReadMask = D3D11_DEFAULT_STENCIL_READ_MASK;
  operation_desc.StencilWriteMask = D3D11_DEFAULT_STENCIL_WRITE_MASK;
  operation_desc.FrontFace =
      StencilFace(D3D11_COMPARISON_ALWAYS, test_case.operation);
  operation_desc.BackFace = operation_desc.FrontFace;
  auto operation_state = CreateDepthStencilState(operation_desc);
  ASSERT_TRUE(operation_state);

  D3D11_DEPTH_STENCIL_DESC validation_desc = {};
  validation_desc.DepthEnable = FALSE;
  validation_desc.StencilEnable = TRUE;
  validation_desc.StencilReadMask = D3D11_DEFAULT_STENCIL_READ_MASK;
  validation_desc.StencilWriteMask = 0;
  validation_desc.FrontFace = StencilFace(test_case.function);
  validation_desc.BackFace = validation_desc.FrontFace;
  auto validation_state = CreateDepthStencilState(validation_desc);
  ASSERT_TRUE(validation_state);

  Clear(1.0f, test_case.initial);
  Draw(operation_state.get(), 0.0f, test_case.reference,
       color_write_none_.get());
  Draw(validation_state.get(), 0.0f, test_case.expected_for_equal,
       color_write_all_.get());
  ExpectCenterColor(test_case.expect_green);
}

INSTANTIATE_TEST_SUITE_P(
    Operations, StencilOperationMatrixSpec,
    ::testing::Values(
        // KEEP leaves stencil at initial=4; EQUAL ref=4 passes.
        StencilOperationCase{D3D11_STENCIL_OP_KEEP, D3D11_COMPARISON_EQUAL, 4,
                             3, 4, true, "KeepEqualPass"},
        // KEEP leaves stencil at 4; EQUAL ref=3 fails.
        StencilOperationCase{D3D11_STENCIL_OP_KEEP, D3D11_COMPARISON_EQUAL, 4,
                             3, 3, false, "KeepEqualFail"},
        // REPLACE writes reference=7; EQUAL ref=7 passes.
        StencilOperationCase{D3D11_STENCIL_OP_REPLACE, D3D11_COMPARISON_EQUAL, 4,
                             7, 7, true, "ReplaceEqualPass"},
        // REPLACE writes 7; NOT_EQUAL ref=4 passes (7 != 4).
        StencilOperationCase{D3D11_STENCIL_OP_REPLACE, D3D11_COMPARISON_NOT_EQUAL,
                             4, 7, 4, true, "ReplaceNotEqualPass"},
        // REPLACE writes 7; NOT_EQUAL ref=7 fails.
        StencilOperationCase{D3D11_STENCIL_OP_REPLACE, D3D11_COMPARISON_NOT_EQUAL,
                             4, 7, 7, false, "ReplaceNotEqualFail"},
        // INCR wraps 4 -> 5; EQUAL ref=5 passes.
        StencilOperationCase{D3D11_STENCIL_OP_INCR, D3D11_COMPARISON_EQUAL, 4, 0,
                             5, true, "IncrEqualPass"},
        // INCR 4 -> 5; EQUAL ref=4 fails.
        StencilOperationCase{D3D11_STENCIL_OP_INCR, D3D11_COMPARISON_EQUAL, 4, 0,
                             4, false, "IncrEqualFail"},
        // INCR wrap 255 -> 0; EQUAL ref=0 passes.
        StencilOperationCase{D3D11_STENCIL_OP_INCR, D3D11_COMPARISON_EQUAL, 255,
                             0, 0, true, "IncrWrapEqualPass"}),
    StencilOperationMatrixSpec::Name);

class DepthWriteMatrixSpec
    : public DepthStencilMatrixFixture,
      public ::testing::WithParamInterface<DepthWriteCase> {
public:
  static std::string
  Name(const ::testing::TestParamInfo<DepthWriteCase> &info) {
    return info.param.name;
  }
};

// First draw at z=0.5 with ALWAYS (and optional depth write). Color writes are
// disabled so only depth is considered. Second draw at z=0.75 with LESS writes
// green when the stored depth is still the clear value (write disabled) and is
// rejected when the first draw wrote 0.5.
TEST_P(DepthWriteMatrixSpec, WriteEnableAffectsSecondDraw) {
  const auto &test_case = GetParam();

  D3D11_DEPTH_STENCIL_DESC first_desc = {};
  first_desc.DepthEnable = TRUE;
  first_desc.DepthWriteMask = test_case.depth_write_enable
                                  ? D3D11_DEPTH_WRITE_MASK_ALL
                                  : D3D11_DEPTH_WRITE_MASK_ZERO;
  first_desc.DepthFunc = D3D11_COMPARISON_ALWAYS;
  first_desc.StencilEnable = FALSE;
  auto first_state = CreateDepthStencilState(first_desc);
  ASSERT_TRUE(first_state);

  D3D11_DEPTH_STENCIL_DESC second_desc = {};
  second_desc.DepthEnable = TRUE;
  second_desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
  second_desc.DepthFunc = D3D11_COMPARISON_LESS;
  second_desc.StencilEnable = FALSE;
  auto second_state = CreateDepthStencilState(second_desc);
  ASSERT_TRUE(second_state);

  Clear(1.0f, 0);
  Draw(first_state.get(), 0.5f, 0, color_write_none_.get());
  Draw(second_state.get(), 0.75f, 0, color_write_all_.get());
  ExpectCenterColor(test_case.second_draw_passes);
}

INSTANTIATE_TEST_SUITE_P(
    WriteMask, DepthWriteMatrixSpec,
    ::testing::Values(
        DepthWriteCase{TRUE, false, "WriteEnabledSecondRejected"},
        DepthWriteCase{FALSE, true, "WriteDisabledSecondAccepted"}),
    DepthWriteMatrixSpec::Name);

} // namespace
