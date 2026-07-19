#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"

#include <cstring>

namespace {

using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

TEST(D3D12CachedBlobSpec, ReturnsStableBlobForPublicPipelineState) {
  D3D12TestContext context;
  ASSERT_EQ(context.Initialize(), S_OK);

  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  auto root_signature = context.CreateRootSignature(root_desc);
  ASSERT_TRUE(root_signature);

  const auto shader = CompileShader(
      "[numthreads(1, 1, 1)] void main() {}", "cs_5_0");
  ASSERT_EQ(shader.result, S_OK) << shader.diagnostic_text();
  const D3D12_SHADER_BYTECODE bytecode = {
      shader.bytecode->GetBufferPointer(), shader.bytecode->GetBufferSize()};
  auto pipeline = context.CreateComputePipeline(root_signature.get(), bytecode);
  ASSERT_TRUE(pipeline);

  ComPtr<ID3DBlob> first;
  ComPtr<ID3DBlob> second;
  ASSERT_EQ(pipeline->GetCachedBlob(first.put()), S_OK);
  ASSERT_EQ(pipeline->GetCachedBlob(second.put()), S_OK);
  ASSERT_TRUE(first);
  ASSERT_TRUE(second);
  ASSERT_GT(first->GetBufferSize(), 0u);
  ASSERT_EQ(first->GetBufferSize(), second->GetBufferSize());
  EXPECT_EQ(std::memcmp(first->GetBufferPointer(), second->GetBufferPointer(),
                        first->GetBufferSize()),
            0);
}

} // namespace
