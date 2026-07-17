#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

class MarkerEventSpec : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  template <typename Interface> ComPtr<Interface> QueryList() {
    ComPtr<Interface> result;
    EXPECT_EQ(context_.list()->QueryInterface(
                  __uuidof(Interface),
                  reinterpret_cast<void **>(result.put())),
              S_OK);
    return result;
  }

  void ExpectCopyExecution() {
    constexpr std::array<std::uint32_t, 8> expected = {
        0x10203040u, 0x50607080u, 0x90a0b0c0u, 0xd0e0f001u,
        0x13579bdfu, 0x2468ace0u, 0xfeedc0deu, 0x0badf00du};
    auto upload = context_.CreateUploadBuffer(sizeof(expected), expected.data(),
                                               sizeof(expected));
    auto destination = context_.CreateBuffer(
        sizeof(expected), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
        D3D12_RESOURCE_STATE_COPY_DEST);
    ASSERT_TRUE(upload);
    ASSERT_TRUE(destination);
    context_.list()->CopyBufferRegion(destination.get(), 0, upload.get(), 0,
                                      sizeof(expected));
    D3D12TestContext::Transition(context_.list(), destination.get(),
                                 D3D12_RESOURCE_STATE_COPY_DEST,
                                 D3D12_RESOURCE_STATE_COPY_SOURCE);

    std::vector<std::uint8_t> actual;
    ASSERT_EQ(context_.ReadbackBuffer(destination.get(), sizeof(expected),
                                      &actual),
              S_OK);
    ASSERT_EQ(actual.size(), sizeof(expected));
    EXPECT_EQ(std::memcmp(actual.data(), expected.data(), sizeof(expected)), 0);
    EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
  }

  D3D12TestContext context_;
};

TEST_F(MarkerEventSpec, CommandListMarkersPreserveExecutionAndCloseState) {
  constexpr std::array<std::uint8_t, 7> payload = {0x50, 0x49, 0x58, 0,
                                                   0x7f, 0x80, 0xff};
  context_.list()->SetMarker(0, nullptr, 0);
  context_.list()->BeginEvent(1, payload.data(), payload.size());
  context_.list()->SetMarker(2, payload.data(), payload.size());
  context_.list()->BeginEvent(3, nullptr, 0);
  context_.list()->EndEvent();
  context_.list()->EndEvent();
  ExpectCopyExecution();
}

TEST_F(MarkerEventSpec, QueueMarkersPreserveSubmissionAndFenceProgress) {
  constexpr std::array<std::uint32_t, 3> payload = {
      0x10203040u, 0x50607080u, 0x90a0b0c0u};
  context_.queue()->BeginEvent(4, payload.data(), sizeof(payload));
  context_.queue()->SetMarker(5, nullptr, 0);
  context_.queue()->SetMarker(6, payload.data(), sizeof(payload));
  ExpectCopyExecution();
  context_.queue()->EndEvent();
}

TEST_F(MarkerEventSpec, ViewInstanceMaskDoesNotAffectNonInstancedCopy) {
  auto list1 = QueryList<ID3D12GraphicsCommandList1>();
  ASSERT_TRUE(list1);
  list1->SetViewInstanceMask(1);
  list1->SetViewInstanceMask(UINT_MAX);
  ExpectCopyExecution();
}

TEST_F(MarkerEventSpec, NullProtectedSessionPreservesUnprotectedExecution) {
  auto list3 = QueryList<ID3D12GraphicsCommandList3>();
  ASSERT_TRUE(list3);
  list3->SetProtectedResourceSession(nullptr);
  ExpectCopyExecution();
}

} // namespace
