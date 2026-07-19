#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

#include <string>

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

class D3D12SharedHandleSpec : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  D3D12TestContext context_;
};

TEST_F(D3D12SharedHandleSpec, SharesFenceByHandleAndNameWhenSupported) {
  ComPtr<ID3D12Fence> fence;
  ASSERT_EQ(context_.device()->CreateFence(
                7, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(fence.put())),
            S_OK);

  const std::wstring name =
      L"dxmt-public-fence-" + std::to_wstring(GetCurrentProcessId());
  HANDLE shared_handle = nullptr;
  const HRESULT create_hr = context_.device()->CreateSharedHandle(
      fence.get(), nullptr, GENERIC_ALL, name.c_str(), &shared_handle);
  if (create_hr == E_NOTIMPL || create_hr == DXGI_ERROR_UNSUPPORTED) {
    EXPECT_EQ(shared_handle, nullptr);
    GTEST_SKIP() << "shared handles are not supported by this runtime";
  }
  ASSERT_EQ(create_hr, S_OK);
  ASSERT_NE(shared_handle, nullptr);

  ComPtr<ID3D12Fence> opened;
  ASSERT_EQ(context_.device()->OpenSharedHandle(
                shared_handle, __uuidof(ID3D12Fence),
                reinterpret_cast<void **>(opened.put())),
            S_OK);
  ASSERT_TRUE(opened);
  EXPECT_EQ(opened->GetCompletedValue(), 7u);

  HANDLE named_handle = nullptr;
  ASSERT_EQ(context_.device()->OpenSharedHandleByName(
                name.c_str(), GENERIC_ALL, &named_handle),
            S_OK);
  ASSERT_NE(named_handle, nullptr);

  ComPtr<ID3D12Fence> opened_by_name;
  ASSERT_EQ(context_.device()->OpenSharedHandle(
                named_handle, __uuidof(ID3D12Fence),
                reinterpret_cast<void **>(opened_by_name.put())),
            S_OK);
  ASSERT_TRUE(opened_by_name);
  EXPECT_EQ(opened_by_name->GetCompletedValue(), 7u);

  EXPECT_TRUE(CloseHandle(named_handle));
  EXPECT_TRUE(CloseHandle(shared_handle));
}

} // namespace
