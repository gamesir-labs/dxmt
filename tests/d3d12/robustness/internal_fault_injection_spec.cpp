#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

UINT ConfiguredInternalOccurrence(const char *name) {
  const char *value = std::getenv(name);
  if (!value || !*value)
    return 0;
  char *end = nullptr;
  const auto parsed = std::strtoul(value, &end, 0);
  return end != value && !*end && parsed <= UINT_MAX
             ? static_cast<UINT>(parsed)
             : 0;
}

class FaultMarker {
public:
  ~FaultMarker() {
    SetEnvironmentVariableA("DXMT_TEST_FAULT_MARKER", nullptr);
    if (!path_.empty())
      DeleteFileA(path_.c_str());
  }

  bool Initialize() {
    char temporary_path[MAX_PATH + 1] = {};
    char marker_path[MAX_PATH + 1] = {};
    const DWORD length = GetTempPathA(ARRAYSIZE(temporary_path), temporary_path);
    if (!length || length >= ARRAYSIZE(temporary_path) ||
        !GetTempFileNameA(temporary_path, "dxf", 0, marker_path))
      return false;
    path_ = marker_path;
    return !!SetEnvironmentVariableA("DXMT_TEST_FAULT_MARKER", path_.c_str());
  }

  std::size_t Count(std::string_view fault_name) const {
    std::ifstream input(path_);
    std::size_t count = 0;
    std::string line;
    while (std::getline(input, line)) {
      if (line == fault_name)
        ++count;
    }
    return count;
  }

private:
  std::string path_;
};

class D3D12InternalFaultInjectionSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    D3D12_ROOT_PARAMETER parameter = {};
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    D3D12_ROOT_SIGNATURE_DESC desc = {};
    desc.NumParameters = 1;
    desc.pParameters = &parameter;
    root_ = context_.CreateRootSignature(desc);
    ASSERT_TRUE(root_);
  }

  ComPtr<ID3D12PipelineState> CreateWriterPipeline(UINT value) {
    const std::string source =
        "RWByteAddressBuffer output : register(u0);"
        "[numthreads(1,1,1)] void main() { output.Store(0," +
        std::to_string(value) + "u); }";
    const auto shader = CompileShader(source, "cs_5_0");
    EXPECT_EQ(shader.result, S_OK) << shader.diagnostic_text();
    if (FAILED(shader.result))
      return {};
    return context_.CreateComputePipeline(
        root_.get(), {shader.bytecode->GetBufferPointer(),
                      shader.bytecode->GetBufferSize()});
  }

  UINT ExecuteWriter(ID3D12PipelineState *pipeline) {
    const UINT zero = 0;
    auto upload = context_.CreateUploadBuffer(sizeof(zero), &zero, sizeof(zero));
    auto output = context_.CreateBuffer(
        sizeof(zero), D3D12_HEAP_TYPE_DEFAULT,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_DEST);
    EXPECT_TRUE(upload);
    EXPECT_TRUE(output);
    if (!upload || !output)
      return UINT_MAX;
    context_.list()->CopyBufferRegion(output.get(), 0, upload.get(), 0,
                                       sizeof(zero));
    D3D12TestContext::Transition(
        context_.list(), output.get(), D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    context_.list()->SetComputeRootSignature(root_.get());
    context_.list()->SetPipelineState(pipeline);
    context_.list()->SetComputeRootUnorderedAccessView(
        0, output->GetGPUVirtualAddress());
    context_.list()->Dispatch(1, 1, 1);
    D3D12TestContext::Transition(
        context_.list(), output.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_SOURCE);
    std::vector<std::uint8_t> bytes;
    EXPECT_EQ(context_.ReadbackBuffer(output.get(), sizeof(UINT), &bytes), S_OK);
    UINT value = UINT_MAX;
    if (bytes.size() == sizeof(value))
      std::memcpy(&value, bytes.data(), sizeof(value));
    EXPECT_EQ(context_.ResetCommandList(), S_OK);
    return value;
  }

  D3D12TestContext context_;
  ComPtr<ID3D12RootSignature> root_;
};

TEST_F(D3D12InternalFaultInjectionSpec,
       AirInitializationFailureClearsOutputAndRecovers) {
  const UINT target =
      ConfiguredInternalOccurrence("DXMT_TEST_FAIL_AIR_INITIALIZATION_AT");
  if (!target)
    GTEST_SKIP() << "AIR initialization fault injection is disabled";
  FaultMarker marker;
  ASSERT_TRUE(marker.Initialize());

  const auto shader =
      CompileShader("[numthreads(1,1,1)] void main() {}", "cs_5_0");
  ASSERT_EQ(shader.result, S_OK) << shader.diagnostic_text();
  D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
  desc.pRootSignature = root_.get();
  desc.CS = {shader.bytecode->GetBufferPointer(),
             shader.bytecode->GetBufferSize()};
  for (UINT occurrence = 1; occurrence <= target + 1; ++occurrence) {
    void *pipeline = reinterpret_cast<void *>(static_cast<uintptr_t>(1));
    const HRESULT hr = context_.device()->CreateComputePipelineState(
        &desc, __uuidof(ID3D12PipelineState), &pipeline);
    if (occurrence == target) {
      EXPECT_EQ(hr, E_OUTOFMEMORY);
      EXPECT_EQ(pipeline, nullptr);
    } else {
      EXPECT_EQ(hr, S_OK);
      ASSERT_NE(pipeline, nullptr);
      static_cast<ID3D12PipelineState *>(pipeline)->Release();
    }
  }
  EXPECT_EQ(marker.Count("DXMT_TEST_FAIL_AIR_INITIALIZATION_AT"), 1u);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D12InternalFaultInjectionSpec,
       MetalPipelineFailureAndLaterPipelineRecovers) {
  const char *configuration =
      std::getenv("DXMT_TEST_FAIL_METAL_COMPUTE_PIPELINE_AT");
  if (!configuration || std::string_view(configuration) != "always")
    GTEST_SKIP() << "Metal pipeline fault injection must be set to always";
  FaultMarker marker;
  ASSERT_TRUE(marker.Initialize());

  auto failed_pipeline = CreateWriterPipeline(0x11112222u);
  EXPECT_FALSE(failed_pipeline);

  ASSERT_TRUE(SetEnvironmentVariableA(
      "DXMT_TEST_FAIL_METAL_COMPUTE_PIPELINE_AT", nullptr));

  auto recovery_pipeline = CreateWriterPipeline(0x33334444u);
  ASSERT_TRUE(recovery_pipeline);
  EXPECT_EQ(ExecuteWriter(recovery_pipeline.get()), 0x33334444u);
  EXPECT_GE(marker.Count("DXMT_TEST_FAIL_METAL_COMPUTE_PIPELINE_AT"), 1u);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D12InternalFaultInjectionSpec,
       PipelineArchiveLookupFaultFallsBackAndExecutionContinues) {
  if (!ConfiguredInternalOccurrence("DXMT_TEST_FAIL_PSO_ARCHIVE_LOOKUP_AT"))
    GTEST_SKIP() << "pipeline archive lookup fault injection is disabled";
  FaultMarker marker;
  ASSERT_TRUE(marker.Initialize());

  auto first = CreateWriterPipeline(0x51525354u);
  ASSERT_TRUE(first);
  EXPECT_EQ(ExecuteWriter(first.get()), 0x51525354u);
  auto second = CreateWriterPipeline(0x61626364u);
  ASSERT_TRUE(second);
  EXPECT_EQ(ExecuteWriter(second.get()), 0x61626364u);
  EXPECT_EQ(marker.Count("DXMT_TEST_FAIL_PSO_ARCHIVE_LOOKUP_AT"), 1u);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

class D3D12DescriptorInternalFaultInjectionSpec : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  D3D12TestContext context_;
};

TEST_F(D3D12DescriptorInternalFaultInjectionSpec,
       DescriptorFailureFallsBackAndLaterWritesExecute) {
  const UINT allocation_target = ConfiguredInternalOccurrence(
      "DXMT_TEST_FAIL_DESCRIPTOR_TABLE_ALLOCATION_AT");
  const UINT residency_target = ConfiguredInternalOccurrence(
      "DXMT_TEST_FAIL_RESIDENCY_INSERTION_AT");
  ASSERT_FALSE(allocation_target && residency_target)
      << "configure only one descriptor fault at a time";
  const UINT target = std::max(allocation_target, residency_target);
  if (!target)
    GTEST_SKIP() << "descriptor internal fault injection is disabled";
  FaultMarker marker;
  ASSERT_TRUE(marker.Initialize());

  constexpr UINT input_value = 0x718293a4u;
  const UINT descriptor_count = target + 1;
  auto input = context_.CreateUploadBuffer(
      sizeof(input_value), &input_value, sizeof(input_value));
  auto heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, descriptor_count, true);
  ASSERT_TRUE(input);
  ASSERT_TRUE(heap);

  D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
  srv.Format = DXGI_FORMAT_R32_TYPELESS;
  srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
  srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv.Buffer.NumElements = 1;
  srv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
  for (UINT index = 0; index < descriptor_count; ++index) {
    context_.device()->CreateShaderResourceView(
        input.get(), &srv, context_.CpuDescriptorHandle(heap.get(), index));
  }

  D3D12_DESCRIPTOR_RANGE range = {};
  range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  range.NumDescriptors = 1;
  D3D12_ROOT_PARAMETER parameters[2] = {};
  parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[0].DescriptorTable.NumDescriptorRanges = 1;
  parameters[0].DescriptorTable.pDescriptorRanges = &range;
  parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  root_desc.NumParameters = 2;
  root_desc.pParameters = parameters;
  auto root = context_.CreateRootSignature(root_desc);
  const auto shader = CompileShader(R"(
    ByteAddressBuffer input : register(t0);
    RWByteAddressBuffer output : register(u0);
    [numthreads(1, 1, 1)]
    void main() { output.Store(0, input.Load(0)); }
  )", "cs_5_0");
  ASSERT_EQ(shader.result, S_OK) << shader.diagnostic_text();
  auto pipeline = context_.CreateComputePipeline(
      root.get(), {shader.bytecode->GetBufferPointer(),
                   shader.bytecode->GetBufferSize()});
  ASSERT_TRUE(root);
  ASSERT_TRUE(pipeline);

  std::vector<UINT> initial(descriptor_count, 0);
  auto initial_upload = context_.CreateUploadBuffer(
      initial.size() * sizeof(UINT), initial.data(),
      initial.size() * sizeof(UINT));
  auto output = context_.CreateBuffer(
      initial.size() * sizeof(UINT), D3D12_HEAP_TYPE_DEFAULT,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(initial_upload);
  ASSERT_TRUE(output);
  context_.list()->CopyBufferRegion(
      output.get(), 0, initial_upload.get(), 0,
      initial.size() * sizeof(UINT));
  D3D12TestContext::Transition(
      context_.list(), output.get(), D3D12_RESOURCE_STATE_COPY_DEST,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  ID3D12DescriptorHeap *heaps[] = {heap.get()};
  context_.list()->SetDescriptorHeaps(1, heaps);
  context_.list()->SetComputeRootSignature(root.get());
  context_.list()->SetPipelineState(pipeline.get());
  for (UINT index = 0; index < descriptor_count; ++index) {
    context_.list()->SetComputeRootDescriptorTable(
        0, context_.GpuDescriptorHandle(heap.get(), index));
    context_.list()->SetComputeRootUnorderedAccessView(
        1, output->GetGPUVirtualAddress() + index * sizeof(UINT));
    context_.list()->Dispatch(1, 1, 1);
  }
  D3D12TestContext::Transition(
      context_.list(), output.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_COPY_SOURCE);
  std::vector<std::uint8_t> bytes;
  ASSERT_EQ(context_.ReadbackBuffer(
                output.get(), initial.size() * sizeof(UINT), &bytes),
            S_OK);
  ASSERT_EQ(bytes.size(), initial.size() * sizeof(UINT));
  for (UINT index = 0; index < descriptor_count; ++index) {
    UINT actual = 0;
    std::memcpy(&actual, bytes.data() + index * sizeof(UINT), sizeof(actual));
    EXPECT_EQ(actual, input_value)
        << "descriptor occurrence " << index + 1;
  }

  ASSERT_EQ(context_.ResetCommandList(), S_OK);
  const UINT recovered_index = target - 1;
  context_.device()->CreateShaderResourceView(
      input.get(), &srv,
      context_.CpuDescriptorHandle(heap.get(), recovered_index));
  D3D12TestContext::Transition(
      context_.list(), output.get(), D3D12_RESOURCE_STATE_COPY_SOURCE,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  context_.list()->SetDescriptorHeaps(1, heaps);
  context_.list()->SetComputeRootSignature(root.get());
  context_.list()->SetPipelineState(pipeline.get());
  context_.list()->SetComputeRootDescriptorTable(
      0, context_.GpuDescriptorHandle(heap.get(), recovered_index));
  context_.list()->SetComputeRootUnorderedAccessView(
      1, output->GetGPUVirtualAddress() + recovered_index * sizeof(UINT));
  context_.list()->Dispatch(1, 1, 1);
  D3D12TestContext::Transition(
      context_.list(), output.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_COPY_SOURCE);
  bytes.clear();
  ASSERT_EQ(context_.ReadbackBuffer(
                output.get(), initial.size() * sizeof(UINT), &bytes),
            S_OK);
  ASSERT_EQ(bytes.size(), initial.size() * sizeof(UINT));
  UINT recovered = 0;
  std::memcpy(&recovered,
              bytes.data() + recovered_index * sizeof(UINT),
              sizeof(recovered));
  EXPECT_EQ(recovered, input_value);

  const char *fault_name = allocation_target
                               ? "DXMT_TEST_FAIL_DESCRIPTOR_TABLE_ALLOCATION_AT"
                               : "DXMT_TEST_FAIL_RESIDENCY_INSERTION_AT";
  EXPECT_EQ(marker.Count(fault_name), 1u);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
