#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"
#include "shaders/runtime_test_shaders.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <utility>
#include <vector>

namespace {

using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;
using dxmt::test::DualSourcePixelShader;
using dxmt::test::FullscreenVertexShader;
using dxmt::test::TextureReadback;

class GraphicsOutputMergerSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    auto all_targets = CompileShader(R"(
      struct Output {
        float4 target0 : SV_Target0;
        float4 target1 : SV_Target1;
        float4 target2 : SV_Target2;
        float4 target3 : SV_Target3;
        float4 target4 : SV_Target4;
        float4 target5 : SV_Target5;
        float4 target6 : SV_Target6;
        float4 target7 : SV_Target7;
      };
      Output main() {
        Output output;
        output.target0 = float4(1.0, 0.0, 0.0, 1.0);
        output.target1 = float4(0.0, 1.0, 0.0, 1.0);
        output.target2 = float4(0.0, 0.0, 1.0, 1.0);
        output.target3 = float4(1.0, 1.0, 0.0, 1.0);
        output.target4 = float4(0.0, 1.0, 1.0, 1.0);
        output.target5 = float4(1.0, 0.0, 1.0, 1.0);
        output.target6 = float4(1.0, 1.0, 1.0, 1.0);
        output.target7 = float4(0.0, 0.0, 0.0, 1.0);
        return output;
      })",
                                     "ps_5_0");
    ASSERT_EQ(all_targets.result, S_OK) << all_targets.diagnostic_text();
    all_targets_shader_ = std::move(all_targets.bytecode);

    auto first_target = CompileShader(
        "float4 main() : SV_Target0 { return float4(1, 0, 0, 1); }", "ps_5_0");
    ASSERT_EQ(first_target.result, S_OK) << first_target.diagnostic_text();
    first_target_shader_ = std::move(first_target.bytecode);

    D3D12_ROOT_SIGNATURE_DESC root_desc = {};
    root_desc.Flags =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    root_signature_ = context_.CreateRootSignature(root_desc);
    ASSERT_TRUE(root_signature_);
  }

  static D3D12_RENDER_TARGET_BLEND_DESC
  BlendTarget(UINT8 write_mask = D3D12_COLOR_WRITE_ENABLE_ALL) {
    D3D12_RENDER_TARGET_BLEND_DESC blend = {};
    blend.SrcBlend = D3D12_BLEND_ONE;
    blend.DestBlend = D3D12_BLEND_ZERO;
    blend.BlendOp = D3D12_BLEND_OP_ADD;
    blend.SrcBlendAlpha = D3D12_BLEND_ONE;
    blend.DestBlendAlpha = D3D12_BLEND_ZERO;
    blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blend.RenderTargetWriteMask = write_mask;
    return blend;
  }

  ComPtr<ID3D12PipelineState> CreatePipeline(
      const std::vector<DXGI_FORMAT> &formats,
      const std::vector<D3D12_RENDER_TARGET_BLEND_DESC> &blend_states,
      ID3DBlob *pixel_shader) {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
    desc.pRootSignature = root_signature_.get();
    desc.VS = FullscreenVertexShader();
    desc.PS = {pixel_shader->GetBufferPointer(), pixel_shader->GetBufferSize()};
    desc.BlendState.IndependentBlendEnable = !blend_states.empty();
    for (UINT i = 0; i < formats.size(); ++i) {
      desc.BlendState.RenderTarget[i] =
          blend_states.empty() ? BlendTarget() : blend_states[i];
      desc.RTVFormats[i] = formats[i];
    }
    desc.SampleMask = UINT_MAX;
    desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets = static_cast<UINT>(formats.size());
    desc.SampleDesc.Count = 1;
    ComPtr<ID3D12PipelineState> pipeline;
    EXPECT_EQ(context_.device()->CreateGraphicsPipelineState(
                  &desc, IID_PPV_ARGS(pipeline.put())),
              S_OK);
    return pipeline;
  }

  void RecordFullscreenDraw(ID3D12PipelineState *pipeline) {
    context_.list()->SetGraphicsRootSignature(root_signature_.get());
    context_.list()->SetPipelineState(pipeline);
    context_.list()->IASetPrimitiveTopology(
        D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    const D3D12_VIEWPORT viewport = {
        0.0f, 0.0f, static_cast<float>(kSize), static_cast<float>(kSize),
        0.0f, 1.0f};
    const D3D12_RECT scissor = {0, 0, kSize, kSize};
    context_.list()->RSSetViewports(1, &viewport);
    context_.list()->RSSetScissorRects(1, &scissor);
    context_.list()->DrawInstanced(3, 1, 0, 0);
  }

  static std::vector<std::uint8_t>
  ExpectedPixel(DXGI_FORMAT format, const std::array<float, 4> &color) {
    auto unorm = [](float value, UINT maximum) {
      return static_cast<UINT>(std::lround(std::clamp(value, 0.0f, 1.0f) *
                                           static_cast<float>(maximum)));
    };
    if (format == DXGI_FORMAT_R8G8B8A8_UNORM ||
        format == DXGI_FORMAT_B8G8R8A8_UNORM) {
      std::vector<std::uint8_t> bytes(4);
      bytes[0] = static_cast<std::uint8_t>(
          unorm(color[format == DXGI_FORMAT_R8G8B8A8_UNORM ? 0 : 2], 255));
      bytes[1] = static_cast<std::uint8_t>(unorm(color[1], 255));
      bytes[2] = static_cast<std::uint8_t>(
          unorm(color[format == DXGI_FORMAT_R8G8B8A8_UNORM ? 2 : 0], 255));
      bytes[3] = static_cast<std::uint8_t>(unorm(color[3], 255));
      return bytes;
    }
    if (format == DXGI_FORMAT_R10G10B10A2_UNORM) {
      const std::uint32_t packed =
          unorm(color[0], 1023) | (unorm(color[1], 1023) << 10) |
          (unorm(color[2], 1023) << 20) | (unorm(color[3], 3) << 30);
      std::vector<std::uint8_t> bytes(sizeof(packed));
      std::memcpy(bytes.data(), &packed, sizeof(packed));
      return bytes;
    }
    if (format == DXGI_FORMAT_R32_FLOAT) {
      std::vector<std::uint8_t> bytes(sizeof(float));
      std::memcpy(bytes.data(), color.data(), sizeof(float));
      return bytes;
    }
    return {};
  }

  struct TargetBinding {
    DXGI_FORMAT format;
    bool present;
  };

  void Run(const std::vector<DXGI_FORMAT> &formats,
           const std::vector<std::array<float, 4>> &expected,
           const std::vector<std::array<float, 4>> &clear_colors = {},
           const std::vector<D3D12_RENDER_TARGET_BLEND_DESC> &blend_states = {},
           const std::vector<TargetBinding> &bindings = {},
           ID3DBlob *pixel_shader = nullptr) {
    ASSERT_FALSE(formats.empty());
    ASSERT_LE(formats.size(), D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT);
    ASSERT_EQ(expected.size(), formats.size());
    ASSERT_TRUE(clear_colors.empty() || clear_colors.size() == formats.size());
    ASSERT_TRUE(blend_states.empty() || blend_states.size() == formats.size());
    ASSERT_TRUE(bindings.empty() || bindings.size() == formats.size());

    auto pipeline =
        CreatePipeline(formats, blend_states,
                       pixel_shader ? pixel_shader : all_targets_shader_.get());
    ASSERT_TRUE(pipeline);
    auto heap =
        context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
                                      static_cast<UINT>(formats.size()), false);
    ASSERT_TRUE(heap);

    std::vector<ComPtr<ID3D12Resource>> targets(formats.size());
    std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> handles(formats.size());
    for (UINT i = 0; i < formats.size(); ++i) {
      handles[i] = context_.CpuDescriptorHandle(heap.get(), i);
      const TargetBinding binding =
          bindings.empty() ? TargetBinding{formats[i], true} : bindings[i];
      if (binding.present) {
        targets[i] =
            context_.CreateTexture2D(kSize, kSize, 1, binding.format,
                                     D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
                                     D3D12_RESOURCE_STATE_RENDER_TARGET);
        ASSERT_TRUE(targets[i]) << "target " << i;
        context_.device()->CreateRenderTargetView(targets[i].get(), nullptr,
                                                  handles[i]);
        const auto &clear = clear_colors.empty() ? kBlack : clear_colors[i];
        context_.list()->ClearRenderTargetView(handles[i], clear.data(), 0,
                                               nullptr);
      } else {
        D3D12_RENDER_TARGET_VIEW_DESC null_desc = {};
        null_desc.Format = binding.format;
        null_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        context_.device()->CreateRenderTargetView(nullptr, &null_desc,
                                                  handles[i]);
      }
    }

    context_.list()->OMSetRenderTargets(static_cast<UINT>(handles.size()),
                                        handles.data(), FALSE, nullptr);
    RecordFullscreenDraw(pipeline.get());

    struct ReadbackSlot {
      UINT target;
      D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
    };
    std::vector<ReadbackSlot> slots;
    UINT64 buffer_size = 0;
    for (UINT i = 0; i < targets.size(); ++i) {
      if (!targets[i])
        continue;
      D3D12TestContext::Transition(context_.list(), targets[i].get(),
                                   D3D12_RESOURCE_STATE_RENDER_TARGET,
                                   D3D12_RESOURCE_STATE_COPY_SOURCE);
      D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
      UINT64 size = 0;
      const auto desc = targets[i]->GetDesc();
      context_.device()->GetCopyableFootprints(&desc, 0, 1, 0, &footprint,
                                               nullptr, nullptr, &size);
      buffer_size = (buffer_size + D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1) &
                    ~(D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1);
      footprint.Offset = buffer_size;
      buffer_size += size;
      slots.push_back({i, footprint});
    }
    auto readback = context_.CreateBuffer(buffer_size, D3D12_HEAP_TYPE_READBACK,
                                          D3D12_RESOURCE_FLAG_NONE,
                                          D3D12_RESOURCE_STATE_COPY_DEST);
    ASSERT_TRUE(readback);
    for (const auto &slot : slots) {
      D3D12_TEXTURE_COPY_LOCATION destination = {};
      destination.pResource = readback.get();
      destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
      destination.PlacedFootprint = slot.footprint;
      D3D12_TEXTURE_COPY_LOCATION source = {};
      source.pResource = targets[slot.target].get();
      source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
      context_.list()->CopyTextureRegion(&destination, 0, 0, 0, &source,
                                         nullptr);
    }
    ASSERT_EQ(context_.ExecuteAndWait(), S_OK);

    void *mapping = nullptr;
    const D3D12_RANGE read_range = {0, static_cast<SIZE_T>(buffer_size)};
    ASSERT_EQ(readback->Map(0, &read_range, &mapping), S_OK);
    const auto *bytes = static_cast<const std::uint8_t *>(mapping);
    for (const auto &slot : slots) {
      const auto pixel = ExpectedPixel(targets[slot.target]->GetDesc().Format,
                                       expected[slot.target]);
      ASSERT_FALSE(pixel.empty()) << "target " << slot.target;
      for (UINT y = 0; y < kSize; ++y) {
        for (UINT x = 0; x < kSize; ++x) {
          const auto *actual = bytes + slot.footprint.Offset +
                               y * slot.footprint.Footprint.RowPitch +
                               x * pixel.size();
          const std::vector<std::uint8_t> actual_pixel(actual,
                                                       actual + pixel.size());
          EXPECT_EQ(actual_pixel, pixel) << "target " << slot.target
                                         << " pixel (" << x << ", " << y << ")";
        }
      }
    }
    const D3D12_RANGE written = {0, 0};
    readback->Unmap(0, &written);
  }

  static constexpr UINT kSize = 4;
  static constexpr std::array<float, 4> kBlack = {};
  static constexpr std::array<std::array<float, 4>, 8> kColors = {{
      {1.0f, 0.0f, 0.0f, 1.0f},
      {0.0f, 1.0f, 0.0f, 1.0f},
      {0.0f, 0.0f, 1.0f, 1.0f},
      {1.0f, 1.0f, 0.0f, 1.0f},
      {0.0f, 1.0f, 1.0f, 1.0f},
      {1.0f, 0.0f, 1.0f, 1.0f},
      {1.0f, 1.0f, 1.0f, 1.0f},
      {0.0f, 0.0f, 0.0f, 1.0f},
  }};
  D3D12TestContext context_;
  ComPtr<ID3DBlob> all_targets_shader_;
  ComPtr<ID3DBlob> first_target_shader_;
  ComPtr<ID3D12RootSignature> root_signature_;
};

class GraphicsOutputMergerCountSpec
    : public GraphicsOutputMergerSpec,
      public ::testing::WithParamInterface<UINT> {
public:
  static const char *Name(const ::testing::TestParamInfo<UINT> &info) {
    switch (info.param) {
    case 1:
      return "OneTarget";
    case 2:
      return "TwoTargets";
    case 4:
      return "FourTargets";
    case 8:
      return "EightTargets";
    }
    return "Unknown";
  }
};

TEST_P(GraphicsOutputMergerCountSpec, MultipleRenderTargetMatrix) {
  const UINT count = GetParam();
  std::vector<DXGI_FORMAT> formats(count, DXGI_FORMAT_R8G8B8A8_UNORM);
  std::vector<std::array<float, 4>> expected(kColors.begin(),
                                             kColors.begin() + count);
  Run(formats, expected);
}

INSTANTIATE_TEST_SUITE_P(TargetCounts, GraphicsOutputMergerCountSpec,
                         ::testing::Values(1u, 2u, 4u, 8u),
                         GraphicsOutputMergerCountSpec::Name);

TEST_F(GraphicsOutputMergerSpec, WritesMixedRenderTargetFormats) {
  const std::vector formats = {
      DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM,
      DXGI_FORMAT_R10G10B10A2_UNORM, DXGI_FORMAT_R32_FLOAT};
  Run(formats, {kColors[0], kColors[1], kColors[2], kColors[3]});
}

TEST_F(GraphicsOutputMergerSpec, AppliesIndependentWriteMasks) {
  constexpr std::array<float, 4> clear = {0.0f, 0.0f, 0.0f, 1.0f};
  const std::vector formats(2, DXGI_FORMAT_R8G8B8A8_UNORM);
  const std::vector clears(2, clear);
  Run(formats, {kColors[0], kColors[1]}, clears,
      {BlendTarget(D3D12_COLOR_WRITE_ENABLE_RED),
       BlendTarget(D3D12_COLOR_WRITE_ENABLE_GREEN)});
}

TEST_F(GraphicsOutputMergerSpec, AppliesIndependentBlendStates) {
  auto additive = BlendTarget();
  additive.BlendEnable = TRUE;
  additive.DestBlend = D3D12_BLEND_ONE;
  additive.DestBlendAlpha = D3D12_BLEND_ONE;

  auto preserve_destination = BlendTarget();
  preserve_destination.BlendEnable = TRUE;
  preserve_destination.SrcBlend = D3D12_BLEND_ZERO;
  preserve_destination.DestBlend = D3D12_BLEND_ONE;
  preserve_destination.SrcBlendAlpha = D3D12_BLEND_ZERO;
  preserve_destination.DestBlendAlpha = D3D12_BLEND_ONE;

  const std::vector formats(2, DXGI_FORMAT_R8G8B8A8_UNORM);
  Run(formats, {kColors[5], kColors[0]}, {kColors[2], kColors[0]},
      {additive, preserve_destination});
}

// Keep the dual-source fixed-function path out of the concurrent Metal wave.
DXMT_SERIAL_TEST_F(GraphicsOutputMergerSpec, AppliesDualSourceBlendFactors) {
  D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
  desc.pRootSignature = root_signature_.get();
  desc.VS = FullscreenVertexShader();
  desc.PS = DualSourcePixelShader();
  auto &blend = desc.BlendState.RenderTarget[0];
  blend.BlendEnable = TRUE;
  blend.SrcBlend = D3D12_BLEND_SRC1_COLOR;
  blend.DestBlend = D3D12_BLEND_INV_SRC1_COLOR;
  blend.BlendOp = D3D12_BLEND_OP_ADD;
  blend.SrcBlendAlpha = D3D12_BLEND_SRC1_ALPHA;
  blend.DestBlendAlpha = D3D12_BLEND_INV_SRC1_ALPHA;
  blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
  blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
  desc.SampleMask = UINT_MAX;
  desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
  desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  desc.NumRenderTargets = 1;
  desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  ComPtr<ID3D12PipelineState> pipeline;
  ASSERT_EQ(context_.device()->CreateGraphicsPipelineState(
                &desc, IID_PPV_ARGS(pipeline.put())),
            S_OK);

  auto target =
      context_.CreateTexture2D(kSize, kSize, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
                               D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
                               D3D12_RESOURCE_STATE_RENDER_TARGET);
  auto heap =
      context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
  ASSERT_TRUE(target);
  ASSERT_TRUE(heap);
  const auto rtv = heap->GetCPUDescriptorHandleForHeapStart();
  context_.device()->CreateRenderTargetView(target.get(), nullptr, rtv);

  // The DXIL shader outputs red as source 0 and 0.25 in every source-1
  // channel. Blending that over blue exercises all four source-1 factors.
  constexpr std::array<float, 4> destination = {0.0f, 0.0f, 1.0f, 0.0f};
  constexpr std::array<float, 4> expected = {0.25f, 0.0f, 0.75f, 0.25f};
  context_.list()->ClearRenderTargetView(rtv, destination.data(), 0, nullptr);
  context_.list()->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
  RecordFullscreenDraw(pipeline.get());
  D3D12TestContext::Transition(context_.list(), target.get(),
                               D3D12_RESOURCE_STATE_RENDER_TARGET,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);

  TextureReadback readback;
  ASSERT_EQ(context_.ReadbackTexture(target.get(), &readback), S_OK);
  const auto expected_pixel =
      ExpectedPixel(DXGI_FORMAT_R8G8B8A8_UNORM, expected);
  for (UINT y = 0; y < kSize; ++y) {
    for (UINT x = 0; x < kSize; ++x) {
      const auto *actual = readback.data.data() + y * readback.row_pitch +
                           x * expected_pixel.size();
      EXPECT_EQ(std::vector<std::uint8_t>(actual,
                                          actual + expected_pixel.size()),
                expected_pixel)
          << "pixel (" << x << ", " << y << ")";
    }
  }
}

TEST_F(GraphicsOutputMergerSpec, LeavesUnwrittenTargetUnchanged) {
  constexpr std::array<float, 4> clear = {0.0f, 1.0f, 1.0f, 1.0f};
  const std::vector formats(2, DXGI_FORMAT_R8G8B8A8_UNORM);
  const std::vector clears(2, clear);
  Run(formats, {kColors[0], clear}, clears, {}, {}, first_target_shader_.get());
}

TEST_F(GraphicsOutputMergerSpec, AcceptsNullRenderTargetSlot) {
  const std::vector formats = {DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_UNKNOWN,
                               DXGI_FORMAT_R8G8B8A8_UNORM};
  Run(formats, {kColors[0], kBlack, kColors[2]}, {}, {},
      {{DXGI_FORMAT_R8G8B8A8_UNORM, true},
       {DXGI_FORMAT_R8G8B8A8_UNORM, false},
       {DXGI_FORMAT_R8G8B8A8_UNORM, true}});
}

TEST_F(GraphicsOutputMergerSpec, AcceptsZeroRenderTargets) {
  auto pipeline = CreatePipeline({}, {}, all_targets_shader_.get());
  ASSERT_TRUE(pipeline);
  context_.list()->OMSetRenderTargets(0, nullptr, FALSE, nullptr);
  RecordFullscreenDraw(pipeline.get());
  EXPECT_EQ(context_.ExecuteAndWait(), S_OK);
}

} // namespace
