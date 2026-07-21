#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

namespace {

using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

class PipelineLibrarySpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);

    D3D12_FEATURE_DATA_SHADER_CACHE feature = {};
    ASSERT_EQ(context_.device()->CheckFeatureSupport(
                  D3D12_FEATURE_SHADER_CACHE, &feature, sizeof(feature)),
              S_OK);
    if (!(feature.SupportFlags & D3D12_SHADER_CACHE_SUPPORT_LIBRARY))
      GTEST_SKIP() << "Pipeline libraries are not supported";

    ASSERT_EQ(context_.device()->QueryInterface(IID_PPV_ARGS(device1_.put())),
              S_OK);
    ASSERT_TRUE(device1_);

    auto shader = CompileShader(R"(
      RWByteAddressBuffer output : register(u0);
      [numthreads(1, 1, 1)]
      void main() { output.Store(0, 0x12345678u); }
    )",
                                      "cs_5_0");
    ASSERT_EQ(shader.result, S_OK) << shader.diagnostic_text();
    shader_ = std::move(shader.bytecode);

    D3D12_ROOT_PARAMETER parameter = {};
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    D3D12_ROOT_SIGNATURE_DESC root_desc = {};
    root_desc.NumParameters = 1;
    root_desc.pParameters = &parameter;
    root_signature_ = context_.CreateRootSignature(root_desc);
    ASSERT_TRUE(root_signature_);

    pipeline_desc_.pRootSignature = root_signature_.get();
    pipeline_desc_.CS = {shader_->GetBufferPointer(), shader_->GetBufferSize()};
    ASSERT_EQ(context_.device()->CreateComputePipelineState(
                  &pipeline_desc_, IID_PPV_ARGS(pipeline_.put())),
              S_OK);
    ASSERT_TRUE(pipeline_);
  }

  ComPtr<ID3D12PipelineLibrary> CreateStoredLibrary() {
    ComPtr<ID3D12PipelineLibrary> library;
    EXPECT_EQ(device1_->CreatePipelineLibrary(
                  nullptr, 0, IID_PPV_ARGS(library.put())),
              S_OK);
    if (!library)
      return {};
    EXPECT_EQ(library->StorePipeline(L"compute", pipeline_.get()), S_OK);
    return library;
  }

  std::vector<std::uint8_t> Serialize(ID3D12PipelineLibrary *library) {
    const SIZE_T size = library->GetSerializedSize();
    EXPECT_GT(size, 0u);
    if (!size)
      return {};
    std::vector<std::uint8_t> data(size);
    EXPECT_EQ(library->Serialize(data.data(), data.size()), S_OK);
    return data;
  }

  void ExpectPipelinesExecuteIdentically(ID3D12PipelineState *cold,
                                         ID3D12PipelineState *warm) {
    constexpr UINT64 word_size = sizeof(std::uint32_t);
    auto cold_output = context_.CreateBuffer(
        word_size, D3D12_HEAP_TYPE_DEFAULT,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    auto warm_output = context_.CreateBuffer(
        word_size, D3D12_HEAP_TYPE_DEFAULT,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    auto readback = context_.CreateBuffer(
        2 * word_size, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE,
        D3D12_RESOURCE_STATE_COPY_DEST);
    ASSERT_TRUE(cold_output);
    ASSERT_TRUE(warm_output);
    ASSERT_TRUE(readback);

    context_.list()->SetComputeRootSignature(root_signature_.get());
    context_.list()->SetPipelineState(cold);
    context_.list()->SetComputeRootUnorderedAccessView(
        0, cold_output->GetGPUVirtualAddress());
    context_.list()->Dispatch(1, 1, 1);
    context_.list()->SetPipelineState(warm);
    context_.list()->SetComputeRootUnorderedAccessView(
        0, warm_output->GetGPUVirtualAddress());
    context_.list()->Dispatch(1, 1, 1);

    D3D12TestContext::Transition(
        context_.list(), cold_output.get(),
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12TestContext::Transition(
        context_.list(), warm_output.get(),
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_SOURCE);
    context_.list()->CopyBufferRegion(readback.get(), 0, cold_output.get(), 0,
                                      word_size);
    context_.list()->CopyBufferRegion(readback.get(), word_size,
                                      warm_output.get(), 0, word_size);
    ASSERT_EQ(context_.ExecuteAndWait(), S_OK);

    void *mapped = nullptr;
    const D3D12_RANGE read_range = {0, 2 * word_size};
    ASSERT_EQ(readback->Map(0, &read_range, &mapped), S_OK);
    std::array<std::uint32_t, 2> actual = {};
    std::memcpy(actual.data(), mapped, sizeof(actual));
    const D3D12_RANGE written_range = {0, 0};
    readback->Unmap(0, &written_range);
    EXPECT_EQ(actual[0], 0x12345678u);
    EXPECT_EQ(actual[1], actual[0]);
  }

  D3D12TestContext context_;
  ComPtr<ID3D12Device1> device1_;
  ComPtr<ID3DBlob> shader_;
  ComPtr<ID3D12RootSignature> root_signature_;
  ComPtr<ID3D12PipelineState> pipeline_;
  D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline_desc_ = {};
};

TEST_F(PipelineLibrarySpec, ColdAndWarmPipelinesExecuteIdentically) {
  auto cold_library = CreateStoredLibrary();
  ASSERT_TRUE(cold_library);
  const auto serialized = Serialize(cold_library.get());
  ASSERT_FALSE(serialized.empty());

  ComPtr<ID3D12PipelineLibrary> warm_library;
  ASSERT_EQ(device1_->CreatePipelineLibrary(
                serialized.data(), serialized.size(),
                IID_PPV_ARGS(warm_library.put())),
            S_OK);
  ASSERT_TRUE(warm_library);
  ComPtr<ID3D12PipelineState> warm_pipeline;
  ASSERT_EQ(warm_library->LoadComputePipeline(
                L"compute", &pipeline_desc_, IID_PPV_ARGS(warm_pipeline.put())),
            S_OK);
  ASSERT_TRUE(warm_pipeline);

  ExpectPipelinesExecuteIdentically(pipeline_.get(), warm_pipeline.get());
}

TEST_F(PipelineLibrarySpec, CorruptedSerializationIsRejected) {
  auto library = CreateStoredLibrary();
  ASSERT_TRUE(library);
  auto serialized = Serialize(library.get());
  ASSERT_FALSE(serialized.empty());
  serialized.front() ^= 0xff;

  ComPtr<ID3D12PipelineLibrary> corrupted;
  EXPECT_TRUE(FAILED(device1_->CreatePipelineLibrary(
      serialized.data(), serialized.size(), IID_PPV_ARGS(corrupted.put()))));
  EXPECT_FALSE(corrupted);
}

} // namespace
