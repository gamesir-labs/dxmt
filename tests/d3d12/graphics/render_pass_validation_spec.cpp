#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

#include <array>
#include <string>

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

enum class InvalidRenderPassCase {
  EndWithoutBegin,
  NestedBegin,
  NullTargetsWithNonzeroCount,
  TooManyRenderTargets,
  SuspendingFlag,
  ResumingFlag,
};

class RenderPassValidationSpec
    : public ::testing::TestWithParam<InvalidRenderPassCase> {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_EQ(context_.list()->QueryInterface(IID_PPV_ARGS(list4_.put())), S_OK);
  }

  D3D12TestContext context_;
  ComPtr<ID3D12GraphicsCommandList4> list4_;
};

TEST_P(RenderPassValidationSpec, InvalidStateFailsAtClose) {
  std::array<D3D12_RENDER_PASS_RENDER_TARGET_DESC,
             D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT + 1>
      targets = {};
  HRESULT expected = E_INVALIDARG;
  switch (GetParam()) {
  case InvalidRenderPassCase::EndWithoutBegin:
    list4_->EndRenderPass();
    break;
  case InvalidRenderPassCase::NestedBegin:
    list4_->BeginRenderPass(0, nullptr, nullptr, D3D12_RENDER_PASS_FLAG_NONE);
    list4_->BeginRenderPass(0, nullptr, nullptr, D3D12_RENDER_PASS_FLAG_NONE);
    break;
  case InvalidRenderPassCase::NullTargetsWithNonzeroCount:
    list4_->BeginRenderPass(1, nullptr, nullptr, D3D12_RENDER_PASS_FLAG_NONE);
    break;
  case InvalidRenderPassCase::TooManyRenderTargets:
    list4_->BeginRenderPass(static_cast<UINT>(targets.size()), targets.data(),
                            nullptr, D3D12_RENDER_PASS_FLAG_NONE);
    break;
  case InvalidRenderPassCase::SuspendingFlag:
    list4_->BeginRenderPass(0, nullptr, nullptr,
                            D3D12_RENDER_PASS_FLAG_SUSPENDING_PASS);
    expected = E_NOTIMPL;
    break;
  case InvalidRenderPassCase::ResumingFlag:
    list4_->BeginRenderPass(0, nullptr, nullptr,
                            D3D12_RENDER_PASS_FLAG_RESUMING_PASS);
    expected = E_NOTIMPL;
    break;
  }
  EXPECT_EQ(context_.list()->Close(), expected);
}

std::string InvalidRenderPassCaseName(
    const ::testing::TestParamInfo<InvalidRenderPassCase> &info) {
  switch (info.param) {
  case InvalidRenderPassCase::EndWithoutBegin:
    return "EndWithoutBegin";
  case InvalidRenderPassCase::NestedBegin:
    return "NestedBegin";
  case InvalidRenderPassCase::NullTargetsWithNonzeroCount:
    return "NullTargetsWithNonzeroCount";
  case InvalidRenderPassCase::TooManyRenderTargets:
    return "TooManyRenderTargets";
  case InvalidRenderPassCase::SuspendingFlag:
    return "SuspendingFlag";
  case InvalidRenderPassCase::ResumingFlag:
    return "ResumingFlag";
  }
  return "Unknown";
}

INSTANTIATE_TEST_SUITE_P(
    InvalidStateMatrix, RenderPassValidationSpec,
    ::testing::Values(InvalidRenderPassCase::EndWithoutBegin,
                      InvalidRenderPassCase::NestedBegin,
                      InvalidRenderPassCase::NullTargetsWithNonzeroCount,
                      InvalidRenderPassCase::TooManyRenderTargets,
                      InvalidRenderPassCase::SuspendingFlag,
                      InvalidRenderPassCase::ResumingFlag),
    InvalidRenderPassCaseName);

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
