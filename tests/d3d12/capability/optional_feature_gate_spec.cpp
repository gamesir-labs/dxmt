#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace {

using dxmt::test::ColorsMatch;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;
using dxmt::test::TextureReadback;

class D3D12OptionalFeatureGateSpec : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_TRUE(SUCCEEDED(context_.Initialize())); }

  D3D12TestContext context_;
};

TEST_F(D3D12OptionalFeatureGateSpec,
       EnhancedBarrierAdvertisementExecutesBufferOracle) {
  D3D12_FEATURE_DATA_D3D12_OPTIONS12 options = {};
  ASSERT_EQ(context_.device()->CheckFeatureSupport(
                D3D12_FEATURE_D3D12_OPTIONS12, &options, sizeof(options)),
            S_OK);

  ComPtr<ID3D12GraphicsCommandList7> list7;
  const HRESULT interface_hr = context_.list()->QueryInterface(
      __uuidof(ID3D12GraphicsCommandList7),
      reinterpret_cast<void **>(list7.put()));
  if (!options.EnhancedBarriersSupported) {
    EXPECT_EQ(interface_hr, E_NOINTERFACE);
    EXPECT_FALSE(list7);
    return;
  }

  ASSERT_EQ(interface_hr, S_OK);
  ASSERT_TRUE(list7);

  constexpr std::array<std::uint32_t, 4> expected = {
      0x10203040u, 0x50607080u, 0x90a0b0c0u, 0xd0e0f001u};
  constexpr UINT64 size = sizeof(expected);
  auto source =
      context_.CreateUploadBuffer(size, expected.data(), sizeof(expected));
  auto destination = context_.CreateBuffer(
      size, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COMMON);
  ASSERT_TRUE(source);
  ASSERT_TRUE(destination);

  D3D12_BUFFER_BARRIER barrier = {};
  barrier.SyncBefore = D3D12_BARRIER_SYNC_NONE;
  barrier.SyncAfter = D3D12_BARRIER_SYNC_COPY;
  barrier.AccessBefore = D3D12_BARRIER_ACCESS_COMMON;
  barrier.AccessAfter = D3D12_BARRIER_ACCESS_COPY_DEST;
  barrier.pResource = destination.get();
  barrier.Size = std::numeric_limits<UINT64>::max();
  D3D12_BARRIER_GROUP group = {};
  group.Type = D3D12_BARRIER_TYPE_BUFFER;
  group.NumBarriers = 1;
  group.pBufferBarriers = &barrier;
  list7->Barrier(1, &group);
  list7->CopyBufferRegion(destination.get(), 0, source.get(), 0, size);

  barrier.SyncBefore = D3D12_BARRIER_SYNC_COPY;
  barrier.AccessBefore = D3D12_BARRIER_ACCESS_COPY_DEST;
  barrier.AccessAfter = D3D12_BARRIER_ACCESS_COPY_SOURCE;
  list7->Barrier(1, &group);

  ASSERT_EQ(context_.ExecuteAndWait(), S_OK);
  ASSERT_EQ(context_.ResetCommandList(), S_OK);
  std::vector<std::uint8_t> actual;
  ASSERT_EQ(context_.ReadbackBuffer(destination.get(), size, &actual), S_OK);
  ASSERT_EQ(actual.size(), sizeof(expected));
  EXPECT_EQ(std::memcmp(actual.data(), expected.data(), sizeof(expected)), 0);
}

TEST_F(D3D12OptionalFeatureGateSpec,
       RenderPassAdvertisementExecutesClearOracle) {
  D3D12_FEATURE_DATA_D3D12_OPTIONS5 options = {};
  ASSERT_EQ(context_.device()->CheckFeatureSupport(
                D3D12_FEATURE_D3D12_OPTIONS5, &options, sizeof(options)),
            S_OK);
  if (options.RenderPassesTier == D3D12_RENDER_PASS_TIER_0)
    return;

  ComPtr<ID3D12GraphicsCommandList4> list4;
  ASSERT_EQ(context_.list()->QueryInterface(
                __uuidof(ID3D12GraphicsCommandList4),
                reinterpret_cast<void **>(list4.put())),
            S_OK);
  ASSERT_TRUE(list4);

  constexpr UINT size = 4;
  auto target = context_.CreateTexture2D(
      size, size, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
      D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
      D3D12_RESOURCE_STATE_RENDER_TARGET);
  auto rtv_heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
  ASSERT_TRUE(target);
  ASSERT_TRUE(rtv_heap);
  const D3D12_CPU_DESCRIPTOR_HANDLE rtv =
      rtv_heap->GetCPUDescriptorHandleForHeapStart();
  context_.device()->CreateRenderTargetView(target.get(), nullptr, rtv);

  D3D12_RENDER_PASS_RENDER_TARGET_DESC pass_target = {};
  pass_target.cpuDescriptor = rtv;
  pass_target.BeginningAccess.Type =
      D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR;
  pass_target.BeginningAccess.Clear.ClearValue.Format =
      DXGI_FORMAT_R8G8B8A8_UNORM;
  pass_target.BeginningAccess.Clear.ClearValue.Color[0] = 0.25f;
  pass_target.BeginningAccess.Clear.ClearValue.Color[1] = 0.5f;
  pass_target.BeginningAccess.Clear.ClearValue.Color[2] = 0.75f;
  pass_target.BeginningAccess.Clear.ClearValue.Color[3] = 1.0f;
  pass_target.EndingAccess.Type = D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_PRESERVE;

  list4->BeginRenderPass(1, &pass_target, nullptr,
                         D3D12_RENDER_PASS_FLAG_NONE);
  list4->EndRenderPass();
  D3D12TestContext::Transition(context_.list(), target.get(),
                               D3D12_RESOURCE_STATE_RENDER_TARGET,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);

  TextureReadback readback;
  ASSERT_EQ(context_.ReadbackTexture(target.get(), &readback), S_OK);
  constexpr std::uint32_t expected = 0xffbf8040u;
  for (UINT y = 0; y < size; ++y) {
    for (UINT x = 0; x < size; ++x) {
      std::uint32_t actual = 0;
      std::memcpy(&actual,
                  readback.data.data() + y * readback.row_pitch +
                      x * sizeof(actual),
                  sizeof(actual));
      EXPECT_TRUE(ColorsMatch(actual, expected, 1))
          << "pixel (" << x << ", " << y << ")";
    }
  }
}

TEST_F(D3D12OptionalFeatureGateSpec,
       UnsupportedRaytracingCreationClearsOutput) {
  D3D12_FEATURE_DATA_D3D12_OPTIONS5 options = {};
  ASSERT_EQ(context_.device()->CheckFeatureSupport(
                D3D12_FEATURE_D3D12_OPTIONS5, &options, sizeof(options)),
            S_OK);
  ASSERT_EQ(options.RaytracingTier, D3D12_RAYTRACING_TIER_NOT_SUPPORTED)
      << "add a raytracing execution oracle before advertising the tier";

  ComPtr<ID3D12Device5> device5;
  ASSERT_EQ(context_.device()->QueryInterface(
                __uuidof(ID3D12Device5),
                reinterpret_cast<void **>(device5.put())),
            S_OK);
  ASSERT_TRUE(device5);

  D3D12_STATE_OBJECT_DESC desc = {};
  desc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
  void *output = reinterpret_cast<void *>(static_cast<std::uintptr_t>(1));
  EXPECT_EQ(device5->CreateStateObject(&desc, __uuidof(ID3D12StateObject),
                                       &output),
            E_NOTIMPL);
  EXPECT_EQ(output, nullptr);
}

TEST_F(D3D12OptionalFeatureGateSpec,
       UnsupportedRaytracingCommandFailsClose) {
  D3D12_FEATURE_DATA_D3D12_OPTIONS5 options = {};
  ASSERT_EQ(context_.device()->CheckFeatureSupport(
                D3D12_FEATURE_D3D12_OPTIONS5, &options, sizeof(options)),
            S_OK);
  ASSERT_EQ(options.RaytracingTier, D3D12_RAYTRACING_TIER_NOT_SUPPORTED);

  ComPtr<ID3D12GraphicsCommandList4> list4;
  ASSERT_EQ(context_.list()->QueryInterface(
                __uuidof(ID3D12GraphicsCommandList4),
                reinterpret_cast<void **>(list4.put())),
            S_OK);
  D3D12_DISPATCH_RAYS_DESC desc = {};
  list4->DispatchRays(&desc);
  EXPECT_EQ(list4->Close(), E_NOTIMPL);
}

TEST_F(D3D12OptionalFeatureGateSpec, UnsupportedMeshDispatchFailsClose) {
  D3D12_FEATURE_DATA_D3D12_OPTIONS7 options = {};
  ASSERT_EQ(context_.device()->CheckFeatureSupport(
                D3D12_FEATURE_D3D12_OPTIONS7, &options, sizeof(options)),
            S_OK);
  ASSERT_EQ(options.MeshShaderTier, D3D12_MESH_SHADER_TIER_NOT_SUPPORTED)
      << "add a mesh shader execution oracle before advertising the tier";

  ComPtr<ID3D12GraphicsCommandList6> list6;
  ASSERT_EQ(context_.list()->QueryInterface(
                __uuidof(ID3D12GraphicsCommandList6),
                reinterpret_cast<void **>(list6.put())),
            S_OK);
  list6->DispatchMesh(1, 1, 1);
  EXPECT_EQ(list6->Close(), E_NOTIMPL);
}

TEST_F(D3D12OptionalFeatureGateSpec,
       UnsupportedVariableRateShadingCommandFailsClose) {
  D3D12_FEATURE_DATA_D3D12_OPTIONS6 options = {};
  ASSERT_EQ(context_.device()->CheckFeatureSupport(
                D3D12_FEATURE_D3D12_OPTIONS6, &options, sizeof(options)),
            S_OK);
  ASSERT_EQ(options.VariableShadingRateTier,
            D3D12_VARIABLE_SHADING_RATE_TIER_NOT_SUPPORTED)
      << "add a VRS execution oracle before advertising the tier";

  ComPtr<ID3D12GraphicsCommandList5> list5;
  ASSERT_EQ(context_.list()->QueryInterface(
                __uuidof(ID3D12GraphicsCommandList5),
                reinterpret_cast<void **>(list5.put())),
            S_OK);
  list5->RSSetShadingRate(D3D12_SHADING_RATE_2X2, nullptr);
  EXPECT_EQ(list5->Close(), E_NOTIMPL);
}

TEST_F(D3D12OptionalFeatureGateSpec, SamplerFeedbackRemainsFeatureGated) {
  D3D12_FEATURE_DATA_D3D12_OPTIONS7 options = {};
  ASSERT_EQ(context_.device()->CheckFeatureSupport(
                D3D12_FEATURE_D3D12_OPTIONS7, &options, sizeof(options)),
            S_OK);
  EXPECT_EQ(options.SamplerFeedbackTier,
            D3D12_SAMPLER_FEEDBACK_TIER_NOT_SUPPORTED)
      << "add a sampler feedback execution oracle before advertising the tier";
}

} // namespace
