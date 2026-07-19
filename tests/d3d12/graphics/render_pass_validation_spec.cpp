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

} // namespace
