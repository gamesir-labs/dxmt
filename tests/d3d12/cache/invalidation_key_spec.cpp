#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"
#include "shaders/runtime_test_shaders.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

class D3D12PipelineInvalidationKeySpec : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  ComPtr<ID3D12RootSignature> CreateRootSignature(bool with_constants) {
    D3D12_ROOT_PARAMETER parameter = {};
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameter.Constants.Num32BitValues = 1;
    D3D12_ROOT_SIGNATURE_DESC desc = {};
    desc.NumParameters = with_constants ? 1 : 0;
    desc.pParameters = with_constants ? &parameter : nullptr;
    desc.Flags =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    return context_.CreateRootSignature(desc);
  }

  static D3D12_GRAPHICS_PIPELINE_STATE_DESC
  PipelineDesc(ID3D12RootSignature *root_signature) {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
    desc.pRootSignature = root_signature;
    desc.VS = dxmt::test::FullscreenVertexShader();
    desc.PS = dxmt::test::TextureUavPixelShader();
    desc.BlendState.RenderTarget[0].RenderTargetWriteMask =
        D3D12_COLOR_WRITE_ENABLE_ALL;
    desc.SampleMask = UINT_MAX;
    desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    return desc;
  }

  ComPtr<ID3D12PipelineState>
  CreatePipeline(const D3D12_GRAPHICS_PIPELINE_STATE_DESC &desc) {
    ComPtr<ID3D12PipelineState> pipeline;
    EXPECT_EQ(context_.device()->CreateGraphicsPipelineState(
                  &desc, __uuidof(ID3D12PipelineState),
                  reinterpret_cast<void **>(pipeline.put())),
              S_OK);
    return pipeline;
  }

  static ComPtr<ID3DBlob> CachedBlob(ID3D12PipelineState *pipeline) {
    ComPtr<ID3DBlob> blob;
    EXPECT_EQ(pipeline->GetCachedBlob(blob.put()), S_OK);
    return blob;
  }

  static bool BlobsEqual(ID3DBlob *left, ID3DBlob *right) {
    if (!left || !right || left->GetBufferSize() != right->GetBufferSize())
      return false;
    return std::memcmp(left->GetBufferPointer(), right->GetBufferPointer(),
                       left->GetBufferSize()) == 0;
  }

  D3D12TestContext context_;
};

TEST_F(D3D12PipelineInvalidationKeySpec,
       RootSignatureContentInvalidatesButObjectIdentityDoesNot) {
  auto first_root = CreateRootSignature(false);
  auto equivalent_root = CreateRootSignature(false);
  auto changed_root = CreateRootSignature(true);
  ASSERT_TRUE(first_root);
  ASSERT_TRUE(equivalent_root);
  ASSERT_TRUE(changed_root);

  auto first = CreatePipeline(PipelineDesc(first_root.get()));
  auto equivalent = CreatePipeline(PipelineDesc(equivalent_root.get()));
  auto changed = CreatePipeline(PipelineDesc(changed_root.get()));
  ASSERT_TRUE(first);
  ASSERT_TRUE(equivalent);
  ASSERT_TRUE(changed);

  auto first_blob = CachedBlob(first.get());
  auto equivalent_blob = CachedBlob(equivalent.get());
  auto changed_blob = CachedBlob(changed.get());
  EXPECT_TRUE(BlobsEqual(first_blob.get(), equivalent_blob.get()));
  EXPECT_FALSE(BlobsEqual(first_blob.get(), changed_blob.get()));
}

TEST_F(D3D12PipelineInvalidationKeySpec,
       AttachmentFormatAndSampleCountInvalidateKey) {
  auto root = CreateRootSignature(false);
  ASSERT_TRUE(root);
  auto base_desc = PipelineDesc(root.get());
  auto format_desc = base_desc;
  format_desc.RTVFormats[0] = DXGI_FORMAT_B8G8R8A8_UNORM;
  auto samples_desc = base_desc;
  samples_desc.SampleDesc.Count = 2;

  auto base = CreatePipeline(base_desc);
  auto format = CreatePipeline(format_desc);
  auto samples = CreatePipeline(samples_desc);
  ASSERT_TRUE(base);
  ASSERT_TRUE(format);
  ASSERT_TRUE(samples);

  auto base_blob = CachedBlob(base.get());
  auto format_blob = CachedBlob(format.get());
  auto samples_blob = CachedBlob(samples.get());
  EXPECT_FALSE(BlobsEqual(base_blob.get(), format_blob.get()));
  EXPECT_FALSE(BlobsEqual(base_blob.get(), samples_blob.get()));
  EXPECT_FALSE(BlobsEqual(format_blob.get(), samples_blob.get()));
}

TEST_F(D3D12PipelineInvalidationKeySpec,
       StructPaddingDoesNotInvalidateKey) {
  auto root = CreateRootSignature(false);
  ASSERT_TRUE(root);
  auto canonical_desc = PipelineDesc(root.get());
  auto padded_desc = canonical_desc;

  for (auto &target : padded_desc.BlendState.RenderTarget) {
    auto *bytes = reinterpret_cast<std::uint8_t *>(&target);
    const auto padding_begin =
        offsetof(D3D12_RENDER_TARGET_BLEND_DESC, RenderTargetWriteMask) +
        sizeof(target.RenderTargetWriteMask);
    std::memset(bytes + padding_begin, 0xa5, sizeof(target) - padding_begin);
  }
  auto *depth_bytes =
      reinterpret_cast<std::uint8_t *>(&padded_desc.DepthStencilState);
  const auto depth_padding_begin =
      offsetof(D3D12_DEPTH_STENCIL_DESC, StencilWriteMask) +
      sizeof(padded_desc.DepthStencilState.StencilWriteMask);
  const auto depth_padding_end =
      offsetof(D3D12_DEPTH_STENCIL_DESC, FrontFace);
  std::memset(depth_bytes + depth_padding_begin, 0x5a,
              depth_padding_end - depth_padding_begin);

  auto canonical = CreatePipeline(canonical_desc);
  auto padded = CreatePipeline(padded_desc);
  ASSERT_TRUE(canonical);
  ASSERT_TRUE(padded);
  auto canonical_blob = CachedBlob(canonical.get());
  auto padded_blob = CachedBlob(padded.get());
  EXPECT_TRUE(BlobsEqual(canonical_blob.get(), padded_blob.get()));
}

TEST_F(D3D12PipelineInvalidationKeySpec,
       InactiveRenderTargetBlendStateDoesNotInvalidateKey) {
  auto root = CreateRootSignature(false);
  ASSERT_TRUE(root);
  auto canonical_desc = PipelineDesc(root.get());
  auto inactive_desc = canonical_desc;
  inactive_desc.BlendState.RenderTarget[7].SrcBlend = D3D12_BLEND_SRC_ALPHA;
  inactive_desc.BlendState.RenderTarget[7].DestBlend =
      D3D12_BLEND_INV_SRC_ALPHA;
  inactive_desc.BlendState.RenderTarget[7].RenderTargetWriteMask =
      D3D12_COLOR_WRITE_ENABLE_ALL;

  auto canonical = CreatePipeline(canonical_desc);
  auto inactive = CreatePipeline(inactive_desc);
  ASSERT_TRUE(canonical);
  ASSERT_TRUE(inactive);
  auto canonical_blob = CachedBlob(canonical.get());
  auto inactive_blob = CachedBlob(inactive.get());
  EXPECT_TRUE(BlobsEqual(canonical_blob.get(), inactive_blob.get()));
}

} // namespace
