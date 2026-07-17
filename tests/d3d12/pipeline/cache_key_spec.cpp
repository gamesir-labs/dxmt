#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"
#include "shaders/runtime_test_shaders.hpp"

#include <cstdint>
#include <cstring>
#include <limits>

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

class D3D12PipelineCacheKeySpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    D3D12_ROOT_SIGNATURE_DESC root_desc = {};
    root_desc.Flags =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    root_signature_ = context_.CreateRootSignature(root_desc);
    ASSERT_TRUE(root_signature_);
  }

  ComPtr<ID3D12PipelineState> CreatePipeline(UINT sample_mask) {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
    desc.pRootSignature = root_signature_.get();
    desc.VS = dxmt::test::FullscreenVertexShader();
    desc.PS = dxmt::test::TextureUavPixelShader();
    desc.BlendState.RenderTarget[0].RenderTargetWriteMask =
        D3D12_COLOR_WRITE_ENABLE_ALL;
    desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    desc.SampleMask = sample_mask;
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;

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

  D3D12TestContext context_;
  ComPtr<ID3D12RootSignature> root_signature_;
};

TEST_F(D3D12PipelineCacheKeySpec,
       SampleMaskChangesCachedBlobAndSameDescIsStable) {
  constexpr UINT kAllSamples = std::numeric_limits<UINT>::max();
  constexpr UINT kOneBitMasked = kAllSamples - 1u;
  auto all_samples = CreatePipeline(kAllSamples);
  auto one_bit_masked = CreatePipeline(kOneBitMasked);
  auto all_samples_again = CreatePipeline(kAllSamples);
  ASSERT_TRUE(all_samples);
  ASSERT_TRUE(one_bit_masked);
  ASSERT_TRUE(all_samples_again);

  auto all_blob = CachedBlob(all_samples.get());
  auto masked_blob = CachedBlob(one_bit_masked.get());
  auto repeated_blob = CachedBlob(all_samples_again.get());
  ASSERT_TRUE(all_blob);
  ASSERT_TRUE(masked_blob);
  ASSERT_TRUE(repeated_blob);
  ASSERT_GT(all_blob->GetBufferSize(), 0u);
  ASSERT_EQ(all_blob->GetBufferSize(), masked_blob->GetBufferSize());
  ASSERT_EQ(all_blob->GetBufferSize(), repeated_blob->GetBufferSize());

  EXPECT_NE(std::memcmp(all_blob->GetBufferPointer(),
                        masked_blob->GetBufferPointer(),
                        all_blob->GetBufferSize()),
            0)
      << "SampleMask must participate in the persistent shader cache key";
  EXPECT_EQ(std::memcmp(all_blob->GetBufferPointer(),
                        repeated_blob->GetBufferPointer(),
                        all_blob->GetBufferSize()),
            0)
      << "identical pipeline descriptions must produce stable cached blobs";
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
