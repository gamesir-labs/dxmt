#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

#include <array>
#include <cstring>
#include <vector>

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;
using dxmt::test::TextureReadback;

class D3D12QueueSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_TRUE(SUCCEEDED(context_.Initialize()));
  }

  D3D12TestContext context_;
};

TEST_F(D3D12QueueSpec, CompletesBufferCopyBeforeFenceSignal) {
  const std::array<std::uint32_t, 16> expected = {
      0x01020304, 0x11121314, 0x21222324, 0x31323334,
      0x41424344, 0x51525354, 0x61626364, 0x71727374,
      0x81828384, 0x91929394, 0xa1a2a3a4, 0xb1b2b3b4,
      0xc1c2c3c4, 0xd1d2d3d4, 0xe1e2e3e4, 0xf1f2f3f4,
  };
  ComPtr<ID3D12Resource> upload = context_.CreateUploadBuffer(
      sizeof(expected), expected.data(), sizeof(expected));
  ComPtr<ID3D12Resource> destination = context_.CreateBuffer(
      sizeof(expected), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(upload);
  ASSERT_TRUE(destination);

  context_.list()->CopyBufferRegion(destination.get(), 0, upload.get(), 0,
                                    sizeof(expected));
  ASSERT_TRUE(SUCCEEDED(context_.ExecuteAndWait()));
  ASSERT_TRUE(SUCCEEDED(context_.ResetCommandList()));

  D3D12TestContext::Transition(
      context_.list(), destination.get(), D3D12_RESOURCE_STATE_COPY_DEST,
      D3D12_RESOURCE_STATE_COPY_SOURCE);
  std::vector<std::uint8_t> actual;
  ASSERT_TRUE(SUCCEEDED(context_.ReadbackBuffer(destination.get(),
                                               sizeof(expected), &actual)));
  ASSERT_EQ(actual.size(), sizeof(expected));
  EXPECT_EQ(std::memcmp(actual.data(), expected.data(), sizeof(expected)), 0);
}

TEST_F(D3D12QueueSpec, PreservesTextureUploadAcrossSubmissions) {
  const std::array<std::uint32_t, 16> expected = {
      0xff000001, 0xff000002, 0xff000003, 0xff000004,
      0xff000011, 0xff000012, 0xff000013, 0xff000014,
      0xff000021, 0xff000022, 0xff000023, 0xff000024,
      0xff000031, 0xff000032, 0xff000033, 0xff000034,
  };
  ComPtr<ID3D12Resource> texture = context_.CreateTexture2D(
      4, 4, 1, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(texture);
  ASSERT_TRUE(SUCCEEDED(context_.UploadTextureAndReset(
      texture.get(), expected.data(), 4 * sizeof(std::uint32_t),
      sizeof(expected))));

  D3D12TestContext::Transition(
      context_.list(), texture.get(), D3D12_RESOURCE_STATE_COPY_DEST,
      D3D12_RESOURCE_STATE_COPY_SOURCE);
  TextureReadback readback;
  ASSERT_TRUE(SUCCEEDED(context_.ReadbackTexture(texture.get(), &readback)));
  ASSERT_EQ(readback.width, 4u);
  ASSERT_EQ(readback.height, 4u);
  for (UINT y = 0; y < readback.height; ++y) {
    for (UINT x = 0; x < readback.width; ++x) {
      std::uint32_t actual = 0;
      std::memcpy(&actual,
                  readback.data.data() + y * readback.row_pitch +
                      x * sizeof(actual),
                  sizeof(actual));
      EXPECT_EQ(actual, expected[y * readback.width + x]);
    }
  }
}

} // namespace
