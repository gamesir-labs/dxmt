#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace {

using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

struct DescriptorBoundaryCase {
  UINT heap_size;
  const char *name;
};

class DescriptorBoundaryExecutionSpec
    : public ::testing::TestWithParam<DescriptorBoundaryCase> {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);

    const auto shader = CompileShader(R"(
      cbuffer Parameters : register(b0) {
        uint addend;
        uint xor_mask;
        uint output_base;
        uint input_count;
      };
      ByteAddressBuffer input : register(t0);
      RWByteAddressBuffer output : register(u0);

      [numthreads(8, 1, 1)]
      void main(uint3 thread_id : SV_DispatchThreadID) {
        if (thread_id.x < input_count) {
          output.Store((output_base + thread_id.x) * 4,
                       (input.Load(thread_id.x * 4) ^ xor_mask) + addend);
        }
      }
    )", "cs_5_0");
    ASSERT_EQ(shader.result, S_OK) << shader.diagnostic_text();

    std::array<D3D12_DESCRIPTOR_RANGE, 3> ranges = {};
    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
    ranges[0].NumDescriptors = 1;
    ranges[0].BaseShaderRegister = 0;
    ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[1].NumDescriptors = 1;
    ranges[1].BaseShaderRegister = 0;
    ranges[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    ranges[2].NumDescriptors = 1;
    ranges[2].BaseShaderRegister = 0;

    std::array<D3D12_ROOT_PARAMETER, 3> parameters = {};
    for (UINT i = 0; i < parameters.size(); ++i) {
      parameters[i].ParameterType =
          D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
      parameters[i].DescriptorTable.NumDescriptorRanges = 1;
      parameters[i].DescriptorTable.pDescriptorRanges = &ranges[i];
    }

    D3D12_ROOT_SIGNATURE_DESC root_desc = {};
    root_desc.NumParameters = static_cast<UINT>(parameters.size());
    root_desc.pParameters = parameters.data();
    root_signature_ = context_.CreateRootSignature(root_desc);
    ASSERT_TRUE(root_signature_);

    const D3D12_SHADER_BYTECODE bytecode = {
        shader.bytecode->GetBufferPointer(), shader.bytecode->GetBufferSize()};
    pipeline_ =
        context_.CreateComputePipeline(root_signature_.get(), bytecode);
    ASSERT_TRUE(pipeline_);
  }

  D3D12TestContext context_;
  ComPtr<ID3D12RootSignature> root_signature_;
  ComPtr<ID3D12PipelineState> pipeline_;
};

TEST_P(DescriptorBoundaryExecutionSpec,
       ExecutesCbvSrvUavTablesAtFirstMiddleAndLastDescriptors) {
  constexpr UINT kInputCount = 8;
  constexpr UINT kDispatchCount = 3;
  constexpr UINT kSegmentStride = kInputCount + 1;
  constexpr UINT kOutputCount = kDispatchCount * kSegmentStride + 1;
  constexpr std::array<std::uint32_t, kInputCount> kInput = {
      0x00000000u, 0x00000001u, 0x01234567u, 0x89abcdefu,
      0xffffffffu, 0x80000000u, 0x13579bdfu, 0x2468ace0u};
  constexpr std::array<std::uint32_t, kDispatchCount> kAddends = {
      0x10203040u, 0x55667788u, 0x01010101u};
  constexpr std::array<std::uint32_t, kDispatchCount> kXorMasks = {
      0xdeadbeefu, 0xa5a5a5a5u, 0x5a5a5a5au};

  const UINT heap_size = GetParam().heap_size;
  ASSERT_GE(heap_size, 3u);
  const std::array<UINT, 3> boundary_indices = {
      0u, heap_size / 2u, heap_size - 1u};
  ASSERT_NE(boundary_indices[0], boundary_indices[1]);
  ASSERT_NE(boundary_indices[1], boundary_indices[2]);

  auto input = context_.CreateUploadBuffer(sizeof(kInput), kInput.data(),
                                            sizeof(kInput));
  auto output = context_.CreateBuffer(
      UINT64(kOutputCount) * sizeof(std::uint32_t), D3D12_HEAP_TYPE_DEFAULT,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(input);
  ASSERT_TRUE(output);

  std::array<std::uint32_t, kOutputCount> expected = {};
  for (UINT i = 0; i < kOutputCount; ++i)
    expected[i] = 0xc0decafeu ^ (0x9e3779b9u * i);
  auto poison = context_.CreateUploadBuffer(sizeof(expected), expected.data(),
                                             sizeof(expected));
  ASSERT_TRUE(poison);
  context_.list()->CopyBufferRegion(output.get(), 0, poison.get(), 0,
                                    sizeof(expected));
  D3D12TestContext::Transition(context_.list(), output.get(),
                               D3D12_RESOURCE_STATE_COPY_DEST,
                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

  struct alignas(16) Parameters {
    std::uint32_t addend;
    std::uint32_t xor_mask;
    std::uint32_t output_base;
    std::uint32_t input_count;
  };
  static_assert(sizeof(Parameters) == 16);

  std::array<ComPtr<ID3D12Resource>, kDispatchCount> constants;
  std::array<ComPtr<ID3D12DescriptorHeap>, kDispatchCount> heaps;
  std::array<std::array<UINT, 3>, kDispatchCount> descriptor_indices = {};

  D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
  srv.Format = DXGI_FORMAT_R32_TYPELESS;
  srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
  srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv.Buffer.NumElements = kInputCount;
  srv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;

  D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
  uav.Format = DXGI_FORMAT_R32_TYPELESS;
  uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav.Buffer.NumElements = kOutputCount;
  uav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;

  for (UINT dispatch = 0; dispatch < kDispatchCount; ++dispatch) {
    const UINT output_base = 1u + dispatch * kSegmentStride;
    const Parameters parameters = {kAddends[dispatch], kXorMasks[dispatch],
                                   output_base, kInputCount};
    constants[dispatch] = context_.CreateUploadBuffer(
        D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, &parameters,
        sizeof(parameters));
    heaps[dispatch] = context_.CreateDescriptorHeap(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, heap_size, true);
    ASSERT_TRUE(constants[dispatch]);
    ASSERT_TRUE(heaps[dispatch]);

    // Rotate CBV/SRV/UAV through the first, middle, and last descriptor. Across
    // the three dispatches every descriptor kind executes at every position.
    descriptor_indices[dispatch] = {
        boundary_indices[dispatch % 3],
        boundary_indices[(dispatch + 1) % 3],
        boundary_indices[(dispatch + 2) % 3]};

    D3D12_CONSTANT_BUFFER_VIEW_DESC cbv = {};
    cbv.BufferLocation = constants[dispatch]->GetGPUVirtualAddress();
    cbv.SizeInBytes = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
    context_.device()->CreateConstantBufferView(
        &cbv, context_.CpuDescriptorHandle(heaps[dispatch].get(),
                                           descriptor_indices[dispatch][0]));
    context_.device()->CreateShaderResourceView(
        input.get(), &srv,
        context_.CpuDescriptorHandle(heaps[dispatch].get(),
                                     descriptor_indices[dispatch][1]));
    context_.device()->CreateUnorderedAccessView(
        output.get(), nullptr, &uav,
        context_.CpuDescriptorHandle(heaps[dispatch].get(),
                                     descriptor_indices[dispatch][2]));

    for (UINT i = 0; i < kInputCount; ++i) {
      expected[output_base + i] =
          (kInput[i] ^ kXorMasks[dispatch]) + kAddends[dispatch];
    }
  }

  context_.list()->SetComputeRootSignature(root_signature_.get());
  context_.list()->SetPipelineState(pipeline_.get());
  for (UINT dispatch = 0; dispatch < kDispatchCount; ++dispatch) {
    SCOPED_TRACE(::testing::Message()
                 << "heap_size=" << heap_size << " dispatch=" << dispatch
                 << " cbv=" << descriptor_indices[dispatch][0]
                 << " srv=" << descriptor_indices[dispatch][1]
                 << " uav=" << descriptor_indices[dispatch][2]);
    ID3D12DescriptorHeap *bound[] = {heaps[dispatch].get()};
    context_.list()->SetDescriptorHeaps(1, bound);
    context_.list()->SetComputeRootDescriptorTable(
        0, context_.GpuDescriptorHandle(heaps[dispatch].get(),
                                        descriptor_indices[dispatch][0]));
    context_.list()->SetComputeRootDescriptorTable(
        1, context_.GpuDescriptorHandle(heaps[dispatch].get(),
                                        descriptor_indices[dispatch][1]));
    context_.list()->SetComputeRootDescriptorTable(
        2, context_.GpuDescriptorHandle(heaps[dispatch].get(),
                                        descriptor_indices[dispatch][2]));
    context_.list()->Dispatch(1, 1, 1);
    D3D12TestContext::UavBarrier(context_.list(), output.get());
  }

  D3D12TestContext::Transition(context_.list(), output.get(),
                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
  std::vector<std::uint8_t> bytes;
  ASSERT_EQ(context_.ReadbackBuffer(output.get(), sizeof(expected), &bytes),
            S_OK);
  ASSERT_EQ(bytes.size(), sizeof(expected));
  std::array<std::uint32_t, kOutputCount> actual = {};
  std::memcpy(actual.data(), bytes.data(), sizeof(actual));
  for (UINT i = 0; i < kOutputCount; ++i)
    EXPECT_EQ(actual[i], expected[i]) << "word " << i;
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

std::string DescriptorBoundaryCaseName(
    const ::testing::TestParamInfo<DescriptorBoundaryCase> &info) {
  return info.param.name;
}

constexpr std::array<DescriptorBoundaryCase, 3> kDescriptorBoundaryCases = {{
    {31, "Count31"},
    {32, "Count32"},
    {33, "Count33"},
}};

INSTANTIATE_TEST_SUITE_P(Boundary31_32_33, DescriptorBoundaryExecutionSpec,
                         ::testing::ValuesIn(kDescriptorBoundaryCases),
                         DescriptorBoundaryCaseName);

class DescriptorHeapIndependenceSpec : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  D3D12TestContext context_;
};

TEST_F(DescriptorHeapIndependenceSpec,
       SwitchingOneHeapTypeDoesNotCorruptOtherHeap) {
  const auto shader = CompileShader(R"(
    Texture2D<float> input : register(t0);
    SamplerState input_sampler : register(s0);
    RWByteAddressBuffer output : register(u0);
    cbuffer Parameters : register(b0) { uint output_index; };

    [numthreads(1, 1, 1)]
    void main() {
      const float value =
          input.SampleLevel(input_sampler, float2(-0.25f, 0.25f), 0.0f);
      output.Store(output_index * 4, asuint(value));
    }
  )", "cs_5_0");
  ASSERT_EQ(shader.result, S_OK) << shader.diagnostic_text();

  std::array<D3D12_DESCRIPTOR_RANGE, 2> resource_ranges = {};
  resource_ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  resource_ranges[0].NumDescriptors = 1;
  resource_ranges[0].OffsetInDescriptorsFromTableStart = 0;
  resource_ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  resource_ranges[1].NumDescriptors = 1;
  resource_ranges[1].OffsetInDescriptorsFromTableStart = 1;
  D3D12_DESCRIPTOR_RANGE sampler_range = {};
  sampler_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
  sampler_range.NumDescriptors = 1;

  std::array<D3D12_ROOT_PARAMETER, 3> parameters = {};
  parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[0].DescriptorTable.NumDescriptorRanges = 2;
  parameters[0].DescriptorTable.pDescriptorRanges = resource_ranges.data();
  parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[1].DescriptorTable.NumDescriptorRanges = 1;
  parameters[1].DescriptorTable.pDescriptorRanges = &sampler_range;
  parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  parameters[2].Constants.ShaderRegister = 0;
  parameters[2].Constants.Num32BitValues = 1;
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  root_desc.NumParameters = static_cast<UINT>(parameters.size());
  root_desc.pParameters = parameters.data();
  auto root_signature = context_.CreateRootSignature(root_desc);
  ASSERT_TRUE(root_signature);
  const D3D12_SHADER_BYTECODE bytecode = {
      shader.bytecode->GetBufferPointer(), shader.bytecode->GetBufferSize()};
  auto pipeline =
      context_.CreateComputePipeline(root_signature.get(), bytecode);
  ASSERT_TRUE(pipeline);

  constexpr std::array<float, 4> kTextureA = {10.0f, 20.0f, 30.0f, 40.0f};
  constexpr std::array<float, 4> kTextureB = {100.0f, 200.0f, 300.0f,
                                              400.0f};
  auto texture_a = context_.CreateTexture2D(
      2, 2, 1, DXGI_FORMAT_R32_FLOAT, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  auto texture_b = context_.CreateTexture2D(
      2, 2, 1, DXGI_FORMAT_R32_FLOAT, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(texture_a);
  ASSERT_TRUE(texture_b);
  ASSERT_EQ(context_.UploadTextureAndReset(
                texture_a.get(), kTextureA.data(), 2 * sizeof(float),
                sizeof(kTextureA)),
            S_OK);
  ASSERT_EQ(context_.UploadTextureAndReset(
                texture_b.get(), kTextureB.data(), 2 * sizeof(float),
                sizeof(kTextureB)),
            S_OK);

  constexpr std::array<std::uint32_t, 4> kPoison = {
      0x7fc00001u, 0x7fc00002u, 0x7fc00003u, 0x7fc00004u};
  auto output = context_.CreateBuffer(
      sizeof(kPoison), D3D12_HEAP_TYPE_DEFAULT,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_COPY_DEST);
  auto poison = context_.CreateUploadBuffer(sizeof(kPoison), kPoison.data(),
                                             sizeof(kPoison));
  auto resources_a = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, true);
  auto resources_b = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, true);
  auto clamp_samplers = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 1, true);
  auto wrap_samplers = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 1, true);
  ASSERT_TRUE(output);
  ASSERT_TRUE(poison);
  ASSERT_TRUE(resources_a);
  ASSERT_TRUE(resources_b);
  ASSERT_TRUE(clamp_samplers);
  ASSERT_TRUE(wrap_samplers);

  D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
  srv.Format = DXGI_FORMAT_R32_FLOAT;
  srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv.Texture2D.MipLevels = 1;
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
  uav.Format = DXGI_FORMAT_R32_TYPELESS;
  uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav.Buffer.NumElements = kPoison.size();
  uav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
  for (const auto &entry : std::array{
           std::pair{texture_a.get(), resources_a.get()},
           std::pair{texture_b.get(), resources_b.get()}}) {
    context_.device()->CreateShaderResourceView(
        entry.first, &srv, context_.CpuDescriptorHandle(entry.second, 0));
    context_.device()->CreateUnorderedAccessView(
        output.get(), nullptr, &uav,
        context_.CpuDescriptorHandle(entry.second, 1));
  }

  D3D12_SAMPLER_DESC clamp = {};
  clamp.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
  clamp.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  clamp.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  clamp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  clamp.MaxAnisotropy = 1;
  clamp.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
  clamp.MaxLOD = D3D12_FLOAT32_MAX;
  auto wrap = clamp;
  wrap.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  wrap.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  wrap.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  context_.device()->CreateSampler(
      &clamp, clamp_samplers->GetCPUDescriptorHandleForHeapStart());
  context_.device()->CreateSampler(
      &wrap, wrap_samplers->GetCPUDescriptorHandleForHeapStart());

  context_.list()->CopyBufferRegion(output.get(), 0, poison.get(), 0,
                                    sizeof(kPoison));
  D3D12TestContext::Transition(context_.list(), output.get(),
                               D3D12_RESOURCE_STATE_COPY_DEST,
                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  D3D12TestContext::Transition(
      context_.list(), texture_a.get(), D3D12_RESOURCE_STATE_COPY_DEST,
      D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  D3D12TestContext::Transition(
      context_.list(), texture_b.get(), D3D12_RESOURCE_STATE_COPY_DEST,
      D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  context_.list()->SetComputeRootSignature(root_signature.get());
  context_.list()->SetPipelineState(pipeline.get());

  // Bind every table for the initial A+clamp combination.
  ID3D12DescriptorHeap *a_clamp[] = {resources_a.get(),
                                     clamp_samplers.get()};
  context_.list()->SetDescriptorHeaps(2, a_clamp);
  context_.list()->SetComputeRootDescriptorTable(
      0, resources_a->GetGPUDescriptorHandleForHeapStart());
  context_.list()->SetComputeRootDescriptorTable(
      1, clamp_samplers->GetGPUDescriptorHandleForHeapStart());
  context_.list()->SetComputeRoot32BitConstant(2, 0, 0);
  context_.list()->Dispatch(1, 1, 1);
  D3D12TestContext::UavBarrier(context_.list(), output.get());

  // Switch only the resource heap. SetDescriptorHeaps invalidates descriptor
  // table bindings, so legally rebind both tables and verify that the
  // unchanged sampler heap still supplies the same clamp descriptor.
  ID3D12DescriptorHeap *b_clamp[] = {resources_b.get(),
                                     clamp_samplers.get()};
  context_.list()->SetDescriptorHeaps(2, b_clamp);
  context_.list()->SetComputeRootDescriptorTable(
      0, resources_b->GetGPUDescriptorHandleForHeapStart());
  context_.list()->SetComputeRootDescriptorTable(
      1, clamp_samplers->GetGPUDescriptorHandleForHeapStart());
  context_.list()->SetComputeRoot32BitConstant(2, 1, 0);
  context_.list()->Dispatch(1, 1, 1);
  D3D12TestContext::UavBarrier(context_.list(), output.get());

  // Switch only the sampler heap and rebind both tables. Texture B and the
  // output UAV in the unchanged resource heap must remain intact.
  ID3D12DescriptorHeap *b_wrap[] = {resources_b.get(), wrap_samplers.get()};
  context_.list()->SetDescriptorHeaps(2, b_wrap);
  context_.list()->SetComputeRootDescriptorTable(
      0, resources_b->GetGPUDescriptorHandleForHeapStart());
  context_.list()->SetComputeRootDescriptorTable(
      1, wrap_samplers->GetGPUDescriptorHandleForHeapStart());
  context_.list()->SetComputeRoot32BitConstant(2, 2, 0);
  context_.list()->Dispatch(1, 1, 1);
  D3D12TestContext::UavBarrier(context_.list(), output.get());

  // Switch the resource heap independently once more while retaining wrap;
  // both table bindings are re-established after the heap-set change.
  ID3D12DescriptorHeap *a_wrap[] = {resources_a.get(), wrap_samplers.get()};
  context_.list()->SetDescriptorHeaps(2, a_wrap);
  context_.list()->SetComputeRootDescriptorTable(
      0, resources_a->GetGPUDescriptorHandleForHeapStart());
  context_.list()->SetComputeRootDescriptorTable(
      1, wrap_samplers->GetGPUDescriptorHandleForHeapStart());
  context_.list()->SetComputeRoot32BitConstant(2, 3, 0);
  context_.list()->Dispatch(1, 1, 1);
  D3D12TestContext::UavBarrier(context_.list(), output.get());

  D3D12TestContext::Transition(context_.list(), output.get(),
                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
  std::vector<std::uint8_t> bytes;
  ASSERT_EQ(context_.ReadbackBuffer(output.get(), sizeof(kPoison), &bytes),
            S_OK);
  ASSERT_EQ(bytes.size(), sizeof(kPoison));
  std::array<std::uint32_t, 4> actual = {};
  std::memcpy(actual.data(), bytes.data(), sizeof(actual));
  const std::array<std::uint32_t, 4> expected = {
      std::bit_cast<std::uint32_t>(10.0f),
      std::bit_cast<std::uint32_t>(100.0f),
      std::bit_cast<std::uint32_t>(200.0f),
      std::bit_cast<std::uint32_t>(20.0f)};
  EXPECT_EQ(actual, expected);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
