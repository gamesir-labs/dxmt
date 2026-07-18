#include <dxmt_d3d12_test_path.hpp>
#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"
#include "shaders/runtime_test_shaders.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace {

using dxmt::d3d12::test::ExecutionPathConfig;
using dxmt::d3d12::test::ExecutionPathMode;
using dxmt::d3d12::test::ExecutionPathStats;
using dxmt::test::ColorsMatch;
using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;
using dxmt::test::FullscreenVertexShader;

constexpr std::array<ExecutionPathMode, 3> kPathModes = {
    ExecutionPathMode::NativeCompiled,
    ExecutionPathMode::Fallback,
    ExecutionPathMode::Auto,
};

const char *PathModeName(ExecutionPathMode mode) {
  switch (mode) {
  case ExecutionPathMode::NativeCompiled:
    return "NativeCompiled";
  case ExecutionPathMode::Fallback:
    return "Fallback";
  case ExecutionPathMode::Auto:
  default:
    return "Auto";
  }
}

enum class ComputeFamily {
  Copy,
  Clear,
  Uav,
  Barrier,
  Query,
  ExecuteIndirect,
  SparseAccess,
};

struct PathRunResult {
  HRESULT close_result = E_FAIL;
  HRESULT signal_result = E_FAIL;
  HRESULT wait_result = E_FAIL;
  HRESULT device_result = E_FAIL;
  UINT64 completed_fence = 0;
  ExecutionPathStats stats = {};
  std::array<std::uint32_t, 4> output = {};
  std::array<std::uint32_t, 8> auxiliary = {};
  UINT auxiliary_count = 0;
  std::array<UINT64, 2> query = {};
  std::vector<std::uint32_t> pixels;
};

class PathModeParitySpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_EQ(context_.device()->CheckFeatureSupport(
                  D3D12_FEATURE_D3D12_OPTIONS, &options_, sizeof(options_)),
              S_OK);

    const auto compute_shader = CompileShader(R"(
      RWBuffer<uint> output : register(u0);

      [numthreads(1, 1, 1)]
      void main() {
        uint previous;
        InterlockedAdd(output[0], 1u, previous);
        output[1] = previous;
      }
    )",
                                              "cs_5_0");
    ASSERT_EQ(compute_shader.result, S_OK) << compute_shader.diagnostic_text();

    D3D12_DESCRIPTOR_RANGE uav_range = {};
    uav_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    uav_range.NumDescriptors = 1;
    D3D12_ROOT_PARAMETER compute_parameter = {};
    compute_parameter.ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    compute_parameter.DescriptorTable.NumDescriptorRanges = 1;
    compute_parameter.DescriptorTable.pDescriptorRanges = &uav_range;
    D3D12_ROOT_SIGNATURE_DESC compute_root_desc = {};
    compute_root_desc.NumParameters = 1;
    compute_root_desc.pParameters = &compute_parameter;
    compute_root_ = context_.CreateRootSignature(compute_root_desc);
    ASSERT_TRUE(compute_root_);
    const D3D12_SHADER_BYTECODE compute_bytecode = {
        compute_shader.bytecode->GetBufferPointer(),
        compute_shader.bytecode->GetBufferSize()};
    compute_pipeline_ =
        context_.CreateComputePipeline(compute_root_.get(), compute_bytecode);
    ASSERT_TRUE(compute_pipeline_);

    const auto pixel_shader = CompileShader(R"(
      float4 main() : SV_Target {
        return float4(0.25, 0.25, 0.25, 0.25);
      }
    )",
                                            "ps_5_0");
    ASSERT_EQ(pixel_shader.result, S_OK) << pixel_shader.diagnostic_text();

    D3D12_ROOT_SIGNATURE_DESC graphics_root_desc = {};
    graphics_root_desc.Flags =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    graphics_root_ = context_.CreateRootSignature(graphics_root_desc);
    ASSERT_TRUE(graphics_root_);

    D3D12_GRAPHICS_PIPELINE_STATE_DESC graphics_desc = {};
    graphics_desc.pRootSignature = graphics_root_.get();
    graphics_desc.VS = FullscreenVertexShader();
    graphics_desc.PS = {pixel_shader.bytecode->GetBufferPointer(),
                        pixel_shader.bytecode->GetBufferSize()};
    auto &blend = graphics_desc.BlendState.RenderTarget[0];
    blend.BlendEnable = TRUE;
    blend.SrcBlend = D3D12_BLEND_ONE;
    blend.DestBlend = D3D12_BLEND_ONE;
    blend.BlendOp = D3D12_BLEND_OP_ADD;
    blend.SrcBlendAlpha = D3D12_BLEND_ONE;
    blend.DestBlendAlpha = D3D12_BLEND_ONE;
    blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    graphics_desc.SampleMask = UINT_MAX;
    graphics_desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    graphics_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    graphics_desc.PrimitiveTopologyType =
        D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    graphics_desc.NumRenderTargets = 1;
    graphics_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    graphics_desc.SampleDesc.Count = 1;
    ASSERT_EQ(context_.device()->CreateGraphicsPipelineState(
                  &graphics_desc, IID_PPV_ARGS(graphics_pipeline_.put())),
              S_OK);

    constexpr std::array<std::uint16_t, 3> kIndices = {0, 1, 2};
    index_buffer_ = context_.CreateUploadBuffer(
        sizeof(kIndices), kIndices.data(), sizeof(kIndices));
    ASSERT_TRUE(index_buffer_);

    constexpr D3D12_DISPATCH_ARGUMENTS kDispatchArguments = {1, 1, 1};
    indirect_arguments_ = context_.CreateUploadBuffer(
        sizeof(kDispatchArguments), &kDispatchArguments,
        sizeof(kDispatchArguments));
    ASSERT_TRUE(indirect_arguments_);
    D3D12_INDIRECT_ARGUMENT_DESC argument_desc = {};
    argument_desc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
    D3D12_COMMAND_SIGNATURE_DESC signature_desc = {};
    signature_desc.ByteStride = sizeof(kDispatchArguments);
    signature_desc.NumArgumentDescs = 1;
    signature_desc.pArgumentDescs = &argument_desc;
    ASSERT_EQ(
        context_.device()->CreateCommandSignature(
            &signature_desc, nullptr, IID_PPV_ARGS(dispatch_signature_.put())),
        S_OK);
  }

  void SetPathMode(ExecutionPathMode mode) {
    ExecutionPathConfig config = {};
    config.mode = mode;
    ASSERT_EQ(context_.list()->SetPrivateData(
                  dxmt::d3d12::test::kExecutionPathConfigGuid, sizeof(config),
                  &config),
              S_OK);

    ExecutionPathConfig observed = {};
    UINT size = sizeof(observed);
    ASSERT_EQ(
        context_.list()->GetPrivateData(
            dxmt::d3d12::test::kExecutionPathConfigGuid, &size, &observed),
        S_OK);
    ASSERT_EQ(size, sizeof(observed));
    ASSERT_EQ(observed.struct_size, sizeof(observed));
    ASSERT_EQ(observed.mode, mode);
    ASSERT_EQ(observed.flags, dxmt::d3d12::test::ExecutionPathFlagNone);
  }

  void ExecuteConfiguredList(PathRunResult *result) {
    ASSERT_NE(result, nullptr);
    ComPtr<ID3D12Fence> fence;
    ASSERT_EQ(context_.device()->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                             IID_PPV_ARGS(fence.put())),
              S_OK);

    result->close_result = context_.list()->Close();
    ASSERT_EQ(result->close_result, S_OK);
    ID3D12CommandList *submission = context_.list();
    context_.queue()->ExecuteCommandLists(1, &submission);

    constexpr UINT64 kCompletionValue = 0x706172697479ull;
    result->signal_result =
        context_.queue()->Signal(fence.get(), kCompletionValue);
    ASSERT_EQ(result->signal_result, S_OK);
    result->wait_result = context_.WaitForFence(fence.get(), kCompletionValue);
    ASSERT_EQ(result->wait_result, S_OK);
    result->completed_fence = fence->GetCompletedValue();
    EXPECT_GE(result->completed_fence, kCompletionValue);
    result->device_result = context_.device()->GetDeviceRemovedReason();
    EXPECT_EQ(result->device_result, S_OK);

    UINT stats_size = sizeof(result->stats);
    ASSERT_EQ(context_.list()->GetPrivateData(
                  dxmt::d3d12::test::kExecutionPathStatsGuid, &stats_size,
                  &result->stats),
              S_OK);
    ASSERT_EQ(stats_size, sizeof(result->stats));
    ASSERT_EQ(result->stats.struct_size, sizeof(result->stats));
  }

  ComPtr<ID3D12Resource> CreatePoisonedReadback(UINT64 size) {
    auto readback = context_.CreateBuffer(size, D3D12_HEAP_TYPE_READBACK,
                                          D3D12_RESOURCE_FLAG_NONE,
                                          D3D12_RESOURCE_STATE_COPY_DEST);
    EXPECT_TRUE(readback);
    if (!readback)
      return {};
    void *mapping = nullptr;
    const D3D12_RANGE no_read = {0, 0};
    EXPECT_EQ(readback->Map(0, &no_read, &mapping), S_OK);
    if (!mapping)
      return {};
    std::memset(mapping, 0xcd, static_cast<std::size_t>(size));
    const D3D12_RANGE written = {0, static_cast<SIZE_T>(size)};
    readback->Unmap(0, &written);
    return readback;
  }

  template <typename T, std::size_t N>
  void ReadArray(ID3D12Resource *readback, std::array<T, N> *values,
                 UINT count = N) {
    ASSERT_NE(readback, nullptr);
    ASSERT_NE(values, nullptr);
    ASSERT_LE(count, N);
    T *mapping = nullptr;
    const D3D12_RANGE read_range = {0, static_cast<SIZE_T>(count * sizeof(T))};
    ASSERT_EQ(
        readback->Map(0, &read_range, reinterpret_cast<void **>(&mapping)),
        S_OK);
    ASSERT_NE(mapping, nullptr);
    std::copy(mapping, mapping + count, values->begin());
    const D3D12_RANGE no_write = {0, 0};
    readback->Unmap(0, &no_write);
  }

  void BindCompute(ID3D12DescriptorHeap *heap) {
    ID3D12DescriptorHeap *heaps[] = {heap};
    context_.list()->SetDescriptorHeaps(1, heaps);
    context_.list()->SetComputeRootSignature(compute_root_.get());
    context_.list()->SetPipelineState(compute_pipeline_.get());
    context_.list()->SetComputeRootDescriptorTable(
        0, heap->GetGPUDescriptorHandleForHeapStart());
  }

  void EnsureRootBindingPipeline() {
    if (root_binding_pipeline_)
      return;

    const auto shader = CompileShader(R"(
      cbuffer InlineConstants : register(b0) {
        uint multiplier;
        uint bias;
      };
      cbuffer RootCbv : register(b1) {
        uint cbv_value;
      };
      ByteAddressBuffer input_buffer : register(t0);
      RWByteAddressBuffer output_buffer : register(u0);
      RWStructuredBuffer<uint> counter_buffer : register(u1);

      [numthreads(4, 1, 1)]
      void main(uint3 id : SV_DispatchThreadID) {
        uint input_value = input_buffer.Load(id.x * 4u);
        output_buffer.Store(id.x * 4u,
                            input_value * multiplier + bias + cbv_value);
        if (id.x == 0) {
          uint previous;
          InterlockedAdd(counter_buffer[0], 1u, previous);
        }
      }
    )",
                                      "cs_5_0");
    ASSERT_EQ(shader.result, S_OK) << shader.diagnostic_text();

    std::array<D3D12_ROOT_PARAMETER, 5> parameters = {};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[0].Constants.ShaderRegister = 0;
    parameters[0].Constants.Num32BitValues = 2;
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameters[1].Descriptor.ShaderRegister = 1;
    parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    parameters[2].Descriptor.ShaderRegister = 0;
    parameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    parameters[3].Descriptor.ShaderRegister = 0;
    parameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    parameters[4].Descriptor.ShaderRegister = 1;
    for (auto &parameter : parameters)
      parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC root_desc = {};
    root_desc.NumParameters = parameters.size();
    root_desc.pParameters = parameters.data();
    root_binding_root_ = context_.CreateRootSignature(root_desc);
    ASSERT_TRUE(root_binding_root_);
    const D3D12_SHADER_BYTECODE bytecode = {shader.bytecode->GetBufferPointer(),
                                            shader.bytecode->GetBufferSize()};
    root_binding_pipeline_ =
        context_.CreateComputePipeline(root_binding_root_.get(), bytecode);
    ASSERT_TRUE(root_binding_pipeline_);
  }

  void RunRootBindingCase(ExecutionPathMode mode, PathRunResult *result) {
    ASSERT_NE(result, nullptr);
    SCOPED_TRACE(PathModeName(mode));
    ASSERT_NO_FATAL_FAILURE(EnsureRootBindingPipeline());

    constexpr std::array<std::uint32_t, 4> kInput = {2u, 4u, 8u, 16u};
    constexpr std::array<std::uint32_t, 4> kZeroOutput = {};
    constexpr std::uint32_t kZeroCounter = 0;
    constexpr std::uint32_t kCbvValue = 11;
    constexpr UINT64 kCbvSize = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;

    auto input_upload = context_.CreateUploadBuffer(
        sizeof(kInput), kInput.data(), sizeof(kInput));
    auto output_upload = context_.CreateUploadBuffer(
        sizeof(kZeroOutput), kZeroOutput.data(), sizeof(kZeroOutput));
    auto counter_upload = context_.CreateUploadBuffer(
        sizeof(kZeroCounter), &kZeroCounter, sizeof(kZeroCounter));
    auto cbv =
        context_.CreateUploadBuffer(kCbvSize, &kCbvValue, sizeof(kCbvValue));
    auto input = context_.CreateBuffer(sizeof(kInput), D3D12_HEAP_TYPE_DEFAULT,
                                       D3D12_RESOURCE_FLAG_NONE,
                                       D3D12_RESOURCE_STATE_COPY_DEST);
    auto output =
        context_.CreateBuffer(sizeof(kZeroOutput), D3D12_HEAP_TYPE_DEFAULT,
                              D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                              D3D12_RESOURCE_STATE_COPY_DEST);
    auto counter =
        context_.CreateBuffer(sizeof(kZeroCounter), D3D12_HEAP_TYPE_DEFAULT,
                              D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                              D3D12_RESOURCE_STATE_COPY_DEST);
    auto output_readback = CreatePoisonedReadback(sizeof(kZeroOutput));
    auto counter_readback = CreatePoisonedReadback(sizeof(kZeroCounter));
    ASSERT_TRUE(input_upload);
    ASSERT_TRUE(output_upload);
    ASSERT_TRUE(counter_upload);
    ASSERT_TRUE(cbv);
    ASSERT_TRUE(input);
    ASSERT_TRUE(output);
    ASSERT_TRUE(counter);
    ASSERT_TRUE(output_readback);
    ASSERT_TRUE(counter_readback);

    context_.list()->CopyBufferRegion(input.get(), 0, input_upload.get(), 0,
                                      sizeof(kInput));
    context_.list()->CopyBufferRegion(output.get(), 0, output_upload.get(), 0,
                                      sizeof(kZeroOutput));
    context_.list()->CopyBufferRegion(counter.get(), 0, counter_upload.get(), 0,
                                      sizeof(kZeroCounter));
    D3D12TestContext::Transition(
        context_.list(), input.get(), D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    D3D12TestContext::Transition(context_.list(), output.get(),
                                 D3D12_RESOURCE_STATE_COPY_DEST,
                                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    D3D12TestContext::Transition(context_.list(), counter.get(),
                                 D3D12_RESOURCE_STATE_COPY_DEST,
                                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    ASSERT_EQ(context_.ExecuteAndWait(), S_OK);
    ASSERT_EQ(context_.ResetCommandList(), S_OK);

    ASSERT_NO_FATAL_FAILURE(SetPathMode(mode));
    constexpr std::array<std::uint32_t, 2> kInlineConstants = {3u, 5u};
    context_.list()->SetComputeRootSignature(root_binding_root_.get());
    context_.list()->SetPipelineState(root_binding_pipeline_.get());
    context_.list()->SetComputeRoot32BitConstants(0, kInlineConstants.size(),
                                                  kInlineConstants.data(), 0);
    context_.list()->SetComputeRootConstantBufferView(
        1, cbv->GetGPUVirtualAddress());
    context_.list()->SetComputeRootShaderResourceView(
        2, input->GetGPUVirtualAddress());
    context_.list()->SetComputeRootUnorderedAccessView(
        3, output->GetGPUVirtualAddress());
    context_.list()->SetComputeRootUnorderedAccessView(
        4, counter->GetGPUVirtualAddress());
    context_.list()->Dispatch(1, 1, 1);
    D3D12TestContext::Transition(context_.list(), output.get(),
                                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                 D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12TestContext::Transition(context_.list(), counter.get(),
                                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                 D3D12_RESOURCE_STATE_COPY_SOURCE);
    context_.list()->CopyBufferRegion(output_readback.get(), 0, output.get(), 0,
                                      sizeof(kZeroOutput));
    context_.list()->CopyBufferRegion(counter_readback.get(), 0, counter.get(),
                                      0, sizeof(kZeroCounter));

    ASSERT_NO_FATAL_FAILURE(ExecuteConfiguredList(result));
    ASSERT_NO_FATAL_FAILURE(ReadArray(output_readback.get(), &result->output));
    result->auxiliary_count = 1;
    ASSERT_NO_FATAL_FAILURE(
        ReadArray(counter_readback.get(), &result->auxiliary, 1));
    ASSERT_EQ(context_.ResetCommandList(), S_OK);
  }

  void RunComputeCase(ComputeFamily family, ExecutionPathMode mode,
                      PathRunResult *result) {
    ASSERT_NE(result, nullptr);
    SCOPED_TRACE(PathModeName(mode));
    constexpr std::array<std::uint32_t, 4> kZero = {};
    auto zero_upload =
        context_.CreateUploadBuffer(sizeof(kZero), kZero.data(), sizeof(kZero));
    auto output =
        context_.CreateBuffer(sizeof(kZero), D3D12_HEAP_TYPE_DEFAULT,
                              D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                              D3D12_RESOURCE_STATE_COPY_DEST);
    auto output_readback = CreatePoisonedReadback(sizeof(kZero));
    auto heap = context_.CreateDescriptorHeap(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true);
    auto cpu_heap = context_.CreateDescriptorHeap(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, false);
    ASSERT_TRUE(zero_upload);
    ASSERT_TRUE(output);
    ASSERT_TRUE(output_readback);
    ASSERT_TRUE(heap);
    ASSERT_TRUE(cpu_heap);

    D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
    uav.Format = DXGI_FORMAT_R32_UINT;
    uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uav.Buffer.NumElements = kZero.size();
    context_.device()->CreateUnorderedAccessView(
        output.get(), nullptr, &uav,
        heap->GetCPUDescriptorHandleForHeapStart());
    context_.device()->CreateUnorderedAccessView(
        output.get(), nullptr, &uav,
        cpu_heap->GetCPUDescriptorHandleForHeapStart());

    context_.list()->CopyBufferRegion(output.get(), 0, zero_upload.get(), 0,
                                      sizeof(kZero));
    D3D12TestContext::Transition(context_.list(), output.get(),
                                 D3D12_RESOURCE_STATE_COPY_DEST,
                                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    ASSERT_EQ(context_.ExecuteAndWait(), S_OK);
    ASSERT_EQ(context_.ResetCommandList(), S_OK);

    ComPtr<ID3D12Resource> auxiliary_source;
    ComPtr<ID3D12Resource> auxiliary_resource;
    ComPtr<ID3D12Resource> auxiliary_readback;
    ComPtr<ID3D12QueryHeap> query_heap;
    ComPtr<ID3D12Resource> query_result;
    ComPtr<ID3D12Heap> sparse_backing;

    constexpr std::array<std::uint32_t, 4> kCopyValues = {
        0x10203040u, 0x55667788u, 0x89abcdefu, 0xfedcba98u};
    constexpr std::array<std::uint32_t, 8> kSparseValues = {
        0x01010101u, 0x12121212u, 0x23232323u, 0x34343434u,
        0x45454545u, 0x56565656u, 0x67676767u, 0x78787878u};

    if (family == ComputeFamily::Copy) {
      auxiliary_source = context_.CreateUploadBuffer(
          sizeof(kCopyValues), kCopyValues.data(), sizeof(kCopyValues));
      auxiliary_resource = context_.CreateBuffer(
          sizeof(kCopyValues), D3D12_HEAP_TYPE_DEFAULT,
          D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
      auxiliary_readback = CreatePoisonedReadback(sizeof(kCopyValues));
      result->auxiliary_count = kCopyValues.size();
    } else if (family == ComputeFamily::Query) {
      D3D12_QUERY_HEAP_DESC query_desc = {};
      query_desc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
      query_desc.Count = 2;
      ASSERT_EQ(context_.device()->CreateQueryHeap(
                    &query_desc, IID_PPV_ARGS(query_heap.put())),
                S_OK);
      query_result = context_.CreateBuffer(
          sizeof(result->query), D3D12_HEAP_TYPE_READBACK,
          D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
      ASSERT_TRUE(query_result);
      UINT64 *mapping = nullptr;
      const D3D12_RANGE no_read = {0, 0};
      ASSERT_EQ(
          query_result->Map(0, &no_read, reinterpret_cast<void **>(&mapping)),
          S_OK);
      ASSERT_NE(mapping, nullptr);
      std::fill(mapping, mapping + result->query.size(),
                std::numeric_limits<UINT64>::max());
      const D3D12_RANGE written = {0, sizeof(result->query)};
      query_result->Unmap(0, &written);
    } else if (family == ComputeFamily::SparseAccess) {
      ASSERT_GE(options_.TiledResourcesTier, D3D12_TILED_RESOURCES_TIER_1);
      D3D12_RESOURCE_DESC sparse_desc = {};
      sparse_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
      sparse_desc.Width = D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES;
      sparse_desc.Height = 1;
      sparse_desc.DepthOrArraySize = 1;
      sparse_desc.MipLevels = 1;
      sparse_desc.SampleDesc.Count = 1;
      sparse_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
      ASSERT_EQ(context_.device()->CreateReservedResource(
                    &sparse_desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                    IID_PPV_ARGS(auxiliary_resource.put())),
                S_OK);
      ASSERT_TRUE(auxiliary_resource);

      UINT tile_count = 0;
      D3D12_PACKED_MIP_INFO packed = {};
      D3D12_TILE_SHAPE shape = {};
      D3D12_SUBRESOURCE_TILING tiling = {};
      UINT subresource_count = 1;
      context_.device()->GetResourceTiling(auxiliary_resource.get(),
                                           &tile_count, &packed, &shape,
                                           &subresource_count, 0, &tiling);
      ASSERT_GT(tile_count, 0u);
      ASSERT_EQ(subresource_count, 1u);

      D3D12_HEAP_DESC heap_desc = {};
      heap_desc.SizeInBytes =
          UINT64(tile_count) * D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES;
      heap_desc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
      heap_desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
      ASSERT_EQ(context_.device()->CreateHeap(
                    &heap_desc, IID_PPV_ARGS(sparse_backing.put())),
                S_OK);
      ASSERT_TRUE(sparse_backing);

      D3D12_TILED_RESOURCE_COORDINATE coordinate = {};
      D3D12_TILE_REGION_SIZE region = {};
      region.NumTiles = tile_count;
      const D3D12_TILE_RANGE_FLAGS range_flag = D3D12_TILE_RANGE_FLAG_NONE;
      const UINT heap_offset = 0;
      context_.queue()->UpdateTileMappings(
          auxiliary_resource.get(), 1, &coordinate, &region,
          sparse_backing.get(), 1, &range_flag, &heap_offset, &tile_count,
          D3D12_TILE_MAPPING_FLAG_NONE);

      auxiliary_source = context_.CreateUploadBuffer(
          sizeof(kSparseValues), kSparseValues.data(), sizeof(kSparseValues));
      auxiliary_readback = CreatePoisonedReadback(sizeof(kSparseValues));
      result->auxiliary_count = kSparseValues.size();
    }
    ASSERT_TRUE(family != ComputeFamily::Copy || auxiliary_source);
    ASSERT_TRUE(family != ComputeFamily::Copy || auxiliary_resource);
    ASSERT_TRUE(family != ComputeFamily::Copy || auxiliary_readback);
    ASSERT_TRUE(family != ComputeFamily::SparseAccess || auxiliary_source);
    ASSERT_TRUE(family != ComputeFamily::SparseAccess || auxiliary_readback);

    ASSERT_NO_FATAL_FAILURE(SetPathMode(mode));
    BindCompute(heap.get());

    switch (family) {
    case ComputeFamily::Copy:
      context_.list()->Dispatch(1, 1, 1);
      context_.list()->CopyBufferRegion(auxiliary_resource.get(), 0,
                                        auxiliary_source.get(), 0,
                                        sizeof(kCopyValues));
      D3D12TestContext::Transition(context_.list(), auxiliary_resource.get(),
                                   D3D12_RESOURCE_STATE_COPY_DEST,
                                   D3D12_RESOURCE_STATE_COPY_SOURCE);
      context_.list()->CopyBufferRegion(auxiliary_readback.get(), 0,
                                        auxiliary_resource.get(), 0,
                                        sizeof(kCopyValues));
      break;
    case ComputeFamily::Clear: {
      constexpr UINT kClear[4] = {7u, 7u, 7u, 7u};
      context_.list()->ClearUnorderedAccessViewUint(
          heap->GetGPUDescriptorHandleForHeapStart(),
          cpu_heap->GetCPUDescriptorHandleForHeapStart(), output.get(), kClear,
          0, nullptr);
      D3D12TestContext::UavBarrier(context_.list(), output.get());
      context_.list()->Dispatch(1, 1, 1);
      break;
    }
    case ComputeFamily::Uav:
      context_.list()->Dispatch(1, 1, 1);
      break;
    case ComputeFamily::Barrier:
      context_.list()->Dispatch(1, 1, 1);
      D3D12TestContext::UavBarrier(context_.list(), output.get());
      context_.list()->Dispatch(1, 1, 1);
      break;
    case ComputeFamily::Query:
      context_.list()->EndQuery(query_heap.get(), D3D12_QUERY_TYPE_TIMESTAMP,
                                0);
      context_.list()->Dispatch(1, 1, 1);
      context_.list()->EndQuery(query_heap.get(), D3D12_QUERY_TYPE_TIMESTAMP,
                                1);
      context_.list()->ResolveQueryData(query_heap.get(),
                                        D3D12_QUERY_TYPE_TIMESTAMP, 0, 2,
                                        query_result.get(), 0);
      break;
    case ComputeFamily::ExecuteIndirect:
      context_.list()->Dispatch(1, 1, 1);
      D3D12TestContext::UavBarrier(context_.list(), output.get());
      context_.list()->ExecuteIndirect(dispatch_signature_.get(), 1,
                                       indirect_arguments_.get(), 0, nullptr,
                                       0);
      break;
    case ComputeFamily::SparseAccess:
      context_.list()->Dispatch(1, 1, 1);
      context_.list()->CopyBufferRegion(auxiliary_resource.get(), 0,
                                        auxiliary_source.get(), 0,
                                        sizeof(kSparseValues));
      D3D12TestContext::Transition(context_.list(), auxiliary_resource.get(),
                                   D3D12_RESOURCE_STATE_COPY_DEST,
                                   D3D12_RESOURCE_STATE_COPY_SOURCE);
      context_.list()->CopyBufferRegion(auxiliary_readback.get(), 0,
                                        auxiliary_resource.get(), 0,
                                        sizeof(kSparseValues));
      break;
    }

    D3D12TestContext::Transition(context_.list(), output.get(),
                                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                 D3D12_RESOURCE_STATE_COPY_SOURCE);
    context_.list()->CopyBufferRegion(output_readback.get(), 0, output.get(), 0,
                                      sizeof(result->output));
    ASSERT_NO_FATAL_FAILURE(ExecuteConfiguredList(result));
    ASSERT_NO_FATAL_FAILURE(ReadArray(output_readback.get(), &result->output));
    if (auxiliary_readback) {
      ASSERT_NO_FATAL_FAILURE(ReadArray(auxiliary_readback.get(),
                                        &result->auxiliary,
                                        result->auxiliary_count));
    }
    if (query_result) {
      ASSERT_NO_FATAL_FAILURE(ReadArray(query_result.get(), &result->query));
    }
    ASSERT_EQ(context_.ResetCommandList(), S_OK);
  }

  void RunGraphicsCase(bool indexed, ExecutionPathMode mode,
                       PathRunResult *result) {
    ASSERT_NE(result, nullptr);
    SCOPED_TRACE(PathModeName(mode));
    constexpr UINT kWidth = 8;
    constexpr UINT kHeight = 8;
    auto target =
        context_.CreateTexture2D(kWidth, kHeight, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
                                 D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
                                 D3D12_RESOURCE_STATE_RENDER_TARGET);
    auto rtv_heap =
        context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
    ASSERT_TRUE(target);
    ASSERT_TRUE(rtv_heap);
    const auto rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
    context_.device()->CreateRenderTargetView(target.get(), nullptr, rtv);

    const D3D12_RESOURCE_DESC target_desc = target->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
    UINT rows = 0;
    UINT64 row_size = 0;
    UINT64 total_size = 0;
    context_.device()->GetCopyableFootprints(&target_desc, 0, 1, 0, &footprint,
                                             &rows, &row_size, &total_size);
    ASSERT_EQ(rows, kHeight);
    ASSERT_EQ(row_size, UINT64(kWidth * sizeof(std::uint32_t)));
    auto readback = CreatePoisonedReadback(total_size);
    ASSERT_TRUE(readback);

    ASSERT_NO_FATAL_FAILURE(SetPathMode(mode));
    constexpr FLOAT kBlack[4] = {};
    context_.list()->ClearRenderTargetView(rtv, kBlack, 0, nullptr);
    context_.list()->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    context_.list()->SetGraphicsRootSignature(graphics_root_.get());
    context_.list()->SetPipelineState(graphics_pipeline_.get());
    context_.list()->IASetPrimitiveTopology(
        D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    const D3D12_VIEWPORT viewport = {
        0.0f, 0.0f, static_cast<FLOAT>(kWidth), static_cast<FLOAT>(kHeight),
        0.0f, 1.0f};
    const D3D12_RECT scissor = {0, 0, kWidth, kHeight};
    context_.list()->RSSetViewports(1, &viewport);
    context_.list()->RSSetScissorRects(1, &scissor);
    if (indexed) {
      const D3D12_INDEX_BUFFER_VIEW index_view = {
          index_buffer_->GetGPUVirtualAddress(), 3 * sizeof(std::uint16_t),
          DXGI_FORMAT_R16_UINT};
      context_.list()->IASetIndexBuffer(&index_view);
      context_.list()->DrawIndexedInstanced(3, 1, 0, 0, 0);
    } else {
      context_.list()->DrawInstanced(3, 1, 0, 0);
    }
    D3D12TestContext::Transition(context_.list(), target.get(),
                                 D3D12_RESOURCE_STATE_RENDER_TARGET,
                                 D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION destination = {};
    destination.pResource = readback.get();
    destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    destination.PlacedFootprint = footprint;
    D3D12_TEXTURE_COPY_LOCATION source = {};
    source.pResource = target.get();
    source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    context_.list()->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);

    ASSERT_NO_FATAL_FAILURE(ExecuteConfiguredList(result));
    std::uint8_t *mapping = nullptr;
    const D3D12_RANGE read_range = {0, static_cast<SIZE_T>(total_size)};
    ASSERT_EQ(
        readback->Map(0, &read_range, reinterpret_cast<void **>(&mapping)),
        S_OK);
    ASSERT_NE(mapping, nullptr);
    result->pixels.reserve(kWidth * kHeight);
    for (UINT y = 0; y < kHeight; ++y) {
      for (UINT x = 0; x < kWidth; ++x) {
        std::uint32_t pixel = 0;
        std::memcpy(&pixel,
                    mapping + footprint.Offset +
                        UINT64(y) * footprint.Footprint.RowPitch +
                        x * sizeof(pixel),
                    sizeof(pixel));
        result->pixels.push_back(pixel);
      }
    }
    const D3D12_RANGE no_write = {0, 0};
    readback->Unmap(0, &no_write);
    ASSERT_EQ(context_.ResetCommandList(), S_OK);
  }

  void RunComputeModes(ComputeFamily family,
                       std::array<PathRunResult, 3> *results) {
    ASSERT_NE(results, nullptr);
    for (std::size_t index = 0; index < kPathModes.size(); ++index) {
      ASSERT_NO_FATAL_FAILURE(
          RunComputeCase(family, kPathModes[index], &(*results)[index]));
    }
  }

  void RunGraphicsModes(bool indexed, std::array<PathRunResult, 3> *results) {
    ASSERT_NE(results, nullptr);
    for (std::size_t index = 0; index < kPathModes.size(); ++index) {
      ASSERT_NO_FATAL_FAILURE(
          RunGraphicsCase(indexed, kPathModes[index], &(*results)[index]));
    }
  }

  void RunRootBindingModes(std::array<PathRunResult, 3> *results) {
    ASSERT_NE(results, nullptr);
    for (std::size_t index = 0; index < kPathModes.size(); ++index) {
      ASSERT_NO_FATAL_FAILURE(
          RunRootBindingCase(kPathModes[index], &(*results)[index]));
    }
  }

  void ExpectTelemetry(const PathRunResult &result, ExecutionPathMode mode,
                       UINT work_records, UINT graphics_packets,
                       UINT compute_packets) {
    SCOPED_TRACE(PathModeName(mode));
    const auto &stats = result.stats;
    EXPECT_EQ(stats.mode, mode);
    EXPECT_GT(stats.record_count, work_records);
    EXPECT_EQ(stats.work_record_count, work_records);
    EXPECT_EQ(stats.replayed_compiled_packet_fallbacks, 0u);
    EXPECT_EQ(stats.empty_native_segments, 0u);
    EXPECT_EQ(stats.empty_fallback_segments, 0u);
    EXPECT_EQ(stats.replayed_empty_native_segments, 0u);
    EXPECT_EQ(stats.replayed_empty_fallback_segments, 0u);

    if (mode == ExecutionPathMode::Fallback) {
      EXPECT_EQ(stats.compiled_work_record_count, 0u);
      EXPECT_EQ(stats.selected_graphics_packets, 0u);
      EXPECT_EQ(stats.selected_compute_packets, 0u);
      EXPECT_EQ(stats.retained_graphics_packets, 0u);
      EXPECT_EQ(stats.retained_compute_packets, 0u);
      EXPECT_EQ(stats.has_native_root_base_buffer, 0u);
      EXPECT_EQ(stats.replayed_graphics_packets, 0u);
      EXPECT_EQ(stats.replayed_compute_packets, 0u);
      EXPECT_EQ(stats.replayed_fallback_records, stats.record_count);
    } else {
      EXPECT_EQ(stats.compiled_work_record_count, work_records);
      EXPECT_EQ(stats.selected_graphics_packets, graphics_packets);
      EXPECT_EQ(stats.selected_compute_packets, compute_packets);
      EXPECT_EQ(stats.retained_graphics_packets, graphics_packets);
      EXPECT_EQ(stats.retained_compute_packets, compute_packets);
      EXPECT_EQ(stats.replayed_graphics_packets, graphics_packets);
      EXPECT_EQ(stats.replayed_compute_packets, compute_packets);
      EXPECT_EQ(stats.replayed_fallback_records + graphics_packets +
                    compute_packets,
                stats.record_count);
      if (mode == ExecutionPathMode::NativeCompiled) {
        EXPECT_EQ(stats.native_requirement_satisfied, 1u);
      }
    }
    EXPECT_GT(stats.fallback_segments, 0u);
    EXPECT_GT(stats.replayed_fallback_ranges, 0u);
    EXPECT_EQ(stats.replayed_fallback_records +
                  stats.replayed_graphics_packets +
                  stats.replayed_compute_packets,
              stats.record_count);
  }

  void ExpectComputeTelemetry(const std::array<PathRunResult, 3> &results,
                              UINT dispatch_count) {
    for (std::size_t index = 0; index < kPathModes.size(); ++index) {
      ExpectTelemetry(results[index], kPathModes[index], dispatch_count, 0,
                      dispatch_count);
    }
  }

  void ExpectGraphicsTelemetry(const std::array<PathRunResult, 3> &results) {
    for (std::size_t index = 0; index < kPathModes.size(); ++index)
      ExpectTelemetry(results[index], kPathModes[index], 1, 1, 0);
  }

  void ExpectOutput(const std::array<PathRunResult, 3> &results,
                    const std::array<std::uint32_t, 4> &expected) {
    for (std::size_t index = 0; index < kPathModes.size(); ++index) {
      SCOPED_TRACE(PathModeName(kPathModes[index]));
      EXPECT_EQ(results[index].output, expected);
    }
    EXPECT_EQ(results[0].output, results[1].output);
    EXPECT_EQ(results[0].output, results[2].output);
  }

  D3D12TestContext context_;
  D3D12_FEATURE_DATA_D3D12_OPTIONS options_ = {};
  ComPtr<ID3D12RootSignature> compute_root_;
  ComPtr<ID3D12PipelineState> compute_pipeline_;
  ComPtr<ID3D12RootSignature> graphics_root_;
  ComPtr<ID3D12PipelineState> graphics_pipeline_;
  ComPtr<ID3D12Resource> index_buffer_;
  ComPtr<ID3D12Resource> indirect_arguments_;
  ComPtr<ID3D12CommandSignature> dispatch_signature_;
  ComPtr<ID3D12RootSignature> root_binding_root_;
  ComPtr<ID3D12PipelineState> root_binding_pipeline_;
};

DXMT_GROUP_SERIAL_TESTS("PathModeParitySpec.*",
                        "d3d12-execution-path-parity");
DXMT_SERIAL_TEST_F(PathModeParitySpec, DrawIsEquivalentAcrossModes) {
  std::array<PathRunResult, 3> results;
  ASSERT_NO_FATAL_FAILURE(RunGraphicsModes(false, &results));
  ExpectGraphicsTelemetry(results);
  for (std::size_t mode = 0; mode < results.size(); ++mode) {
    SCOPED_TRACE(PathModeName(kPathModes[mode]));
    ASSERT_EQ(results[mode].pixels.size(), 64u);
    for (std::size_t pixel = 0; pixel < results[mode].pixels.size(); ++pixel) {
      EXPECT_TRUE(ColorsMatch(results[mode].pixels[pixel], 0x40404040u, 1))
          << "pixel " << pixel;
    }
  }
  EXPECT_EQ(results[0].pixels, results[1].pixels);
  EXPECT_EQ(results[0].pixels, results[2].pixels);
}

DXMT_SERIAL_TEST_F(PathModeParitySpec, DrawIndexedIsEquivalentAcrossModes) {
  std::array<PathRunResult, 3> results;
  ASSERT_NO_FATAL_FAILURE(RunGraphicsModes(true, &results));
  ExpectGraphicsTelemetry(results);
  for (std::size_t mode = 0; mode < results.size(); ++mode) {
    SCOPED_TRACE(PathModeName(kPathModes[mode]));
    ASSERT_EQ(results[mode].pixels.size(), 64u);
    for (std::size_t pixel = 0; pixel < results[mode].pixels.size(); ++pixel) {
      EXPECT_TRUE(ColorsMatch(results[mode].pixels[pixel], 0x40404040u, 1))
          << "pixel " << pixel;
    }
  }
  EXPECT_EQ(results[0].pixels, results[1].pixels);
  EXPECT_EQ(results[0].pixels, results[2].pixels);
}

DXMT_SERIAL_TEST_F(PathModeParitySpec, CopyIsEquivalentAcrossModes) {
  std::array<PathRunResult, 3> results;
  ASSERT_NO_FATAL_FAILURE(RunComputeModes(ComputeFamily::Copy, &results));
  ExpectComputeTelemetry(results, 1);
  ExpectOutput(results, {1u, 0u, 0u, 0u});
  constexpr std::array<std::uint32_t, 4> kExpected = {0x10203040u, 0x55667788u,
                                                      0x89abcdefu, 0xfedcba98u};
  for (std::size_t mode = 0; mode < results.size(); ++mode) {
    SCOPED_TRACE(PathModeName(kPathModes[mode]));
    EXPECT_EQ(results[mode].auxiliary_count, kExpected.size());
    EXPECT_TRUE(std::equal(kExpected.begin(), kExpected.end(),
                           results[mode].auxiliary.begin()));
  }
  EXPECT_EQ(results[0].auxiliary, results[1].auxiliary);
  EXPECT_EQ(results[0].auxiliary, results[2].auxiliary);
}

DXMT_SERIAL_TEST_F(PathModeParitySpec, ClearIsEquivalentAcrossModes) {
  std::array<PathRunResult, 3> results;
  ASSERT_NO_FATAL_FAILURE(RunComputeModes(ComputeFamily::Clear, &results));
  ExpectComputeTelemetry(results, 1);
  ExpectOutput(results, {8u, 7u, 7u, 7u});
}

DXMT_SERIAL_TEST_F(PathModeParitySpec, UavAccessIsEquivalentAcrossModes) {
  std::array<PathRunResult, 3> results;
  ASSERT_NO_FATAL_FAILURE(RunComputeModes(ComputeFamily::Uav, &results));
  ExpectComputeTelemetry(results, 1);
  ExpectOutput(results, {1u, 0u, 0u, 0u});
}

DXMT_SERIAL_TEST_F(PathModeParitySpec,
                   RootConstantsAndDescriptorsAreEquivalentAcrossModes) {
  std::array<PathRunResult, 3> results;
  ASSERT_NO_FATAL_FAILURE(RunRootBindingModes(&results));
  ExpectComputeTelemetry(results, 1);
  // One shader reads inline root constants plus root CBV/SRV descriptors and
  // writes through root UAV descriptors. The counter makes duplicate replay
  // observable independently of the deterministic output stores.
  ExpectOutput(results, {22u, 28u, 40u, 64u});
  for (std::size_t mode = 0; mode < results.size(); ++mode) {
    SCOPED_TRACE(PathModeName(kPathModes[mode]));
    EXPECT_EQ(results[mode].auxiliary_count, 1u);
    EXPECT_EQ(results[mode].auxiliary[0], 1u);
  }
  EXPECT_EQ(results[0].auxiliary, results[1].auxiliary);
  EXPECT_EQ(results[0].auxiliary, results[2].auxiliary);
}

DXMT_SERIAL_TEST_F(PathModeParitySpec, BarrierIsEquivalentAcrossModes) {
  std::array<PathRunResult, 3> results;
  ASSERT_NO_FATAL_FAILURE(RunComputeModes(ComputeFamily::Barrier, &results));
  ExpectComputeTelemetry(results, 2);
  ExpectOutput(results, {2u, 1u, 0u, 0u});
}

DXMT_SERIAL_TEST_F(PathModeParitySpec, QueryIsEquivalentAcrossModes) {
  std::array<PathRunResult, 3> results;
  ASSERT_NO_FATAL_FAILURE(RunComputeModes(ComputeFamily::Query, &results));
  ExpectComputeTelemetry(results, 1);
  ExpectOutput(results, {1u, 0u, 0u, 0u});
  constexpr UINT64 kPoison = std::numeric_limits<UINT64>::max();
  for (std::size_t mode = 0; mode < results.size(); ++mode) {
    SCOPED_TRACE(PathModeName(kPathModes[mode]));
    EXPECT_NE(results[mode].query[0], kPoison);
    EXPECT_NE(results[mode].query[1], kPoison);
    EXPECT_LE(results[mode].query[0], results[mode].query[1]);
  }
}

DXMT_SERIAL_TEST_F(PathModeParitySpec, ExecuteIndirectIsEquivalentAcrossModes) {
  std::array<PathRunResult, 3> results;
  ASSERT_NO_FATAL_FAILURE(
      RunComputeModes(ComputeFamily::ExecuteIndirect, &results));
  // ExecuteIndirect remains a fallback record. Only the explicit Dispatch is
  // counted as compiled work; both commands still increment the UAV once.
  ExpectComputeTelemetry(results, 1);
  ExpectOutput(results, {2u, 1u, 0u, 0u});
}

DXMT_SERIAL_TEST_F(PathModeParitySpec, SparseAccessIsEquivalentAcrossModes) {
  if (options_.TiledResourcesTier < D3D12_TILED_RESOURCES_TIER_1)
    GTEST_SKIP() << "Tiled resources are not supported";

  std::array<PathRunResult, 3> results;
  ASSERT_NO_FATAL_FAILURE(
      RunComputeModes(ComputeFamily::SparseAccess, &results));
  ExpectComputeTelemetry(results, 1);
  ExpectOutput(results, {1u, 0u, 0u, 0u});
  constexpr std::array<std::uint32_t, 8> kExpected = {
      0x01010101u, 0x12121212u, 0x23232323u, 0x34343434u,
      0x45454545u, 0x56565656u, 0x67676767u, 0x78787878u};
  for (std::size_t mode = 0; mode < results.size(); ++mode) {
    SCOPED_TRACE(PathModeName(kPathModes[mode]));
    EXPECT_EQ(results[mode].auxiliary_count, kExpected.size());
    EXPECT_EQ(results[mode].auxiliary, kExpected);
  }
  EXPECT_EQ(results[0].auxiliary, results[1].auxiliary);
  EXPECT_EQ(results[0].auxiliary, results[2].auxiliary);
}

} // namespace
