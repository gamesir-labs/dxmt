#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "../../../include/dxmt_d3d12_test_path.hpp"
#include "d3d12_test_context.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace {

using dxmt::d3d12::test::PipelineNativeArtifactIdentity;
using dxmt::d3d12::test::PipelineNativeArtifactCacheStats;
using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

class PipelineNativeCacheSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    const D3D12_ROOT_SIGNATURE_DESC root_desc = {};
    root_signature_ = context_.CreateRootSignature(root_desc);
    ASSERT_TRUE(root_signature_);

    vertex_ = CompileShader(R"(
      float4 main(uint id : SV_VertexID) : SV_Position {
        const float2 positions[3] = {
          float2(-1.0, -1.0), float2(-1.0, 3.0), float2(3.0, -1.0)
        };
        return float4(positions[id], 0.0, 1.0);
      }
    )", "vs_5_0");
    pixel_ = CompileShader(
        "float4 main() : SV_Target { return float4(1, 1, 1, 1); }",
        "ps_5_0");
    compute_ = CompileShader(
        "[numthreads(1, 1, 1)] void main() {}", "cs_5_0");
    ASSERT_EQ(vertex_.result, S_OK) << vertex_.diagnostic_text();
    ASSERT_EQ(pixel_.result, S_OK) << pixel_.diagnostic_text();
    ASSERT_EQ(compute_.result, S_OK) << compute_.diagnostic_text();
  }

  D3D12_SHADER_BYTECODE Bytecode(ID3DBlob *blob) const {
    return {blob->GetBufferPointer(), blob->GetBufferSize()};
  }

  D3D12_GRAPHICS_PIPELINE_STATE_DESC GraphicsDesc() const {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
    desc.pRootSignature = root_signature_.get();
    desc.VS = Bytecode(vertex_.bytecode.get());
    desc.PS = Bytecode(pixel_.bytecode.get());
    desc.BlendState.RenderTarget[0].RenderTargetWriteMask =
        D3D12_COLOR_WRITE_ENABLE_ALL;
    desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    desc.SampleMask = UINT_MAX;
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    return desc;
  }

  D3D12_COMPUTE_PIPELINE_STATE_DESC ComputeDesc() const {
    D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
    desc.pRootSignature = root_signature_.get();
    desc.CS = Bytecode(compute_.bytecode.get());
    return desc;
  }

  ComPtr<ID3D12PipelineState>
  CreateGraphics(const D3D12_GRAPHICS_PIPELINE_STATE_DESC &desc) const {
    ComPtr<ID3D12PipelineState> pipeline;
    const HRESULT hr = context_.device()->CreateGraphicsPipelineState(
        &desc, __uuidof(ID3D12PipelineState),
        reinterpret_cast<void **>(pipeline.put()));
    return SUCCEEDED(hr) ? std::move(pipeline)
                         : ComPtr<ID3D12PipelineState>();
  }

  ComPtr<ID3D12PipelineState>
  CreateCompute(const D3D12_COMPUTE_PIPELINE_STATE_DESC &desc) const {
    ComPtr<ID3D12PipelineState> pipeline;
    const HRESULT hr = context_.device()->CreateComputePipelineState(
        &desc, __uuidof(ID3D12PipelineState),
        reinterpret_cast<void **>(pipeline.put()));
    return SUCCEEDED(hr) ? std::move(pipeline)
                         : ComPtr<ID3D12PipelineState>();
  }

  std::uintptr_t ArtifactIdentity(ID3D12PipelineState *pipeline) const {
    PipelineNativeArtifactIdentity identity = {};
    UINT size = sizeof(identity);
    const HRESULT hr = pipeline->GetPrivateData(
        dxmt::d3d12::test::kPipelineNativeArtifactIdentityGuid, &size,
        &identity);
    EXPECT_EQ(hr, S_OK);
    EXPECT_EQ(size, sizeof(identity));
    EXPECT_EQ(identity.struct_size, sizeof(identity));
    return SUCCEEDED(hr) ? identity.artifact : 0;
  }

  PipelineNativeArtifactCacheStats CacheStats() const {
    PipelineNativeArtifactCacheStats stats = {};
    UINT size = sizeof(stats);
    const HRESULT hr = context_.device()->GetPrivateData(
        dxmt::d3d12::test::kPipelineNativeArtifactCacheStatsGuid, &size,
        &stats);
    EXPECT_EQ(hr, S_OK);
    EXPECT_EQ(size, sizeof(stats));
    EXPECT_EQ(stats.struct_size, sizeof(stats));
    return stats;
  }

  D3D12TestContext context_;
  ComPtr<ID3D12RootSignature> root_signature_;
  dxmt::test::ShaderCompilation vertex_;
  dxmt::test::ShaderCompilation pixel_;
  dxmt::test::ShaderCompilation compute_;
};

TEST_F(PipelineNativeCacheSpec,
       SharesGraphicsArtifactButKeepsWrappersIndependent) {
  const auto before = CacheStats();
  auto desc = GraphicsDesc();
  auto first = CreateGraphics(desc);
  ASSERT_TRUE(first);

  constexpr GUID kPrivateKey = {
      0xa911b45a,
      0x0c8d,
      0x4123,
      {0x87, 0x44, 0x7f, 0x98, 0xe0, 0xc4, 0x43, 0x0e}};
  constexpr std::uint32_t kPrivateValue = 0x13579bdf;
  ASSERT_EQ(first->SetPrivateData(kPrivateKey, sizeof(kPrivateValue),
                                  &kPrivateValue),
            S_OK);

  constexpr std::array<std::uint8_t, 5> kCustomCachedBlob = {1, 3, 5, 7, 9};
  desc.CachedPSO = {kCustomCachedBlob.data(), kCustomCachedBlob.size()};
  auto duplicate = CreateGraphics(desc);
  ASSERT_TRUE(duplicate);
  EXPECT_NE(first.get(), duplicate.get());

  const auto first_identity = ArtifactIdentity(first.get());
  ASSERT_NE(first_identity, 0u);
  EXPECT_EQ(ArtifactIdentity(duplicate.get()), first_identity);

  std::uint32_t private_value = 0;
  UINT private_size = sizeof(private_value);
  EXPECT_EQ(duplicate->GetPrivateData(kPrivateKey, &private_size,
                                      &private_value),
            DXGI_ERROR_NOT_FOUND);

  ComPtr<ID3DBlob> cached_blob;
  ASSERT_EQ(duplicate->GetCachedBlob(cached_blob.put()), S_OK);
  ASSERT_TRUE(cached_blob);
  ASSERT_EQ(cached_blob->GetBufferSize(), kCustomCachedBlob.size());
  EXPECT_EQ(std::memcmp(cached_blob->GetBufferPointer(),
                        kCustomCachedBlob.data(), kCustomCachedBlob.size()),
            0);

  auto changed = desc;
  changed.CachedPSO = {};
  changed.SampleMask ^= 1;
  auto distinct = CreateGraphics(changed);
  ASSERT_TRUE(distinct);
  EXPECT_NE(ArtifactIdentity(distinct.get()), first_identity);

  const auto after = CacheStats();
  EXPECT_EQ(after.hits - before.hits, 1u);
  EXPECT_EQ(after.misses - before.misses, 2u);
  EXPECT_EQ(after.compiles - before.compiles, 2u);
  EXPECT_EQ(after.compile_failures - before.compile_failures, 0u);
}

TEST_F(PipelineNativeCacheSpec, ReleasesArtifactAfterLastWrapper) {
  const auto before = CacheStats();
  const auto desc = ComputeDesc();
  {
    auto first = CreateCompute(desc);
    ASSERT_TRUE(first);
    ASSERT_NE(ArtifactIdentity(first.get()), 0u);
  }

  auto second = CreateCompute(desc);
  ASSERT_TRUE(second);
  const auto after = CacheStats();
  EXPECT_EQ(after.hits - before.hits, 0u);
  EXPECT_EQ(after.misses - before.misses, 2u);
  EXPECT_EQ(after.compiles - before.compiles, 2u);
}

TEST_F(PipelineNativeCacheSpec, FailedCompilationDoesNotPoisonKey) {
  const auto before = CacheStats();
  auto desc = GraphicsDesc();
  desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;

  EXPECT_FALSE(CreateGraphics(desc));
  EXPECT_FALSE(CreateGraphics(desc));

  const auto after = CacheStats();
  EXPECT_EQ(after.hits - before.hits, 0u);
  EXPECT_EQ(after.misses - before.misses, 2u);
  EXPECT_EQ(after.compiles - before.compiles, 2u);
  EXPECT_EQ(after.compile_failures - before.compile_failures, 2u);
}

TEST_F(PipelineNativeCacheSpec, ConcurrentComputeCreatesShareOneArtifact) {
  const auto before = CacheStats();
  constexpr std::size_t kThreadCount = 8;
  const auto desc = ComputeDesc();
  std::array<ComPtr<ID3D12PipelineState>, kThreadCount> pipelines;
  std::atomic_uint ready = 0;
  std::atomic_bool start = false;
  std::vector<std::thread> threads;
  threads.reserve(kThreadCount);

  for (std::size_t index = 0; index < kThreadCount; ++index) {
    threads.emplace_back([&, index] {
      ready.fetch_add(1, std::memory_order_release);
      while (!start.load(std::memory_order_acquire))
        std::this_thread::yield();
      pipelines[index] = CreateCompute(desc);
    });
  }
  while (ready.load(std::memory_order_acquire) != kThreadCount)
    std::this_thread::yield();
  start.store(true, std::memory_order_release);
  for (auto &thread : threads)
    thread.join();

  for (const auto &pipeline : pipelines)
    ASSERT_TRUE(pipeline);
  const auto identity = ArtifactIdentity(pipelines[0].get());
  ASSERT_NE(identity, 0u);
  for (std::size_t index = 1; index < pipelines.size(); ++index) {
    EXPECT_NE(pipelines[index].get(), pipelines[0].get());
    EXPECT_EQ(ArtifactIdentity(pipelines[index].get()), identity);
  }
  const auto after = CacheStats();
  EXPECT_EQ(after.misses - before.misses, 1u);
  EXPECT_EQ(after.compiles - before.compiles, 1u);
  EXPECT_EQ((after.hits - before.hits) + (after.waits - before.waits),
            kThreadCount - 1);
}

} // namespace
