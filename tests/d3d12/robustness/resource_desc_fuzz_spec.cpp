#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

std::uint32_t NextResourceRandom(std::uint32_t &state) {
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return state;
}

class D3D12ResourceDescFuzzSpec : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  D3D12TestContext context_;
};

TEST_F(D3D12ResourceDescFuzzSpec,
       BoundedDescriptorCorpusFailsClosedAndKeepsDeviceUsable) {
  std::uint32_t seed = 0x6d2b79f5u;
  std::size_t case_count = 256;
  if (const char *replay_seed = std::getenv("DXMT_D3D12_RESOURCE_FUZZ_SEED")) {
    char *end = nullptr;
    const auto parsed = std::strtoul(replay_seed, &end, 0);
    ASSERT_NE(end, replay_seed) << "invalid resource fuzz seed";
    ASSERT_EQ(*end, '\0') << "invalid resource fuzz seed";
    seed = static_cast<std::uint32_t>(parsed);
    case_count = 1;
  }

  constexpr std::array<DXGI_FORMAT, 8> formats = {
      DXGI_FORMAT_UNKNOWN,      DXGI_FORMAT_R8_UNORM,
      DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R16_FLOAT,
      DXGI_FORMAT_R32_UINT,     DXGI_FORMAT_D32_FLOAT,
      DXGI_FORMAT_BC1_UNORM,    DXGI_FORMAT_NV12,
  };
  constexpr std::array<UINT, 5> sample_counts = {0, 1, 2, 4, 8};
  D3D12_HEAP_PROPERTIES heap = {};
  heap.Type = D3D12_HEAP_TYPE_DEFAULT;

  for (std::size_t case_index = 0; case_index < case_count; ++case_index) {
    const std::uint32_t case_seed = seed;
    const std::uint32_t a = NextResourceRandom(seed);
    const std::uint32_t b = NextResourceRandom(seed);
    const std::uint32_t c = NextResourceRandom(seed);
    SCOPED_TRACE(::testing::Message()
                 << "case=" << case_index << " seed=0x" << std::hex
                 << case_seed);

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = static_cast<D3D12_RESOURCE_DIMENSION>(1 + (a % 4));
    desc.Alignment = (a & 0x100u) ? 0 : (1ull << (12 + (a % 9)));
    desc.Width = (a & 0x200u) ? 0 : 1 + (UINT64(b & 0x7ffu));
    desc.Height = (b & 0x1000u) ? 0 : 1 + ((b >> 12) & 0x7ffu);
    desc.DepthOrArraySize =
        (b & 0x800000u) ? 0 : static_cast<UINT16>(1 + ((b >> 23) & 0x1fu));
    desc.MipLevels =
        (c & 1u) ? 0 : static_cast<UINT16>(1 + ((c >> 1) & 0xfu));
    desc.Format = formats[(c >> 5) % formats.size()];
    desc.SampleDesc.Count = sample_counts[(c >> 9) % sample_counts.size()];
    desc.SampleDesc.Quality = (c >> 12) & 3u;
    desc.Layout = static_cast<D3D12_TEXTURE_LAYOUT>((c >> 14) % 4);
    desc.Flags = static_cast<D3D12_RESOURCE_FLAGS>((c >> 18) & 0x7fu);

    if (desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) {
      desc.Height = 1;
      desc.DepthOrArraySize = 1;
      desc.MipLevels = 1;
      desc.Format = DXGI_FORMAT_UNKNOWN;
      desc.SampleDesc.Count = (c & 0x400u) ? 0 : 1;
      desc.SampleDesc.Quality = 0;
      desc.Layout = (c & 0x800u) ? D3D12_TEXTURE_LAYOUT_ROW_MAJOR
                                 : D3D12_TEXTURE_LAYOUT_UNKNOWN;
    }

    const auto allocation =
        context_.device()->GetResourceAllocationInfo(0, 1, &desc);
    ComPtr<ID3D12Resource> resource;
    const HRESULT hr = context_.device()->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON,
        nullptr, IID_PPV_ARGS(resource.put()));
    if (SUCCEEDED(hr)) {
      ASSERT_TRUE(resource);
      EXPECT_NE(allocation.SizeInBytes, UINT64_MAX);
      EXPECT_GT(allocation.SizeInBytes, 0u);
      EXPECT_GT(allocation.Alignment, 0u);
      const auto actual = resource->GetDesc();
      EXPECT_EQ(actual.Dimension, desc.Dimension);
      EXPECT_EQ(actual.Width, desc.Width);
      EXPECT_EQ(actual.Height, desc.Height);
    } else {
      EXPECT_FALSE(resource);
    }
    ASSERT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
  }

  auto proof = context_.CreateBuffer(
      256, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COMMON);
  EXPECT_TRUE(proof);
}

} // namespace
