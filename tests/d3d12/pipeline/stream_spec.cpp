#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"
#include "shaders/runtime_test_shaders.hpp"

namespace {

using dxmt::test::ComPtr;
using dxmt::test::CopyTextureComputeShader;
using dxmt::test::D3D12TestContext;
using dxmt::test::TextureUavPixelShader;

template <D3D12_PIPELINE_STATE_SUBOBJECT_TYPE Type>
struct alignas(void *) ShaderSubobject {
  D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type = Type;
  D3D12_SHADER_BYTECODE shader = {};
};

class PipelineStreamSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_TRUE(SUCCEEDED(context_.Initialize()));
    ASSERT_TRUE(SUCCEEDED(context_.device()->QueryInterface(
        __uuidof(ID3D12Device2), reinterpret_cast<void **>(device2_.put()))));
  }

  HRESULT CreatePipeline(void *stream, SIZE_T size,
                         ID3D12PipelineState **pipeline) {
    D3D12_PIPELINE_STATE_STREAM_DESC desc = {size, stream};
    return device2_->CreatePipelineState(
        &desc, __uuidof(ID3D12PipelineState),
        reinterpret_cast<void **>(pipeline));
  }

  D3D12TestContext context_;
  ComPtr<ID3D12Device2> device2_;
};

TEST_F(PipelineStreamSpec, DuplicateSubobjectRejected) {
  struct Stream {
    ShaderSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS> first;
    ShaderSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS> second;
  } stream;
  stream.first.shader = CopyTextureComputeShader();
  stream.second.shader = CopyTextureComputeShader();
  ComPtr<ID3D12PipelineState> pipeline;

  EXPECT_EQ(CreatePipeline(&stream, sizeof(stream), pipeline.put()),
            E_INVALIDARG);
  EXPECT_FALSE(pipeline);
}

TEST_F(PipelineStreamSpec, TruncatedSubobjectRejected) {
  D3D12_PIPELINE_STATE_SUBOBJECT_TYPE stream =
      D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS;
  ComPtr<ID3D12PipelineState> pipeline;

  EXPECT_EQ(CreatePipeline(&stream, sizeof(stream), pipeline.put()),
            E_INVALIDARG);
  EXPECT_FALSE(pipeline);
}

TEST_F(PipelineStreamSpec, ComputeAndGraphicsSubobjectsMixedRejected) {
  struct Stream {
    ShaderSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS> compute;
    ShaderSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS> pixel;
  } stream;
  stream.compute.shader = CopyTextureComputeShader();
  stream.pixel.shader = TextureUavPixelShader();
  ComPtr<ID3D12PipelineState> pipeline;

  EXPECT_EQ(CreatePipeline(&stream, sizeof(stream), pipeline.put()),
            E_INVALIDARG);
  EXPECT_FALSE(pipeline);
}

} // namespace
