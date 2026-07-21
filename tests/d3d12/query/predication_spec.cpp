#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"
#include "shaders/runtime_test_shaders.hpp"

#include <array>
#include <cstring>
#include <string>
#include <vector>

namespace {

using dxmt::test::ClearBufferComputeShader;
using dxmt::test::ColorsMatch;
using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;
using dxmt::test::TextureReadback;

class PredicationSpec : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_TRUE(SUCCEEDED(context_.Initialize())); }

  bool SupportsCpuVisibleCustomHeap() const {
    D3D12_FEATURE_DATA_ARCHITECTURE architecture = {};
    return SUCCEEDED(context_.device()->CheckFeatureSupport(
               D3D12_FEATURE_ARCHITECTURE, &architecture,
               sizeof(architecture))) &&
           architecture.UMA;
  }

  ComPtr<ID3D12Resource> CreateCpuVisibleBuffer(UINT64 size,
                                                D3D12_RESOURCE_FLAGS flags,
                                                D3D12_RESOURCE_STATES state) {
    D3D12_HEAP_PROPERTIES heap_properties = {};
    heap_properties.Type = D3D12_HEAP_TYPE_CUSTOM;
    heap_properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_BACK;
    heap_properties.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;
    heap_properties.CreationNodeMask = 1;
    heap_properties.VisibleNodeMask = 1;
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = size;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = flags;
    ComPtr<ID3D12Resource> buffer;
    EXPECT_EQ(context_.device()->CreateCommittedResource(
                  &heap_properties, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr,
                  IID_PPV_ARGS(buffer.put())),
              S_OK);
    return buffer;
  }

  void ExpectPredicatedDispatch(UINT64 predicate_value,
                                D3D12_PREDICATION_OP operation, bool executes,
                                UINT64 predicate_offset = 0,
                                bool copy_produced = false,
                                bool default_predicate = false,
                                bool dispatch_after_disable = false) {
    constexpr std::uint32_t sentinel = 0xdeadbeef;
    constexpr std::uint32_t dispatch_value = 0x13579bdf;
    std::array<std::uint32_t, 64> initial;
    initial.fill(sentinel);
    auto initial_upload = context_.CreateUploadBuffer(
        sizeof(initial), initial.data(), sizeof(initial));
    auto output =
        context_.CreateBuffer(sizeof(initial), D3D12_HEAP_TYPE_DEFAULT,
                              D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                              D3D12_RESOURCE_STATE_COPY_DEST);
    ASSERT_TRUE(predicate_offset == 0 || predicate_offset == sizeof(UINT64));
    std::array<UINT64, 2> predicate_values = {0xfedcba9876543210ull,
                                              0xfedcba9876543210ull};
    predicate_values[predicate_offset / sizeof(UINT64)] = predicate_value;
    auto predicate_upload = context_.CreateUploadBuffer(
        sizeof(predicate_values), predicate_values.data(),
        sizeof(predicate_values));
    ComPtr<ID3D12Resource> copied_predicate;
    ID3D12Resource *predicate = predicate_upload.get();
    if (copy_produced) {
      copied_predicate =
          default_predicate
              ? context_.CreateBuffer(
                    sizeof(predicate_values), D3D12_HEAP_TYPE_DEFAULT,
                    D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST)
              : CreateCpuVisibleBuffer(sizeof(predicate_values),
                                       D3D12_RESOURCE_FLAG_NONE,
                                       D3D12_RESOURCE_STATE_COPY_DEST);
      if (predicate_upload && copied_predicate) {
        context_.list()->CopyBufferRegion(copied_predicate.get(), 0,
                                          predicate_upload.get(), 0,
                                          sizeof(predicate_values));
        D3D12TestContext::Transition(context_.list(), copied_predicate.get(),
                                     D3D12_RESOURCE_STATE_COPY_DEST,
                                     D3D12_RESOURCE_STATE_PREDICATION);
        EXPECT_EQ(context_.ExecuteAndWait(), S_OK);
        EXPECT_EQ(context_.ResetCommandList(), S_OK);
        predicate = copied_predicate.get();
      }
    }
    auto heap = context_.CreateDescriptorHeap(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true);
    ASSERT_TRUE(initial_upload);
    ASSERT_TRUE(output);
    ASSERT_TRUE(predicate_upload);
    ASSERT_TRUE(!copy_produced || copied_predicate);
    ASSERT_TRUE(heap);

    D3D12_DESCRIPTOR_RANGE range = {};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    range.NumDescriptors = 1;
    D3D12_ROOT_PARAMETER parameters[2] = {};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[0].Constants.Num32BitValues = 1;
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[1].DescriptorTable.NumDescriptorRanges = 1;
    parameters[1].DescriptorTable.pDescriptorRanges = &range;
    D3D12_ROOT_SIGNATURE_DESC root_desc = {};
    root_desc.NumParameters = 2;
    root_desc.pParameters = parameters;
    auto root_signature = context_.CreateRootSignature(root_desc);
    auto pipeline = context_.CreateComputePipeline(root_signature.get(),
                                                   ClearBufferComputeShader());
    ASSERT_TRUE(root_signature);
    ASSERT_TRUE(pipeline);

    D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
    uav.Format = DXGI_FORMAT_R32_UINT;
    uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uav.Buffer.NumElements = initial.size();
    context_.device()->CreateUnorderedAccessView(
        output.get(), nullptr, &uav,
        heap->GetCPUDescriptorHandleForHeapStart());
    context_.list()->CopyBufferRegion(output.get(), 0, initial_upload.get(), 0,
                                      sizeof(initial));
    D3D12TestContext::Transition(context_.list(), output.get(),
                                 D3D12_RESOURCE_STATE_COPY_DEST,
                                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    ID3D12DescriptorHeap *heaps[] = {heap.get()};
    context_.list()->SetDescriptorHeaps(1, heaps);
    context_.list()->SetPipelineState(pipeline.get());
    context_.list()->SetComputeRootSignature(root_signature.get());
    context_.list()->SetComputeRoot32BitConstant(0, dispatch_value, 0);
    context_.list()->SetComputeRootDescriptorTable(
        1, heap->GetGPUDescriptorHandleForHeapStart());
    context_.list()->SetPredication(predicate, predicate_offset, operation);
    context_.list()->Dispatch(1, 1, 1);
    context_.list()->SetPredication(nullptr, 0,
                                    D3D12_PREDICATION_OP_EQUAL_ZERO);
    if (dispatch_after_disable)
      context_.list()->Dispatch(1, 1, 1);
    D3D12TestContext::Transition(context_.list(), output.get(),
                                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                 D3D12_RESOURCE_STATE_COPY_SOURCE);

    std::vector<std::uint8_t> bytes;
    ASSERT_TRUE(SUCCEEDED(
        context_.ReadbackBuffer(output.get(), sizeof(initial), &bytes)));
    ASSERT_EQ(bytes.size(), sizeof(initial));
    const std::uint32_t expected =
        executes || dispatch_after_disable ? dispatch_value : sentinel;
    for (std::size_t index = 0; index < initial.size(); ++index) {
      std::uint32_t actual = 0;
      std::memcpy(&actual, bytes.data() + index * sizeof(actual),
                  sizeof(actual));
      EXPECT_EQ(actual, expected) << "element " << index;
    }
  }

  D3D12TestContext context_;
};

TEST_F(PredicationSpec, EqualZeroSkips) {
  ExpectPredicatedDispatch(0, D3D12_PREDICATION_OP_EQUAL_ZERO, false);
}

TEST_F(PredicationSpec, EqualZeroExecutes) {
  ExpectPredicatedDispatch(1, D3D12_PREDICATION_OP_EQUAL_ZERO, true);
}

TEST_F(PredicationSpec, NotEqualZeroSkips) {
  ExpectPredicatedDispatch(1, D3D12_PREDICATION_OP_NOT_EQUAL_ZERO, false);
}

TEST_F(PredicationSpec, NotEqualZeroExecutes) {
  ExpectPredicatedDispatch(0, D3D12_PREDICATION_OP_NOT_EQUAL_ZERO, true);
}

TEST_F(PredicationSpec, ReadsPredicateAtNonzeroOffset) {
  ExpectPredicatedDispatch(0, D3D12_PREDICATION_OP_EQUAL_ZERO, false,
                           sizeof(UINT64));
}

TEST_F(PredicationSpec, PredicateProducedByCopyControlsDispatch) {
  if (!SupportsCpuVisibleCustomHeap())
    GTEST_SKIP() << "GPU-written CPU-visible predicates require UMA";
  ExpectPredicatedDispatch(1, D3D12_PREDICATION_OP_EQUAL_ZERO, true,
                           sizeof(UINT64), true);
}

TEST_F(PredicationSpec,
       DefaultPredicateProducedByCopyControlsDispatchAfterGpuCompletion) {
  ExpectPredicatedDispatch(1, D3D12_PREDICATION_OP_EQUAL_ZERO, true,
                           sizeof(UINT64), true, true);
}

TEST_F(PredicationSpec, DisablePredicationRestoresUnconditionalDispatch) {
  ExpectPredicatedDispatch(0, D3D12_PREDICATION_OP_EQUAL_ZERO, false, 0, false,
                           false, true);
}

TEST_F(PredicationSpec,
       SinglePredicateSkipsMultipleDispatchesUntilDisabled) {
  const auto shader = CompileShader(R"(
    RWStructuredBuffer<uint> counter : register(u0);
    [numthreads(1, 1, 1)]
    void main() {
      uint ignored;
      InterlockedAdd(counter[0], 1, ignored);
    }
  )",
                                    "cs_5_0");
  ASSERT_EQ(shader.result, S_OK) << shader.diagnostic_text();

  D3D12_ROOT_PARAMETER parameter = {};
  parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  root_desc.NumParameters = 1;
  root_desc.pParameters = &parameter;
  auto root = context_.CreateRootSignature(root_desc);
  const D3D12_SHADER_BYTECODE bytecode = {
      shader.bytecode->GetBufferPointer(), shader.bytecode->GetBufferSize()};
  auto pipeline = context_.CreateComputePipeline(root.get(), bytecode);
  constexpr UINT zero = 0;
  auto initial = context_.CreateUploadBuffer(sizeof(zero), &zero, sizeof(zero));
  auto counter = context_.CreateBuffer(
      sizeof(zero), D3D12_HEAP_TYPE_DEFAULT,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_COPY_DEST);
  constexpr UINT64 skip_predicate = 0;
  auto predicate = context_.CreateUploadBuffer(
      sizeof(skip_predicate), &skip_predicate, sizeof(skip_predicate));
  ASSERT_TRUE(root);
  ASSERT_TRUE(pipeline);
  ASSERT_TRUE(initial);
  ASSERT_TRUE(counter);
  ASSERT_TRUE(predicate);

  context_.list()->CopyBufferRegion(counter.get(), 0, initial.get(), 0,
                                    sizeof(zero));
  D3D12TestContext::Transition(context_.list(), counter.get(),
                               D3D12_RESOURCE_STATE_COPY_DEST,
                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  context_.list()->SetComputeRootSignature(root.get());
  context_.list()->SetPipelineState(pipeline.get());
  context_.list()->SetComputeRootUnorderedAccessView(
      0, counter->GetGPUVirtualAddress());
  context_.list()->SetPredication(predicate.get(), 0,
                                  D3D12_PREDICATION_OP_EQUAL_ZERO);
  for (UINT index = 0; index < 3; ++index)
    context_.list()->Dispatch(1, 1, 1);
  context_.list()->SetPredication(nullptr, 0,
                                  D3D12_PREDICATION_OP_EQUAL_ZERO);
  context_.list()->Dispatch(1, 1, 1);
  D3D12TestContext::Transition(context_.list(), counter.get(),
                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);

  std::vector<std::uint8_t> bytes;
  ASSERT_EQ(context_.ReadbackBuffer(counter.get(), sizeof(UINT), &bytes), S_OK);
  ASSERT_EQ(bytes.size(), sizeof(UINT));
  UINT value = 0;
  std::memcpy(&value, bytes.data(), sizeof(value));
  EXPECT_EQ(value, 1u);
}

TEST_F(PredicationSpec, SinglePredicateSkipsMultipleDrawsUntilDisabled) {
  constexpr UINT width = 16;
  constexpr UINT height = 16;
  auto target = context_.CreateTexture2D(
      width, height, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
      D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
      D3D12_RESOURCE_STATE_RENDER_TARGET);
  auto rtv_heap =
      context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
  constexpr UINT64 skip_predicate = 0;
  auto predicate = context_.CreateUploadBuffer(
      sizeof(skip_predicate), &skip_predicate, sizeof(skip_predicate));
  ASSERT_TRUE(target);
  ASSERT_TRUE(rtv_heap);
  ASSERT_TRUE(predicate);

  const D3D12_CPU_DESCRIPTOR_HANDLE rtv =
      rtv_heap->GetCPUDescriptorHandleForHeapStart();
  context_.device()->CreateRenderTargetView(target.get(), nullptr, rtv);
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  root_desc.Flags =
      D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
  auto root = context_.CreateRootSignature(root_desc);
  const auto pixel = CompileShader(
      "float4 main() : SV_Target { return float4(1, 0, 0, 1); }",
      "ps_5_0");
  ASSERT_EQ(pixel.result, S_OK) << pixel.diagnostic_text();
  const D3D12_SHADER_BYTECODE pixel_bytecode = {
      pixel.bytecode->GetBufferPointer(), pixel.bytecode->GetBufferSize()};
  auto pipeline = context_.CreateGraphicsPipeline(
      root.get(), DXGI_FORMAT_R8G8B8A8_UNORM, pixel_bytecode);
  ASSERT_TRUE(root);
  ASSERT_TRUE(pipeline);

  constexpr FLOAT clear_color[4] = {};
  context_.list()->ClearRenderTargetView(rtv, clear_color, 0, nullptr);
  context_.list()->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
  context_.list()->SetGraphicsRootSignature(root.get());
  context_.list()->SetPipelineState(pipeline.get());
  context_.list()->IASetPrimitiveTopology(
      D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  constexpr D3D12_VIEWPORT viewport = {0.0f, 0.0f, 16.0f, 16.0f, 0.0f, 1.0f};
  constexpr D3D12_RECT left = {0, 0, 8, 16};
  constexpr D3D12_RECT right = {8, 0, 16, 16};
  constexpr D3D12_RECT center = {7, 0, 9, 16};
  context_.list()->RSSetViewports(1, &viewport);
  context_.list()->SetPredication(predicate.get(), 0,
                                  D3D12_PREDICATION_OP_EQUAL_ZERO);
  context_.list()->RSSetScissorRects(1, &left);
  context_.list()->DrawInstanced(3, 1, 0, 0);
  context_.list()->RSSetScissorRects(1, &right);
  context_.list()->DrawInstanced(3, 1, 0, 0);
  context_.list()->SetPredication(nullptr, 0,
                                  D3D12_PREDICATION_OP_EQUAL_ZERO);
  context_.list()->RSSetScissorRects(1, &center);
  context_.list()->DrawInstanced(3, 1, 0, 0);
  D3D12TestContext::Transition(context_.list(), target.get(),
                               D3D12_RESOURCE_STATE_RENDER_TARGET,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);

  TextureReadback readback;
  ASSERT_EQ(context_.ReadbackTexture(target.get(), &readback), S_OK);
  ASSERT_EQ(readback.width, width);
  ASSERT_EQ(readback.height, height);
  for (UINT y = 0; y < height; ++y) {
    for (UINT x = 0; x < width; ++x) {
      UINT pixel_value = 0;
      std::memcpy(&pixel_value,
                  readback.data.data() + y * readback.row_pitch +
                      x * sizeof(pixel_value),
                  sizeof(pixel_value));
      const UINT expected = x >= 7 && x < 9 ? 0xff0000ffu : 0u;
      EXPECT_TRUE(ColorsMatch(pixel_value, expected, 1))
          << "pixel (" << x << ", " << y << ")";
    }
  }
}

TEST_F(PredicationSpec, PredicateProducedByComputeControlsDispatch) {
  if (!SupportsCpuVisibleCustomHeap())
    GTEST_SKIP() << "GPU-written CPU-visible predicates require UMA";
  constexpr UINT output_sentinel = 0xdeadbeefu;
  const auto producer = CompileShader(R"(
    RWByteAddressBuffer predicate : register(u0);
    [numthreads(1, 1, 1)]
    void main() { predicate.Store2(0, uint2(1, 0)); }
  )",
                                      "cs_5_0");
  const auto consumer = CompileShader(R"(
    RWByteAddressBuffer output : register(u0);
    [numthreads(1, 1, 1)]
    void main() { output.Store(0, 0x31415926u); }
  )",
                                      "cs_5_0");
  ASSERT_EQ(producer.result, S_OK) << producer.diagnostic_text();
  ASSERT_EQ(consumer.result, S_OK) << consumer.diagnostic_text();

  D3D12_ROOT_PARAMETER parameter = {};
  parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  root_desc.NumParameters = 1;
  root_desc.pParameters = &parameter;
  auto root = context_.CreateRootSignature(root_desc);
  ASSERT_TRUE(root);
  const D3D12_SHADER_BYTECODE producer_bytecode = {
      producer.bytecode->GetBufferPointer(),
      producer.bytecode->GetBufferSize()};
  const D3D12_SHADER_BYTECODE consumer_bytecode = {
      consumer.bytecode->GetBufferPointer(),
      consumer.bytecode->GetBufferSize()};
  auto producer_pipeline =
      context_.CreateComputePipeline(root.get(), producer_bytecode);
  auto consumer_pipeline =
      context_.CreateComputePipeline(root.get(), consumer_bytecode);
  auto predicate = CreateCpuVisibleBuffer(
      sizeof(UINT64), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  auto output_upload = context_.CreateUploadBuffer(
      sizeof(output_sentinel), &output_sentinel, sizeof(output_sentinel));
  auto output =
      context_.CreateBuffer(sizeof(UINT), D3D12_HEAP_TYPE_DEFAULT,
                            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                            D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(producer_pipeline);
  ASSERT_TRUE(consumer_pipeline);
  ASSERT_TRUE(predicate);
  ASSERT_TRUE(output_upload);
  ASSERT_TRUE(output);

  context_.list()->CopyBufferRegion(output.get(), 0, output_upload.get(), 0,
                                    sizeof(output_sentinel));
  D3D12TestContext::Transition(context_.list(), output.get(),
                               D3D12_RESOURCE_STATE_COPY_DEST,
                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  context_.list()->SetComputeRootSignature(root.get());
  context_.list()->SetPipelineState(producer_pipeline.get());
  context_.list()->SetComputeRootUnorderedAccessView(
      0, predicate->GetGPUVirtualAddress());
  context_.list()->Dispatch(1, 1, 1);
  D3D12TestContext::Transition(context_.list(), predicate.get(),
                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                               D3D12_RESOURCE_STATE_PREDICATION);
  ASSERT_EQ(context_.ExecuteAndWait(), S_OK);
  ASSERT_EQ(context_.ResetCommandList(), S_OK);
  context_.list()->SetComputeRootSignature(root.get());
  context_.list()->SetPipelineState(consumer_pipeline.get());
  context_.list()->SetComputeRootUnorderedAccessView(
      0, output->GetGPUVirtualAddress());
  context_.list()->SetPredication(predicate.get(), 0,
                                  D3D12_PREDICATION_OP_EQUAL_ZERO);
  context_.list()->Dispatch(1, 1, 1);
  context_.list()->SetPredication(nullptr, 0, D3D12_PREDICATION_OP_EQUAL_ZERO);
  D3D12TestContext::Transition(context_.list(), output.get(),
                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
  std::vector<std::uint8_t> bytes;
  ASSERT_EQ(context_.ReadbackBuffer(output.get(), sizeof(UINT), &bytes), S_OK);
  ASSERT_EQ(bytes.size(), sizeof(UINT));
  UINT value = 0;
  std::memcpy(&value, bytes.data(), sizeof(value));
  EXPECT_EQ(value, 0x31415926u);
}


} // namespace
