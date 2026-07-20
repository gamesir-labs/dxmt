#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d11_test_context.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>

// Real D3D11 public-API query matrix: occlusion sample counts, timestamp
// ordering, and occlusion-predicate draw suppression. No DXMT internals.

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

// Matches pipeline_spec solid color: R=0.25 G=0.5 B=0.75 A=1 → 0xffbf8040.
constexpr std::string_view kSolidPixelShader = R"(
float4 main() : SV_Target {
  return float4(0.25, 0.5, 0.75, 1.0);
}
)";

constexpr uint32_t kSolidPixelColor = 0xffbf8040u;
constexpr UINT kTargetSize = 8;

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

// Poll GetData until ready or attempts exhausted. Returns last HRESULT.
template <typename T>
HRESULT WaitForQueryData(ID3D11DeviceContext *context, ID3D11Asynchronous *query,
                         T *out, UINT attempts = 100) {
  HRESULT data_hr = S_FALSE;
  for (UINT attempt = 0; attempt < attempts && data_hr == S_FALSE; ++attempt) {
    data_hr = context->GetData(query, out, sizeof(*out), 0);
    if (data_hr == S_FALSE)
      Sleep(1);
  }
  return data_hr;
}

class D3D11QueryMatrixSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_TRUE(HResultSucceeded(context_.Initialize()));
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_NE(context_.context(), nullptr);

    const auto vertex = CompileShader(kFullscreenVertexShader, "vs_5_0");
    const auto pixel = CompileShader(kSolidPixelShader, "ps_5_0");
    ASSERT_TRUE(HResultSucceeded(vertex.result)) << vertex.diagnostic_text();
    ASSERT_TRUE(HResultSucceeded(pixel.result)) << pixel.diagnostic_text();
    ASSERT_TRUE(HResultSucceeded(context_.device()->CreateVertexShader(
        vertex.bytecode->GetBufferPointer(), vertex.bytecode->GetBufferSize(),
        nullptr, vertex_shader_.put())));
    ASSERT_TRUE(HResultSucceeded(context_.device()->CreatePixelShader(
        pixel.bytecode->GetBufferPointer(), pixel.bytecode->GetBufferSize(),
        nullptr, pixel_shader_.put())));

    D3D11_TEXTURE2D_DESC target_desc = {};
    target_desc.Width = kTargetSize;
    target_desc.Height = kTargetSize;
    target_desc.MipLevels = 1;
    target_desc.ArraySize = 1;
    target_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    target_desc.SampleDesc.Count = 1;
    target_desc.Usage = D3D11_USAGE_DEFAULT;
    target_desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    ASSERT_TRUE(HResultSucceeded(context_.device()->CreateTexture2D(
        &target_desc, nullptr, target_.put())));
    ASSERT_TRUE(HResultSucceeded(context_.device()->CreateRenderTargetView(
        target_.get(), nullptr, target_view_.put())));

    D3D11_RASTERIZER_DESC rasterizer_desc = {};
    rasterizer_desc.FillMode = D3D11_FILL_SOLID;
    rasterizer_desc.CullMode = D3D11_CULL_NONE;
    rasterizer_desc.DepthClipEnable = TRUE;
    rasterizer_desc.ScissorEnable = TRUE;
    ASSERT_TRUE(HResultSucceeded(context_.device()->CreateRasterizerState(
        &rasterizer_desc, scissor_rasterizer_.put())));

    viewport_ = {};
    viewport_.Width = static_cast<float>(kTargetSize);
    viewport_.Height = static_cast<float>(kTargetSize);
    viewport_.MaxDepth = 1.0f;
  }

  void BindFullscreenDrawState() {
    ID3D11RenderTargetView *targets[] = {target_view_.get()};
    context_.context()->OMSetRenderTargets(1, targets, nullptr);
    context_.context()->RSSetViewports(1, &viewport_);
    context_.context()->IASetPrimitiveTopology(
        D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_.context()->VSSetShader(vertex_shader_.get(), nullptr, 0);
    context_.context()->PSSetShader(pixel_shader_.get(), nullptr, 0);
  }

  void UnbindRenderTargets() {
    context_.context()->OMSetRenderTargets(0, nullptr, nullptr);
  }

  UINT64 IssueOcclusionQuery(const D3D11_RECT *scissor_or_null, bool draw) {
    D3D11_QUERY_DESC query_desc = {};
    query_desc.Query = D3D11_QUERY_OCCLUSION;
    ComPtr<ID3D11Query> query;
    EXPECT_TRUE(HResultSucceeded(
        context_.device()->CreateQuery(&query_desc, query.put())));
    if (!query)
      return 0;

    BindFullscreenDrawState();
    if (scissor_or_null) {
      context_.context()->RSSetState(scissor_rasterizer_.get());
      context_.context()->RSSetScissorRects(1, scissor_or_null);
    } else {
      context_.context()->RSSetState(nullptr);
    }

    context_.context()->Begin(query.get());
    if (draw)
      context_.context()->Draw(3, 0);
    context_.context()->End(query.get());
    UnbindRenderTargets();
    context_.context()->Flush();

    UINT64 samples = 0;
    const HRESULT data_hr =
        WaitForQueryData(context_.context(), query.get(), &samples);
    EXPECT_EQ(data_hr, S_OK);
    return samples;
  }

  D3D11TestContext context_;
  ComPtr<ID3D11VertexShader> vertex_shader_;
  ComPtr<ID3D11PixelShader> pixel_shader_;
  ComPtr<ID3D11Texture2D> target_;
  ComPtr<ID3D11RenderTargetView> target_view_;
  ComPtr<ID3D11RasterizerState> scissor_rasterizer_;
  D3D11_VIEWPORT viewport_ = {};
};

// ---------------------------------------------------------------------------
// Occlusion
// ---------------------------------------------------------------------------

TEST_F(D3D11QueryMatrixSpec, OcclusionReportsSamplesForCoveringDraw) {
  D3D11_QUERY_DESC query_desc = {};
  query_desc.Query = D3D11_QUERY_OCCLUSION;
  ComPtr<ID3D11Query> query;
  ASSERT_TRUE(HResultSucceeded(
      context_.device()->CreateQuery(&query_desc, query.put())));

  BindFullscreenDrawState();
  context_.context()->RSSetState(nullptr);
  context_.context()->Begin(query.get());
  context_.context()->Draw(3, 0);
  context_.context()->End(query.get());
  UnbindRenderTargets();
  context_.context()->Flush();

  UINT64 samples = 0;
  ASSERT_EQ(WaitForQueryData(context_.context(), query.get(), &samples), S_OK);
  EXPECT_GT(samples, 0ull);
}

TEST_F(D3D11QueryMatrixSpec, OcclusionReportsZeroForEmptyBracket) {
  D3D11_QUERY_DESC query_desc = {};
  query_desc.Query = D3D11_QUERY_OCCLUSION;
  ComPtr<ID3D11Query> query;
  ASSERT_TRUE(HResultSucceeded(
      context_.device()->CreateQuery(&query_desc, query.put())));

  BindFullscreenDrawState();
  context_.context()->Begin(query.get());
  // No draw — bracket is empty.
  context_.context()->End(query.get());
  UnbindRenderTargets();
  context_.context()->Flush();

  UINT64 samples = 0;
  ASSERT_EQ(WaitForQueryData(context_.context(), query.get(), &samples), S_OK);
  EXPECT_EQ(samples, 0ull);
}

TEST_F(D3D11QueryMatrixSpec, OcclusionReportsZeroForScissoredOffDraw) {
  D3D11_QUERY_DESC query_desc = {};
  query_desc.Query = D3D11_QUERY_OCCLUSION;
  ComPtr<ID3D11Query> query;
  ASSERT_TRUE(HResultSucceeded(
      context_.device()->CreateQuery(&query_desc, query.put())));

  constexpr D3D11_RECT empty_scissor = {0, 0, 0, 0};
  BindFullscreenDrawState();
  context_.context()->RSSetState(scissor_rasterizer_.get());
  context_.context()->RSSetScissorRects(1, &empty_scissor);
  context_.context()->Begin(query.get());
  context_.context()->Draw(3, 0);
  context_.context()->End(query.get());
  UnbindRenderTargets();
  context_.context()->Flush();

  UINT64 samples = 0;
  ASSERT_EQ(WaitForQueryData(context_.context(), query.get(), &samples), S_OK);
  EXPECT_EQ(samples, 0ull);
}

TEST_F(D3D11QueryMatrixSpec, OcclusionCoveringVsScissoredOffSampleCounts) {
  constexpr D3D11_RECT full_scissor = {0, 0, static_cast<LONG>(kTargetSize),
                                       static_cast<LONG>(kTargetSize)};
  constexpr D3D11_RECT empty_scissor = {0, 0, 0, 0};

  const UINT64 covering = IssueOcclusionQuery(&full_scissor, true);
  const UINT64 scissored_off = IssueOcclusionQuery(&empty_scissor, true);
  const UINT64 empty_bracket = IssueOcclusionQuery(nullptr, false);

  EXPECT_GT(covering, 0ull);
  EXPECT_EQ(scissored_off, 0ull);
  EXPECT_EQ(empty_bracket, 0ull);
}

// ---------------------------------------------------------------------------
// Timestamp
// ---------------------------------------------------------------------------

TEST_F(D3D11QueryMatrixSpec, TwoTimestampsAreMonotonicallyOrdered) {
  D3D11_QUERY_DESC query_desc = {};
  query_desc.Query = D3D11_QUERY_TIMESTAMP;
  ComPtr<ID3D11Query> first;
  ComPtr<ID3D11Query> second;
  const HRESULT first_hr =
      context_.device()->CreateQuery(&query_desc, first.put());
  const HRESULT second_hr =
      context_.device()->CreateQuery(&query_desc, second.put());
  if (FAILED(first_hr) || FAILED(second_hr) || !first || !second) {
    GTEST_SKIP() << "D3D11_QUERY_TIMESTAMP is not supported";
  }

  // Optional disjoint bracket — skip Disjoint checks if CreateQuery fails.
  ComPtr<ID3D11Query> disjoint;
  D3D11_QUERY_DESC disjoint_desc = {};
  disjoint_desc.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
  const bool have_disjoint = SUCCEEDED(
      context_.device()->CreateQuery(&disjoint_desc, disjoint.put()));

  if (have_disjoint)
    context_.context()->Begin(disjoint.get());

  context_.context()->End(first.get());
  // Insert GPU work so the clock can advance between stamps.
  BindFullscreenDrawState();
  context_.context()->Draw(3, 0);
  UnbindRenderTargets();
  context_.context()->End(second.get());

  if (have_disjoint)
    context_.context()->End(disjoint.get());

  context_.context()->Flush();

  if (have_disjoint) {
    D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint_data = {};
    ASSERT_EQ(
        WaitForQueryData(context_.context(), disjoint.get(), &disjoint_data),
        S_OK);
    // Disjoint==TRUE makes absolute ordering across the bracket unreliable.
    if (disjoint_data.Disjoint)
      GTEST_SKIP() << "timestamp discontinuous across the sampled bracket";
    EXPECT_GT(disjoint_data.Frequency, 0ull);
  }

  UINT64 stamp0 = 0;
  UINT64 stamp1 = 0;
  ASSERT_EQ(WaitForQueryData(context_.context(), first.get(), &stamp0), S_OK);
  ASSERT_EQ(WaitForQueryData(context_.context(), second.get(), &stamp1), S_OK);
  EXPECT_GE(stamp1, stamp0);
}

// ---------------------------------------------------------------------------
// Occlusion predicate / SetPredication / GetPredication (public state API)
// ---------------------------------------------------------------------------

TEST_F(D3D11QueryMatrixSpec, OcclusionPredicateDataReflectsCoveringDraw) {
  D3D11_QUERY_DESC predicate_desc = {};
  predicate_desc.Query = D3D11_QUERY_OCCLUSION_PREDICATE;
  ComPtr<ID3D11Predicate> predicate;
  const HRESULT create_hr =
      context_.device()->CreatePredicate(&predicate_desc, predicate.put());
  if (FAILED(create_hr) || !predicate) {
    GTEST_SKIP() << "D3D11_QUERY_OCCLUSION_PREDICATE is not supported";
  }

  BindFullscreenDrawState();
  context_.context()->RSSetState(nullptr);
  context_.context()->Begin(predicate.get());
  context_.context()->Draw(3, 0);
  context_.context()->End(predicate.get());
  UnbindRenderTargets();
  context_.context()->Flush();

  BOOL predicate_data = FALSE;
  ASSERT_EQ(
      WaitForQueryData(context_.context(), predicate.get(), &predicate_data),
      S_OK);
  EXPECT_TRUE(predicate_data);
}

TEST_F(D3D11QueryMatrixSpec, OcclusionPredicateDataReflectsEmptyBracket) {
  D3D11_QUERY_DESC predicate_desc = {};
  predicate_desc.Query = D3D11_QUERY_OCCLUSION_PREDICATE;
  ComPtr<ID3D11Predicate> predicate;
  const HRESULT create_hr =
      context_.device()->CreatePredicate(&predicate_desc, predicate.put());
  if (FAILED(create_hr) || !predicate) {
    GTEST_SKIP() << "D3D11_QUERY_OCCLUSION_PREDICATE is not supported";
  }

  context_.context()->Begin(predicate.get());
  context_.context()->End(predicate.get());
  context_.context()->Flush();

  BOOL predicate_data = TRUE;
  ASSERT_EQ(
      WaitForQueryData(context_.context(), predicate.get(), &predicate_data),
      S_OK);
  EXPECT_FALSE(predicate_data);
}

TEST_F(D3D11QueryMatrixSpec, SetPredicationRoundTripsThroughGetPredication) {
  D3D11_QUERY_DESC predicate_desc = {};
  predicate_desc.Query = D3D11_QUERY_OCCLUSION_PREDICATE;
  ComPtr<ID3D11Predicate> predicate;
  const HRESULT create_hr =
      context_.device()->CreatePredicate(&predicate_desc, predicate.put());
  if (FAILED(create_hr) || !predicate) {
    GTEST_SKIP() << "D3D11_QUERY_OCCLUSION_PREDICATE is not supported";
  }

  // Fill the predicate so it is a live completed object (data not required for
  // GetPredication state).
  BindFullscreenDrawState();
  context_.context()->RSSetState(nullptr);
  context_.context()->Begin(predicate.get());
  context_.context()->Draw(3, 0);
  context_.context()->End(predicate.get());
  UnbindRenderTargets();
  context_.context()->Flush();
  BOOL data = FALSE;
  ASSERT_EQ(WaitForQueryData(context_.context(), predicate.get(), &data), S_OK);

  context_.context()->SetPredication(predicate.get(), TRUE);
  {
    ID3D11Predicate *got = nullptr;
    BOOL value = FALSE;
    context_.context()->GetPredication(&got, &value);
    EXPECT_EQ(got, predicate.get());
    EXPECT_TRUE(value);
    if (got)
      got->Release();
  }

  context_.context()->SetPredication(predicate.get(), FALSE);
  {
    ID3D11Predicate *got = nullptr;
    BOOL value = TRUE;
    context_.context()->GetPredication(&got, &value);
    EXPECT_EQ(got, predicate.get());
    EXPECT_FALSE(value);
    if (got)
      got->Release();
  }

  context_.context()->SetPredication(nullptr, FALSE);
  {
    ID3D11Predicate *got = reinterpret_cast<ID3D11Predicate *>(0x1);
    BOOL value = TRUE;
    context_.context()->GetPredication(&got, &value);
    EXPECT_EQ(got, nullptr);
    EXPECT_FALSE(value);
  }
}

} // namespace
