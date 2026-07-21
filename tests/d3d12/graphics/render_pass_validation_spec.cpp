#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

#include <array>
#include <string>

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

class RenderPassValidationSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_EQ(context_.list()->QueryInterface(IID_PPV_ARGS(list4_.put())), S_OK);
  }

  D3D12TestContext context_;
  ComPtr<ID3D12GraphicsCommandList4> list4_;
};

TEST_F(RenderPassValidationSpec, EmptyPassBeginsAndEnds) {
  list4_->BeginRenderPass(0, nullptr, nullptr, D3D12_RENDER_PASS_FLAG_NONE);
  list4_->EndRenderPass();
  EXPECT_EQ(context_.list()->Close(), S_OK);
}

TEST_F(RenderPassValidationSpec, AllowUavWritesFlagIsAccepted) {
  list4_->BeginRenderPass(0, nullptr, nullptr,
                          D3D12_RENDER_PASS_FLAG_ALLOW_UAV_WRITES);
  list4_->EndRenderPass();
  EXPECT_EQ(context_.list()->Close(), S_OK);
}

TEST_F(RenderPassValidationSpec, BeginWhileActiveRejectsNestedPass) {
  list4_->BeginRenderPass(0, nullptr, nullptr, D3D12_RENDER_PASS_FLAG_NONE);
  list4_->BeginRenderPass(0, nullptr, nullptr, D3D12_RENDER_PASS_FLAG_NONE);
  list4_->EndRenderPass();

  EXPECT_EQ(context_.list()->Close(), E_FAIL);
}

TEST_F(RenderPassValidationSpec, EndWithoutBeginRejectsCommandList) {
  list4_->EndRenderPass();

  EXPECT_EQ(context_.list()->Close(), E_FAIL);
}

TEST_F(RenderPassValidationSpec, CloseWhilePassIsOpenFails) {
  list4_->BeginRenderPass(0, nullptr, nullptr, D3D12_RENDER_PASS_FLAG_NONE);

  EXPECT_EQ(context_.list()->Close(), E_FAIL);
}

TEST_F(RenderPassValidationSpec,
       ClearRenderTargetViewInsidePassRejectsCommandList) {
  auto target =
      context_.CreateTexture2D(4, 4, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
                               D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
                               D3D12_RESOURCE_STATE_RENDER_TARGET);
  auto heap =
      context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
  ASSERT_TRUE(target);
  ASSERT_TRUE(heap);
  const auto rtv = heap->GetCPUDescriptorHandleForHeapStart();
  context_.device()->CreateRenderTargetView(target.get(), nullptr, rtv);

  D3D12_RENDER_PASS_RENDER_TARGET_DESC pass = {};
  pass.cpuDescriptor = rtv;
  pass.BeginningAccess.Type = D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_PRESERVE;
  pass.EndingAccess.Type = D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_PRESERVE;
  list4_->BeginRenderPass(1, &pass, nullptr, D3D12_RENDER_PASS_FLAG_NONE);
  constexpr FLOAT kClear[4] = {};
  context_.list()->ClearRenderTargetView(rtv, kClear, 0, nullptr);
  list4_->EndRenderPass();

  EXPECT_EQ(context_.list()->Close(), E_FAIL);
}

TEST_F(RenderPassValidationSpec,
       ClearDepthStencilViewInsidePassRejectsCommandList) {
  auto target = context_.CreateTexture2D(
      4, 4, 1, DXGI_FORMAT_D32_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL,
      D3D12_RESOURCE_STATE_DEPTH_WRITE);
  auto heap =
      context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false);
  ASSERT_TRUE(target);
  ASSERT_TRUE(heap);
  const auto dsv = heap->GetCPUDescriptorHandleForHeapStart();
  context_.device()->CreateDepthStencilView(target.get(), nullptr, dsv);

  D3D12_RENDER_PASS_DEPTH_STENCIL_DESC pass = {};
  pass.cpuDescriptor = dsv;
  pass.DepthBeginningAccess.Type =
      D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_PRESERVE;
  pass.DepthEndingAccess.Type = D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_PRESERVE;
  pass.StencilBeginningAccess.Type =
      D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_NO_ACCESS;
  pass.StencilEndingAccess.Type =
      D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_NO_ACCESS;
  list4_->BeginRenderPass(0, nullptr, &pass, D3D12_RENDER_PASS_FLAG_NONE);
  context_.list()->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0,
                                         0, nullptr);
  list4_->EndRenderPass();

  EXPECT_EQ(context_.list()->Close(), E_FAIL);
}

} // namespace
