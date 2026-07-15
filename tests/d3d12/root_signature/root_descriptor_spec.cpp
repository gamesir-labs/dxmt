#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"

#include <cstring>
#include <vector>

namespace {

using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

class RootDescriptorSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    shader_ = CompileShader(R"(
      ByteAddressBuffer input : register(t0);
      RWByteAddressBuffer output : register(u0);
      cbuffer Parameters : register(b0) {
        uint addend;
        uint byte_offset;
      };

      [numthreads(1, 1, 1)]
      void main() {
        output.Store(byte_offset, input.Load(byte_offset) + addend);
      }
    )",
                            "cs_5_0");
    ASSERT_EQ(shader_.result, S_OK) << shader_.diagnostic_text();

    D3D12_ROOT_PARAMETER parameters[3] = {};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    parameters[0].Descriptor.ShaderRegister = 0;
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    parameters[1].Descriptor.ShaderRegister = 0;
    parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameters[2].Descriptor.ShaderRegister = 0;
    D3D12_ROOT_SIGNATURE_DESC desc = {};
    desc.NumParameters = 3;
    desc.pParameters = parameters;
    root_signature_ = context_.CreateRootSignature(desc);
    ASSERT_TRUE(root_signature_);

    const D3D12_SHADER_BYTECODE bytecode = {
        shader_.bytecode->GetBufferPointer(), shader_.bytecode->GetBufferSize()};
    pipeline_ =
        context_.CreateComputePipeline(root_signature_.get(), bytecode);
    ASSERT_TRUE(pipeline_);
  }

  void RunCaseWithResources(ID3D12Resource *input, ID3D12Resource *output,
                            UINT64 input_base, UINT64 output_base,
                            UINT64 cbv_base, UINT byte_offset) {
    constexpr UINT64 kBufferSize = 4096;
    constexpr UINT64 kCbvSize = 512;
    constexpr UINT kInputValue = 0x13579bdf;
    constexpr UINT kAddend = 0x10203040;
    const UINT64 input_offset = input_base + byte_offset;
    const UINT64 output_offset = output_base + byte_offset;
    ASSERT_LE(input_offset + sizeof(UINT), kBufferSize);
    ASSERT_LE(output_offset + sizeof(UINT), kBufferSize);
    ASSERT_LE(cbv_base + 2 * sizeof(UINT), kCbvSize);

    std::vector<std::uint8_t> input_bytes(kBufferSize);
    std::memcpy(input_bytes.data() + input_offset, &kInputValue,
                sizeof(kInputValue));
    std::vector<std::uint8_t> cbv_bytes(kCbvSize);
    const UINT parameters[] = {kAddend, byte_offset};
    std::memcpy(cbv_bytes.data() + cbv_base, parameters, sizeof(parameters));
    std::vector<std::uint8_t> zero_bytes(kBufferSize);
    auto input_upload = context_.CreateUploadBuffer(
        kBufferSize, input_bytes.data(), input_bytes.size());
    auto output_upload = context_.CreateUploadBuffer(
        kBufferSize, zero_bytes.data(), zero_bytes.size());
    auto constants = context_.CreateUploadBuffer(
        kCbvSize, cbv_bytes.data(), cbv_bytes.size());
    ASSERT_TRUE(input_upload);
    ASSERT_TRUE(output_upload);
    ASSERT_TRUE(constants);

    context_.list()->CopyBufferRegion(input, 0, input_upload.get(), 0,
                                      kBufferSize);
    context_.list()->CopyBufferRegion(output, 0, output_upload.get(), 0,
                                      kBufferSize);
    D3D12TestContext::Transition(
        context_.list(), input, D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    D3D12TestContext::Transition(
        context_.list(), output, D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    context_.list()->SetComputeRootSignature(root_signature_.get());
    context_.list()->SetPipelineState(pipeline_.get());
    context_.list()->SetComputeRootShaderResourceView(
        0, input->GetGPUVirtualAddress() + input_base);
    context_.list()->SetComputeRootUnorderedAccessView(
        1, output->GetGPUVirtualAddress() + output_base);
    context_.list()->SetComputeRootConstantBufferView(
        2, constants->GetGPUVirtualAddress() + cbv_base);
    context_.list()->Dispatch(1, 1, 1);
    D3D12TestContext::Transition(
        context_.list(), output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_SOURCE);

    std::vector<std::uint8_t> actual;
    ASSERT_EQ(context_.ReadbackBuffer(output, kBufferSize, &actual), S_OK);
    ASSERT_EQ(actual.size(), kBufferSize);
    for (UINT64 offset = 0; offset < kBufferSize; offset += sizeof(UINT)) {
      UINT value = 0;
      std::memcpy(&value, actual.data() + offset, sizeof(value));
      EXPECT_EQ(value, offset == output_offset ? kInputValue + kAddend : 0u)
          << "dword " << offset / sizeof(UINT);
    }
  }

  void RunCase(UINT64 input_base, UINT64 output_base, UINT64 cbv_base,
               UINT byte_offset) {
    constexpr UINT64 kBufferSize = 4096;
    auto input = context_.CreateBuffer(
        kBufferSize, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
        D3D12_RESOURCE_STATE_COPY_DEST);
    auto output = context_.CreateBuffer(
        kBufferSize, D3D12_HEAP_TYPE_DEFAULT,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_DEST);
    ASSERT_TRUE(input);
    ASSERT_TRUE(output);
    RunCaseWithResources(input.get(), output.get(), input_base, output_base,
                         cbv_base, byte_offset);
  }

  D3D12TestContext context_;
  dxmt::test::ShaderCompilation shader_;
  ComPtr<ID3D12RootSignature> root_signature_;
  ComPtr<ID3D12PipelineState> pipeline_;
};

TEST_F(RootDescriptorSpec, BindsCbvSrvAndUavAtBaseAddresses) {
  RunCase(0, 0, 0, 0);
}

TEST_F(RootDescriptorSpec, BindsCbvSrvAndUavAtNonzeroGpuVaOffsets) {
  RunCase(128, 192, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, 0);
}

TEST_F(RootDescriptorSpec, AccessesLastValidDword) {
  constexpr UINT64 descriptor_base = 4096 - 16;
  RunCase(descriptor_base, descriptor_base,
          D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, 12);
}

TEST_F(RootDescriptorSpec, BindsPlacedBufferGpuVirtualAddresses) {
  constexpr UINT64 kBufferSize = 4096;
  auto buffer_desc = [](D3D12_RESOURCE_FLAGS flags) {
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = kBufferSize;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = flags;
    return desc;
  };
  const auto input_desc = buffer_desc(D3D12_RESOURCE_FLAG_NONE);
  const auto output_desc =
      buffer_desc(D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
  const auto input_info =
      context_.device()->GetResourceAllocationInfo(0, 1, &input_desc);
  const auto output_info =
      context_.device()->GetResourceAllocationInfo(0, 1, &output_desc);
  ASSERT_NE(input_info.SizeInBytes, UINT64_MAX);
  ASSERT_NE(output_info.SizeInBytes, UINT64_MAX);
  const UINT64 output_placement =
      (input_info.SizeInBytes + output_info.Alignment - 1) &
      ~(output_info.Alignment - 1);

  D3D12_HEAP_DESC heap_desc = {};
  heap_desc.SizeInBytes = output_placement + output_info.SizeInBytes;
  heap_desc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
  heap_desc.Properties.CreationNodeMask = 1;
  heap_desc.Properties.VisibleNodeMask = 1;
  heap_desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
  heap_desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
  ComPtr<ID3D12Heap> heap;
  ASSERT_EQ(context_.device()->CreateHeap(
                &heap_desc, __uuidof(ID3D12Heap),
                reinterpret_cast<void **>(heap.put())),
            S_OK);

  ComPtr<ID3D12Resource> input;
  ComPtr<ID3D12Resource> output;
  ASSERT_EQ(context_.device()->CreatePlacedResource(
                heap.get(), 0, &input_desc, D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr, __uuidof(ID3D12Resource),
                reinterpret_cast<void **>(input.put())),
            S_OK);
  ASSERT_EQ(context_.device()->CreatePlacedResource(
                heap.get(), output_placement, &output_desc,
                D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                __uuidof(ID3D12Resource),
                reinterpret_cast<void **>(output.put())),
            S_OK);

  RunCaseWithResources(
      input.get(), output.get(), 128, 192,
      D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, 0);
}

} // namespace
