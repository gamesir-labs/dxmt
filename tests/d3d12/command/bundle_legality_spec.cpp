#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"

#include <array>
#include <cstring>
#include <vector>

namespace {

using dxmt::test::ComPtr;
using dxmt::test::CompileShader;
using dxmt::test::D3D12TestContext;

class BundleLegalitySpec : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  D3D12TestContext context_;
};


TEST_F(BundleLegalitySpec,
       ExecutesSameBundleFromTwoDirectListsAfterBundleRelease) {
  ComPtr<ID3D12CommandAllocator> bundle_allocator;
  ComPtr<ID3D12GraphicsCommandList> bundle;
  ASSERT_EQ(context_.device()->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_BUNDLE,
                IID_PPV_ARGS(bundle_allocator.put())),
            S_OK);
  ASSERT_EQ(context_.device()->CreateCommandList(
                0, D3D12_COMMAND_LIST_TYPE_BUNDLE, bundle_allocator.get(),
                nullptr, IID_PPV_ARGS(bundle.put())),
            S_OK);
  bundle->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  ASSERT_EQ(bundle->Close(), S_OK);

  ComPtr<ID3D12CommandAllocator> second_allocator;
  ComPtr<ID3D12GraphicsCommandList> second_list;
  ASSERT_EQ(context_.device()->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                IID_PPV_ARGS(second_allocator.put())),
            S_OK);
  ASSERT_EQ(context_.device()->CreateCommandList(
                0, D3D12_COMMAND_LIST_TYPE_DIRECT, second_allocator.get(),
                nullptr, IID_PPV_ARGS(second_list.put())),
            S_OK);
  context_.list()->ExecuteBundle(bundle.get());
  second_list->ExecuteBundle(bundle.get());
  bundle.reset();
  bundle_allocator.reset();

  EXPECT_EQ(context_.list()->Close(), S_OK);
  EXPECT_EQ(second_list->Close(), S_OK);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
