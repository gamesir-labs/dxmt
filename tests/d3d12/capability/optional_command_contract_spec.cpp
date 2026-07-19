#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

class OptionalCommandContractSpec : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  D3D12TestContext context_;
};

TEST_F(OptionalCommandContractSpec, EmptyStreamOutputTargetIsHarmless) {
  context_.list()->SOSetTargets(0, 0, nullptr);
  EXPECT_EQ(context_.ExecuteAndWait(), S_OK);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(OptionalCommandContractSpec,
       NullProgrammableSamplePositionsResetIsHarmless) {
  ComPtr<ID3D12GraphicsCommandList1> list1;
  ASSERT_EQ(context_.list()->QueryInterface(
                __uuidof(ID3D12GraphicsCommandList1),
                reinterpret_cast<void **>(list1.put())),
            S_OK);
  list1->SetSamplePositions(0, 0, nullptr);
  EXPECT_EQ(context_.ExecuteAndWait(), S_OK);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
