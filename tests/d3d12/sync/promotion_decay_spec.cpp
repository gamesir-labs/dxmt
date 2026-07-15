#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

#include <array>
#include <cstring>

namespace {

using dxmt::test::D3D12TestContext;

class PromotionDecaySpec : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_TRUE(SUCCEEDED(context_.Initialize())); }

  D3D12TestContext context_;
};

TEST_F(PromotionDecaySpec, CommonBufferPromotesToCopyDestination) {
  const std::array<std::uint32_t, 8> expected = {
      0x01020304, 0x11121314, 0x21222324, 0x31323334,
      0x41424344, 0x51525354, 0x61626364, 0x71727374,
  };
  auto upload = context_.CreateUploadBuffer(sizeof(expected), expected.data(),
                                            sizeof(expected));
  auto resource = context_.CreateBuffer(
      sizeof(expected), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COMMON);
  auto readback = context_.CreateBuffer(
      sizeof(expected), D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(upload);
  ASSERT_TRUE(resource);
  ASSERT_TRUE(readback);

  context_.list()->CopyBufferRegion(resource.get(), 0, upload.get(), 0,
                                    sizeof(expected));
  D3D12TestContext::Transition(context_.list(), resource.get(),
                               D3D12_RESOURCE_STATE_COPY_DEST,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
  context_.list()->CopyBufferRegion(readback.get(), 0, resource.get(), 0,
                                    sizeof(expected));
  ASSERT_TRUE(SUCCEEDED(context_.ExecuteAndWait()));

  void *mapping = nullptr;
  const D3D12_RANGE read_range = {0, sizeof(expected)};
  ASSERT_TRUE(SUCCEEDED(readback->Map(0, &read_range, &mapping)));
  EXPECT_EQ(std::memcmp(mapping, expected.data(), sizeof(expected)), 0);
  const D3D12_RANGE written_range = {0, 0};
  readback->Unmap(0, &written_range);
}

TEST_F(PromotionDecaySpec, CommonBufferDecaysBetweenSubmissions) {
  const std::array<std::uint32_t, 8> expected = {
      0x89abcdef, 0x01234567, 0x76543210, 0xfedcba98,
      0x13579bdf, 0x2468ace0, 0x55aa55aa, 0xaa55aa55,
  };
  auto upload = context_.CreateUploadBuffer(sizeof(expected), expected.data(),
                                            sizeof(expected));
  auto resource = context_.CreateBuffer(
      sizeof(expected), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COMMON);
  auto readback = context_.CreateBuffer(
      sizeof(expected), D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(upload);
  ASSERT_TRUE(resource);
  ASSERT_TRUE(readback);

  context_.list()->CopyBufferRegion(resource.get(), 0, upload.get(), 0,
                                    sizeof(expected));
  ASSERT_TRUE(SUCCEEDED(context_.ExecuteAndWait()));
  ASSERT_TRUE(SUCCEEDED(context_.ResetCommandList()));
  context_.list()->CopyBufferRegion(readback.get(), 0, resource.get(), 0,
                                    sizeof(expected));
  ASSERT_TRUE(SUCCEEDED(context_.ExecuteAndWait()));

  void *mapping = nullptr;
  const D3D12_RANGE read_range = {0, sizeof(expected)};
  ASSERT_TRUE(SUCCEEDED(readback->Map(0, &read_range, &mapping)));
  EXPECT_EQ(std::memcmp(mapping, expected.data(), sizeof(expected)), 0);
  const D3D12_RANGE written_range = {0, 0};
  readback->Unmap(0, &written_range);
}

TEST_F(PromotionDecaySpec, CommonPromotionMatchesExplicitTransition) {
  const std::array<std::uint32_t, 8> expected = {
      0x10213243, 0x54657687, 0x98a9bacb, 0xdcedfe0f,
      0x11223344, 0x55667788, 0x99aabbcc, 0xddeeff00,
  };
  auto upload = context_.CreateUploadBuffer(sizeof(expected), expected.data(),
                                            sizeof(expected));
  auto implicit = context_.CreateBuffer(
      sizeof(expected), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COMMON);
  auto explicit_resource = context_.CreateBuffer(
      sizeof(expected), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COMMON);
  auto readback = context_.CreateBuffer(
      2 * sizeof(expected), D3D12_HEAP_TYPE_READBACK,
      D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(upload);
  ASSERT_TRUE(implicit);
  ASSERT_TRUE(explicit_resource);
  ASSERT_TRUE(readback);

  context_.list()->CopyBufferRegion(implicit.get(), 0, upload.get(), 0,
                                    sizeof(expected));
  D3D12TestContext::Transition(
      context_.list(), explicit_resource.get(), D3D12_RESOURCE_STATE_COMMON,
      D3D12_RESOURCE_STATE_COPY_DEST);
  context_.list()->CopyBufferRegion(explicit_resource.get(), 0, upload.get(),
                                    0, sizeof(expected));
  D3D12TestContext::Transition(
      context_.list(), implicit.get(), D3D12_RESOURCE_STATE_COPY_DEST,
      D3D12_RESOURCE_STATE_COPY_SOURCE);
  D3D12TestContext::Transition(
      context_.list(), explicit_resource.get(),
      D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
  context_.list()->CopyBufferRegion(readback.get(), 0, implicit.get(), 0,
                                    sizeof(expected));
  context_.list()->CopyBufferRegion(readback.get(), sizeof(expected),
                                    explicit_resource.get(), 0,
                                    sizeof(expected));
  ASSERT_EQ(context_.ExecuteAndWait(), S_OK);

  void *mapping = nullptr;
  const D3D12_RANGE read_range = {0, 2 * sizeof(expected)};
  ASSERT_EQ(readback->Map(0, &read_range, &mapping), S_OK);
  EXPECT_EQ(std::memcmp(mapping, expected.data(), sizeof(expected)), 0);
  EXPECT_EQ(std::memcmp(static_cast<const std::uint8_t *>(mapping) +
                            sizeof(expected),
                        expected.data(), sizeof(expected)),
            0);
  const D3D12_RANGE written_range = {0, 0};
  readback->Unmap(0, &written_range);
}

} // namespace
