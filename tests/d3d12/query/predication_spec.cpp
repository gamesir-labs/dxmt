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
using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::CreateIsolatedD3D12Device;
using dxmt::test::D3D12TestContext;

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

TEST_F(PredicationSpec, EqualZeroExecutes) {
  ExpectPredicatedDispatch(0, D3D12_PREDICATION_OP_EQUAL_ZERO, true);
}

TEST_F(PredicationSpec, EqualZeroSkips) {
  ExpectPredicatedDispatch(1, D3D12_PREDICATION_OP_EQUAL_ZERO, false);
}

TEST_F(PredicationSpec, NotEqualZeroExecutes) {
  ExpectPredicatedDispatch(1, D3D12_PREDICATION_OP_NOT_EQUAL_ZERO, true);
}

TEST_F(PredicationSpec, NotEqualZeroSkips) {
  ExpectPredicatedDispatch(0, D3D12_PREDICATION_OP_NOT_EQUAL_ZERO, false);
}

TEST_F(PredicationSpec, ReadsPredicateAtNonzeroOffset) {
  ExpectPredicatedDispatch(0, D3D12_PREDICATION_OP_EQUAL_ZERO, true,
                           sizeof(UINT64));
}

TEST_F(PredicationSpec, PredicateProducedByCopyControlsDispatch) {
  if (!SupportsCpuVisibleCustomHeap())
    GTEST_SKIP() << "GPU-written CPU-visible predicates require UMA";
  ExpectPredicatedDispatch(1, D3D12_PREDICATION_OP_NOT_EQUAL_ZERO, true,
                           sizeof(UINT64), true);
}

TEST_F(PredicationSpec,
       DefaultPredicateProducedByCopyControlsDispatchAfterGpuCompletion) {
  ExpectPredicatedDispatch(1, D3D12_PREDICATION_OP_NOT_EQUAL_ZERO, true,
                           sizeof(UINT64), true, true);
}

TEST_F(PredicationSpec, DisablePredicationRestoresUnconditionalDispatch) {
  ExpectPredicatedDispatch(1, D3D12_PREDICATION_OP_EQUAL_ZERO, false, 0, false,
                           false, true);
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
                                  D3D12_PREDICATION_OP_NOT_EQUAL_ZERO);
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

enum class InvalidPredicationCase {
  ForeignBuffer,
  Texture,
  MisalignedOffset,
  OutOfBoundsOffset,
  InvalidOperation,
};

class InvalidPredicationSpec
    : public PredicationSpec,
      public ::testing::WithParamInterface<InvalidPredicationCase> {};

TEST_P(InvalidPredicationSpec, RejectsInvalidArgumentsAndAllowsFreshRecovery) {
  auto local_buffer = context_.CreateBuffer(
      16, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_PREDICATION);
  ASSERT_TRUE(local_buffer);
  ComPtr<ID3D12Resource> alternate_buffer;
  ID3D12Resource *selected_buffer = local_buffer.get();
  UINT64 offset = 0;
  D3D12_PREDICATION_OP operation = D3D12_PREDICATION_OP_EQUAL_ZERO;
  ComPtr<ID3D12Device> foreign_device;
  D3D12TestContext foreign_context;

  switch (GetParam()) {
  case InvalidPredicationCase::ForeignBuffer:
    foreign_device = CreateIsolatedD3D12Device();
    ASSERT_TRUE(foreign_device);
    ASSERT_EQ(foreign_context.Initialize(foreign_device.get()), S_OK);
    alternate_buffer = foreign_context.CreateBuffer(
        16, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
        D3D12_RESOURCE_STATE_PREDICATION);
    selected_buffer = alternate_buffer.get();
    break;
  case InvalidPredicationCase::Texture:
    alternate_buffer = context_.CreateTexture2D(
        1, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_NONE,
        D3D12_RESOURCE_STATE_COMMON);
    selected_buffer = alternate_buffer.get();
    break;
  case InvalidPredicationCase::MisalignedOffset:
    offset = 4;
    break;
  case InvalidPredicationCase::OutOfBoundsOffset:
    offset = 16;
    break;
  case InvalidPredicationCase::InvalidOperation:
    operation = static_cast<D3D12_PREDICATION_OP>(2);
    break;
  }
  ASSERT_NE(selected_buffer, nullptr);

  context_.list()->SetPredication(selected_buffer, offset, operation);
  EXPECT_EQ(context_.list()->Close(), E_INVALIDARG);

  ComPtr<ID3D12CommandAllocator> allocator;
  ComPtr<ID3D12GraphicsCommandList> list;
  ASSERT_EQ(context_.device()->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(allocator.put())),
            S_OK);
  ASSERT_EQ(context_.device()->CreateCommandList(
                0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.get(), nullptr,
                IID_PPV_ARGS(list.put())),
            S_OK);
  list->SetPredication(local_buffer.get(), 8,
                       D3D12_PREDICATION_OP_NOT_EQUAL_ZERO);
  list->SetPredication(nullptr, 0, D3D12_PREDICATION_OP_EQUAL_ZERO);
  EXPECT_EQ(list->Close(), S_OK);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

std::string InvalidPredicationCaseName(
    const ::testing::TestParamInfo<InvalidPredicationCase> &info) {
  switch (info.param) {
  case InvalidPredicationCase::ForeignBuffer:
    return "ForeignBuffer";
  case InvalidPredicationCase::Texture:
    return "Texture";
  case InvalidPredicationCase::MisalignedOffset:
    return "MisalignedOffset";
  case InvalidPredicationCase::OutOfBoundsOffset:
    return "OutOfBoundsOffset";
  case InvalidPredicationCase::InvalidOperation:
    return "InvalidOperation";
  }
  return "Unknown";
}

INSTANTIATE_TEST_SUITE_P(
    Validation, InvalidPredicationSpec,
    ::testing::Values(InvalidPredicationCase::ForeignBuffer,
                      InvalidPredicationCase::Texture,
                      InvalidPredicationCase::MisalignedOffset,
                      InvalidPredicationCase::OutOfBoundsOffset,
                      InvalidPredicationCase::InvalidOperation),
    InvalidPredicationCaseName);

} // namespace
