#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

using dxmt::test::ComPtr;
using dxmt::test::CompileShader;
using dxmt::test::D3D12TestContext;

constexpr UINT kWordCount = 16;
constexpr UINT kTextureWidth = 4;
constexpr UINT kTextureHeight = 4;
constexpr UINT kBaseLaneCount = 3;
constexpr UINT kReservedLane = 3;
constexpr UINT64 kLaneBytes = UINT64(kWordCount) * sizeof(UINT);

constexpr std::array<UINT, 4> kLaneBases = {
    0x11000000u, 0x22000000u, 0x33000000u, 0x44000000u};
constexpr std::array<UINT, 4> kLaneStrides = {
    0x00000101u, 0x00000203u, 0x00000307u, 0x00000409u};
constexpr std::array<UINT, 4> kLaneXors = {
    0xa5a50001u, 0x5a5a0002u, 0x3c3c0003u, 0xc3c30004u};

struct QueuePair {
  D3D12_COMMAND_LIST_TYPE producer;
  D3D12_COMMAND_LIST_TYPE consumer;
};

const char *QueueTypeName(D3D12_COMMAND_LIST_TYPE type) {
  switch (type) {
  case D3D12_COMMAND_LIST_TYPE_DIRECT:
    return "Direct";
  case D3D12_COMMAND_LIST_TYPE_COMPUTE:
    return "Compute";
  case D3D12_COMMAND_LIST_TYPE_COPY:
    return "Copy";
  default:
    return "Unsupported";
  }
}

bool IsShaderQueue(D3D12_COMMAND_LIST_TYPE type) {
  return type == D3D12_COMMAND_LIST_TYPE_DIRECT ||
         type == D3D12_COMMAND_LIST_TYPE_COMPUTE;
}

std::array<UINT, kWordCount> LaneValues(UINT lane) {
  std::array<UINT, kWordCount> values = {};
  for (UINT word = 0; word < kWordCount; ++word)
    values[word] = kLaneBases[lane] + word * kLaneStrides[lane];
  return values;
}

std::array<UINT, kWordCount> LanePoison(UINT lane) {
  auto values = LaneValues(lane);
  for (auto &value : values)
    value = ~value;
  return values;
}

std::array<UINT, kWordCount> ConsumerValues(UINT lane) {
  auto values = LaneValues(lane);
  for (auto &value : values)
    value ^= kLaneXors[lane];
  return values;
}

struct MatrixResources {
  ComPtr<ID3D12Resource> committed_buffer;
  ComPtr<ID3D12Resource> texture;
  ComPtr<ID3D12Heap> placed_heap;
  ComPtr<ID3D12Resource> placed_buffer;
  ComPtr<ID3D12Heap> reserved_backing;
  ComPtr<ID3D12Resource> reserved_buffer;
  ComPtr<ID3D12Resource> consumer_result;
};

class CrossQueueBatchMatrixSpec
    : public ::testing::TestWithParam<QueuePair> {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_EQ(context_.device()->CheckFeatureSupport(
                  D3D12_FEATURE_D3D12_OPTIONS, &options_, sizeof(options_)),
              S_OK);
  }

  ComPtr<ID3D12CommandQueue> CreateQueue(D3D12_COMMAND_LIST_TYPE type) {
    D3D12_COMMAND_QUEUE_DESC desc = {};
    desc.Type = type;
    ComPtr<ID3D12CommandQueue> queue;
    EXPECT_EQ(context_.device()->CreateCommandQueue(
                  &desc, IID_PPV_ARGS(queue.put())),
              S_OK);
    return queue;
  }

  ComPtr<ID3D12GraphicsCommandList>
  CreateList(D3D12_COMMAND_LIST_TYPE type,
             ComPtr<ID3D12CommandAllocator> *allocator) {
    EXPECT_EQ(context_.device()->CreateCommandAllocator(
                  type, IID_PPV_ARGS(allocator->put())),
              S_OK);
    ComPtr<ID3D12GraphicsCommandList> list;
    EXPECT_EQ(context_.device()->CreateCommandList(
                  0, type, allocator->get(), nullptr,
                  IID_PPV_ARGS(list.put())),
              S_OK);
    return list;
  }

  ComPtr<ID3D12Resource> CreatePlacedBuffer(UINT64 size) {
    D3D12_RESOURCE_DESC resource_desc = {};
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resource_desc.Width = size;
    resource_desc.Height = 1;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.MipLevels = 1;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    const auto allocation = context_.device()->GetResourceAllocationInfo(
        0, 1, &resource_desc);
    EXPECT_GT(allocation.Alignment, 0u);
    EXPECT_GT(allocation.SizeInBytes, 0u);
    if (!allocation.Alignment || !allocation.SizeInBytes)
      return {};

    D3D12_HEAP_DESC heap_desc = {};
    heap_desc.SizeInBytes = allocation.Alignment + allocation.SizeInBytes;
    heap_desc.Alignment = allocation.Alignment;
    heap_desc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap_desc.Properties.CreationNodeMask = 1;
    heap_desc.Properties.VisibleNodeMask = 1;
    heap_desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
    EXPECT_EQ(context_.device()->CreateHeap(
                  &heap_desc, IID_PPV_ARGS(resources_.placed_heap.put())),
              S_OK);
    if (!resources_.placed_heap)
      return {};

    ComPtr<ID3D12Resource> resource;
    EXPECT_EQ(context_.device()->CreatePlacedResource(
                  resources_.placed_heap.get(), allocation.Alignment,
                  &resource_desc, D3D12_RESOURCE_STATE_COMMON, nullptr,
                  IID_PPV_ARGS(resource.put())),
              S_OK);
    return resource;
  }

  void CreateReservedBufferIfSupported() {
    if (options_.TiledResourcesTier < D3D12_TILED_RESOURCES_TIER_1)
      return;

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    ASSERT_EQ(context_.device()->CreateReservedResource(
                  &desc, D3D12_RESOURCE_STATE_COMMON, nullptr,
                  IID_PPV_ARGS(resources_.reserved_buffer.put())),
              S_OK);
    ASSERT_TRUE(resources_.reserved_buffer);

    UINT tile_count = 0;
    D3D12_PACKED_MIP_INFO packed = {};
    D3D12_TILE_SHAPE shape = {};
    D3D12_SUBRESOURCE_TILING tiling = {};
    UINT subresource_count = 1;
    context_.device()->GetResourceTiling(
        resources_.reserved_buffer.get(), &tile_count, &packed, &shape,
        &subresource_count, 0, &tiling);
    ASSERT_GT(tile_count, 0u);
    ASSERT_EQ(subresource_count, 1u);

    D3D12_HEAP_DESC heap_desc = {};
    heap_desc.SizeInBytes =
        UINT64(tile_count) * D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES;
    heap_desc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap_desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
    ASSERT_EQ(context_.device()->CreateHeap(
                  &heap_desc,
                  IID_PPV_ARGS(resources_.reserved_backing.put())),
              S_OK);
    ASSERT_TRUE(resources_.reserved_backing);

    D3D12_TILED_RESOURCE_COORDINATE coordinate = {};
    D3D12_TILE_REGION_SIZE region = {};
    region.NumTiles = tile_count;
    const D3D12_TILE_RANGE_FLAGS range_flag = D3D12_TILE_RANGE_FLAG_NONE;
    const UINT heap_offset = 0;
    context_.queue()->UpdateTileMappings(
        resources_.reserved_buffer.get(), 1, &coordinate, &region,
        resources_.reserved_backing.get(), 1, &range_flag, &heap_offset,
        &tile_count, D3D12_TILE_MAPPING_FLAG_NONE);
  }

  ComPtr<ID3D12Resource>
  CreateTextureUpload(const std::array<UINT, kWordCount> &values,
                      const D3D12_PLACED_SUBRESOURCE_FOOTPRINT &footprint,
                      UINT64 total_size) {
    auto upload = context_.CreateUploadBuffer(total_size);
    if (!upload)
      return {};
    void *mapping = nullptr;
    const D3D12_RANGE no_read = {};
    EXPECT_EQ(upload->Map(0, &no_read, &mapping), S_OK);
    if (!mapping)
      return {};
    std::memset(mapping, 0xd7, static_cast<std::size_t>(total_size));
    for (UINT row = 0; row < kTextureHeight; ++row) {
      std::memcpy(static_cast<std::uint8_t *>(mapping) + footprint.Offset +
                      UINT64(row) * footprint.Footprint.RowPitch,
                  values.data() + row * kTextureWidth,
                  kTextureWidth * sizeof(UINT));
    }
    const D3D12_RANGE written = {0, static_cast<SIZE_T>(total_size)};
    upload->Unmap(0, &written);
    return upload;
  }

  void CreateRawBufferUav(ID3D12Resource *resource,
                          ID3D12DescriptorHeap *heap, UINT index) {
    D3D12_UNORDERED_ACCESS_VIEW_DESC desc = {};
    desc.Format = DXGI_FORMAT_R32_TYPELESS;
    desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    desc.Buffer.NumElements = kWordCount;
    desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
    context_.device()->CreateUnorderedAccessView(
        resource, nullptr, &desc,
        context_.CpuDescriptorHandle(heap, index));
  }

  void CreateRawBufferSrv(ID3D12Resource *resource,
                          ID3D12DescriptorHeap *heap, UINT index) {
    D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
    desc.Format = DXGI_FORMAT_R32_TYPELESS;
    desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    desc.Buffer.NumElements = kWordCount;
    desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
    context_.device()->CreateShaderResourceView(
        resource, &desc, context_.CpuDescriptorHandle(heap, index));
  }

  ComPtr<ID3D12RootSignature> CreateProducerRoot(UINT lane_count) {
    D3D12_DESCRIPTOR_RANGE range = {};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    range.NumDescriptors = lane_count;
    D3D12_ROOT_PARAMETER parameter = {};
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameter.DescriptorTable.NumDescriptorRanges = 1;
    parameter.DescriptorTable.pDescriptorRanges = &range;
    D3D12_ROOT_SIGNATURE_DESC desc = {};
    desc.NumParameters = 1;
    desc.pParameters = &parameter;
    return context_.CreateRootSignature(desc);
  }

  ComPtr<ID3D12RootSignature> CreateConsumerRoot(UINT lane_count) {
    D3D12_DESCRIPTOR_RANGE ranges[2] = {};
    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[0].NumDescriptors = lane_count;
    ranges[0].OffsetInDescriptorsFromTableStart = 0;
    ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    ranges[1].NumDescriptors = 1;
    ranges[1].OffsetInDescriptorsFromTableStart = lane_count;
    D3D12_ROOT_PARAMETER parameter = {};
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameter.DescriptorTable.NumDescriptorRanges = 2;
    parameter.DescriptorTable.pDescriptorRanges = ranges;
    D3D12_ROOT_SIGNATURE_DESC desc = {};
    desc.NumParameters = 1;
    desc.pParameters = &parameter;
    return context_.CreateRootSignature(desc);
  }

  std::string ProducerShaderSource(bool has_reserved) {
    std::string source = R"(
      RWByteAddressBuffer committed_output : register(u0);
      RWTexture2D<uint> texture_output : register(u1);
      RWByteAddressBuffer placed_output : register(u2);
    )";
    if (has_reserved)
      source += "RWByteAddressBuffer reserved_output : register(u3);\n";
    source += R"(
      [numthreads(16, 1, 1)]
      void main(uint3 id : SV_DispatchThreadID) {
        uint word = id.x;
        committed_output.Store(word * 4, 0x11000000u + word * 0x101u);
        texture_output[uint2(word & 3u, word >> 2u)] =
            0x22000000u + word * 0x203u;
        placed_output.Store(word * 4, 0x33000000u + word * 0x307u);
    )";
    if (has_reserved)
      source +=
          "reserved_output.Store(word * 4, "
          "0x44000000u + word * 0x409u);\n";
    source += "}\n";
    return source;
  }

  std::string ConsumerShaderSource(bool has_reserved) {
    std::string source = R"(
      ByteAddressBuffer committed_input : register(t0);
      Texture2D<uint> texture_input : register(t1);
      ByteAddressBuffer placed_input : register(t2);
    )";
    if (has_reserved)
      source += "ByteAddressBuffer reserved_input : register(t3);\n";
    source += R"(
      RWByteAddressBuffer result : register(u0);

      [numthreads(16, 1, 1)]
      void main(uint3 id : SV_DispatchThreadID) {
        uint word = id.x;
        result.Store((0u * 16u + word) * 4u,
                     committed_input.Load(word * 4u) ^ 0xa5a50001u);
        result.Store((1u * 16u + word) * 4u,
                     texture_input.Load(int3(word & 3u, word >> 2u, 0)) ^
                         0x5a5a0002u);
        result.Store((2u * 16u + word) * 4u,
                     placed_input.Load(word * 4u) ^ 0x3c3c0003u);
    )";
    if (has_reserved) {
      source += R"(
        result.Store((3u * 16u + word) * 4u,
                     reserved_input.Load(word * 4u) ^ 0xc3c30004u);
      )";
    }
    source += "}\n";
    return source;
  }

  ComPtr<ID3D12PipelineState>
  CreateComputePipeline(ID3D12RootSignature *root,
                        const std::string &source) {
    const auto shader = CompileShader(source, "cs_5_0");
    EXPECT_EQ(shader.result, S_OK) << shader.diagnostic_text();
    if (shader.result != S_OK)
      return {};
    const D3D12_SHADER_BYTECODE bytecode = {
        shader.bytecode->GetBufferPointer(), shader.bytecode->GetBufferSize()};
    return context_.CreateComputePipeline(root, bytecode);
  }

  void TransitionMatrix(ID3D12GraphicsCommandList *list,
                        D3D12_RESOURCE_STATES before,
                        D3D12_RESOURCE_STATES after, bool include_result) {
    D3D12TestContext::Transition(list, resources_.committed_buffer.get(),
                                 before, after);
    D3D12TestContext::Transition(list, resources_.texture.get(), before,
                                 after);
    D3D12TestContext::Transition(list, resources_.placed_buffer.get(), before,
                                 after);
    if (resources_.reserved_buffer) {
      D3D12TestContext::Transition(list, resources_.reserved_buffer.get(),
                                   before, after);
    }
    if (include_result) {
      D3D12TestContext::Transition(list, resources_.consumer_result.get(),
                                   before, after);
    }
  }

  void FillReadbackPoison(ID3D12Resource *readback, UINT64 size) {
    void *mapping = nullptr;
    const D3D12_RANGE no_read = {};
    ASSERT_EQ(readback->Map(0, &no_read, &mapping), S_OK);
    ASSERT_NE(mapping, nullptr);
    std::memset(mapping, 0xd7, static_cast<std::size_t>(size));
    const D3D12_RANGE written = {0, static_cast<SIZE_T>(size)};
    readback->Unmap(0, &written);
  }

  D3D12TestContext context_;
  D3D12_FEATURE_DATA_D3D12_OPTIONS options_ = {};
  MatrixResources resources_;
};

TEST_P(CrossQueueBatchMatrixSpec,
       BatchedResourcesSurviveSignalWaitAndExactReadback) {
  const auto pair = GetParam();
  const bool has_reserved =
      options_.TiledResourcesTier >= D3D12_TILED_RESOURCES_TIER_1;
  const UINT lane_count = kBaseLaneCount + (has_reserved ? 1u : 0u);

  resources_.committed_buffer = context_.CreateBuffer(
      kLaneBytes, D3D12_HEAP_TYPE_DEFAULT,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_COMMON);
  resources_.texture = context_.CreateTexture2D(
      kTextureWidth, kTextureHeight, 1, DXGI_FORMAT_R32_UINT,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_COMMON);
  resources_.placed_buffer = CreatePlacedBuffer(kLaneBytes);
  ASSERT_TRUE(resources_.committed_buffer);
  ASSERT_TRUE(resources_.texture);
  ASSERT_TRUE(resources_.placed_buffer);
  ASSERT_NO_FATAL_FAILURE(CreateReservedBufferIfSupported());
  ASSERT_EQ(static_cast<bool>(resources_.reserved_buffer), has_reserved);

  const UINT64 result_size = UINT64(lane_count) * kLaneBytes;
  resources_.consumer_result = context_.CreateBuffer(
      result_size, D3D12_HEAP_TYPE_DEFAULT,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_COMMON);
  ASSERT_TRUE(resources_.consumer_result);

  const auto texture_desc = resources_.texture->GetDesc();
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT upload_footprint = {};
  UINT texture_rows = 0;
  UINT64 texture_row_size = 0;
  UINT64 texture_upload_size = 0;
  context_.device()->GetCopyableFootprints(
      &texture_desc, 0, 1, 0, &upload_footprint, &texture_rows,
      &texture_row_size, &texture_upload_size);
  ASSERT_EQ(texture_rows, kTextureHeight);
  ASSERT_EQ(texture_row_size, UINT64(kTextureWidth) * sizeof(UINT));

  const auto committed_values = LaneValues(0);
  const auto texture_values = LaneValues(1);
  const auto placed_values = LaneValues(2);
  const auto reserved_values = LaneValues(kReservedLane);
  const auto committed_poison = LanePoison(0);
  const auto texture_poison = LanePoison(1);
  const auto placed_poison = LanePoison(2);
  const auto reserved_poison = LanePoison(kReservedLane);
  auto committed_upload = context_.CreateUploadBuffer(
      kLaneBytes, committed_values.data(), kLaneBytes);
  auto placed_upload = context_.CreateUploadBuffer(
      kLaneBytes, placed_values.data(), kLaneBytes);
  auto reserved_upload = context_.CreateUploadBuffer(
      kLaneBytes, reserved_values.data(), kLaneBytes);
  auto committed_poison_upload = context_.CreateUploadBuffer(
      kLaneBytes, committed_poison.data(), kLaneBytes);
  auto placed_poison_upload = context_.CreateUploadBuffer(
      kLaneBytes, placed_poison.data(), kLaneBytes);
  auto reserved_poison_upload = context_.CreateUploadBuffer(
      kLaneBytes, reserved_poison.data(), kLaneBytes);
  auto texture_upload = CreateTextureUpload(
      texture_values, upload_footprint, texture_upload_size);
  auto texture_poison_upload = CreateTextureUpload(
      texture_poison, upload_footprint, texture_upload_size);
  ASSERT_TRUE(committed_upload);
  ASSERT_TRUE(placed_upload);
  ASSERT_TRUE(committed_poison_upload);
  ASSERT_TRUE(placed_poison_upload);
  ASSERT_TRUE(texture_upload);
  ASSERT_TRUE(texture_poison_upload);
  if (has_reserved) {
    ASSERT_TRUE(reserved_upload);
    ASSERT_TRUE(reserved_poison_upload);
  }

  std::vector<UINT> result_poison(lane_count * kWordCount);
  for (UINT lane = 0; lane < lane_count; ++lane) {
    const auto expected = ConsumerValues(lane);
    for (UINT word = 0; word < kWordCount; ++word)
      result_poison[lane * kWordCount + word] = ~expected[word];
  }
  auto result_poison_upload = context_.CreateUploadBuffer(
      result_size, result_poison.data(), result_size);
  ASSERT_TRUE(result_poison_upload);

  // Establish exact poison in every producer destination and in the final UAV
  // result before either cross-queue submission is allowed to run.
  TransitionMatrix(context_.list(), D3D12_RESOURCE_STATE_COMMON,
                   D3D12_RESOURCE_STATE_COPY_DEST, true);
  context_.list()->CopyBufferRegion(resources_.committed_buffer.get(), 0,
                                    committed_poison_upload.get(), 0,
                                    kLaneBytes);
  context_.list()->CopyBufferRegion(resources_.placed_buffer.get(), 0,
                                    placed_poison_upload.get(), 0,
                                    kLaneBytes);
  if (has_reserved) {
    context_.list()->CopyBufferRegion(resources_.reserved_buffer.get(), 0,
                                      reserved_poison_upload.get(), 0,
                                      kLaneBytes);
  }
  D3D12_TEXTURE_COPY_LOCATION poison_source = {};
  poison_source.pResource = texture_poison_upload.get();
  poison_source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  poison_source.PlacedFootprint = upload_footprint;
  D3D12_TEXTURE_COPY_LOCATION texture_destination = {};
  texture_destination.pResource = resources_.texture.get();
  texture_destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  context_.list()->CopyTextureRegion(&texture_destination, 0, 0, 0,
                                     &poison_source, nullptr);
  context_.list()->CopyBufferRegion(resources_.consumer_result.get(), 0,
                                    result_poison_upload.get(), 0,
                                    result_size);
  TransitionMatrix(context_.list(), D3D12_RESOURCE_STATE_COPY_DEST,
                   D3D12_RESOURCE_STATE_COMMON, true);
  ASSERT_EQ(context_.ExecuteAndWait(), S_OK);
  ASSERT_EQ(context_.ResetCommandList(), S_OK);

  auto producer_heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, lane_count, true);
  auto consumer_heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, lane_count + 1, true);
  ASSERT_TRUE(producer_heap);
  ASSERT_TRUE(consumer_heap);
  CreateRawBufferUav(resources_.committed_buffer.get(), producer_heap.get(), 0);
  D3D12_UNORDERED_ACCESS_VIEW_DESC texture_uav = {};
  texture_uav.Format = DXGI_FORMAT_R32_UINT;
  texture_uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
  context_.device()->CreateUnorderedAccessView(
      resources_.texture.get(), nullptr, &texture_uav,
      context_.CpuDescriptorHandle(producer_heap.get(), 1));
  CreateRawBufferUav(resources_.placed_buffer.get(), producer_heap.get(), 2);
  if (has_reserved) {
    CreateRawBufferUav(resources_.reserved_buffer.get(), producer_heap.get(),
                       kReservedLane);
  }

  CreateRawBufferSrv(resources_.committed_buffer.get(), consumer_heap.get(), 0);
  D3D12_SHADER_RESOURCE_VIEW_DESC texture_srv = {};
  texture_srv.Format = DXGI_FORMAT_R32_UINT;
  texture_srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  texture_srv.Shader4ComponentMapping =
      D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  texture_srv.Texture2D.MipLevels = 1;
  context_.device()->CreateShaderResourceView(
      resources_.texture.get(), &texture_srv,
      context_.CpuDescriptorHandle(consumer_heap.get(), 1));
  CreateRawBufferSrv(resources_.placed_buffer.get(), consumer_heap.get(), 2);
  if (has_reserved) {
    CreateRawBufferSrv(resources_.reserved_buffer.get(), consumer_heap.get(),
                       kReservedLane);
  }
  D3D12_UNORDERED_ACCESS_VIEW_DESC result_uav = {};
  result_uav.Format = DXGI_FORMAT_R32_TYPELESS;
  result_uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  result_uav.Buffer.NumElements = lane_count * kWordCount;
  result_uav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
  context_.device()->CreateUnorderedAccessView(
      resources_.consumer_result.get(), nullptr, &result_uav,
      context_.CpuDescriptorHandle(consumer_heap.get(), lane_count));

  ComPtr<ID3D12RootSignature> producer_root;
  ComPtr<ID3D12RootSignature> consumer_root;
  ComPtr<ID3D12PipelineState> producer_pipeline;
  ComPtr<ID3D12PipelineState> consumer_pipeline;
  if (IsShaderQueue(pair.producer)) {
    producer_root = CreateProducerRoot(lane_count);
    ASSERT_TRUE(producer_root);
    producer_pipeline = CreateComputePipeline(
        producer_root.get(), ProducerShaderSource(has_reserved));
    ASSERT_TRUE(producer_pipeline);
  }
  if (IsShaderQueue(pair.consumer)) {
    consumer_root = CreateConsumerRoot(lane_count);
    ASSERT_TRUE(consumer_root);
    consumer_pipeline = CreateComputePipeline(
        consumer_root.get(), ConsumerShaderSource(has_reserved));
    ASSERT_TRUE(consumer_pipeline);
  }

  D3D12_PLACED_SUBRESOURCE_FOOTPRINT readback_footprint = {};
  UINT readback_rows = 0;
  UINT64 readback_row_size = 0;
  UINT64 copy_readback_size = 0;
  context_.device()->GetCopyableFootprints(
      &texture_desc, 0, 1, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT,
      &readback_footprint, &readback_rows, &readback_row_size,
      &copy_readback_size);
  ASSERT_EQ(readback_rows, kTextureHeight);
  ASSERT_EQ(readback_row_size, texture_row_size);
  const UINT64 texture_readback_end =
      readback_footprint.Offset +
      UINT64(readback_rows - 1) * readback_footprint.Footprint.RowPitch +
      readback_row_size;
  if (copy_readback_size < texture_readback_end)
    copy_readback_size = texture_readback_end;
  const UINT64 readback_size = IsShaderQueue(pair.consumer)
                                   ? result_size
                                   : copy_readback_size;
  auto readback = context_.CreateBuffer(
      readback_size, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(readback);
  ASSERT_NO_FATAL_FAILURE(FillReadbackPoison(readback.get(), readback_size));

  auto producer_queue = CreateQueue(pair.producer);
  auto consumer_queue = CreateQueue(pair.consumer);
  ASSERT_TRUE(producer_queue);
  ASSERT_TRUE(consumer_queue);
  ComPtr<ID3D12CommandAllocator> producer_allocator;
  ComPtr<ID3D12CommandAllocator> consumer_allocator;
  auto producer_list = CreateList(pair.producer, &producer_allocator);
  auto consumer_list = CreateList(pair.consumer, &consumer_allocator);
  ASSERT_TRUE(producer_list);
  ASSERT_TRUE(consumer_list);

  if (pair.producer == D3D12_COMMAND_LIST_TYPE_COPY) {
    TransitionMatrix(producer_list.get(), D3D12_RESOURCE_STATE_COMMON,
                     D3D12_RESOURCE_STATE_COPY_DEST, false);
    producer_list->CopyBufferRegion(resources_.committed_buffer.get(), 0,
                                    committed_upload.get(), 0, kLaneBytes);
    producer_list->CopyBufferRegion(resources_.placed_buffer.get(), 0,
                                    placed_upload.get(), 0, kLaneBytes);
    if (has_reserved) {
      producer_list->CopyBufferRegion(resources_.reserved_buffer.get(), 0,
                                      reserved_upload.get(), 0, kLaneBytes);
    }
    D3D12_TEXTURE_COPY_LOCATION expected_source = {};
    expected_source.pResource = texture_upload.get();
    expected_source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    expected_source.PlacedFootprint = upload_footprint;
    producer_list->CopyTextureRegion(&texture_destination, 0, 0, 0,
                                     &expected_source, nullptr);
    TransitionMatrix(producer_list.get(), D3D12_RESOURCE_STATE_COPY_DEST,
                     D3D12_RESOURCE_STATE_COMMON, false);
  } else {
    TransitionMatrix(producer_list.get(), D3D12_RESOURCE_STATE_COMMON,
                     D3D12_RESOURCE_STATE_UNORDERED_ACCESS, false);
    ID3D12DescriptorHeap *heaps[] = {producer_heap.get()};
    producer_list->SetDescriptorHeaps(1, heaps);
    producer_list->SetPipelineState(producer_pipeline.get());
    producer_list->SetComputeRootSignature(producer_root.get());
    producer_list->SetComputeRootDescriptorTable(
        0, producer_heap->GetGPUDescriptorHandleForHeapStart());
    producer_list->Dispatch(1, 1, 1);
    TransitionMatrix(producer_list.get(),
                     D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                     D3D12_RESOURCE_STATE_COMMON, false);
  }

  if (pair.consumer == D3D12_COMMAND_LIST_TYPE_COPY) {
    TransitionMatrix(consumer_list.get(), D3D12_RESOURCE_STATE_COMMON,
                     D3D12_RESOURCE_STATE_COPY_SOURCE, false);
    consumer_list->CopyBufferRegion(readback.get(), 0,
                                    resources_.committed_buffer.get(), 0,
                                    kLaneBytes);
    consumer_list->CopyBufferRegion(readback.get(), kLaneBytes,
                                    resources_.placed_buffer.get(), 0,
                                    kLaneBytes);
    if (has_reserved) {
      consumer_list->CopyBufferRegion(readback.get(), 2 * kLaneBytes,
                                      resources_.reserved_buffer.get(), 0,
                                      kLaneBytes);
    }
    D3D12_TEXTURE_COPY_LOCATION readback_destination = {};
    readback_destination.pResource = readback.get();
    readback_destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    readback_destination.PlacedFootprint = readback_footprint;
    D3D12_TEXTURE_COPY_LOCATION texture_source = {};
    texture_source.pResource = resources_.texture.get();
    texture_source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    consumer_list->CopyTextureRegion(&readback_destination, 0, 0, 0,
                                     &texture_source, nullptr);
  } else {
    TransitionMatrix(consumer_list.get(), D3D12_RESOURCE_STATE_COMMON,
                     D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, false);
    D3D12TestContext::Transition(
        consumer_list.get(), resources_.consumer_result.get(),
        D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    ID3D12DescriptorHeap *heaps[] = {consumer_heap.get()};
    consumer_list->SetDescriptorHeaps(1, heaps);
    consumer_list->SetPipelineState(consumer_pipeline.get());
    consumer_list->SetComputeRootSignature(consumer_root.get());
    consumer_list->SetComputeRootDescriptorTable(
        0, consumer_heap->GetGPUDescriptorHandleForHeapStart());
    consumer_list->Dispatch(1, 1, 1);
    D3D12TestContext::Transition(
        consumer_list.get(), resources_.consumer_result.get(),
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_SOURCE);
    consumer_list->CopyBufferRegion(readback.get(), 0,
                                    resources_.consumer_result.get(), 0,
                                    result_size);
  }

  ASSERT_EQ(producer_list->Close(), S_OK);
  ASSERT_EQ(consumer_list->Close(), S_OK);
  ComPtr<ID3D12Fence> fence;
  ASSERT_EQ(context_.device()->CreateFence(
                0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence.put())),
            S_OK);
  ID3D12CommandList *producer_submission = producer_list.get();
  ID3D12CommandList *consumer_submission = consumer_list.get();
  producer_queue->ExecuteCommandLists(1, &producer_submission);
  ASSERT_EQ(producer_queue->Signal(fence.get(), 1), S_OK);
  ASSERT_EQ(consumer_queue->Wait(fence.get(), 1), S_OK);
  consumer_queue->ExecuteCommandLists(1, &consumer_submission);
  ASSERT_EQ(consumer_queue->Signal(fence.get(), 2), S_OK);
  ASSERT_EQ(context_.WaitForFence(fence.get(), 2), S_OK);
  EXPECT_GE(fence->GetCompletedValue(), 2u);

  void *mapping = nullptr;
  const D3D12_RANGE read_range = {0, static_cast<SIZE_T>(readback_size)};
  ASSERT_EQ(readback->Map(0, &read_range, &mapping), S_OK);
  ASSERT_NE(mapping, nullptr);
  const auto *bytes = static_cast<const std::uint8_t *>(mapping);
  if (IsShaderQueue(pair.consumer)) {
    const auto *actual = reinterpret_cast<const UINT *>(bytes);
    for (UINT lane = 0; lane < lane_count; ++lane) {
      const auto expected = ConsumerValues(lane);
      for (UINT word = 0; word < kWordCount; ++word) {
        EXPECT_EQ(actual[lane * kWordCount + word], expected[word])
            << "lane=" << lane << ", word=" << word;
      }
    }
  } else {
    const auto *committed_actual = reinterpret_cast<const UINT *>(bytes);
    const auto *placed_actual =
        reinterpret_cast<const UINT *>(bytes + kLaneBytes);
    for (UINT word = 0; word < kWordCount; ++word) {
      EXPECT_EQ(committed_actual[word], committed_values[word])
          << "committed word=" << word;
      EXPECT_EQ(placed_actual[word], placed_values[word])
          << "placed word=" << word;
    }
    if (has_reserved) {
      const auto *reserved_actual =
          reinterpret_cast<const UINT *>(bytes + 2 * kLaneBytes);
      for (UINT word = 0; word < kWordCount; ++word) {
        EXPECT_EQ(reserved_actual[word], reserved_values[word])
            << "reserved word=" << word;
      }
    }
    for (UINT row = 0; row < kTextureHeight; ++row) {
      EXPECT_EQ(std::memcmp(
                    bytes + readback_footprint.Offset +
                        UINT64(row) * readback_footprint.Footprint.RowPitch,
                    texture_values.data() + row * kTextureWidth,
                    kTextureWidth * sizeof(UINT)),
                0)
          << "texture row=" << row;
    }
  }
  const D3D12_RANGE no_write = {};
  readback->Unmap(0, &no_write);

  RecordProperty("producer_queue", QueueTypeName(pair.producer));
  RecordProperty("consumer_queue", QueueTypeName(pair.consumer));
  RecordProperty("batched_resource_count", static_cast<int>(lane_count));
  RecordProperty("reserved_resource_executed", has_reserved ? 1 : 0);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

INSTANTIATE_TEST_SUITE_P(
    SixDirections, CrossQueueBatchMatrixSpec,
    ::testing::Values(
        QueuePair{D3D12_COMMAND_LIST_TYPE_COPY,
                  D3D12_COMMAND_LIST_TYPE_DIRECT},
        QueuePair{D3D12_COMMAND_LIST_TYPE_COPY,
                  D3D12_COMMAND_LIST_TYPE_COMPUTE},
        QueuePair{D3D12_COMMAND_LIST_TYPE_COMPUTE,
                  D3D12_COMMAND_LIST_TYPE_DIRECT},
        QueuePair{D3D12_COMMAND_LIST_TYPE_DIRECT,
                  D3D12_COMMAND_LIST_TYPE_COMPUTE},
        QueuePair{D3D12_COMMAND_LIST_TYPE_DIRECT,
                  D3D12_COMMAND_LIST_TYPE_COPY},
        QueuePair{D3D12_COMMAND_LIST_TYPE_COMPUTE,
                  D3D12_COMMAND_LIST_TYPE_COPY}),
    [](const ::testing::TestParamInfo<QueuePair> &info) {
      return std::string(QueueTypeName(info.param.producer)) + "To" +
             QueueTypeName(info.param.consumer);
    });

} // namespace
