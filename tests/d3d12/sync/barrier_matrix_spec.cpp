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
using dxmt::test::CreateIsolatedD3D12Device;
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

struct InvalidBarrierCase {
  D3D12_RESOURCE_BARRIER barrier;
  const char *name;
};

class InvalidLegacyBarrierSpec
    : public ::testing::Test,
      public ::testing::WithParamInterface<InvalidBarrierCase> {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }
  D3D12TestContext context_;
};

TEST_P(InvalidLegacyBarrierSpec, InvalidBarrierFailsClose) {
  auto resource = context_.CreateBuffer(
      4096, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(resource);
  auto barrier = GetParam().barrier;
  if (barrier.Type == D3D12_RESOURCE_BARRIER_TYPE_TRANSITION &&
      !barrier.Transition.pResource)
    barrier.Transition.pResource = resource.get();
  context_.list()->ResourceBarrier(1, &barrier);
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
  D3D12_RESOURCE_BARRIER recovery = {};
  recovery.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  recovery.Transition.pResource = resource.get();
  recovery.Transition.Subresource =
      D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  recovery.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
  recovery.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
  recovery_list->ResourceBarrier(1, &recovery);
  EXPECT_EQ(recovery_list->Close(), S_OK);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

std::vector<InvalidBarrierCase> BuildInvalidBarrierCases() {
  std::vector<InvalidBarrierCase> cases;
  D3D12_RESOURCE_BARRIER transition = {};
  transition.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  transition.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  transition.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
  transition.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;

  auto both_split = transition;
  both_split.Flags = static_cast<D3D12_RESOURCE_BARRIER_FLAGS>(
      D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY |
      D3D12_RESOURCE_BARRIER_FLAG_END_ONLY);
  cases.push_back({both_split, "BothSplitFlags"});
  auto unknown_flag = transition;
  unknown_flag.Flags = static_cast<D3D12_RESOURCE_BARRIER_FLAGS>(0x80000000u);
  cases.push_back({unknown_flag, "UnknownFlag"});
  auto conflicting_write = transition;
  conflicting_write.Transition.StateAfter = static_cast<D3D12_RESOURCE_STATES>(
      D3D12_RESOURCE_STATE_COPY_DEST |
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  cases.push_back({conflicting_write, "ConflictingWriteStates"});
  auto unknown_type = transition;
  unknown_type.Type = static_cast<D3D12_RESOURCE_BARRIER_TYPE>(0x7fffffff);
  cases.push_back({unknown_type, "UnknownType"});
  D3D12_RESOURCE_BARRIER uav = {};
  uav.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
  uav.Flags = D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY;
  cases.push_back({uav, "SplitFlagOnUav"});
  D3D12_RESOURCE_BARRIER aliasing = {};
  aliasing.Type = D3D12_RESOURCE_BARRIER_TYPE_ALIASING;
  aliasing.Flags = D3D12_RESOURCE_BARRIER_FLAG_END_ONLY;
  cases.push_back({aliasing, "SplitFlagOnAliasing"});
  return cases;
}

INSTANTIATE_TEST_SUITE_P(
    InvalidMatrix, InvalidLegacyBarrierSpec,
    ::testing::ValuesIn(BuildInvalidBarrierCases()),
    [](const ::testing::TestParamInfo<InvalidBarrierCase> &info) {
      return std::string(info.param.name);
    });

enum class ForeignBarrierResource {
  Transition,
  Uav,
  AliasingBefore,
  AliasingAfter,
};

class ForeignLegacyBarrierSpec
    : public ::testing::TestWithParam<ForeignBarrierResource> {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }
  D3D12TestContext context_;
};

TEST_P(ForeignLegacyBarrierSpec,
       RejectsWholeBatchAndAllowsFreshListRecovery) {
  auto local = context_.CreateBuffer(
      4096, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  auto foreign_device = CreateIsolatedD3D12Device();
  ASSERT_TRUE(local);
  ASSERT_TRUE(foreign_device);
  D3D12TestContext foreign_context;
  ASSERT_EQ(foreign_context.Initialize(foreign_device.get()), S_OK);
  auto foreign = foreign_context.CreateBuffer(
      4096, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(foreign);

  std::array<D3D12_RESOURCE_BARRIER, 2> barriers = {};
  barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
  switch (GetParam()) {
  case ForeignBarrierResource::Transition:
    barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[1].Transition.pResource = foreign.get();
    barriers[1].Transition.Subresource =
        D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    break;
  case ForeignBarrierResource::Uav:
    barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barriers[1].UAV.pResource = foreign.get();
    break;
  case ForeignBarrierResource::AliasingBefore:
    barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_ALIASING;
    barriers[1].Aliasing.pResourceBefore = foreign.get();
    barriers[1].Aliasing.pResourceAfter = local.get();
    break;
  case ForeignBarrierResource::AliasingAfter:
    barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_ALIASING;
    barriers[1].Aliasing.pResourceBefore = local.get();
    barriers[1].Aliasing.pResourceAfter = foreign.get();
    break;
  }
  context_.list()->ResourceBarrier(static_cast<UINT>(barriers.size()),
                                   barriers.data());
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
  D3D12_RESOURCE_BARRIER local_transition = {};
  local_transition.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  local_transition.Transition.pResource = local.get();
  local_transition.Transition.Subresource =
      D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  local_transition.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
  local_transition.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
  recovery_list->ResourceBarrier(1, &local_transition);
  EXPECT_EQ(recovery_list->Close(), S_OK);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

std::string ForeignBarrierResourceName(
    const ::testing::TestParamInfo<ForeignBarrierResource> &info) {
  switch (info.param) {
  case ForeignBarrierResource::Transition:
    return "Transition";
  case ForeignBarrierResource::Uav:
    return "Uav";
  case ForeignBarrierResource::AliasingBefore:
    return "AliasingBefore";
  case ForeignBarrierResource::AliasingAfter:
    return "AliasingAfter";
  }
  return "Unknown";
}

INSTANTIATE_TEST_SUITE_P(
    CrossDeviceMatrix, ForeignLegacyBarrierSpec,
    ::testing::Values(ForeignBarrierResource::Transition,
                      ForeignBarrierResource::Uav,
                      ForeignBarrierResource::AliasingBefore,
                      ForeignBarrierResource::AliasingAfter),
    ForeignBarrierResourceName);

} // namespace
