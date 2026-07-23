#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <d3d11_4.h>

#include <cstdint>

// Public ID3D11Multithread contracts for immediate and deferred contexts.
// Protection state belongs to the test-local device, so these tests are safe
// under the default parallel scheduler.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

class D3D11MultithreadContractSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_NE(context_.context(), nullptr);
  }

  D3D11TestContext context_;
};

TEST_F(D3D11MultithreadContractSpec,
       ImmediateContextRoundTripsProtectionStateAndIdentity) {
  ComPtr<ID3D11Multithread> multithread;
  ASSERT_EQ(context_.context()->QueryInterface(
                __uuidof(ID3D11Multithread),
                reinterpret_cast<void **>(multithread.put())),
            S_OK);
  ASSERT_NE(multithread.get(), nullptr);

  ComPtr<IUnknown> context_identity;
  ComPtr<IUnknown> multithread_identity;
  ASSERT_EQ(context_.context()->QueryInterface(
                __uuidof(IUnknown),
                reinterpret_cast<void **>(context_identity.put())),
            S_OK);
  ASSERT_EQ(multithread->QueryInterface(
                __uuidof(IUnknown),
                reinterpret_cast<void **>(multithread_identity.put())),
            S_OK);
  EXPECT_EQ(multithread_identity.get(), context_identity.get());

  EXPECT_EQ(multithread->GetMultithreadProtected(), FALSE);
  EXPECT_EQ(multithread->SetMultithreadProtected(TRUE), FALSE);
  EXPECT_EQ(multithread->GetMultithreadProtected(), TRUE);
  EXPECT_EQ(multithread->SetMultithreadProtected(TRUE), TRUE);
  EXPECT_EQ(multithread->GetMultithreadProtected(), TRUE);
  EXPECT_EQ(multithread->SetMultithreadProtected(FALSE), TRUE);
  EXPECT_EQ(multithread->GetMultithreadProtected(), FALSE);
  EXPECT_EQ(multithread->SetMultithreadProtected(FALSE), FALSE);
  EXPECT_EQ(multithread->GetMultithreadProtected(), FALSE);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11MultithreadContractSpec,
       DeferredContextRejectsMultithreadInterface) {
  ComPtr<ID3D11DeviceContext> deferred;
  ASSERT_EQ(context_.device()->CreateDeferredContext(0, deferred.put()), S_OK);
  ASSERT_NE(deferred.get(), nullptr);

  void *output = reinterpret_cast<void *>(uintptr_t{1});
  EXPECT_EQ(deferred->QueryInterface(__uuidof(ID3D11Multithread), &output),
            E_NOINTERFACE);
  EXPECT_EQ(output, nullptr);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
