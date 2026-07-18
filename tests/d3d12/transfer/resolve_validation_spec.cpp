#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

#include <string>

namespace {

using dxmt::test::ComPtr;
using dxmt::test::CreateIsolatedD3D12Device;
using dxmt::test::D3D12TestContext;

ComPtr<ID3D12Resource> CreateResolveTexture(ID3D12Device *device, UINT64 width,
                                            UINT height, UINT samples,
                                            DXGI_FORMAT format,
                                            D3D12_RESOURCE_STATES state) {
  D3D12_HEAP_PROPERTIES heap = {};
  heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width = width;
  desc.Height = height;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.Format = format;
  desc.SampleDesc.Count = samples;
  desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  ComPtr<ID3D12Resource> resource;
  const HRESULT hr = device->CreateCommittedResource(
      &heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr,
      IID_PPV_ARGS(resource.put()));
  return SUCCEEDED(hr) ? std::move(resource) : ComPtr<ID3D12Resource>{};
}

enum class InvalidResolveCase {
  NullDestination,
  NullSource,
  BufferDestination,
  BufferSource,
  SingleSampleSource,
  MultisampleDestination,
  SourceSubresourceOutOfRange,
  DestinationSubresourceOutOfRange,
  UnknownFormat,
  IncompatibleFormat,
  FullExtentMismatch,
  NegativeSourceRect,
  EmptySourceRect,
  SourceRectOutOfRange,
  DestinationXOutOfRange,
  DestinationYOutOfRange,
  ForeignSource,
  ForeignDestination,
  ForeignRegionSource,
  ForeignRegionDestination,
};

class ResolveValidationSpec
    : public ::testing::TestWithParam<InvalidResolveCase> {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  D3D12TestContext context_;
};

TEST_P(ResolveValidationSpec, InvalidResolveFailsCommandListClose) {
  ComPtr<ID3D12GraphicsCommandList1> list1;
  ASSERT_EQ(context_.list()->QueryInterface(IID_PPV_ARGS(list1.put())), S_OK);
  auto source = CreateResolveTexture(
      context_.device(), 8, 8, 4, DXGI_FORMAT_R8G8B8A8_UNORM,
      D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
  auto destination = CreateResolveTexture(
      context_.device(), 8, 8, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
      D3D12_RESOURCE_STATE_RESOLVE_DEST);
  auto buffer = context_.CreateBuffer(256, D3D12_HEAP_TYPE_DEFAULT,
                                      D3D12_RESOURCE_FLAG_NONE,
                                      D3D12_RESOURCE_STATE_COMMON);
  ASSERT_TRUE(source);
  ASSERT_TRUE(destination);
  ASSERT_TRUE(buffer);

  D3D12_RECT rect = {0, 0, 4, 4};
  switch (GetParam()) {
  case InvalidResolveCase::NullDestination:
    context_.list()->ResolveSubresource(nullptr, 0, source.get(), 0,
                                        DXGI_FORMAT_R8G8B8A8_UNORM);
    break;
  case InvalidResolveCase::NullSource:
    context_.list()->ResolveSubresource(destination.get(), 0, nullptr, 0,
                                        DXGI_FORMAT_R8G8B8A8_UNORM);
    break;
  case InvalidResolveCase::BufferDestination:
    context_.list()->ResolveSubresource(buffer.get(), 0, source.get(), 0,
                                        DXGI_FORMAT_R8G8B8A8_UNORM);
    break;
  case InvalidResolveCase::BufferSource:
    context_.list()->ResolveSubresource(destination.get(), 0, buffer.get(), 0,
                                        DXGI_FORMAT_R8G8B8A8_UNORM);
    break;
  case InvalidResolveCase::SingleSampleSource: {
    auto single = CreateResolveTexture(
        context_.device(), 8, 8, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
        D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
    ASSERT_TRUE(single);
    context_.list()->ResolveSubresource(destination.get(), 0, single.get(), 0,
                                        DXGI_FORMAT_R8G8B8A8_UNORM);
    break;
  }
  case InvalidResolveCase::MultisampleDestination: {
    auto multisample_destination = CreateResolveTexture(
        context_.device(), 8, 8, 4, DXGI_FORMAT_R8G8B8A8_UNORM,
        D3D12_RESOURCE_STATE_RESOLVE_DEST);
    ASSERT_TRUE(multisample_destination);
    context_.list()->ResolveSubresource(multisample_destination.get(), 0,
                                        source.get(), 0,
                                        DXGI_FORMAT_R8G8B8A8_UNORM);
    break;
  }
  case InvalidResolveCase::SourceSubresourceOutOfRange:
    context_.list()->ResolveSubresource(destination.get(), 0, source.get(), 1,
                                        DXGI_FORMAT_R8G8B8A8_UNORM);
    break;
  case InvalidResolveCase::DestinationSubresourceOutOfRange:
    context_.list()->ResolveSubresource(destination.get(), 1, source.get(), 0,
                                        DXGI_FORMAT_R8G8B8A8_UNORM);
    break;
  case InvalidResolveCase::UnknownFormat:
    context_.list()->ResolveSubresource(destination.get(), 0, source.get(), 0,
                                        DXGI_FORMAT_UNKNOWN);
    break;
  case InvalidResolveCase::IncompatibleFormat:
    context_.list()->ResolveSubresource(destination.get(), 0, source.get(), 0,
                                        DXGI_FORMAT_R32_FLOAT);
    break;
  case InvalidResolveCase::FullExtentMismatch: {
    auto narrow = CreateResolveTexture(
        context_.device(), 7, 8, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
        D3D12_RESOURCE_STATE_RESOLVE_DEST);
    ASSERT_TRUE(narrow);
    context_.list()->ResolveSubresource(narrow.get(), 0, source.get(), 0,
                                        DXGI_FORMAT_R8G8B8A8_UNORM);
    break;
  }
  case InvalidResolveCase::NegativeSourceRect:
    rect = {-1, 0, 4, 4};
    list1->ResolveSubresourceRegion(
        destination.get(), 0, 0, 0, source.get(), 0, &rect,
        DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOLVE_MODE_AVERAGE);
    break;
  case InvalidResolveCase::EmptySourceRect:
    rect = {2, 2, 2, 4};
    list1->ResolveSubresourceRegion(
        destination.get(), 0, 0, 0, source.get(), 0, &rect,
        DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOLVE_MODE_AVERAGE);
    break;
  case InvalidResolveCase::SourceRectOutOfRange:
    rect = {0, 0, 9, 8};
    list1->ResolveSubresourceRegion(
        destination.get(), 0, 0, 0, source.get(), 0, &rect,
        DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOLVE_MODE_AVERAGE);
    break;
  case InvalidResolveCase::DestinationXOutOfRange:
    list1->ResolveSubresourceRegion(
        destination.get(), 0, 5, 0, source.get(), 0, &rect,
        DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOLVE_MODE_AVERAGE);
    break;
  case InvalidResolveCase::DestinationYOutOfRange:
    list1->ResolveSubresourceRegion(
        destination.get(), 0, 0, 5, source.get(), 0, &rect,
        DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOLVE_MODE_AVERAGE);
    break;
  case InvalidResolveCase::ForeignSource: {
    auto foreign_device = CreateIsolatedD3D12Device();
    ASSERT_TRUE(foreign_device);
    auto foreign_source = CreateResolveTexture(
        foreign_device.get(), 8, 8, 4, DXGI_FORMAT_R8G8B8A8_UNORM,
        D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
    ASSERT_TRUE(foreign_source);
    context_.list()->ResolveSubresource(destination.get(), 0,
                                        foreign_source.get(), 0,
                                        DXGI_FORMAT_R8G8B8A8_UNORM);
    break;
  }
  case InvalidResolveCase::ForeignDestination: {
    auto foreign_device = CreateIsolatedD3D12Device();
    ASSERT_TRUE(foreign_device);
    auto foreign_destination = CreateResolveTexture(
        foreign_device.get(), 8, 8, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
        D3D12_RESOURCE_STATE_RESOLVE_DEST);
    ASSERT_TRUE(foreign_destination);
    context_.list()->ResolveSubresource(
        foreign_destination.get(), 0, source.get(), 0,
        DXGI_FORMAT_R8G8B8A8_UNORM);
    break;
  }
  case InvalidResolveCase::ForeignRegionSource: {
    auto foreign_device = CreateIsolatedD3D12Device();
    ASSERT_TRUE(foreign_device);
    auto foreign_source = CreateResolveTexture(
        foreign_device.get(), 8, 8, 4, DXGI_FORMAT_R8G8B8A8_UNORM,
        D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
    ASSERT_TRUE(foreign_source);
    list1->ResolveSubresourceRegion(
        destination.get(), 0, 0, 0, foreign_source.get(), 0, &rect,
        DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOLVE_MODE_AVERAGE);
    break;
  }
  case InvalidResolveCase::ForeignRegionDestination: {
    auto foreign_device = CreateIsolatedD3D12Device();
    ASSERT_TRUE(foreign_device);
    auto foreign_destination = CreateResolveTexture(
        foreign_device.get(), 8, 8, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
        D3D12_RESOURCE_STATE_RESOLVE_DEST);
    ASSERT_TRUE(foreign_destination);
    list1->ResolveSubresourceRegion(
        foreign_destination.get(), 0, 0, 0, source.get(), 0, &rect,
        DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOLVE_MODE_AVERAGE);
    break;
  }
  }

  EXPECT_EQ(context_.list()->Close(), E_INVALIDARG);

  ComPtr<ID3D12CommandAllocator> allocator;
  ComPtr<ID3D12GraphicsCommandList> recovery_list;
  ASSERT_EQ(context_.device()->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(allocator.put())),
            S_OK);
  ASSERT_EQ(context_.device()->CreateCommandList(
                0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.get(), nullptr,
                IID_PPV_ARGS(recovery_list.put())),
            S_OK);
  recovery_list->ResolveSubresource(destination.get(), 0, source.get(), 0,
                                    DXGI_FORMAT_R8G8B8A8_UNORM);
  EXPECT_EQ(recovery_list->Close(), S_OK);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

std::string InvalidResolveCaseName(
    const ::testing::TestParamInfo<InvalidResolveCase> &info) {
  switch (info.param) {
  case InvalidResolveCase::NullDestination:
    return "NullDestination";
  case InvalidResolveCase::NullSource:
    return "NullSource";
  case InvalidResolveCase::BufferDestination:
    return "BufferDestination";
  case InvalidResolveCase::BufferSource:
    return "BufferSource";
  case InvalidResolveCase::SingleSampleSource:
    return "SingleSampleSource";
  case InvalidResolveCase::MultisampleDestination:
    return "MultisampleDestination";
  case InvalidResolveCase::SourceSubresourceOutOfRange:
    return "SourceSubresourceOutOfRange";
  case InvalidResolveCase::DestinationSubresourceOutOfRange:
    return "DestinationSubresourceOutOfRange";
  case InvalidResolveCase::UnknownFormat:
    return "UnknownFormat";
  case InvalidResolveCase::IncompatibleFormat:
    return "IncompatibleFormat";
  case InvalidResolveCase::FullExtentMismatch:
    return "FullExtentMismatch";
  case InvalidResolveCase::NegativeSourceRect:
    return "NegativeSourceRect";
  case InvalidResolveCase::EmptySourceRect:
    return "EmptySourceRect";
  case InvalidResolveCase::SourceRectOutOfRange:
    return "SourceRectOutOfRange";
  case InvalidResolveCase::DestinationXOutOfRange:
    return "DestinationXOutOfRange";
  case InvalidResolveCase::DestinationYOutOfRange:
    return "DestinationYOutOfRange";
  case InvalidResolveCase::ForeignSource:
    return "ForeignSource";
  case InvalidResolveCase::ForeignDestination:
    return "ForeignDestination";
  case InvalidResolveCase::ForeignRegionSource:
    return "ForeignRegionSource";
  case InvalidResolveCase::ForeignRegionDestination:
    return "ForeignRegionDestination";
  }
  return "Unknown";
}

INSTANTIATE_TEST_SUITE_P(
    InvalidMatrix, ResolveValidationSpec,
    ::testing::Values(
        InvalidResolveCase::NullDestination, InvalidResolveCase::NullSource,
        InvalidResolveCase::BufferDestination, InvalidResolveCase::BufferSource,
        InvalidResolveCase::SingleSampleSource,
        InvalidResolveCase::MultisampleDestination,
        InvalidResolveCase::SourceSubresourceOutOfRange,
        InvalidResolveCase::DestinationSubresourceOutOfRange,
        InvalidResolveCase::UnknownFormat,
        InvalidResolveCase::IncompatibleFormat,
        InvalidResolveCase::FullExtentMismatch,
        InvalidResolveCase::NegativeSourceRect,
        InvalidResolveCase::EmptySourceRect,
        InvalidResolveCase::SourceRectOutOfRange,
        InvalidResolveCase::DestinationXOutOfRange,
        InvalidResolveCase::DestinationYOutOfRange,
        InvalidResolveCase::ForeignSource,
        InvalidResolveCase::ForeignDestination,
        InvalidResolveCase::ForeignRegionSource,
        InvalidResolveCase::ForeignRegionDestination),
    InvalidResolveCaseName);

} // namespace
