#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

class D3D12SharedHandleSpec : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  D3D12TestContext context_;
};

TEST_F(D3D12SharedHandleSpec, CreateSharedHandleIsUnsupportedAndClearsHandle) {
  auto resource = context_.CreateBuffer(256, D3D12_HEAP_TYPE_DEFAULT,
                                        D3D12_RESOURCE_FLAG_NONE,
                                        D3D12_RESOURCE_STATE_COMMON);
  ASSERT_TRUE(resource);

  HANDLE handle = reinterpret_cast<HANDLE>(static_cast<UINT_PTR>(1));
  EXPECT_EQ(context_.device()->CreateSharedHandle(
                resource.get(), nullptr, GENERIC_ALL,
                L"dxmt-d3d12-shared-resource", &handle),
            E_NOTIMPL);
  EXPECT_EQ(handle, nullptr);

  ComPtr<ID3D12Fence> fence;
  ASSERT_EQ(context_.device()->CreateFence(0, D3D12_FENCE_FLAG_SHARED,
                                           IID_PPV_ARGS(fence.put())),
            S_OK);
  handle = reinterpret_cast<HANDLE>(static_cast<UINT_PTR>(1));
  EXPECT_EQ(context_.device()->CreateSharedHandle(
                fence.get(), nullptr, GENERIC_ALL, L"dxmt-d3d12-shared-fence",
                &handle),
            E_NOTIMPL);
  EXPECT_EQ(handle, nullptr);
}

TEST_F(D3D12SharedHandleSpec, OpenSharedHandleIsUnsupportedAndClearsObject) {
  void *object = reinterpret_cast<void *>(static_cast<UINT_PTR>(1));
  EXPECT_EQ(context_.device()->OpenSharedHandle(
                reinterpret_cast<HANDLE>(static_cast<UINT_PTR>(1)),
                __uuidof(ID3D12Resource), &object),
            E_NOTIMPL);
  EXPECT_EQ(object, nullptr);

  object = reinterpret_cast<void *>(static_cast<UINT_PTR>(1));
  EXPECT_EQ(context_.device()->OpenSharedHandle(nullptr, __uuidof(ID3D12Fence),
                                                &object),
            E_NOTIMPL);
  EXPECT_EQ(object, nullptr);
}

TEST_F(D3D12SharedHandleSpec,
       OpenSharedHandleByNameIsUnsupportedAndClearsHandle) {
  HANDLE handle = reinterpret_cast<HANDLE>(static_cast<UINT_PTR>(1));
  EXPECT_EQ(context_.device()->OpenSharedHandleByName(
                L"dxmt-d3d12-missing-shared-object", GENERIC_ALL, &handle),
            E_NOTIMPL);
  EXPECT_EQ(handle, nullptr);

  handle = reinterpret_cast<HANDLE>(static_cast<UINT_PTR>(1));
  EXPECT_EQ(
      context_.device()->OpenSharedHandleByName(nullptr, GENERIC_ALL, &handle),
      E_NOTIMPL);
  EXPECT_EQ(handle, nullptr);
}

} // namespace
