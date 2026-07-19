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
using dxmt::test::D3D12TestContext;

class ExecuteIndirectSpec : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_TRUE(SUCCEEDED(context_.Initialize())); }

  void ExpectDispatch(UINT max_command_count, bool executes,
                      UINT64 argument_offset = 0,
                      const UINT *count_value = nullptr,
                      bool default_count_buffer = false) {
    constexpr std::uint32_t sentinel = 0xdeadbeef;
    constexpr std::uint32_t dispatch_value = 0x2468ace0;
    std::array<std::uint32_t, 64> initial;
    initial.fill(sentinel);
    auto initial_upload = context_.CreateUploadBuffer(
        sizeof(initial), initial.data(), sizeof(initial));
    auto output =
        context_.CreateBuffer(sizeof(initial), D3D12_HEAP_TYPE_DEFAULT,
                              D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                              D3D12_RESOURCE_STATE_COPY_DEST);
    auto heap = context_.CreateDescriptorHeap(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true);
    const D3D12_DISPATCH_ARGUMENTS dispatch = {1, 1, 1};
    std::array<std::uint8_t, 64> argument_data = {};
    ASSERT_LE(argument_offset + sizeof(dispatch), argument_data.size());
    std::memcpy(argument_data.data() + argument_offset, &dispatch,
                sizeof(dispatch));
    auto arguments = context_.CreateUploadBuffer(
        argument_data.size(), argument_data.data(), argument_data.size());
    ComPtr<ID3D12Resource> count_buffer;
    ComPtr<ID3D12Resource> count_upload;
    constexpr UINT64 count_offset = sizeof(UINT);
    if (count_value) {
      const std::array<UINT, 2> count_data = {0xdeadbeef, *count_value};
      if (default_count_buffer) {
        count_upload = context_.CreateUploadBuffer(
            sizeof(count_data), count_data.data(), sizeof(count_data));
        count_buffer = context_.CreateBuffer(
            sizeof(count_data), D3D12_HEAP_TYPE_DEFAULT,
            D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
        if (count_upload && count_buffer) {
          context_.list()->CopyBufferRegion(count_buffer.get(), count_offset,
                                            count_upload.get(), sizeof(UINT),
                                            sizeof(*count_value));
          D3D12TestContext::Transition(context_.list(), count_buffer.get(),
                                       D3D12_RESOURCE_STATE_COPY_DEST,
                                       D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
        }
      } else {
        count_buffer = context_.CreateUploadBuffer(
            sizeof(count_data), count_data.data(), sizeof(count_data));
      }
    }
    ASSERT_TRUE(initial_upload);
    ASSERT_TRUE(output);
    ASSERT_TRUE(heap);
    ASSERT_TRUE(arguments);
    ASSERT_TRUE(!count_value || count_buffer);
    ASSERT_TRUE(!default_count_buffer || count_upload);

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

    D3D12_INDIRECT_ARGUMENT_DESC argument_desc = {};
    argument_desc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
    D3D12_COMMAND_SIGNATURE_DESC signature_desc = {};
    signature_desc.ByteStride = sizeof(dispatch);
    signature_desc.NumArgumentDescs = 1;
    signature_desc.pArgumentDescs = &argument_desc;
    ComPtr<ID3D12CommandSignature> signature;
    ASSERT_TRUE(SUCCEEDED(context_.device()->CreateCommandSignature(
        &signature_desc, nullptr, __uuidof(ID3D12CommandSignature),
        reinterpret_cast<void **>(signature.put()))));

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
    context_.list()->ExecuteIndirect(signature.get(), max_command_count,
                                     arguments.get(), argument_offset,
                                     count_buffer.get(), count_offset);
    D3D12TestContext::Transition(context_.list(), output.get(),
                                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                 D3D12_RESOURCE_STATE_COPY_SOURCE);

    std::vector<std::uint8_t> bytes;
    ASSERT_TRUE(SUCCEEDED(
        context_.ReadbackBuffer(output.get(), sizeof(initial), &bytes)));
    ASSERT_EQ(bytes.size(), sizeof(initial));
    const std::uint32_t expected = executes ? dispatch_value : sentinel;
    for (std::size_t index = 0; index < initial.size(); ++index) {
      std::uint32_t actual = 0;
      std::memcpy(&actual, bytes.data() + index * sizeof(actual),
                  sizeof(actual));
      EXPECT_EQ(actual, expected) << "element " << index;
    }
  }

  D3D12TestContext context_;
};

TEST_F(ExecuteIndirectSpec, Dispatch) { ExpectDispatch(1, true); }

TEST_F(ExecuteIndirectSpec, ZeroCount) { ExpectDispatch(0, false); }

TEST_F(ExecuteIndirectSpec, ArgumentAtNonzeroOffset) {
  ExpectDispatch(1, true, 16);
}

TEST_F(ExecuteIndirectSpec, CountBufferZeroSkipsDispatch) {
  const UINT count = 0;
  ExpectDispatch(1, false, 0, &count);
}

TEST_F(ExecuteIndirectSpec, CountBufferOneExecutesDispatch) {
  const UINT count = 1;
  ExpectDispatch(1, true, 0, &count);
}

TEST_F(ExecuteIndirectSpec, DefaultCountBufferOneExecutesDispatch) {
  const UINT count = 1;
  ExpectDispatch(1, true, 0, &count, true);
}

class ExecuteIndirectExtendedSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    const auto counter_shader = CompileShader(R"(
      RWStructuredBuffer<uint> output : register(u0);
      [numthreads(1, 1, 1)]
      void main() {
        uint ignored;
        InterlockedAdd(output[0], 1, ignored);
      }
    )", "cs_5_0");
    const auto argument_shader = CompileShader(R"(
      RWByteAddressBuffer arguments : register(u0);
      [numthreads(1, 1, 1)]
      void main() {
        arguments.Store(0, 1);
        arguments.Store(4, 1);
        arguments.Store(8, 1);
      }
    )", "cs_5_0");
    ASSERT_EQ(counter_shader.result, S_OK) << counter_shader.diagnostic_text();
    ASSERT_EQ(argument_shader.result, S_OK)
        << argument_shader.diagnostic_text();

    D3D12_ROOT_PARAMETER parameter = {};
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    D3D12_ROOT_SIGNATURE_DESC root_desc = {};
    root_desc.NumParameters = 1;
    root_desc.pParameters = &parameter;
    root_ = context_.CreateRootSignature(root_desc);
    ASSERT_TRUE(root_);
    counter_pipeline_ = context_.CreateComputePipeline(
        root_.get(), {counter_shader.bytecode->GetBufferPointer(),
                      counter_shader.bytecode->GetBufferSize()});
    argument_pipeline_ = context_.CreateComputePipeline(
        root_.get(), {argument_shader.bytecode->GetBufferPointer(),
                      argument_shader.bytecode->GetBufferSize()});
    ASSERT_TRUE(counter_pipeline_);
    ASSERT_TRUE(argument_pipeline_);

    D3D12_INDIRECT_ARGUMENT_DESC argument = {};
    argument.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
    D3D12_COMMAND_SIGNATURE_DESC desc = {};
    desc.ByteStride = sizeof(D3D12_DISPATCH_ARGUMENTS);
    desc.NumArgumentDescs = 1;
    desc.pArgumentDescs = &argument;
    ASSERT_EQ(context_.device()->CreateCommandSignature(
                  &desc, nullptr, IID_PPV_ARGS(signature_.put())),
              S_OK);
  }

  ComPtr<ID3D12Resource> CreateCounter() {
    const UINT zero = 0;
    counter_upload_ =
        context_.CreateUploadBuffer(sizeof(zero), &zero, sizeof(zero));
    auto counter = context_.CreateBuffer(
        sizeof(zero), D3D12_HEAP_TYPE_DEFAULT,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_DEST);
    EXPECT_TRUE(counter_upload_);
    EXPECT_TRUE(counter);
    if (counter_upload_ && counter) {
      context_.list()->CopyBufferRegion(counter.get(), 0,
                                        counter_upload_.get(), 0,
                                        sizeof(zero));
      D3D12TestContext::Transition(
          context_.list(), counter.get(), D3D12_RESOURCE_STATE_COPY_DEST,
          D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
    return counter;
  }

  UINT ReadCounter(ID3D12Resource *counter) {
    D3D12TestContext::Transition(
        context_.list(), counter, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_SOURCE);
    std::vector<std::uint8_t> bytes;
    EXPECT_EQ(context_.ReadbackBuffer(counter, sizeof(UINT), &bytes), S_OK);
    UINT value = 0;
    if (bytes.size() == sizeof(value))
      std::memcpy(&value, bytes.data(), sizeof(value));
    return value;
  }

  void BindCounter(ID3D12Resource *counter) {
    context_.list()->SetComputeRootSignature(root_.get());
    context_.list()->SetPipelineState(counter_pipeline_.get());
    context_.list()->SetComputeRootUnorderedAccessView(
        0, counter->GetGPUVirtualAddress());
  }

  D3D12TestContext context_;
  ComPtr<ID3D12RootSignature> root_;
  ComPtr<ID3D12PipelineState> counter_pipeline_;
  ComPtr<ID3D12PipelineState> argument_pipeline_;
  ComPtr<ID3D12CommandSignature> signature_;
  ComPtr<ID3D12Resource> counter_upload_;
};

TEST_F(ExecuteIndirectExtendedSpec, ExecutesMultipleCommands) {
  const std::array<D3D12_DISPATCH_ARGUMENTS, 2> commands = {
      D3D12_DISPATCH_ARGUMENTS{1, 1, 1},
      D3D12_DISPATCH_ARGUMENTS{1, 1, 1}};
  auto arguments = context_.CreateUploadBuffer(
      sizeof(commands), commands.data(), sizeof(commands));
  auto counter = CreateCounter();
  ASSERT_TRUE(arguments);
  ASSERT_TRUE(counter);
  BindCounter(counter.get());
  context_.list()->ExecuteIndirect(signature_.get(), 2, arguments.get(), 0,
                                   nullptr, 0);
  EXPECT_EQ(ReadCounter(counter.get()), 2u);
}

TEST_F(ExecuteIndirectExtendedSpec, CountBufferClampsMultipleCommands) {
  const std::array<D3D12_DISPATCH_ARGUMENTS, 2> commands = {
      D3D12_DISPATCH_ARGUMENTS{1, 1, 1},
      D3D12_DISPATCH_ARGUMENTS{1, 1, 1}};
  const UINT count = 1;
  auto arguments = context_.CreateUploadBuffer(
      sizeof(commands), commands.data(), sizeof(commands));
  auto count_buffer =
      context_.CreateUploadBuffer(sizeof(count), &count, sizeof(count));
  auto counter = CreateCounter();
  ASSERT_TRUE(arguments);
  ASSERT_TRUE(count_buffer);
  ASSERT_TRUE(counter);
  BindCounter(counter.get());
  context_.list()->ExecuteIndirect(signature_.get(), 2, arguments.get(), 0,
                                   count_buffer.get(), 0);
  EXPECT_EQ(ReadCounter(counter.get()), 1u);
}

TEST_F(ExecuteIndirectExtendedSpec, ArgumentBufferProducedByCopyExecutes) {
  const D3D12_DISPATCH_ARGUMENTS command = {1, 1, 1};
  auto upload = context_.CreateUploadBuffer(
      sizeof(command), &command, sizeof(command));
  auto arguments = context_.CreateBuffer(
      sizeof(command), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  auto counter = CreateCounter();
  ASSERT_TRUE(upload);
  ASSERT_TRUE(arguments);
  ASSERT_TRUE(counter);
  context_.list()->CopyBufferRegion(arguments.get(), 0, upload.get(), 0,
                                     sizeof(command));
  D3D12TestContext::Transition(
      context_.list(), arguments.get(), D3D12_RESOURCE_STATE_COPY_DEST,
      D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
  BindCounter(counter.get());
  context_.list()->ExecuteIndirect(signature_.get(), 1, arguments.get(), 0,
                                   nullptr, 0);
  EXPECT_EQ(ReadCounter(counter.get()), 1u);
}

TEST_F(ExecuteIndirectExtendedSpec, ArgumentBufferProducedByComputeExecutes) {
  auto arguments = context_.CreateBuffer(
      sizeof(D3D12_DISPATCH_ARGUMENTS), D3D12_HEAP_TYPE_DEFAULT,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  auto counter = CreateCounter();
  ASSERT_TRUE(arguments);
  ASSERT_TRUE(counter);
  context_.list()->SetComputeRootSignature(root_.get());
  context_.list()->SetPipelineState(argument_pipeline_.get());
  context_.list()->SetComputeRootUnorderedAccessView(
      0, arguments->GetGPUVirtualAddress());
  context_.list()->Dispatch(1, 1, 1);
  D3D12TestContext::Transition(
      context_.list(), arguments.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
  BindCounter(counter.get());
  context_.list()->ExecuteIndirect(signature_.get(), 1, arguments.get(), 0,
                                   nullptr, 0);
  EXPECT_EQ(ReadCounter(counter.get()), 1u);
}


} // namespace
