#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

#include <string>
#include <vector>

// Public D3D12 command allocator/list/queue type matrix.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

struct ListTypeCase {
  D3D12_COMMAND_LIST_TYPE type;
  bool create_queue;
  bool expect_allocator_ok;
  bool expect_list_ok;
  bool expect_queue_ok;
};

std::vector<ListTypeCase> BuildListTypeCases() {
  return {
      {D3D12_COMMAND_LIST_TYPE_DIRECT, true, true, true, true},
      {D3D12_COMMAND_LIST_TYPE_BUNDLE, true, true, true, false},
      {D3D12_COMMAND_LIST_TYPE_COMPUTE, true, true, true, true},
      {D3D12_COMMAND_LIST_TYPE_COPY, true, true, true, true},
      {static_cast<D3D12_COMMAND_LIST_TYPE>(4), true, false, false, false},
      {static_cast<D3D12_COMMAND_LIST_TYPE>(100), true, false, false, false},
      {static_cast<D3D12_COMMAND_LIST_TYPE>(UINT_MAX), true, false, false,
       false},
  };
}

class CommandListTypeMatrixSpec
    : public ::testing::TestWithParam<ListTypeCase> {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  D3D12TestContext context_;
};

TEST_P(CommandListTypeMatrixSpec, AllocatorCreationMatchesExpectation) {
  const auto &test = GetParam();
  ComPtr<ID3D12CommandAllocator> allocator;
  const HRESULT hr = context_.device()->CreateCommandAllocator(
      test.type, IID_PPV_ARGS(allocator.put()));
  if (test.expect_allocator_ok) {
    ASSERT_EQ(hr, S_OK);
    ASSERT_TRUE(allocator);
  } else {
    EXPECT_HRESULT_FAILED(hr);
  }
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_P(CommandListTypeMatrixSpec, ListCreationMatchesExpectation) {
  const auto &test = GetParam();
  if (!test.expect_allocator_ok)
    GTEST_SKIP() << "allocator type invalid";
  ComPtr<ID3D12CommandAllocator> allocator;
  ASSERT_EQ(context_.device()->CreateCommandAllocator(
                test.type, IID_PPV_ARGS(allocator.put())),
            S_OK);
  ComPtr<ID3D12GraphicsCommandList> list;
  const HRESULT hr = context_.device()->CreateCommandList(
      0, test.type, allocator.get(), nullptr, IID_PPV_ARGS(list.put()));
  if (test.expect_list_ok) {
    ASSERT_EQ(hr, S_OK);
    ASSERT_TRUE(list);
    EXPECT_EQ(list->Close(), S_OK);
  } else {
    EXPECT_HRESULT_FAILED(hr);
  }
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_P(CommandListTypeMatrixSpec, QueueCreationMatchesExpectation) {
  const auto &test = GetParam();
  if (!test.create_queue)
    GTEST_SKIP();
  D3D12_COMMAND_QUEUE_DESC desc = {};
  desc.Type = test.type;
  desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
  desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
  ComPtr<ID3D12CommandQueue> queue;
  const HRESULT hr = context_.device()->CreateCommandQueue(
      &desc, IID_PPV_ARGS(queue.put()));
  if (test.expect_queue_ok) {
    ASSERT_EQ(hr, S_OK);
    ASSERT_TRUE(queue);
    EXPECT_EQ(queue->GetDesc().Type, test.type);
  } else {
    EXPECT_HRESULT_FAILED(hr);
  }
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

std::string ListTypeName(const ::testing::TestParamInfo<ListTypeCase> &info) {
  return "Type" + std::to_string(static_cast<UINT>(info.param.type));
}

INSTANTIATE_TEST_SUITE_P(TypeMatrix, CommandListTypeMatrixSpec,
                         ::testing::ValuesIn(BuildListTypeCases()),
                         ListTypeName);

} // namespace
