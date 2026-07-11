#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"
#include "shaders/runtime_test_shaders.hpp"

#include <cstdint>
#include <cstring>
#include <array>
#include <algorithm>

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;
using dxmt::test::SparseTextureComputeShader;

enum class SparseCase {
  CreateOnly,
  WriteSrv,
  CopySrv,
  EmptySubmit,
  SampleUnmapped,
  SampleMapped,
  CopyToUnmapped,
  CopyToMapped,
};

class D3D12SparseResourceSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_TRUE(SUCCEEDED(context_.Initialize()));
  }

  ComPtr<ID3D12Heap> MapAllTiles(ID3D12Resource *resource) {
    D3D12_PACKED_MIP_INFO packed_mip_info = {};
    D3D12_TILE_SHAPE tile_shape = {};
    D3D12_SUBRESOURCE_TILING tiling = {};
    UINT subresource_count = 1;
    UINT total_tile_count = 0;
    context_.device()->GetResourceTiling(
        resource, &total_tile_count, &packed_mip_info, &tile_shape,
        &subresource_count, 0, &tiling);
    EXPECT_NE(total_tile_count, 0u);
    if (!total_tile_count)
      return {};

    D3D12_HEAP_DESC heap_desc = {};
    heap_desc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap_desc.SizeInBytes =
        static_cast<UINT64>(total_tile_count) *
        D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    heap_desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES;
    ComPtr<ID3D12Heap> heap;
    HRESULT hr = context_.device()->CreateHeap(
        &heap_desc, __uuidof(ID3D12Heap),
        reinterpret_cast<void **>(heap.put()));
    EXPECT_TRUE(SUCCEEDED(hr));
    if (FAILED(hr))
      return {};

    D3D12_TILED_RESOURCE_COORDINATE coordinate = {};
    D3D12_TILE_REGION_SIZE region_size = {};
    region_size.NumTiles = total_tile_count;
    D3D12_TILE_RANGE_FLAGS range_flag = D3D12_TILE_RANGE_FLAG_NONE;
    UINT heap_range_offset = 0;
    context_.queue()->UpdateTileMappings(
        resource, 1, &coordinate, &region_size, heap.get(), 1, &range_flag,
        &heap_range_offset, &total_tile_count, D3D12_TILE_MAPPING_FLAG_NONE);
    return heap;
  }

  void SampleTexture(ID3D12DescriptorHeap *descriptor_heap,
                     std::array<std::uint32_t, 28> *results) {
    constexpr std::uint32_t sentinel = 0xf17e5a3cu;
    D3D12_DESCRIPTOR_RANGE range = {};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 1;
    D3D12_ROOT_PARAMETER parameters[2] = {};
    parameters[0].ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[0].DescriptorTable.NumDescriptorRanges = 1;
    parameters[0].DescriptorTable.pDescriptorRanges = &range;
    parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    parameters[1].Descriptor.ShaderRegister = 0;
    parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_ROOT_SIGNATURE_DESC root_desc = {};
    root_desc.NumParameters = 2;
    root_desc.pParameters = parameters;

    ComPtr<ID3D12RootSignature> root_signature =
        context_.CreateRootSignature(root_desc);
    ASSERT_TRUE(root_signature);
    ComPtr<ID3D12PipelineState> pipeline = context_.CreateComputePipeline(
        root_signature.get(), SparseTextureComputeShader());
    ASSERT_TRUE(pipeline);
    std::array<std::uint32_t, 28> initial_values = {};
    initial_values.fill(sentinel);
    ComPtr<ID3D12Resource> initial = context_.CreateUploadBuffer(
        sizeof(initial_values), initial_values.data(), sizeof(initial_values));
    ComPtr<ID3D12Resource> output = context_.CreateBuffer(
        sizeof(initial_values), D3D12_HEAP_TYPE_DEFAULT,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_DEST);
    ASSERT_TRUE(initial);
    ASSERT_TRUE(output);
    context_.list()->CopyBufferRegion(output.get(), 0, initial.get(), 0,
                                      sizeof(initial_values));
    D3D12TestContext::Transition(
        context_.list(), output.get(), D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    ID3D12DescriptorHeap *heaps[] = {descriptor_heap};
    context_.list()->SetDescriptorHeaps(1, heaps);
    context_.list()->SetComputeRootSignature(root_signature.get());
    context_.list()->SetPipelineState(pipeline.get());
    context_.list()->SetComputeRootDescriptorTable(
        0, context_.GpuDescriptorHandle(descriptor_heap, 1));
    context_.list()->SetComputeRootUnorderedAccessView(
        1, output->GetGPUVirtualAddress());
    context_.list()->Dispatch(1, 1, 1);
    D3D12TestContext::Transition(
        context_.list(), output.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_SOURCE);
    std::vector<std::uint8_t> bytes;
    ASSERT_TRUE(SUCCEEDED(
        context_.ReadbackBuffer(output.get(), sizeof(*results), &bytes)));
    ASSERT_EQ(bytes.size(), sizeof(*results));
    std::memcpy(results->data(), bytes.data(), sizeof(*results));
    EXPECT_TRUE(std::none_of(
        results->begin(), results->end(), [sentinel](std::uint32_t value) {
          return value == sentinel;
        }));
  }

  void CopyToTexture(ID3D12Resource *texture) {
    const D3D12_RESOURCE_DESC texture_desc = texture->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
    UINT row_count = 0;
    UINT64 row_size = 0;
    UINT64 total_size = 0;
    context_.device()->GetCopyableFootprints(
        &texture_desc, 0, 1, 0, &footprint, &row_count, &row_size,
        &total_size);
    ComPtr<ID3D12Resource> upload = context_.CreateUploadBuffer(total_size);
    ASSERT_TRUE(upload);

    void *mapped = nullptr;
    D3D12_RANGE read_range = {0, 0};
    ASSERT_TRUE(SUCCEEDED(upload->Map(0, &read_range, &mapped)));
    std::memset(mapped, 0x5a, static_cast<std::size_t>(total_size));
    D3D12_RANGE written_range = {0, static_cast<SIZE_T>(total_size)};
    upload->Unmap(0, &written_range);

    D3D12_TEXTURE_COPY_LOCATION source = {};
    source.pResource = upload.get();
    source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    source.PlacedFootprint = footprint;
    D3D12_TEXTURE_COPY_LOCATION destination = {};
    destination.pResource = texture;
    destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    destination.SubresourceIndex = 0;
    context_.list()->CopyTextureRegion(&destination, 0, 0, 0, &source,
                                       nullptr);
    ASSERT_TRUE(SUCCEEDED(context_.ExecuteAndWait()));
  }

  void RunCase(SparseCase sparse_case);

  D3D12TestContext context_;
};

void D3D12SparseResourceSpec::RunCase(SparseCase sparse_case) {
  D3D12_FEATURE_DATA_D3D12_OPTIONS options = {};
  HRESULT hr = context_.device()->CheckFeatureSupport(
      D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options));
  ASSERT_TRUE(SUCCEEDED(hr));
  if (options.TiledResourcesTier < D3D12_TILED_RESOURCES_TIER_1)
    GTEST_SKIP() << "Tiled resources are not supported";

  const bool copy_case = sparse_case == SparseCase::CopyToUnmapped ||
                         sparse_case == SparseCase::CopyToMapped;
  const bool sample_case = sparse_case == SparseCase::SampleUnmapped ||
                           sparse_case == SparseCase::SampleMapped;
  D3D12_RESOURCE_STATES initial_state = D3D12_RESOURCE_STATE_COMMON;
  if (copy_case)
    initial_state = D3D12_RESOURCE_STATE_COPY_DEST;
  else if (sample_case)
    initial_state = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

  D3D12_RESOURCE_DESC resource_desc = {};
  resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  resource_desc.Width = 512;
  resource_desc.Height = 512;
  resource_desc.DepthOrArraySize = 1;
  resource_desc.MipLevels = 10;
  resource_desc.Format = DXGI_FORMAT_R32_UINT;
  resource_desc.SampleDesc.Count = 1;
  resource_desc.Layout = D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE;
  resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  ComPtr<ID3D12Resource> texture;
  hr = context_.device()->CreateReservedResource(
      &resource_desc, initial_state, nullptr, __uuidof(ID3D12Resource),
      reinterpret_cast<void **>(texture.put()));
  ASSERT_TRUE(SUCCEEDED(hr));
  ASSERT_TRUE(texture);
  if (sparse_case == SparseCase::CreateOnly)
    return;

  ComPtr<ID3D12DescriptorHeap> descriptor_heap =
      context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                                    2, true);
  ASSERT_TRUE(descriptor_heap);
  D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
  srv_desc.Format = resource_desc.Format;
  srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv_desc.Texture2D.MipLevels = resource_desc.MipLevels;
  context_.device()->CreateShaderResourceView(
      texture.get(), &srv_desc,
      context_.CpuDescriptorHandle(descriptor_heap.get(), 0));
  if (sparse_case == SparseCase::WriteSrv)
    return;

  context_.device()->CopyDescriptorsSimple(
      1, context_.CpuDescriptorHandle(descriptor_heap.get(), 1),
      context_.CpuDescriptorHandle(descriptor_heap.get(), 0),
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  if (sparse_case == SparseCase::CopySrv)
    return;
  if (sparse_case == SparseCase::EmptySubmit) {
    ASSERT_TRUE(SUCCEEDED(context_.ExecuteAndWait()));
    return;
  }

  ComPtr<ID3D12Heap> backing_heap;
  if (sparse_case == SparseCase::SampleMapped ||
      sparse_case == SparseCase::CopyToMapped) {
    backing_heap = MapAllTiles(texture.get());
    ASSERT_TRUE(backing_heap);
  }

  if (sample_case) {
    std::array<std::uint32_t, 28> results = {};
    SampleTexture(descriptor_heap.get(), &results);
  } else
    CopyToTexture(texture.get());
}

TEST_F(D3D12SparseResourceSpec, CreatesReservedTexture) {
  RunCase(SparseCase::CreateOnly);
}

TEST_F(D3D12SparseResourceSpec, WritesReservedTextureSrv) {
  RunCase(SparseCase::WriteSrv);
}

TEST_F(D3D12SparseResourceSpec, CopiesReservedTextureSrv) {
  RunCase(SparseCase::CopySrv);
}

TEST_F(D3D12SparseResourceSpec, SubmitsCopiedReservedTextureSrv) {
  RunCase(SparseCase::EmptySubmit);
}

TEST_F(D3D12SparseResourceSpec, SamplesUnmappedReservedTexture) {
  RunCase(SparseCase::SampleUnmapped);
}

TEST_F(D3D12SparseResourceSpec, SamplesMappedReservedTexture) {
  RunCase(SparseCase::SampleMapped);
}

TEST_F(D3D12SparseResourceSpec, CopiesIntoUnmappedReservedTexture) {
  RunCase(SparseCase::CopyToUnmapped);
}

TEST_F(D3D12SparseResourceSpec, CopiesIntoMappedReservedTexture) {
  RunCase(SparseCase::CopyToMapped);
}

TEST_F(D3D12SparseResourceSpec, ReportsPackedMipTilesForEveryArraySlice) {
  D3D12_FEATURE_DATA_D3D12_OPTIONS options = {};
  ASSERT_TRUE(SUCCEEDED(context_.device()->CheckFeatureSupport(
      D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options))));
  if (options.TiledResourcesTier < D3D12_TILED_RESOURCES_TIER_1)
    GTEST_SKIP() << "Tiled resources are not supported";

  constexpr UINT16 array_size = 3;
  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width = 512;
  desc.Height = 512;
  desc.DepthOrArraySize = array_size;
  desc.MipLevels = 10;
  desc.Format = DXGI_FORMAT_R32_UINT;
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE;
  desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  ComPtr<ID3D12Resource> texture;
  ASSERT_TRUE(SUCCEEDED(context_.device()->CreateReservedResource(
      &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, __uuidof(ID3D12Resource),
      reinterpret_cast<void **>(texture.put()))));
  ASSERT_TRUE(texture);

  D3D12_PACKED_MIP_INFO packed = {};
  D3D12_TILE_SHAPE shape = {};
  UINT total_tiles = 0;
  context_.device()->GetResourceTiling(texture.get(), &total_tiles, &packed,
                                       &shape, nullptr, 0, nullptr);
  ASSERT_GT(packed.NumPackedMips, 0u);
  EXPECT_EQ(packed.NumTilesForPackedMips, array_size);
  EXPECT_GE(total_tiles, packed.NumTilesForPackedMips);
}

} // namespace
