#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"

#include <array>
#include <cstring>
#include <vector>

namespace {

using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

enum class DescriptorPublication {
  Direct,
  Copied,
  Overwritten,
};

class BufferViewSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    shader_ = CompileShader(R"(
      Buffer<uint> typed_input : register(t0);
      ByteAddressBuffer raw_input : register(t1);
      StructuredBuffer<uint> structured_input : register(t2);
      RWStructuredBuffer<uint> output : register(u0);

      [numthreads(1, 1, 1)]
      void main() {
        output[0] = typed_input[2];
        output[1] = raw_input.Load(4);
        output[2] = structured_input[1];
      }
    )",
                            "cs_5_0");
    ASSERT_EQ(shader_.result, S_OK) << shader_.diagnostic_text();

    D3D12_DESCRIPTOR_RANGE ranges[2] = {};
    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[0].NumDescriptors = 3;
    ranges[0].BaseShaderRegister = 0;
    ranges[0].OffsetInDescriptorsFromTableStart = 0;
    ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    ranges[1].NumDescriptors = 1;
    ranges[1].BaseShaderRegister = 0;
    ranges[1].OffsetInDescriptorsFromTableStart = 3;
    D3D12_ROOT_PARAMETER parameter = {};
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameter.DescriptorTable.NumDescriptorRanges = 2;
    parameter.DescriptorTable.pDescriptorRanges = ranges;
    D3D12_ROOT_SIGNATURE_DESC desc = {};
    desc.NumParameters = 1;
    desc.pParameters = &parameter;
    root_signature_ = context_.CreateRootSignature(desc);
    ASSERT_TRUE(root_signature_);

    const D3D12_SHADER_BYTECODE bytecode = {
        shader_.bytecode->GetBufferPointer(), shader_.bytecode->GetBufferSize()};
    pipeline_ =
        context_.CreateComputePipeline(root_signature_.get(), bytecode);
    ASSERT_TRUE(pipeline_);
  }

  void WriteViews(ID3D12DescriptorHeap *heap, UINT base,
                  ID3D12Resource *input, ID3D12Resource *output) {
    D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
    srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Format = DXGI_FORMAT_R32_UINT;
    srv.Buffer.FirstElement = 1;
    srv.Buffer.NumElements = 3;
    context_.device()->CreateShaderResourceView(
        input, &srv, context_.CpuDescriptorHandle(heap, base));

    srv.Format = DXGI_FORMAT_R32_TYPELESS;
    srv.Buffer.FirstElement = 5;
    srv.Buffer.NumElements = 2;
    srv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
    context_.device()->CreateShaderResourceView(
        input, &srv, context_.CpuDescriptorHandle(heap, base + 1));

    srv.Format = DXGI_FORMAT_UNKNOWN;
    srv.Buffer.FirstElement = 10;
    srv.Buffer.NumElements = 2;
    srv.Buffer.StructureByteStride = sizeof(UINT);
    srv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
    context_.device()->CreateShaderResourceView(
        input, &srv, context_.CpuDescriptorHandle(heap, base + 2));

    D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
    uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uav.Format = DXGI_FORMAT_UNKNOWN;
    uav.Buffer.FirstElement = 2;
    uav.Buffer.NumElements = 3;
    uav.Buffer.StructureByteStride = sizeof(UINT);
    context_.device()->CreateUnorderedAccessView(
        output, nullptr, &uav,
        context_.CpuDescriptorHandle(heap, base + 3));
  }

  void RunCase(DescriptorPublication publication) {
    constexpr UINT kDwordCount = 32;
    constexpr UINT kTableBase = 1;
    std::array<UINT, kDwordCount> input_data = {};
    for (UINT i = 0; i < input_data.size(); ++i)
      input_data[i] = 0x10000000u + i * 0x01010101u;
    std::array<UINT, kDwordCount> zero_data = {};
    auto input_upload = context_.CreateUploadBuffer(
        sizeof(input_data), input_data.data(), sizeof(input_data));
    auto output_upload = context_.CreateUploadBuffer(
        sizeof(zero_data), zero_data.data(), sizeof(zero_data));
    auto input = context_.CreateBuffer(
        sizeof(input_data), D3D12_HEAP_TYPE_DEFAULT,
        D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
    auto output = context_.CreateBuffer(
        sizeof(zero_data), D3D12_HEAP_TYPE_DEFAULT,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_DEST);
    auto gpu_heap = context_.CreateDescriptorHeap(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 6, true);
    ASSERT_TRUE(input_upload);
    ASSERT_TRUE(output_upload);
    ASSERT_TRUE(input);
    ASSERT_TRUE(output);
    ASSERT_TRUE(gpu_heap);

    ComPtr<ID3D12Resource> decoy_input;
    ComPtr<ID3D12Resource> decoy_output;
    if (publication == DescriptorPublication::Overwritten) {
      decoy_input = context_.CreateBuffer(
          sizeof(input_data), D3D12_HEAP_TYPE_DEFAULT,
          D3D12_RESOURCE_FLAG_NONE,
          D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
      decoy_output = context_.CreateBuffer(
          sizeof(zero_data), D3D12_HEAP_TYPE_DEFAULT,
          D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
          D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
      ASSERT_TRUE(decoy_input);
      ASSERT_TRUE(decoy_output);
      WriteViews(gpu_heap.get(), kTableBase, decoy_input.get(),
                 decoy_output.get());
    }

    if (publication == DescriptorPublication::Copied) {
      auto cpu_heap = context_.CreateDescriptorHeap(
          D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 4, false);
      ASSERT_TRUE(cpu_heap);
      WriteViews(cpu_heap.get(), 0, input.get(), output.get());
      context_.device()->CopyDescriptorsSimple(
          4, context_.CpuDescriptorHandle(gpu_heap.get(), kTableBase),
          cpu_heap->GetCPUDescriptorHandleForHeapStart(),
          D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    } else {
      WriteViews(gpu_heap.get(), kTableBase, input.get(), output.get());
    }

    context_.list()->CopyBufferRegion(input.get(), 0, input_upload.get(), 0,
                                      sizeof(input_data));
    context_.list()->CopyBufferRegion(output.get(), 0, output_upload.get(), 0,
                                      sizeof(zero_data));
    D3D12TestContext::Transition(
        context_.list(), input.get(), D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    D3D12TestContext::Transition(
        context_.list(), output.get(), D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    ID3D12DescriptorHeap *heaps[] = {gpu_heap.get()};
    context_.list()->SetDescriptorHeaps(1, heaps);
    context_.list()->SetComputeRootSignature(root_signature_.get());
    context_.list()->SetPipelineState(pipeline_.get());
    context_.list()->SetComputeRootDescriptorTable(
        0, context_.GpuDescriptorHandle(gpu_heap.get(), kTableBase));
    context_.list()->Dispatch(1, 1, 1);
    D3D12TestContext::UavBarrier(context_.list(), output.get());
    D3D12TestContext::Transition(
        context_.list(), output.get(),
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_SOURCE);

    std::vector<std::uint8_t> output_bytes;
    ASSERT_EQ(context_.ReadbackBuffer(output.get(), sizeof(zero_data),
                                      &output_bytes),
              S_OK);
    ASSERT_EQ(output_bytes.size(), sizeof(zero_data));
    std::array<UINT, kDwordCount> actual = {};
    std::memcpy(actual.data(), output_bytes.data(), output_bytes.size());
    std::array<UINT, kDwordCount> expected = {};
    expected[2] = input_data[3];
    expected[3] = input_data[6];
    expected[4] = input_data[11];
    EXPECT_EQ(actual, expected);
  }

  D3D12TestContext context_;
  dxmt::test::ShaderCompilation shader_;
  ComPtr<ID3D12RootSignature> root_signature_;
  ComPtr<ID3D12PipelineState> pipeline_;
};

TEST_F(BufferViewSpec, ExecutesTypedRawAndStructuredInputViews) {
  RunCase(DescriptorPublication::Direct);
}

TEST_F(BufferViewSpec, CopiesMixedInputViewShapesAtNonzeroHeapOffset) {
  RunCase(DescriptorPublication::Copied);
}

TEST_F(BufferViewSpec, OverwritesAllInputViewShapesBeforeExecution) {
  RunCase(DescriptorPublication::Overwritten);
}

} // namespace
