#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace {

using dxmt::test::ComPtr;
using dxmt::test::CompileShader;
using dxmt::test::D3D12TestContext;

constexpr UINT kWidth = 7;
constexpr UINT kHeight = 5;

struct AdvertisedMsaaCase {
  DXGI_FORMAT resource_format;
  DXGI_FORMAT view_format;
  UINT sample_count;
};

struct TextureCapture {
  ComPtr<ID3D12Resource> readback;
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
  UINT row_count = 0;
  UINT64 row_size = 0;
  UINT64 total_size = 0;
  std::vector<std::uint8_t> bytes;
};

struct MsaaExecutionResources {
  AdvertisedMsaaCase test = {};
  ComPtr<ID3D12PipelineState> pipeline;
  ComPtr<ID3D12Resource> source;
  ComPtr<ID3D12Resource> resolved;
  ComPtr<ID3D12Resource> probe;
  ComPtr<ID3D12Resource> reference;
  TextureCapture reference_one;
  TextureCapture reference_zero;
  TextureCapture destination_poison;
  TextureCapture probe_one;
  TextureCapture resolved_zero;
};

class AdvertisedMsaaFormatFamilySpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    const D3D12_ROOT_SIGNATURE_DESC root_desc = {};
    root_ = context_.CreateRootSignature(root_desc);
    ASSERT_TRUE(root_);

    auto vertex = CompileShader(R"(
      float4 main(uint vertex_id : SV_VertexID) : SV_Position {
        const float2 positions[3] = {
          float2(-1.0, -1.0), float2(-1.0, 3.0), float2(3.0, -1.0)
        };
        return float4(positions[vertex_id], 0.5, 1.0);
      }
    )",
                                      "vs_5_0");
    ASSERT_EQ(vertex.result, S_OK) << vertex.diagnostic_text();
    vertex_ = std::move(vertex.bytecode);

    ASSERT_NO_FATAL_FAILURE(CompilePixelShader("float4", &float_pixel_));
    ASSERT_NO_FATAL_FAILURE(CompilePixelShader("uint4", &uint_pixel_));
    ASSERT_NO_FATAL_FAILURE(CompilePixelShader("int4", &sint_pixel_));
  }

  void CompilePixelShader(const char *type, ComPtr<ID3DBlob> *blob) {
    const auto source = std::string(type) +
                        " main() : SV_Target { return " + type +
                        "(1, 1, 1, 1); }";
    auto pixel = CompileShader(source, "ps_5_0");
    ASSERT_EQ(pixel.result, S_OK) << pixel.diagnostic_text();
    *blob = std::move(pixel.bytecode);
  }

  static DXGI_FORMAT TypedColorView(DXGI_FORMAT format) {
    switch (format) {
    case DXGI_FORMAT_R32G32B32A32_TYPELESS:
      return DXGI_FORMAT_R32G32B32A32_FLOAT;
    case DXGI_FORMAT_R32G32B32_TYPELESS:
      return DXGI_FORMAT_R32G32B32_FLOAT;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS:
      return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case DXGI_FORMAT_R32G32_TYPELESS:
      return DXGI_FORMAT_R32G32_FLOAT;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS:
      return DXGI_FORMAT_R10G10B10A2_UNORM;
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
      return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_R16G16_TYPELESS:
      return DXGI_FORMAT_R16G16_FLOAT;
    case DXGI_FORMAT_R32_TYPELESS:
      return DXGI_FORMAT_R32_FLOAT;
    case DXGI_FORMAT_R8G8_TYPELESS:
      return DXGI_FORMAT_R8G8_UNORM;
    case DXGI_FORMAT_R16_TYPELESS:
      return DXGI_FORMAT_R16_FLOAT;
    case DXGI_FORMAT_R8_TYPELESS:
      return DXGI_FORMAT_R8_UNORM;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
      return DXGI_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_B8G8R8X8_TYPELESS:
      return DXGI_FORMAT_B8G8R8X8_UNORM;
    case DXGI_FORMAT_R32G8X24_TYPELESS:
    case DXGI_FORMAT_R24G8_TYPELESS:
    case DXGI_FORMAT_BC1_TYPELESS:
    case DXGI_FORMAT_BC2_TYPELESS:
    case DXGI_FORMAT_BC3_TYPELESS:
    case DXGI_FORMAT_BC4_TYPELESS:
    case DXGI_FORMAT_BC5_TYPELESS:
    case DXGI_FORMAT_BC6H_TYPELESS:
    case DXGI_FORMAT_BC7_TYPELESS:
      return DXGI_FORMAT_UNKNOWN;
    default:
      return format;
    }
  }

  static bool IsUintFormat(DXGI_FORMAT format) {
    switch (format) {
    case DXGI_FORMAT_R32G32B32A32_UINT:
    case DXGI_FORMAT_R32G32B32_UINT:
    case DXGI_FORMAT_R16G16B16A16_UINT:
    case DXGI_FORMAT_R32G32_UINT:
    case DXGI_FORMAT_R10G10B10A2_UINT:
    case DXGI_FORMAT_R8G8B8A8_UINT:
    case DXGI_FORMAT_R16G16_UINT:
    case DXGI_FORMAT_R32_UINT:
    case DXGI_FORMAT_R8G8_UINT:
    case DXGI_FORMAT_R16_UINT:
    case DXGI_FORMAT_R8_UINT:
      return true;
    default:
      return false;
    }
  }

  static bool IsSintFormat(DXGI_FORMAT format) {
    switch (format) {
    case DXGI_FORMAT_R32G32B32A32_SINT:
    case DXGI_FORMAT_R32G32B32_SINT:
    case DXGI_FORMAT_R16G16B16A16_SINT:
    case DXGI_FORMAT_R32G32_SINT:
    case DXGI_FORMAT_R8G8B8A8_SINT:
    case DXGI_FORMAT_R16G16_SINT:
    case DXGI_FORMAT_R32_SINT:
    case DXGI_FORMAT_R8G8_SINT:
    case DXGI_FORMAT_R16_SINT:
    case DXGI_FORMAT_R8_SINT:
      return true;
    default:
      return false;
    }
  }

  ComPtr<ID3D12PipelineState> CreatePipeline(DXGI_FORMAT format,
                                              UINT sample_count) {
    ID3DBlob *pixel = IsUintFormat(format)   ? uint_pixel_.get()
                       : IsSintFormat(format) ? sint_pixel_.get()
                                              : float_pixel_.get();
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
    desc.pRootSignature = root_.get();
    desc.VS = {vertex_->GetBufferPointer(), vertex_->GetBufferSize()};
    desc.PS = {pixel->GetBufferPointer(), pixel->GetBufferSize()};
    desc.BlendState.RenderTarget[0].RenderTargetWriteMask =
        D3D12_COLOR_WRITE_ENABLE_ALL;
    desc.SampleMask = std::numeric_limits<UINT>::max();
    desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    desc.RasterizerState.DepthClipEnable = TRUE;
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = format;
    desc.SampleDesc.Count = sample_count;

    ComPtr<ID3D12PipelineState> pipeline;
    const HRESULT hr = context_.device()->CreateGraphicsPipelineState(
        &desc, IID_PPV_ARGS(pipeline.put()));
    EXPECT_EQ(hr, S_OK) << "format=" << static_cast<UINT>(format)
                        << " samples=" << sample_count;
    return pipeline;
  }

  void DrawOne(ID3D12PipelineState *pipeline,
               D3D12_CPU_DESCRIPTOR_HANDLE render_target) {
    context_.list()->OMSetRenderTargets(1, &render_target, FALSE, nullptr);
    context_.list()->SetGraphicsRootSignature(root_.get());
    context_.list()->SetPipelineState(pipeline);
    context_.list()->IASetPrimitiveTopology(
        D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    const D3D12_VIEWPORT viewport = {
        0.0f, 0.0f, static_cast<FLOAT>(kWidth), static_cast<FLOAT>(kHeight),
        0.0f, 1.0f};
    const D3D12_RECT scissor = {0, 0, static_cast<LONG>(kWidth),
                                static_cast<LONG>(kHeight)};
    context_.list()->RSSetViewports(1, &viewport);
    context_.list()->RSSetScissorRects(1, &scissor);
    context_.list()->DrawInstanced(3, 1, 0, 0);
  }

  D3D12_FEATURE_DATA_FORMAT_SUPPORT FormatSupport(DXGI_FORMAT format,
                                                   HRESULT *result) {
    D3D12_FEATURE_DATA_FORMAT_SUPPORT support = {};
    support.Format = format;
    *result = context_.device()->CheckFeatureSupport(
        D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support));
    return support;
  }

  ComPtr<ID3D12Resource>
  CreateTexture(DXGI_FORMAT format, UINT sample_count,
                D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state) {
    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = kWidth;
    desc.Height = kHeight;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = format;
    desc.SampleDesc.Count = sample_count;
    desc.SampleDesc.Quality = 0;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = flags;

    ComPtr<ID3D12Resource> resource;
    const HRESULT hr = context_.device()->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr,
        IID_PPV_ARGS(resource.put()));
    EXPECT_EQ(hr, S_OK) << "format=" << static_cast<UINT>(format)
                        << " samples=" << sample_count;
    return SUCCEEDED(hr) ? std::move(resource) : ComPtr<ID3D12Resource>{};
  }

  bool RecordCapture(ID3D12Resource *texture, TextureCapture *capture) {
    const auto desc = texture->GetDesc();
    context_.device()->GetCopyableFootprints(
        &desc, 0, 1, 0, &capture->footprint, &capture->row_count,
        &capture->row_size, &capture->total_size);
    if (!capture->row_count || !capture->row_size || !capture->total_size ||
        capture->total_size == UINT64_MAX) {
      ADD_FAILURE() << "invalid readback footprint for format "
                    << static_cast<UINT>(desc.Format);
      return false;
    }
    capture->readback = context_.CreateBuffer(
        capture->total_size, D3D12_HEAP_TYPE_READBACK,
        D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
    if (!capture->readback) {
      ADD_FAILURE() << "failed to create readback buffer for format "
                    << static_cast<UINT>(desc.Format);
      return false;
    }

    D3D12_TEXTURE_COPY_LOCATION destination = {};
    destination.pResource = capture->readback.get();
    destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    destination.PlacedFootprint = capture->footprint;
    D3D12_TEXTURE_COPY_LOCATION source = {};
    source.pResource = texture;
    source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    source.SubresourceIndex = 0;
    context_.list()->CopyTextureRegion(&destination, 0, 0, 0, &source,
                                       nullptr);
    return true;
  }

  bool MapCapture(TextureCapture *capture) {
    void *mapped = nullptr;
    const D3D12_RANGE read_range = {
        0, static_cast<SIZE_T>(capture->total_size)};
    const HRESULT hr = capture->readback->Map(0, &read_range, &mapped);
    if (FAILED(hr)) {
      ADD_FAILURE() << "readback Map returned 0x" << std::hex
                    << static_cast<unsigned long>(hr);
      return false;
    }
    capture->bytes.resize(static_cast<std::size_t>(capture->total_size));
    std::memcpy(capture->bytes.data(), mapped, capture->bytes.size());
    const D3D12_RANGE written_range = {0, 0};
    capture->readback->Unmap(0, &written_range);
    return true;
  }

  static bool IsDefinedByte(DXGI_FORMAT format, UINT64 byte_in_row) {
    // The X channel is not part of the format's value and its physical bits
    // need not match between a direct clear and a hardware resolve.
    if (format == DXGI_FORMAT_B8G8R8X8_UNORM ||
        format == DXGI_FORMAT_B8G8R8X8_UNORM_SRGB)
      return byte_in_row % 4 != 3;
    return true;
  }

  static bool CompatibleLayout(const TextureCapture &left,
                               const TextureCapture &right) {
    return left.row_count == right.row_count &&
           left.row_size == right.row_size &&
           left.bytes.size() >= left.total_size &&
           right.bytes.size() >= right.total_size;
  }

  void ExpectSameVisibleBytes(const TextureCapture &actual,
                              const TextureCapture &expected,
                              DXGI_FORMAT format, UINT sample_count,
                              const char *operation) {
    ASSERT_TRUE(CompatibleLayout(actual, expected))
        << operation << ", format=" << static_cast<UINT>(format)
        << ", samples=" << sample_count;
    for (UINT row = 0; row < actual.row_count; ++row) {
      const auto *actual_row =
          actual.bytes.data() + row * actual.footprint.Footprint.RowPitch;
      const auto *expected_row =
          expected.bytes.data() + row * expected.footprint.Footprint.RowPitch;
      for (UINT64 byte = 0; byte < actual.row_size; ++byte) {
        if (!IsDefinedByte(format, byte))
          continue;
        if (actual_row[byte] == expected_row[byte])
          continue;
        ADD_FAILURE() << operation << " mismatch: format="
                      << static_cast<UINT>(format)
                      << " samples=" << sample_count << " row=" << row
                      << " byte=" << byte << " expected=0x" << std::hex
                      << static_cast<UINT>(expected_row[byte]) << " actual=0x"
                      << static_cast<UINT>(actual_row[byte]);
        return;
      }
    }
  }

  void ExpectDifferentVisibleBytes(const TextureCapture &left,
                                   const TextureCapture &right,
                                   DXGI_FORMAT format, UINT sample_count,
                                   const char *operation) {
    ASSERT_TRUE(CompatibleLayout(left, right))
        << operation << ", format=" << static_cast<UINT>(format)
        << ", samples=" << sample_count;
    for (UINT row = 0; row < left.row_count; ++row) {
      const auto *left_row =
          left.bytes.data() + row * left.footprint.Footprint.RowPitch;
      const auto *right_row =
          right.bytes.data() + row * right.footprint.Footprint.RowPitch;
      for (UINT64 byte = 0; byte < left.row_size; ++byte) {
        if (IsDefinedByte(format, byte) && left_row[byte] != right_row[byte])
          return;
      }
    }
    ADD_FAILURE() << operation << " unexpectedly matched: format="
                  << static_cast<UINT>(format)
                  << " samples=" << sample_count;
  }

  D3D12TestContext context_;
  ComPtr<ID3D12RootSignature> root_;
  ComPtr<ID3DBlob> vertex_;
  ComPtr<ID3DBlob> float_pixel_;
  ComPtr<ID3DBlob> uint_pixel_;
  ComPtr<ID3DBlob> sint_pixel_;
};

TEST_F(AdvertisedMsaaFormatFamilySpec,
       EveryAdvertisedResolvableFormatAndSampleCountClearsAndResolves) {
  constexpr D3D12_FORMAT_SUPPORT1 required =
      D3D12_FORMAT_SUPPORT1_TEXTURE2D | D3D12_FORMAT_SUPPORT1_RENDER_TARGET |
      D3D12_FORMAT_SUPPORT1_MULTISAMPLE_RENDERTARGET |
      D3D12_FORMAT_SUPPORT1_MULTISAMPLE_RESOLVE;
  constexpr std::array<UINT, 3> sample_counts = {2, 4, 8};

  std::vector<AdvertisedMsaaCase> advertised_cases;
  UINT advertised_formats = 0;
  UINT skipped_sample_counts = 0;
  for (UINT value = DXGI_FORMAT_R32G32B32A32_TYPELESS;
       value <= DXGI_FORMAT_V408; ++value) {
    const auto format = static_cast<DXGI_FORMAT>(value);
    HRESULT support_hr = E_FAIL;
    const auto support = FormatSupport(format, &support_hr);
    if (FAILED(support_hr) || (support.Support1 & required) != required)
      continue;
    const auto view_format = TypedColorView(format);
    ASSERT_NE(view_format, DXGI_FORMAT_UNKNOWN)
        << "advertised MSAA render/resolve format has no legal typed color "
           "view mapping: "
        << value;
    HRESULT view_support_hr = E_FAIL;
    const auto view_support = FormatSupport(view_format, &view_support_hr);
    ASSERT_EQ(view_support_hr, S_OK) << "typed view format="
                                     << static_cast<UINT>(view_format);
    ASSERT_EQ(view_support.Support1 & required, required)
        << "resource format " << value << " maps to typed view "
        << static_cast<UINT>(view_format)
        << " without matching render/resolve support";
    ++advertised_formats;

    for (const UINT sample_count : sample_counts) {
      D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS quality = {};
      quality.Format = view_format;
      quality.SampleCount = sample_count;
      const HRESULT quality_hr = context_.device()->CheckFeatureSupport(
          D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &quality,
          sizeof(quality));
      ASSERT_EQ(quality_hr, S_OK)
          << "format=" << value << " samples=" << sample_count;
      if (!quality.NumQualityLevels) {
        ++skipped_sample_counts;
        continue;
      }
      advertised_cases.push_back({format, view_format, sample_count});
    }
  }

  if (advertised_cases.empty())
    GTEST_SKIP() << "no advertised 2x/4x/8x resolvable color format";

  auto rtv_heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
      static_cast<UINT>(advertised_cases.size() * 4), false);
  ASSERT_TRUE(rtv_heap);
  std::vector<MsaaExecutionResources> executions;
  executions.reserve(advertised_cases.size());
  constexpr FLOAT zero[4] = {};
  constexpr FLOAT one[4] = {1.0f, 1.0f, 1.0f, 1.0f};

  for (std::size_t case_index = 0; case_index < advertised_cases.size();
       ++case_index) {
    const auto &test = advertised_cases[case_index];
    SCOPED_TRACE(::testing::Message()
                 << "resource_format="
                 << static_cast<UINT>(test.resource_format)
                 << " view_format=" << static_cast<UINT>(test.view_format)
                 << " samples=" << test.sample_count);
    executions.emplace_back();
    auto &execution = executions.back();
    execution.test = test;
    execution.pipeline = CreatePipeline(test.view_format, test.sample_count);
    execution.source = CreateTexture(
        test.resource_format, test.sample_count,
        D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    execution.resolved = CreateTexture(
        test.resource_format, 1, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    execution.probe = CreateTexture(
        test.resource_format, 1, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    execution.reference = CreateTexture(
        test.resource_format, 1, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    ASSERT_TRUE(execution.pipeline);
    ASSERT_TRUE(execution.source);
    ASSERT_TRUE(execution.resolved);
    ASSERT_TRUE(execution.probe);
    ASSERT_TRUE(execution.reference);
    EXPECT_EQ(execution.source->GetDesc().SampleDesc.Count,
              test.sample_count);
    EXPECT_EQ(execution.resolved->GetDesc().SampleDesc.Count, 1u);

    const UINT descriptor_base = static_cast<UINT>(case_index * 4);
    const auto source_rtv =
        context_.CpuDescriptorHandle(rtv_heap.get(), descriptor_base);
    const auto resolved_rtv =
        context_.CpuDescriptorHandle(rtv_heap.get(), descriptor_base + 1);
    const auto reference_rtv =
        context_.CpuDescriptorHandle(rtv_heap.get(), descriptor_base + 2);
    const auto probe_rtv =
        context_.CpuDescriptorHandle(rtv_heap.get(), descriptor_base + 3);
    D3D12_RENDER_TARGET_VIEW_DESC source_view = {};
    source_view.Format = test.view_format;
    source_view.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMS;
    context_.device()->CreateRenderTargetView(execution.source.get(),
                                               &source_view, source_rtv);
    D3D12_RENDER_TARGET_VIEW_DESC single_view = {};
    single_view.Format = test.view_format;
    single_view.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    context_.device()->CreateRenderTargetView(execution.resolved.get(),
                                               &single_view, resolved_rtv);
    context_.device()->CreateRenderTargetView(execution.reference.get(),
                                               &single_view, reference_rtv);
    context_.device()->CreateRenderTargetView(execution.probe.get(),
                                               &single_view, probe_rtv);

    // Establish both clear encodings directly in this format. The all-one
    // resolved texture is a deterministic poison value for the final zero
    // resolve, while the direct clears are exact per-format reference oracles.
    context_.list()->ClearRenderTargetView(reference_rtv, one, 0, nullptr);
    context_.list()->ClearRenderTargetView(resolved_rtv, one, 0, nullptr);
    context_.list()->ClearRenderTargetView(source_rtv, zero, 0, nullptr);
    context_.list()->ClearRenderTargetView(probe_rtv, zero, 0, nullptr);
    DrawOne(execution.pipeline.get(), source_rtv);
    D3D12TestContext::Transition(
        context_.list(), execution.reference.get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12TestContext::Transition(
        context_.list(), execution.resolved.get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_COPY_SOURCE);
    ASSERT_TRUE(RecordCapture(execution.reference.get(),
                              &execution.reference_one));
    ASSERT_TRUE(RecordCapture(execution.resolved.get(),
                              &execution.destination_poison));

    D3D12TestContext::Transition(
        context_.list(), execution.reference.get(),
        D3D12_RESOURCE_STATE_COPY_SOURCE,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    context_.list()->ClearRenderTargetView(reference_rtv, zero, 0, nullptr);
    D3D12TestContext::Transition(
        context_.list(), execution.reference.get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_COPY_SOURCE);
    ASSERT_TRUE(RecordCapture(execution.reference.get(),
                              &execution.reference_zero));

    D3D12TestContext::Transition(
        context_.list(), execution.resolved.get(),
        D3D12_RESOURCE_STATE_COPY_SOURCE,
        D3D12_RESOURCE_STATE_RESOLVE_DEST);
    D3D12TestContext::Transition(
        context_.list(), execution.probe.get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_RESOLVE_DEST);
    D3D12TestContext::Transition(
        context_.list(), execution.source.get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
    context_.list()->ResolveSubresource(execution.probe.get(), 0,
                                        execution.source.get(), 0,
                                        test.view_format);
    D3D12TestContext::Transition(
        context_.list(), execution.probe.get(),
        D3D12_RESOURCE_STATE_RESOLVE_DEST,
        D3D12_RESOURCE_STATE_COPY_SOURCE);
    ASSERT_TRUE(
        RecordCapture(execution.probe.get(), &execution.probe_one));

    D3D12TestContext::Transition(
        context_.list(), execution.source.get(),
        D3D12_RESOURCE_STATE_RESOLVE_SOURCE,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    context_.list()->ClearRenderTargetView(source_rtv, zero, 0, nullptr);
    D3D12TestContext::Transition(
        context_.list(), execution.source.get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
    context_.list()->ResolveSubresource(execution.resolved.get(), 0,
                                        execution.source.get(), 0,
                                        test.view_format);
    D3D12TestContext::Transition(
        context_.list(), execution.resolved.get(),
        D3D12_RESOURCE_STATE_RESOLVE_DEST,
        D3D12_RESOURCE_STATE_COPY_SOURCE);
    ASSERT_TRUE(RecordCapture(execution.resolved.get(),
                              &execution.resolved_zero));
  }

  ASSERT_EQ(context_.ExecuteAndWait(), S_OK);
  std::string matrix;
  for (auto &execution : executions) {
    const auto format = execution.test.view_format;
    const auto sample_count = execution.test.sample_count;
    SCOPED_TRACE(::testing::Message()
                 << "format=" << static_cast<UINT>(format)
                 << " samples=" << sample_count);
    ASSERT_TRUE(MapCapture(&execution.reference_one));
    ASSERT_TRUE(MapCapture(&execution.reference_zero));
    ASSERT_TRUE(MapCapture(&execution.destination_poison));
    ASSERT_TRUE(MapCapture(&execution.probe_one));
    ASSERT_TRUE(MapCapture(&execution.resolved_zero));

    ExpectDifferentVisibleBytes(execution.reference_one,
                                execution.reference_zero, format,
                                sample_count, "one/zero clear oracle");
    ExpectSameVisibleBytes(execution.destination_poison,
                           execution.reference_one, format, sample_count,
                           "destination poison clear");
    ExpectSameVisibleBytes(execution.probe_one, execution.reference_one,
                           format, sample_count, "one clear resolve");
    ExpectSameVisibleBytes(execution.resolved_zero,
                           execution.reference_zero, format, sample_count,
                           "zero clear resolve");
    ExpectDifferentVisibleBytes(execution.destination_poison,
                                execution.resolved_zero, format, sample_count,
                                "resolved result replaced poison");

    if (!matrix.empty())
      matrix.push_back(',');
    matrix +=
        std::to_string(static_cast<UINT>(execution.test.resource_format));
    if (execution.test.resource_format != format) {
      matrix.push_back('/');
      matrix += std::to_string(static_cast<UINT>(format));
    }
    matrix.push_back('x');
    matrix += std::to_string(sample_count);
  }

  RecordProperty("advertised_formats", static_cast<int>(advertised_formats));
  RecordProperty("msaa_cases_executed", static_cast<int>(executions.size()));
  RecordProperty("sample_counts_skipped",
                 static_cast<int>(skipped_sample_counts));
  RecordProperty("executed_matrix", matrix);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
  ASSERT_EQ(context_.ResetCommandList(), S_OK);
}

} // namespace
