#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"

#include <cstdint>
#include <cstring>
#include <vector>

namespace {

using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

class RootSignatureVolatilitySpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    D3D12_FEATURE_DATA_ROOT_SIGNATURE feature = {};
    feature.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
    ASSERT_EQ(context_.device()->CheckFeatureSupport(
                  D3D12_FEATURE_ROOT_SIGNATURE, &feature, sizeof(feature)),
              S_OK);
    ASSERT_EQ(feature.HighestVersion, D3D_ROOT_SIGNATURE_VERSION_1_1);

    shader_ = CompileShader(R"(
      Buffer<uint> input : register(t0);
      RWByteAddressBuffer output : register(u0);
      [numthreads(1, 1, 1)]
      void main() { output.Store(0, input[0]); }
    )",
                            "cs_5_0");
    ASSERT_EQ(shader_.result, S_OK) << shader_.diagnostic_text();
  }

  ComPtr<ID3D12RootSignature>
  CreateSignature(D3D12_DESCRIPTOR_RANGE_FLAGS range_flags,
                  D3D12_ROOT_DESCRIPTOR_FLAGS root_flags) {
    D3D12_DESCRIPTOR_RANGE1 range = {};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 1;
    range.Flags = range_flags;
    D3D12_ROOT_PARAMETER1 parameters[2] = {};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[0].DescriptorTable.NumDescriptorRanges = 1;
    parameters[0].DescriptorTable.pDescriptorRanges = &range;
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    parameters[1].Descriptor.Flags = root_flags;
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC desc = {};
    desc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    desc.Desc_1_1.NumParameters = 2;
    desc.Desc_1_1.pParameters = parameters;
    ComPtr<ID3DBlob> blob;
    ComPtr<ID3DBlob> error;
    EXPECT_EQ(
        D3D12SerializeVersionedRootSignature(&desc, blob.put(), error.put()),
        S_OK)
        << (error ? static_cast<const char *>(error->GetBufferPointer()) : "");
    if (!blob)
      return {};
    ComPtr<ID3D12RootSignature> signature;
    EXPECT_EQ(context_.device()->CreateRootSignature(
                  0, blob->GetBufferPointer(), blob->GetBufferSize(),
                  IID_PPV_ARGS(signature.put())),
              S_OK);
    return signature;
  }

  std::uint32_t Execute(D3D12_DESCRIPTOR_RANGE_FLAGS range_flags,
                        D3D12_ROOT_DESCRIPTOR_FLAGS root_flags,
                        bool overwrite_descriptor) {
    auto signature = CreateSignature(range_flags, root_flags);
    EXPECT_TRUE(signature);
    const D3D12_SHADER_BYTECODE bytecode = {
        shader_.bytecode->GetBufferPointer(),
        shader_.bytecode->GetBufferSize()};
    auto pipeline = context_.CreateComputePipeline(signature.get(), bytecode);
    EXPECT_TRUE(pipeline);

    constexpr std::uint32_t first_value = 0x13579bdf;
    constexpr std::uint32_t replacement_value = 0x2468ace0;
    auto first = context_.CreateUploadBuffer(sizeof(first_value), &first_value,
                                             sizeof(first_value));
    auto replacement = context_.CreateUploadBuffer(sizeof(replacement_value),
                                                   &replacement_value,
                                                   sizeof(replacement_value));
    auto output =
        context_.CreateBuffer(sizeof(first_value), D3D12_HEAP_TYPE_DEFAULT,
                              D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                              D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    auto heap = context_.CreateDescriptorHeap(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true);
    EXPECT_TRUE(first);
    EXPECT_TRUE(replacement);
    EXPECT_TRUE(output);
    EXPECT_TRUE(heap);

    D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
    srv.Format = DXGI_FORMAT_R32_UINT;
    srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Buffer.NumElements = 1;
    const auto cpu = heap->GetCPUDescriptorHandleForHeapStart();
    context_.device()->CreateShaderResourceView(first.get(), &srv, cpu);

    ID3D12DescriptorHeap *heaps[] = {heap.get()};
    context_.list()->SetDescriptorHeaps(1, heaps);
    context_.list()->SetPipelineState(pipeline.get());
    context_.list()->SetComputeRootSignature(signature.get());
    context_.list()->SetComputeRootDescriptorTable(
        0, heap->GetGPUDescriptorHandleForHeapStart());
    context_.list()->SetComputeRootUnorderedAccessView(
        1, output->GetGPUVirtualAddress());
    context_.list()->Dispatch(1, 1, 1);
    if (overwrite_descriptor)
      context_.device()->CreateShaderResourceView(replacement.get(), &srv, cpu);
    D3D12TestContext::Transition(context_.list(), output.get(),
                                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                 D3D12_RESOURCE_STATE_COPY_SOURCE);
    std::vector<std::uint8_t> bytes;
    EXPECT_EQ(
        context_.ReadbackBuffer(output.get(), sizeof(first_value), &bytes),
        S_OK);
    EXPECT_EQ(context_.ResetCommandList(), S_OK);
    if (bytes.size() != sizeof(first_value))
      return 0;
    std::uint32_t actual = 0;
    std::memcpy(&actual, bytes.data(), sizeof(actual));
    return actual;
  }

  D3D12TestContext context_;
  dxmt::test::ShaderCompilation shader_;
};

TEST_F(RootSignatureVolatilitySpec,
       VolatileDescriptorsObserveOverwriteBeforeExecution) {
  const auto range_flags = static_cast<D3D12_DESCRIPTOR_RANGE_FLAGS>(
      D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE |
      D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
  EXPECT_EQ(
      Execute(range_flags, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE, true),
      0x2468ace0u);
}

TEST_F(RootSignatureVolatilitySpec, DescriptorDataFlagsExecuteCorrectly) {
  constexpr D3D12_DESCRIPTOR_RANGE_FLAGS flags[] = {
      D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE,
      D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE,
      D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC,
  };
  for (const auto flag : flags) {
    SCOPED_TRACE(::testing::Message() << "range flag=" << UINT(flag));
    EXPECT_EQ(Execute(flag, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE, false),
              0x13579bdfu);
  }
}

TEST_F(RootSignatureVolatilitySpec, RootDescriptorDataFlagsExecuteCorrectly) {
  constexpr D3D12_ROOT_DESCRIPTOR_FLAGS flags[] = {
      D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE,
      D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE,
      D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC,
  };
  const auto range_flags = static_cast<D3D12_DESCRIPTOR_RANGE_FLAGS>(
      D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE |
      D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
  for (const auto flag : flags) {
    SCOPED_TRACE(::testing::Message() << "root flag=" << UINT(flag));
    EXPECT_EQ(Execute(range_flags, flag, false), 0x13579bdfu);
  }
}

} // namespace
