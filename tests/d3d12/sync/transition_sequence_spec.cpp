#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

#include <array>
#include <cstring>
#include <vector>

namespace {

using dxmt::test::D3D12TestContext;

class TransitionSequenceSpec : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_TRUE(SUCCEEDED(context_.Initialize())); }

  D3D12TestContext context_;
};

TEST_F(TransitionSequenceSpec, CommonStateRoundTripPreservesData) {
  const std::array<std::uint32_t, 8> expected = {
      0x01234567, 0x89abcdef, 0x13579bdf, 0x2468ace0,
      0x55aa55aa, 0xaa55aa55, 0x10203040, 0x50607080,
  };
  auto upload = context_.CreateUploadBuffer(sizeof(expected), expected.data(),
                                            sizeof(expected));
  auto resource = context_.CreateBuffer(
      sizeof(expected), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(upload);
  ASSERT_TRUE(resource);

  context_.list()->CopyBufferRegion(resource.get(), 0, upload.get(), 0,
                                    sizeof(expected));
  D3D12TestContext::Transition(context_.list(), resource.get(),
                               D3D12_RESOURCE_STATE_COPY_DEST,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
  D3D12TestContext::Transition(context_.list(), resource.get(),
                               D3D12_RESOURCE_STATE_COPY_SOURCE,
                               D3D12_RESOURCE_STATE_COMMON);
  D3D12TestContext::Transition(context_.list(), resource.get(),
                               D3D12_RESOURCE_STATE_COMMON,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);

  std::vector<std::uint8_t> actual;
  ASSERT_TRUE(SUCCEEDED(
      context_.ReadbackBuffer(resource.get(), sizeof(expected), &actual)));
  ASSERT_EQ(actual.size(), sizeof(expected));
  EXPECT_EQ(std::memcmp(actual.data(), expected.data(), sizeof(expected)), 0);
}

TEST_F(TransitionSequenceSpec, CopySourceToDestinationToSourceRestoresUse) {
  const std::array<std::uint32_t, 8> first = {
      0x11111111, 0x22222222, 0x33333333, 0x44444444,
      0x55555555, 0x66666666, 0x77777777, 0x88888888,
  };
  const std::array<std::uint32_t, 8> second = {
      0x99999999, 0xaaaaaaaa, 0xbbbbbbbb, 0xcccccccc,
      0xdddddddd, 0xeeeeeeee, 0xffffffff, 0x00000000,
  };
  auto first_upload =
      context_.CreateUploadBuffer(sizeof(first), first.data(), sizeof(first));
  auto second_upload = context_.CreateUploadBuffer(
      sizeof(second), second.data(), sizeof(second));
  auto resource = context_.CreateBuffer(sizeof(first), D3D12_HEAP_TYPE_DEFAULT,
                                        D3D12_RESOURCE_FLAG_NONE,
                                        D3D12_RESOURCE_STATE_COPY_DEST);
  auto readback = context_.CreateBuffer(
      sizeof(first) + sizeof(second), D3D12_HEAP_TYPE_READBACK,
      D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(first_upload);
  ASSERT_TRUE(second_upload);
  ASSERT_TRUE(resource);
  ASSERT_TRUE(readback);

  context_.list()->CopyBufferRegion(resource.get(), 0, first_upload.get(), 0,
                                    sizeof(first));
  D3D12TestContext::Transition(context_.list(), resource.get(),
                               D3D12_RESOURCE_STATE_COPY_DEST,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
  context_.list()->CopyBufferRegion(readback.get(), 0, resource.get(), 0,
                                    sizeof(first));
  D3D12TestContext::Transition(context_.list(), resource.get(),
                               D3D12_RESOURCE_STATE_COPY_SOURCE,
                               D3D12_RESOURCE_STATE_COPY_DEST);
  context_.list()->CopyBufferRegion(resource.get(), 0, second_upload.get(), 0,
                                    sizeof(second));
  D3D12TestContext::Transition(context_.list(), resource.get(),
                               D3D12_RESOURCE_STATE_COPY_DEST,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
  context_.list()->CopyBufferRegion(readback.get(), sizeof(first),
                                    resource.get(), 0, sizeof(second));
  ASSERT_TRUE(SUCCEEDED(context_.ExecuteAndWait()));

  void *mapping = nullptr;
  const D3D12_RANGE read_range = {0, sizeof(first) + sizeof(second)};
  ASSERT_TRUE(SUCCEEDED(readback->Map(0, &read_range, &mapping)));
  EXPECT_EQ(std::memcmp(mapping, first.data(), sizeof(first)), 0);
  EXPECT_EQ(std::memcmp(static_cast<std::uint8_t *>(mapping) + sizeof(first),
                        second.data(), sizeof(second)),
            0);
  const D3D12_RANGE no_write = {0, 0};
  readback->Unmap(0, &no_write);
}

} // namespace
