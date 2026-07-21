#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

D3D12_RESOURCE_DESC BufferDesc(UINT64 size = 4096) {
  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  desc.Width = size;
  desc.Height = 1;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  return desc;
}

D3D12_RESOURCE_DESC TextureDesc(D3D12_RESOURCE_DIMENSION dimension,
                                UINT64 width, UINT height, UINT16 depth,
                                UINT16 mips, DXGI_FORMAT format,
                                D3D12_RESOURCE_FLAGS flags =
                                    D3D12_RESOURCE_FLAG_NONE) {
  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = dimension;
  desc.Width = width;
  desc.Height = height;
  desc.DepthOrArraySize = depth;
  desc.MipLevels = mips;
  desc.Format = format;
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  desc.Flags = flags;
  return desc;
}

struct BarrierShapeCase {
  D3D12_RESOURCE_DESC desc;
  D3D12_RESOURCE_STATES before;
  D3D12_RESOURCE_STATES after;
  const char *name;
};

std::vector<BarrierShapeCase> BuildBarrierShapeCases() {
  auto texture1d = TextureDesc(D3D12_RESOURCE_DIMENSION_TEXTURE1D, 32, 1, 1,
                               1, DXGI_FORMAT_R8_UNORM);
  auto texture2d = TextureDesc(D3D12_RESOURCE_DIMENSION_TEXTURE2D, 16, 16, 1,
                               1, DXGI_FORMAT_R8G8B8A8_UNORM);
  auto texture3d = TextureDesc(D3D12_RESOURCE_DIMENSION_TEXTURE3D, 8, 8, 4,
                               1, DXGI_FORMAT_R8_UNORM);
  auto array = TextureDesc(D3D12_RESOURCE_DIMENSION_TEXTURE2D, 16, 16, 4, 1,
                           DXGI_FORMAT_R8_UNORM);
  auto mipmapped = TextureDesc(D3D12_RESOURCE_DIMENSION_TEXTURE2D, 32, 16, 1,
                               6, DXGI_FORMAT_R8G8B8A8_UNORM);
  auto render_target = TextureDesc(
      D3D12_RESOURCE_DIMENSION_TEXTURE2D, 16, 16, 1, 1,
      DXGI_FORMAT_R8G8B8A8_UNORM,
      D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
  auto depth = TextureDesc(D3D12_RESOURCE_DIMENSION_TEXTURE2D, 16, 16, 1, 1,
                           DXGI_FORMAT_D32_FLOAT,
                           D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
  auto simultaneous = texture2d;
  simultaneous.Flags = D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
  return {
      {BufferDesc(), D3D12_RESOURCE_STATE_COPY_DEST,
       D3D12_RESOURCE_STATE_COPY_SOURCE, "Buffer"},
      {texture1d, D3D12_RESOURCE_STATE_COPY_DEST,
       D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, "Texture1D"},
      {texture2d, D3D12_RESOURCE_STATE_COPY_DEST,
       D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, "Texture2D"},
      {texture3d, D3D12_RESOURCE_STATE_COPY_DEST,
       D3D12_RESOURCE_STATE_COPY_SOURCE, "Texture3D"},
      {array, D3D12_RESOURCE_STATE_COPY_DEST,
       D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, "Texture2DArray"},
      {mipmapped, D3D12_RESOURCE_STATE_COPY_DEST,
       D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, "MipmappedTexture"},
      {render_target, D3D12_RESOURCE_STATE_RENDER_TARGET,
       D3D12_RESOURCE_STATE_COPY_SOURCE, "RenderTarget"},
      {depth, D3D12_RESOURCE_STATE_DEPTH_WRITE,
       D3D12_RESOURCE_STATE_COPY_SOURCE, "DepthTexture"},
      {simultaneous, D3D12_RESOURCE_STATE_COMMON,
       D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
       "SimultaneousAccessTexture"},
  };
}

enum class InvalidBarrierKind {
  UnknownType,
  NullTransitionResource,
  ReadWriteStateBefore,
  ReadWriteStateAfter,
  BothSplitFlags,
  UnknownFlags,
  AliasingSplitFlag,
  UavSplitFlag,
};

struct InvalidBarrierCase {
  InvalidBarrierKind kind;
  const char *name;
};

class LegacyBarrierValidationSpec
    : public ::testing::Test,
      public ::testing::WithParamInterface<InvalidBarrierCase> {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }
  D3D12TestContext context_;
};

TEST_P(LegacyBarrierValidationSpec, InvalidBarrierFailsClose) {
  auto resource = context_.CreateBuffer(
      256, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(resource);

  D3D12_RESOURCE_BARRIER barrier = {};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource = resource.get();
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
  barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;

  switch (GetParam().kind) {
  case InvalidBarrierKind::UnknownType:
    barrier = {};
    barrier.Type = static_cast<D3D12_RESOURCE_BARRIER_TYPE>(3);
    break;
  case InvalidBarrierKind::NullTransitionResource:
    barrier.Transition.pResource = nullptr;
    break;
  case InvalidBarrierKind::ReadWriteStateBefore:
    barrier.Transition.StateBefore = static_cast<D3D12_RESOURCE_STATES>(
        D3D12_RESOURCE_STATE_COPY_DEST | D3D12_RESOURCE_STATE_COPY_SOURCE);
    break;
  case InvalidBarrierKind::ReadWriteStateAfter:
    barrier.Transition.StateAfter = static_cast<D3D12_RESOURCE_STATES>(
        D3D12_RESOURCE_STATE_COPY_DEST | D3D12_RESOURCE_STATE_COPY_SOURCE);
    break;
  case InvalidBarrierKind::BothSplitFlags:
    barrier.Flags = static_cast<D3D12_RESOURCE_BARRIER_FLAGS>(
        D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY |
        D3D12_RESOURCE_BARRIER_FLAG_END_ONLY);
    break;
  case InvalidBarrierKind::UnknownFlags:
    barrier.Flags = static_cast<D3D12_RESOURCE_BARRIER_FLAGS>(4);
    break;
  case InvalidBarrierKind::AliasingSplitFlag:
    barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_ALIASING;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY;
    break;
  case InvalidBarrierKind::UavSplitFlag:
    barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_END_ONLY;
    break;
  }

  context_.list()->ResourceBarrier(1, &barrier);

  EXPECT_EQ(context_.list()->Close(), E_INVALIDARG);
}

INSTANTIATE_TEST_SUITE_P(
    InvalidShapeMatrix, LegacyBarrierValidationSpec,
    ::testing::Values(
        InvalidBarrierCase{InvalidBarrierKind::UnknownType, "UnknownType"},
        InvalidBarrierCase{InvalidBarrierKind::NullTransitionResource,
                           "NullTransitionResource"},
        InvalidBarrierCase{InvalidBarrierKind::ReadWriteStateBefore,
                           "ReadWriteStateBefore"},
        InvalidBarrierCase{InvalidBarrierKind::ReadWriteStateAfter,
                           "ReadWriteStateAfter"},
        InvalidBarrierCase{InvalidBarrierKind::BothSplitFlags,
                           "BothSplitFlags"},
        InvalidBarrierCase{InvalidBarrierKind::UnknownFlags, "UnknownFlags"},
        InvalidBarrierCase{InvalidBarrierKind::AliasingSplitFlag,
                           "AliasingSplitFlag"},
        InvalidBarrierCase{InvalidBarrierKind::UavSplitFlag, "UavSplitFlag"}),
    [](const ::testing::TestParamInfo<InvalidBarrierCase> &info) {
      return std::string(info.param.name);
    });

class LegacyBarrierShapeSpec
    : public ::testing::Test,
      public ::testing::WithParamInterface<BarrierShapeCase> {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }
  D3D12TestContext context_;
};

TEST_P(LegacyBarrierShapeSpec, TransitionIsAcceptedForResourceShape) {
  D3D12_HEAP_PROPERTIES heap = {};
  heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  ComPtr<ID3D12Resource> resource;
  ASSERT_EQ(context_.device()->CreateCommittedResource(
                &heap, D3D12_HEAP_FLAG_NONE, &GetParam().desc,
                GetParam().before, nullptr, __uuidof(ID3D12Resource),
                reinterpret_cast<void **>(resource.put())),
            S_OK);
  ASSERT_TRUE(resource);
  D3D12_RESOURCE_BARRIER barrier = {};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource = resource.get();
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  barrier.Transition.StateBefore = GetParam().before;
  barrier.Transition.StateAfter = GetParam().after;
  context_.list()->ResourceBarrier(1, &barrier);
  EXPECT_EQ(context_.ExecuteAndWait(), S_OK);
}

INSTANTIATE_TEST_SUITE_P(
    ResourceShapeMatrix, LegacyBarrierShapeSpec,
    ::testing::ValuesIn(BuildBarrierShapeCases()),
    [](const ::testing::TestParamInfo<BarrierShapeCase> &info) {
      return std::string(info.param.name);
    });

struct ConsumerStateCase {
  D3D12_RESOURCE_STATES state;
  const char *name;
};

class LegacyBarrierConsumerSpec
    : public ::testing::Test,
      public ::testing::WithParamInterface<ConsumerStateCase> {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }
  D3D12TestContext context_;
};

TEST_P(LegacyBarrierConsumerSpec, CopyProducerTransitionsToReadConsumer) {
  auto resource = context_.CreateBuffer(
      4096, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(resource);
  D3D12TestContext::Transition(context_.list(), resource.get(),
                               D3D12_RESOURCE_STATE_COPY_DEST,
                               GetParam().state);
  EXPECT_EQ(context_.ExecuteAndWait(), S_OK);
}

INSTANTIATE_TEST_SUITE_P(
    ProducerConsumerMatrix, LegacyBarrierConsumerSpec,
    ::testing::Values(
        ConsumerStateCase{D3D12_RESOURCE_STATE_COPY_SOURCE, "CopySource"},
        ConsumerStateCase{D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
                          "VertexConstant"},
        ConsumerStateCase{D3D12_RESOURCE_STATE_INDEX_BUFFER, "Index"},
        ConsumerStateCase{D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, "Indirect"},
        ConsumerStateCase{D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                          "PixelShader"},
        ConsumerStateCase{D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                          "NonPixelShader"},
        ConsumerStateCase{static_cast<D3D12_RESOURCE_STATES>(
                              D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
                              D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
                          "AllShader"},
        ConsumerStateCase{D3D12_RESOURCE_STATE_GENERIC_READ, "GenericRead"}),
    [](const ::testing::TestParamInfo<ConsumerStateCase> &info) {
      return std::string(info.param.name);
    });

class LegacyBarrierMatrixSpec : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }
  D3D12TestContext context_;
};

TEST_F(LegacyBarrierMatrixSpec, IndependentTextureSubresourcesTransition) {
  auto desc = TextureDesc(D3D12_RESOURCE_DIMENSION_TEXTURE2D, 16, 16, 2, 3,
                          DXGI_FORMAT_R8G8B8A8_UNORM);
  D3D12_HEAP_PROPERTIES heap = {};
  heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  ComPtr<ID3D12Resource> texture;
  ASSERT_EQ(context_.device()->CreateCommittedResource(
                &heap, D3D12_HEAP_FLAG_NONE, &desc,
                D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                __uuidof(ID3D12Resource),
                reinterpret_cast<void **>(texture.put())),
            S_OK);
  constexpr UINT subresource_count = 6;
  std::array<D3D12_RESOURCE_BARRIER, subresource_count> barriers = {};
  for (UINT i = 0; i < barriers.size(); ++i) {
    barriers[i].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[i].Transition.pResource = texture.get();
    barriers[i].Transition.Subresource = i;
    barriers[i].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barriers[i].Transition.StateAfter =
        i % 2 ? D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
              : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
  }
  context_.list()->ResourceBarrier(static_cast<UINT>(barriers.size()),
                                   barriers.data());
  EXPECT_EQ(context_.ExecuteAndWait(), S_OK);
}

class RepeatedBarrierBoundarySpec
    : public ::testing::Test,
      public ::testing::WithParamInterface<UINT> {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }
  D3D12TestContext context_;
};

TEST_P(RepeatedBarrierBoundarySpec, RepeatedTransitionsPreserveBufferData) {
  constexpr std::array<std::uint32_t, 4> expected = {
      0x12345678u, 0x9abcdef0u, 0x55aa55aau, 0xaa55aa55u};
  auto upload = context_.CreateUploadBuffer(sizeof(expected), expected.data(),
                                             sizeof(expected));
  auto resource = context_.CreateBuffer(
      sizeof(expected), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  auto readback = context_.CreateBuffer(
      sizeof(expected), D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(upload);
  ASSERT_TRUE(resource);
  ASSERT_TRUE(readback);
  context_.list()->CopyBufferRegion(resource.get(), 0, upload.get(), 0,
                                    sizeof(expected));

  const UINT transition_count = GetParam() | 1u;
  std::vector<D3D12_RESOURCE_BARRIER> barriers(transition_count);
  D3D12_RESOURCE_STATES before = D3D12_RESOURCE_STATE_COPY_DEST;
  for (auto &barrier : barriers) {
    const auto after = before == D3D12_RESOURCE_STATE_COPY_DEST
                           ? D3D12_RESOURCE_STATE_COPY_SOURCE
                           : D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource.get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    before = after;
  }
  ASSERT_EQ(before, D3D12_RESOURCE_STATE_COPY_SOURCE);
  context_.list()->ResourceBarrier(static_cast<UINT>(barriers.size()),
                                   barriers.data());
  context_.list()->CopyBufferRegion(readback.get(), 0, resource.get(), 0,
                                    sizeof(expected));
  ASSERT_EQ(context_.ExecuteAndWait(), S_OK);
  void *mapping = nullptr;
  ASSERT_EQ(readback->Map(0, nullptr, &mapping), S_OK);
  ASSERT_NE(mapping, nullptr);
  EXPECT_EQ(std::memcmp(mapping, expected.data(), sizeof(expected)), 0);
  readback->Unmap(0, nullptr);
}

INSTANTIATE_TEST_SUITE_P(Boundaries, RepeatedBarrierBoundarySpec,
                         ::testing::Values(31u, 32u, 33u, 64u, 255u, 256u,
                                           257u));

TEST_F(LegacyBarrierMatrixSpec, MixedBarrierTypesInSingleBatchPreserveData) {
  constexpr std::uint32_t expected = 0xc001d00du;
  auto upload =
      context_.CreateUploadBuffer(sizeof(expected), &expected, sizeof(expected));
  auto resource = context_.CreateBuffer(
      sizeof(expected), D3D12_HEAP_TYPE_DEFAULT,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_COPY_DEST);
  auto unrelated = context_.CreateBuffer(
      sizeof(expected), D3D12_HEAP_TYPE_DEFAULT,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  auto readback = context_.CreateBuffer(
      sizeof(expected), D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(upload);
  ASSERT_TRUE(resource);
  ASSERT_TRUE(unrelated);
  ASSERT_TRUE(readback);
  context_.list()->CopyBufferRegion(resource.get(), 0, upload.get(), 0,
                                    sizeof(expected));
  std::array<D3D12_RESOURCE_BARRIER, 4> barriers = {};
  barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
  barriers[0].UAV.pResource = unrelated.get();
  barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_ALIASING;
  barriers[1].Aliasing.pResourceBefore = nullptr;
  barriers[1].Aliasing.pResourceAfter = nullptr;
  barriers[2].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barriers[2].Transition.pResource = resource.get();
  barriers[2].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  barriers[2].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
  barriers[2].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
  barriers[3].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
  barriers[3].UAV.pResource = nullptr;
  context_.list()->ResourceBarrier(static_cast<UINT>(barriers.size()),
                                   barriers.data());
  context_.list()->CopyBufferRegion(readback.get(), 0, resource.get(), 0,
                                    sizeof(expected));
  ASSERT_EQ(context_.ExecuteAndWait(), S_OK);
  void *mapping = nullptr;
  ASSERT_EQ(readback->Map(0, nullptr, &mapping), S_OK);
  ASSERT_NE(mapping, nullptr);
  EXPECT_EQ(*static_cast<std::uint32_t *>(mapping), expected);
  readback->Unmap(0, nullptr);
}

TEST_F(LegacyBarrierMatrixSpec, SplitBeginAndEndInSameListPreserveData) {
  constexpr std::uint32_t expected = 0x31415926u;
  auto upload =
      context_.CreateUploadBuffer(sizeof(expected), &expected, sizeof(expected));
  auto resource = context_.CreateBuffer(
      sizeof(expected), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  auto readback = context_.CreateBuffer(
      sizeof(expected), D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(upload);
  ASSERT_TRUE(resource);
  ASSERT_TRUE(readback);
  context_.list()->CopyBufferRegion(resource.get(), 0, upload.get(), 0,
                                    sizeof(expected));
  D3D12_RESOURCE_BARRIER barrier = {};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource = resource.get();
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
  barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
  barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY;
  context_.list()->ResourceBarrier(1, &barrier);
  barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_END_ONLY;
  context_.list()->ResourceBarrier(1, &barrier);
  context_.list()->CopyBufferRegion(readback.get(), 0, resource.get(), 0,
                                    sizeof(expected));
  ASSERT_EQ(context_.ExecuteAndWait(), S_OK);
  void *mapping = nullptr;
  ASSERT_EQ(readback->Map(0, nullptr, &mapping), S_OK);
  EXPECT_EQ(*static_cast<std::uint32_t *>(mapping), expected);
  readback->Unmap(0, nullptr);
}


} // namespace
