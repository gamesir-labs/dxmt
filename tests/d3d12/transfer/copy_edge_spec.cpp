#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace {

using dxmt::test::ComPtr;
using dxmt::test::CreateIsolatedD3D12Device;
using dxmt::test::D3D12TestContext;

struct CopyBufferCase {
  UINT64 byte_count;
  UINT64 source_offset;
  UINT64 destination_offset;
  const char *name;
};

class CopyBufferBoundarySpec
    : public ::testing::TestWithParam<CopyBufferCase> {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  D3D12TestContext context_;
};

TEST_P(CopyBufferBoundarySpec, CopiesExactRequestedInterval) {
  constexpr UINT64 kSize = 128;
  std::array<std::uint8_t, kSize> source_data = {};
  std::array<std::uint8_t, kSize> expected = {};
  source_data.fill(0);
  expected.fill(0xcd);
  for (UINT64 index = 0; index < kSize; ++index)
    source_data[index] = static_cast<std::uint8_t>(index * 37u + 11u);
  const auto &test = GetParam();
  std::copy_n(source_data.begin() + test.source_offset, test.byte_count,
              expected.begin() + test.destination_offset);

  auto source = context_.CreateUploadBuffer(kSize, source_data.data(), kSize);
  auto destination = context_.CreateBuffer(
      kSize, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(source);
  ASSERT_TRUE(destination);
  void *mapped = nullptr;
  ASSERT_EQ(destination->Map(0, nullptr, &mapped), S_OK);
  std::memset(mapped, 0xcd, kSize);
  destination->Unmap(0, nullptr);

  context_.list()->CopyBufferRegion(
      destination.get(), test.destination_offset, source.get(),
      test.source_offset, test.byte_count);
  ASSERT_EQ(context_.ExecuteAndWait(), S_OK);
  ASSERT_EQ(destination->Map(0, nullptr, &mapped), S_OK);
  ASSERT_NE(mapped, nullptr);
  EXPECT_EQ(std::memcmp(mapped, expected.data(), kSize), 0);
  destination->Unmap(0, nullptr);
}

std::string CopyBufferCaseName(
    const ::testing::TestParamInfo<CopyBufferCase> &info) {
  return info.param.name;
}

INSTANTIATE_TEST_SUITE_P(
    BoundaryMatrix, CopyBufferBoundarySpec,
    ::testing::Values(
        CopyBufferCase{1, 0, 0, "FirstByte"},
        CopyBufferCase{1, 127, 127, "LastByte"},
        CopyBufferCase{31, 1, 65, "Count31"},
        CopyBufferCase{32, 16, 64, "Count32"},
        CopyBufferCase{33, 7, 73, "Count33"},
        CopyBufferCase{64, 0, 64, "UpperHalf"},
        CopyBufferCase{128, 0, 0, "WholeBuffer"}),
    CopyBufferCaseName);

enum class InvalidCopyBufferCase {
  DestinationOffsetAtEnd,
  SourceOffsetAtEnd,
  DestinationRangePastEnd,
  SourceRangePastEnd,
  DestinationOffsetOverflow,
  SourceOffsetOverflow,
  NullDestination,
  NullSource,
  TextureDestination,
  TextureSource,
  ForeignDestination,
  ForeignSource,
  ExactSelfOverlap,
  PartialSelfOverlap,
};

class CopyBufferInvalidSpec
    : public ::testing::TestWithParam<InvalidCopyBufferCase> {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  D3D12TestContext context_;
};

TEST_P(CopyBufferInvalidSpec, FailsCommandListClose) {
  constexpr UINT64 kSize = 64;
  auto source = context_.CreateBuffer(
      kSize, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_GENERIC_READ);
  auto destination = context_.CreateBuffer(
      kSize, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  auto source_texture = context_.CreateTexture2D(
      4, 4, 1, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_SOURCE);
  auto destination_texture = context_.CreateTexture2D(
      4, 4, 1, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(source);
  ASSERT_TRUE(destination);
  ASSERT_TRUE(source_texture);
  ASSERT_TRUE(destination_texture);

  ComPtr<ID3D12Device> foreign_device;
  D3D12TestContext foreign_context;
  ComPtr<ID3D12Resource> foreign_source;
  ComPtr<ID3D12Resource> foreign_destination;
  if (GetParam() == InvalidCopyBufferCase::ForeignDestination ||
      GetParam() == InvalidCopyBufferCase::ForeignSource) {
    foreign_device = CreateIsolatedD3D12Device();
    ASSERT_TRUE(foreign_device);
    ASSERT_EQ(foreign_context.Initialize(foreign_device.get()), S_OK);
    foreign_source = foreign_context.CreateBuffer(
        kSize, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
        D3D12_RESOURCE_STATE_COPY_SOURCE);
    foreign_destination = foreign_context.CreateBuffer(
        kSize, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
        D3D12_RESOURCE_STATE_COPY_DEST);
    ASSERT_TRUE(foreign_source);
    ASSERT_TRUE(foreign_destination);
  }

  switch (GetParam()) {
  case InvalidCopyBufferCase::DestinationOffsetAtEnd:
    context_.list()->CopyBufferRegion(destination.get(), kSize, source.get(), 0,
                                      1);
    break;
  case InvalidCopyBufferCase::SourceOffsetAtEnd:
    context_.list()->CopyBufferRegion(destination.get(), 0, source.get(), kSize,
                                      1);
    break;
  case InvalidCopyBufferCase::DestinationRangePastEnd:
    context_.list()->CopyBufferRegion(destination.get(), kSize - 1,
                                      source.get(), 0, 2);
    break;
  case InvalidCopyBufferCase::SourceRangePastEnd:
    context_.list()->CopyBufferRegion(destination.get(), 0, source.get(),
                                      kSize - 1, 2);
    break;
  case InvalidCopyBufferCase::DestinationOffsetOverflow:
    context_.list()->CopyBufferRegion(
        destination.get(), std::numeric_limits<UINT64>::max(), source.get(), 0,
        1);
    break;
  case InvalidCopyBufferCase::SourceOffsetOverflow:
    context_.list()->CopyBufferRegion(
        destination.get(), 0, source.get(),
        std::numeric_limits<UINT64>::max(), 1);
    break;
  case InvalidCopyBufferCase::NullDestination:
    context_.list()->CopyBufferRegion(nullptr, 0, source.get(), 0, 1);
    break;
  case InvalidCopyBufferCase::NullSource:
    context_.list()->CopyBufferRegion(destination.get(), 0, nullptr, 0, 1);
    break;
  case InvalidCopyBufferCase::TextureDestination:
    context_.list()->CopyBufferRegion(destination_texture.get(), 0,
                                      source.get(), 0, 1);
    break;
  case InvalidCopyBufferCase::TextureSource:
    context_.list()->CopyBufferRegion(destination.get(), 0,
                                      source_texture.get(), 0, 1);
    break;
  case InvalidCopyBufferCase::ForeignDestination:
    context_.list()->CopyBufferRegion(foreign_destination.get(), 0,
                                      source.get(), 0, 1);
    break;
  case InvalidCopyBufferCase::ForeignSource:
    context_.list()->CopyBufferRegion(destination.get(), 0,
                                      foreign_source.get(), 0, 1);
    break;
  case InvalidCopyBufferCase::ExactSelfOverlap:
    context_.list()->CopyBufferRegion(source.get(), 0, source.get(), 0, 16);
    break;
  case InvalidCopyBufferCase::PartialSelfOverlap:
    context_.list()->CopyBufferRegion(source.get(), 8, source.get(), 0, 16);
    break;
  }

  EXPECT_EQ(context_.list()->Close(), E_INVALIDARG);

  ComPtr<ID3D12CommandAllocator> recovery_allocator;
  ComPtr<ID3D12GraphicsCommandList> recovery_list;
  ASSERT_EQ(context_.device()->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                IID_PPV_ARGS(recovery_allocator.put())),
            S_OK);
  ASSERT_EQ(context_.device()->CreateCommandList(
                0, D3D12_COMMAND_LIST_TYPE_DIRECT, recovery_allocator.get(),
                nullptr, IID_PPV_ARGS(recovery_list.put())),
            S_OK);
  recovery_list->CopyBufferRegion(destination.get(), 0, source.get(), 0, 1);
  EXPECT_EQ(recovery_list->Close(), S_OK);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

std::string InvalidCopyBufferCaseName(
    const ::testing::TestParamInfo<InvalidCopyBufferCase> &info) {
  switch (info.param) {
  case InvalidCopyBufferCase::DestinationOffsetAtEnd:
    return "DestinationOffsetAtEnd";
  case InvalidCopyBufferCase::SourceOffsetAtEnd:
    return "SourceOffsetAtEnd";
  case InvalidCopyBufferCase::DestinationRangePastEnd:
    return "DestinationRangePastEnd";
  case InvalidCopyBufferCase::SourceRangePastEnd:
    return "SourceRangePastEnd";
  case InvalidCopyBufferCase::DestinationOffsetOverflow:
    return "DestinationOffsetOverflow";
  case InvalidCopyBufferCase::SourceOffsetOverflow:
    return "SourceOffsetOverflow";
  case InvalidCopyBufferCase::NullDestination:
    return "NullDestination";
  case InvalidCopyBufferCase::NullSource:
    return "NullSource";
  case InvalidCopyBufferCase::TextureDestination:
    return "TextureDestination";
  case InvalidCopyBufferCase::TextureSource:
    return "TextureSource";
  case InvalidCopyBufferCase::ForeignDestination:
    return "ForeignDestination";
  case InvalidCopyBufferCase::ForeignSource:
    return "ForeignSource";
  case InvalidCopyBufferCase::ExactSelfOverlap:
    return "ExactSelfOverlap";
  case InvalidCopyBufferCase::PartialSelfOverlap:
    return "PartialSelfOverlap";
  }
  return "Unknown";
}

INSTANTIATE_TEST_SUITE_P(
    InvalidMatrix, CopyBufferInvalidSpec,
    ::testing::Values(
        InvalidCopyBufferCase::DestinationOffsetAtEnd,
        InvalidCopyBufferCase::SourceOffsetAtEnd,
        InvalidCopyBufferCase::DestinationRangePastEnd,
        InvalidCopyBufferCase::SourceRangePastEnd,
        InvalidCopyBufferCase::DestinationOffsetOverflow,
        InvalidCopyBufferCase::SourceOffsetOverflow,
        InvalidCopyBufferCase::NullDestination,
        InvalidCopyBufferCase::NullSource,
        InvalidCopyBufferCase::TextureDestination,
        InvalidCopyBufferCase::TextureSource,
        InvalidCopyBufferCase::ForeignDestination,
        InvalidCopyBufferCase::ForeignSource,
        InvalidCopyBufferCase::ExactSelfOverlap,
        InvalidCopyBufferCase::PartialSelfOverlap),
    InvalidCopyBufferCaseName);

class CopyResourceCompatibilitySpec : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  void ExpectRejected(ID3D12Resource *destination, ID3D12Resource *source) {
    ASSERT_TRUE(destination);
    ASSERT_TRUE(source);
    context_.list()->CopyResource(destination, source);
    EXPECT_EQ(context_.list()->Close(), E_INVALIDARG);

    auto recovery_source = context_.CreateBuffer(
        16, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
        D3D12_RESOURCE_STATE_COPY_SOURCE);
    auto recovery_destination = context_.CreateBuffer(
        16, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
        D3D12_RESOURCE_STATE_COPY_DEST);
    ASSERT_TRUE(recovery_source);
    ASSERT_TRUE(recovery_destination);
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    ASSERT_EQ(context_.device()->CreateCommandAllocator(
                  D3D12_COMMAND_LIST_TYPE_DIRECT,
                  IID_PPV_ARGS(allocator.put())),
              S_OK);
    ASSERT_EQ(context_.device()->CreateCommandList(
                  0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.get(), nullptr,
                  IID_PPV_ARGS(list.put())),
              S_OK);
    list->CopyResource(recovery_destination.get(), recovery_source.get());
    EXPECT_EQ(list->Close(), S_OK);
    EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
  }

  ComPtr<ID3D12Resource> CreateTexture(UINT sample_count,
                                       D3D12_RESOURCE_STATES state) {
    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = 4;
    desc.Height = 4;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = sample_count;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    ComPtr<ID3D12Resource> resource;
    if (FAILED(context_.device()->CreateCommittedResource(
            &heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr,
            IID_PPV_ARGS(resource.put()))))
      return {};
    return resource;
  }

  D3D12TestContext context_;
};

TEST_F(CopyResourceCompatibilitySpec, RejectsDifferentBufferSizes) {
  auto source = context_.CreateBuffer(64, D3D12_HEAP_TYPE_DEFAULT,
                                      D3D12_RESOURCE_FLAG_NONE,
                                      D3D12_RESOURCE_STATE_COPY_SOURCE);
  auto destination = context_.CreateBuffer(65, D3D12_HEAP_TYPE_DEFAULT,
                                           D3D12_RESOURCE_FLAG_NONE,
                                           D3D12_RESOURCE_STATE_COPY_DEST);
  ExpectRejected(destination.get(), source.get());
}

TEST_F(CopyResourceCompatibilitySpec, RejectsBufferToTexture) {
  auto source = context_.CreateBuffer(64, D3D12_HEAP_TYPE_DEFAULT,
                                      D3D12_RESOURCE_FLAG_NONE,
                                      D3D12_RESOURCE_STATE_COPY_SOURCE);
  auto destination = context_.CreateTexture2D(
      4, 4, 1, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  ExpectRejected(destination.get(), source.get());
}

TEST_F(CopyResourceCompatibilitySpec, RejectsDifferentTextureExtent) {
  auto source = context_.CreateTexture2D(
      4, 4, 1, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_SOURCE);
  auto destination = context_.CreateTexture2D(
      5, 4, 1, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  ExpectRejected(destination.get(), source.get());
}

TEST_F(CopyResourceCompatibilitySpec, RejectsDifferentTextureFormat) {
  auto source = context_.CreateTexture2D(
      4, 4, 1, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_SOURCE);
  auto destination = context_.CreateTexture2D(
      4, 4, 1, DXGI_FORMAT_R32_FLOAT, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  ExpectRejected(destination.get(), source.get());
}

TEST_F(CopyResourceCompatibilitySpec, AcceptsFormatsInSameTypeGroup) {
  auto source = context_.CreateTexture2D(
      4, 4, 1, DXGI_FORMAT_R8G8B8A8_TYPELESS, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_SOURCE);
  auto destination = context_.CreateTexture2D(
      4, 4, 1, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(source);
  ASSERT_TRUE(destination);
  context_.list()->CopyResource(destination.get(), source.get());
  EXPECT_EQ(context_.list()->Close(), S_OK);
}

TEST_F(CopyResourceCompatibilitySpec, RejectsDifferentMipCounts) {
  auto source = context_.CreateTexture2D(
      8, 8, 1, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_SOURCE);
  auto destination = context_.CreateTexture2D(
      8, 8, 2, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  ExpectRejected(destination.get(), source.get());
}

TEST_F(CopyResourceCompatibilitySpec, RejectsDifferentSampleCounts) {
  D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS support = {};
  support.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  support.SampleCount = 4;
  support.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
  ASSERT_EQ(context_.device()->CheckFeatureSupport(
                D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &support,
                sizeof(support)),
            S_OK);
  if (!support.NumQualityLevels)
    GTEST_SKIP() << "4x MSAA is unavailable";
  auto source = CreateTexture(4, D3D12_RESOURCE_STATE_COPY_SOURCE);
  auto destination = CreateTexture(1, D3D12_RESOURCE_STATE_COPY_DEST);
  ExpectRejected(destination.get(), source.get());
}

TEST_F(CopyResourceCompatibilitySpec, RejectsForeignSource) {
  auto foreign_device = CreateIsolatedD3D12Device();
  ASSERT_TRUE(foreign_device);
  D3D12TestContext foreign_context;
  ASSERT_EQ(foreign_context.Initialize(foreign_device.get()), S_OK);
  auto source = foreign_context.CreateBuffer(
      64, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_SOURCE);
  auto destination = context_.CreateBuffer(
      64, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  ExpectRejected(destination.get(), source.get());
}

TEST_F(CopyResourceCompatibilitySpec, RejectsForeignDestination) {
  auto foreign_device = CreateIsolatedD3D12Device();
  ASSERT_TRUE(foreign_device);
  D3D12TestContext foreign_context;
  ASSERT_EQ(foreign_context.Initialize(foreign_device.get()), S_OK);
  auto source = context_.CreateBuffer(
      64, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_SOURCE);
  auto destination = foreign_context.CreateBuffer(
      64, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  ExpectRejected(destination.get(), source.get());
}

TEST_F(CopyResourceCompatibilitySpec, ZeroLengthBufferCopyIsNoOp) {
  auto source = context_.CreateBuffer(64, D3D12_HEAP_TYPE_UPLOAD,
                                      D3D12_RESOURCE_FLAG_NONE,
                                      D3D12_RESOURCE_STATE_GENERIC_READ);
  auto destination = context_.CreateBuffer(64, D3D12_HEAP_TYPE_READBACK,
                                           D3D12_RESOURCE_FLAG_NONE,
                                           D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(source);
  ASSERT_TRUE(destination);
  context_.list()->CopyBufferRegion(destination.get(), 64, source.get(), 64,
                                    0);
  EXPECT_EQ(context_.list()->Close(), S_OK);
}

} // namespace
