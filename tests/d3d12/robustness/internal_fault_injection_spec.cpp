#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"
#include "shaders/runtime_test_shaders.hpp"

#include <algorithm>
#include <array>
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
  return end != value && !*end && parsed <= UINT_MAX ? static_cast<UINT>(parsed)
                                                     : 0;
}

bool ConfiguredInternalAlways(const char *name) {
  const char *value = std::getenv(name);
  return value && (std::strcmp(value, "always") == 0 ||
                   std::strcmp(value, "all") == 0);
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
    const DWORD length =
        GetTempPathA(ARRAYSIZE(temporary_path), temporary_path);
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
    return context_.CreateComputePipeline(root_.get(),
                                          {shader.bytecode->GetBufferPointer(),
                                           shader.bytecode->GetBufferSize()});
  }

  UINT ExecuteWriter(ID3D12PipelineState *pipeline) {
    const UINT zero = 0;
    auto upload =
        context_.CreateUploadBuffer(sizeof(zero), &zero, sizeof(zero));
    auto output =
        context_.CreateBuffer(sizeof(zero), D3D12_HEAP_TYPE_DEFAULT,
                              D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                              D3D12_RESOURCE_STATE_COPY_DEST);
    EXPECT_TRUE(upload);
    EXPECT_TRUE(output);
    if (!upload || !output)
      return UINT_MAX;
    context_.list()->CopyBufferRegion(output.get(), 0, upload.get(), 0,
                                      sizeof(zero));
    D3D12TestContext::Transition(context_.list(), output.get(),
                                 D3D12_RESOURCE_STATE_COPY_DEST,
                                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    context_.list()->SetComputeRootSignature(root_.get());
    context_.list()->SetPipelineState(pipeline);
    context_.list()->SetComputeRootUnorderedAccessView(
        0, output->GetGPUVirtualAddress());
    context_.list()->Dispatch(1, 1, 1);
    D3D12TestContext::Transition(context_.list(), output.get(),
                                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                 D3D12_RESOURCE_STATE_COPY_SOURCE);
    std::vector<std::uint8_t> bytes;
    EXPECT_EQ(context_.ReadbackBuffer(output.get(), sizeof(UINT), &bytes),
              S_OK);
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
       RepeatedAirInitializationFailuresRecoverAfterFaultIsDisabled) {
  constexpr const char *fault = "DXMT_TEST_FAIL_AIR_INITIALIZATION_AT";
  if (!ConfiguredInternalAlways(fault))
    GTEST_SKIP() << "repeated AIR initialization fault is disabled";
  FaultMarker marker;
  ASSERT_TRUE(marker.Initialize());

  for (UINT attempt = 0; attempt < 3; ++attempt) {
    auto failed_pipeline =
        CreateWriterPipeline(0x41424344u + attempt * 0x01010101u);
    EXPECT_FALSE(failed_pipeline) << "attempt=" << attempt;
  }

  ASSERT_TRUE(SetEnvironmentVariableA(fault, nullptr));
  auto recovery_pipeline = CreateWriterPipeline(0x71727374u);
  ASSERT_TRUE(recovery_pipeline);
  EXPECT_EQ(ExecuteWriter(recovery_pipeline.get()), 0x71727374u);
  EXPECT_EQ(marker.Count(fault), 3u);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D12InternalFaultInjectionSpec,
       MetalPipelineFailureAtConfiguredOccurrencePreservesOtherPipelines) {
  constexpr const char *fault = "DXMT_TEST_FAIL_METAL_COMPUTE_PIPELINE_AT";
  const UINT target = ConfiguredInternalOccurrence(fault);
  if (!target)
    GTEST_SKIP() << "configured Metal pipeline fault injection is disabled";
  ASSERT_LE(target, 3u) << "the test defines three pipeline occurrences";
  FaultMarker marker;
  ASSERT_TRUE(marker.Initialize());

  constexpr std::array<UINT, 3> values = {
      0x10213243u, 0x54657687u, 0x98a9bacbu};
  for (UINT occurrence = 1; occurrence <= values.size(); ++occurrence) {
    auto pipeline = CreateWriterPipeline(values[occurrence - 1]);
    if (occurrence == target) {
      EXPECT_FALSE(pipeline) << "occurrence=" << occurrence;
    } else {
      ASSERT_TRUE(pipeline) << "occurrence=" << occurrence;
      EXPECT_EQ(ExecuteWriter(pipeline.get()), values[occurrence - 1])
          << "occurrence=" << occurrence;
    }
  }

  // A target occurrence is one-shot. Remove the setting as well so recovery
  // remains explicit and exercises the same process and device.
  ASSERT_TRUE(SetEnvironmentVariableA(fault, nullptr));
  auto recovery_pipeline = CreateWriterPipeline(0xdcedfe0fu);
  ASSERT_TRUE(recovery_pipeline);
  EXPECT_EQ(ExecuteWriter(recovery_pipeline.get()), 0xdcedfe0fu);
  EXPECT_EQ(marker.Count(fault), 1u);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D12InternalFaultInjectionSpec,
       RepeatedMetalPipelineFailuresAndLaterPipelineRecovers) {
  const char *configuration =
      std::getenv("DXMT_TEST_FAIL_METAL_COMPUTE_PIPELINE_AT");
  if (!configuration || std::string_view(configuration) != "always")
    GTEST_SKIP() << "Metal pipeline fault injection must be set to always";
  FaultMarker marker;
  ASSERT_TRUE(marker.Initialize());

  for (UINT attempt = 0; attempt < 3; ++attempt) {
    auto failed_pipeline =
        CreateWriterPipeline(0x11112222u + attempt * 0x01010101u);
    EXPECT_FALSE(failed_pipeline) << "attempt=" << attempt;
  }

  ASSERT_TRUE(SetEnvironmentVariableA(
      "DXMT_TEST_FAIL_METAL_COMPUTE_PIPELINE_AT", nullptr));

  auto recovery_pipeline = CreateWriterPipeline(0x33334444u);
  ASSERT_TRUE(recovery_pipeline);
  EXPECT_EQ(ExecuteWriter(recovery_pipeline.get()), 0x33334444u);
  EXPECT_GE(marker.Count("DXMT_TEST_FAIL_METAL_COMPUTE_PIPELINE_AT"), 3u);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D12InternalFaultInjectionSpec,
       PipelineArchiveLookupFaultFallsBackAndExecutionContinues) {
  constexpr const char *fault = "DXMT_TEST_FAIL_PSO_ARCHIVE_LOOKUP_AT";
  const UINT target = ConfiguredInternalOccurrence(fault);
  const bool repeated = ConfiguredInternalAlways(fault);
  ASSERT_FALSE(target && repeated);
  if (!target && !repeated)
    GTEST_SKIP() << "pipeline archive lookup fault injection is disabled";
  ASSERT_TRUE(repeated || target <= 3u)
      << "the test defines three archive lookup occurrences";
  FaultMarker marker;
  ASSERT_TRUE(marker.Initialize());

  constexpr std::array<UINT, 3> values = {
      0x51525354u, 0x61626364u, 0x71727374u};
  for (UINT occurrence = 1; occurrence <= values.size(); ++occurrence) {
    auto pipeline = CreateWriterPipeline(values[occurrence - 1]);
    ASSERT_TRUE(pipeline) << "occurrence=" << occurrence;
    EXPECT_EQ(ExecuteWriter(pipeline.get()), values[occurrence - 1])
        << "occurrence=" << occurrence;
  }

  const std::size_t injected_count = repeated ? values.size() : 1u;
  EXPECT_EQ(marker.Count(fault), injected_count);

  ASSERT_TRUE(SetEnvironmentVariableA(fault, nullptr));
  auto recovery_pipeline = CreateWriterPipeline(0x81828384u);
  ASSERT_TRUE(recovery_pipeline);
  EXPECT_EQ(ExecuteWriter(recovery_pipeline.get()), 0x81828384u);
  EXPECT_EQ(marker.Count(fault), injected_count);
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
  const UINT residency_target =
      ConfiguredInternalOccurrence("DXMT_TEST_FAIL_RESIDENCY_INSERTION_AT");
  ASSERT_FALSE(allocation_target && residency_target)
      << "configure only one descriptor fault at a time";
  const UINT target = std::max(allocation_target, residency_target);
  if (!target)
    GTEST_SKIP() << "descriptor internal fault injection is disabled";
  FaultMarker marker;
  ASSERT_TRUE(marker.Initialize());

  constexpr UINT input_value = 0x718293a4u;
  const UINT descriptor_count = target + 1;
  auto input = context_.CreateUploadBuffer(sizeof(input_value), &input_value,
                                           sizeof(input_value));
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
    void main() {
      output.Store(0, input.Load(0));
    }
  )",
                                    "cs_5_0");
  ASSERT_EQ(shader.result, S_OK) << shader.diagnostic_text();
  auto pipeline = context_.CreateComputePipeline(
      root.get(),
      {shader.bytecode->GetBufferPointer(), shader.bytecode->GetBufferSize()});
  ASSERT_TRUE(root);
  ASSERT_TRUE(pipeline);

  std::vector<UINT> initial(descriptor_count, 0);
  auto initial_upload =
      context_.CreateUploadBuffer(initial.size() * sizeof(UINT), initial.data(),
                                  initial.size() * sizeof(UINT));
  auto output = context_.CreateBuffer(
      initial.size() * sizeof(UINT), D3D12_HEAP_TYPE_DEFAULT,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(initial_upload);
  ASSERT_TRUE(output);
  context_.list()->CopyBufferRegion(output.get(), 0, initial_upload.get(), 0,
                                    initial.size() * sizeof(UINT));
  D3D12TestContext::Transition(context_.list(), output.get(),
                               D3D12_RESOURCE_STATE_COPY_DEST,
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
  D3D12TestContext::Transition(context_.list(), output.get(),
                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
  std::vector<std::uint8_t> bytes;
  ASSERT_EQ(context_.ReadbackBuffer(output.get(), initial.size() * sizeof(UINT),
                                    &bytes),
            S_OK);
  ASSERT_EQ(bytes.size(), initial.size() * sizeof(UINT));
  for (UINT index = 0; index < descriptor_count; ++index) {
    UINT actual = 0;
    std::memcpy(&actual, bytes.data() + index * sizeof(UINT), sizeof(actual));
    EXPECT_EQ(actual, input_value) << "descriptor occurrence " << index + 1;
  }

  ASSERT_EQ(context_.ResetCommandList(), S_OK);
  const UINT recovered_index = target - 1;
  context_.device()->CreateShaderResourceView(
      input.get(), &srv,
      context_.CpuDescriptorHandle(heap.get(), recovered_index));
  D3D12TestContext::Transition(context_.list(), output.get(),
                               D3D12_RESOURCE_STATE_COPY_SOURCE,
                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  context_.list()->SetDescriptorHeaps(1, heaps);
  context_.list()->SetComputeRootSignature(root.get());
  context_.list()->SetPipelineState(pipeline.get());
  context_.list()->SetComputeRootDescriptorTable(
      0, context_.GpuDescriptorHandle(heap.get(), recovered_index));
  context_.list()->SetComputeRootUnorderedAccessView(
      1, output->GetGPUVirtualAddress() + recovered_index * sizeof(UINT));
  context_.list()->Dispatch(1, 1, 1);
  D3D12TestContext::Transition(context_.list(), output.get(),
                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
  bytes.clear();
  ASSERT_EQ(context_.ReadbackBuffer(output.get(), initial.size() * sizeof(UINT),
                                    &bytes),
            S_OK);
  ASSERT_EQ(bytes.size(), initial.size() * sizeof(UINT));
  UINT recovered = 0;
  std::memcpy(&recovered, bytes.data() + recovered_index * sizeof(UINT),
              sizeof(recovered));
  EXPECT_EQ(recovered, input_value);

  const char *fault_name = allocation_target
                               ? "DXMT_TEST_FAIL_DESCRIPTOR_TABLE_ALLOCATION_AT"
                               : "DXMT_TEST_FAIL_RESIDENCY_INSERTION_AT";
  EXPECT_EQ(marker.Count(fault_name), 1u);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D12DescriptorInternalFaultInjectionSpec,
       RepeatedDescriptorFailuresFallBackWithoutPartialPublication) {
  constexpr const char *allocation_fault =
      "DXMT_TEST_FAIL_DESCRIPTOR_TABLE_ALLOCATION_AT";
  constexpr const char *residency_fault =
      "DXMT_TEST_FAIL_RESIDENCY_INSERTION_AT";
  const bool allocation_active = ConfiguredInternalAlways(allocation_fault);
  const bool residency_active = ConfiguredInternalAlways(residency_fault);
  ASSERT_FALSE(allocation_active && residency_active)
      << "configure only one repeated descriptor fault";
  if (!allocation_active && !residency_active)
    GTEST_SKIP() << "repeated descriptor fault injection is disabled";
  const char *fault = allocation_active ? allocation_fault : residency_fault;
  FaultMarker marker;
  ASSERT_TRUE(marker.Initialize());

  constexpr UINT kDescriptorCount = 3;
  constexpr UINT kInputValue = 0x91a2b3c4u;
  auto input = context_.CreateUploadBuffer(sizeof(kInputValue), &kInputValue,
                                           sizeof(kInputValue));
  auto heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kDescriptorCount, true);
  ASSERT_TRUE(input);
  ASSERT_TRUE(heap);
  D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
  srv.Format = DXGI_FORMAT_R32_TYPELESS;
  srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
  srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv.Buffer.NumElements = 1;
  srv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
  for (UINT index = 0; index < kDescriptorCount; ++index) {
    context_.device()->CreateShaderResourceView(
        input.get(), &srv, context_.CpuDescriptorHandle(heap.get(), index));
  }
  EXPECT_EQ(marker.Count(fault), kDescriptorCount);

  // Keep the fault active through execution. Each failed native table
  // publication must leave the legacy descriptor intact so all three
  // dispatches execute exactly once through the fallback path.

  D3D12_DESCRIPTOR_RANGE range = {};
  range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  range.NumDescriptors = 1;
  D3D12_ROOT_PARAMETER parameters[2] = {};
  parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[0].DescriptorTable.NumDescriptorRanges = 1;
  parameters[0].DescriptorTable.pDescriptorRanges = &range;
  parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  root_desc.NumParameters = ARRAYSIZE(parameters);
  root_desc.pParameters = parameters;
  auto root = context_.CreateRootSignature(root_desc);
  ASSERT_TRUE(root);
  const auto shader = CompileShader(R"(
    ByteAddressBuffer input : register(t0);
    RWStructuredBuffer<uint> output : register(u0);
    [numthreads(1, 1, 1)]
    void main() {
      uint original;
      InterlockedAdd(output[0], input.Load(0), original);
    }
  )",
                                    "cs_5_0");
  ASSERT_EQ(shader.result, S_OK) << shader.diagnostic_text();
  auto pipeline = context_.CreateComputePipeline(
      root.get(),
      {shader.bytecode->GetBufferPointer(), shader.bytecode->GetBufferSize()});
  ASSERT_TRUE(pipeline);

  const std::array<UINT, kDescriptorCount> initial = {};
  auto initial_upload = context_.CreateUploadBuffer(
      sizeof(initial), initial.data(), sizeof(initial));
  auto output = context_.CreateBuffer(
      sizeof(initial), D3D12_HEAP_TYPE_DEFAULT,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(initial_upload);
  ASSERT_TRUE(output);
  context_.list()->CopyBufferRegion(output.get(), 0, initial_upload.get(), 0,
                                    sizeof(initial));
  D3D12TestContext::Transition(context_.list(), output.get(),
                               D3D12_RESOURCE_STATE_COPY_DEST,
                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  ID3D12DescriptorHeap *heaps[] = {heap.get()};
  context_.list()->SetDescriptorHeaps(1, heaps);
  context_.list()->SetComputeRootSignature(root.get());
  context_.list()->SetPipelineState(pipeline.get());
  for (UINT index = 0; index < kDescriptorCount; ++index) {
    context_.list()->SetComputeRootDescriptorTable(
        0, context_.GpuDescriptorHandle(heap.get(), index));
    context_.list()->SetComputeRootUnorderedAccessView(
        1, output->GetGPUVirtualAddress() + index * sizeof(UINT));
    context_.list()->Dispatch(1, 1, 1);
  }
  D3D12TestContext::Transition(context_.list(), output.get(),
                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
  std::vector<std::uint8_t> bytes;
  ASSERT_EQ(context_.ReadbackBuffer(output.get(), sizeof(initial), &bytes),
            S_OK);
  ASSERT_EQ(bytes.size(), sizeof(initial));
  std::array<UINT, kDescriptorCount> actual = {};
  std::memcpy(actual.data(), bytes.data(), sizeof(actual));
  EXPECT_EQ(actual, (std::array<UINT, kDescriptorCount>{
                        kInputValue, kInputValue, kInputValue}));
  EXPECT_EQ(marker.Count(fault), kDescriptorCount);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

class D3D12MetalObjectFaultInjectionSpec : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }
  D3D12TestContext context_;
};

TEST_F(D3D12MetalObjectFaultInjectionSpec,
       MetalObjectFailureClearsOutputAndNextOccurrenceRecovers) {
  struct FaultCase {
    const char *environment;
    enum Kind { Buffer, Texture, Heap, GraphicsPipeline } kind;
  };
  constexpr FaultCase cases[] = {
      {"DXMT_TEST_FAIL_METAL_BUFFER_CREATION_AT", FaultCase::Buffer},
      {"DXMT_TEST_FAIL_METAL_TEXTURE_CREATION_AT", FaultCase::Texture},
      {"DXMT_TEST_FAIL_METAL_HEAP_CREATION_AT", FaultCase::Heap},
      {"DXMT_TEST_FAIL_METAL_GRAPHICS_PIPELINE_AT",
       FaultCase::GraphicsPipeline},
  };
  const FaultCase *active = nullptr;
  UINT target = 0;
  for (const auto &test : cases) {
    const UINT configured = ConfiguredInternalOccurrence(test.environment);
    if (!configured)
      continue;
    ASSERT_EQ(active, nullptr) << "configure only one Metal object fault";
    active = &test;
    target = configured;
  }
  if (!active)
    GTEST_SKIP() << "Metal object fault injection is disabled";
  FaultMarker marker;
  ASSERT_TRUE(marker.Initialize());

  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  root_desc.Flags =
      D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
  auto root = context_.CreateRootSignature(root_desc);
  ASSERT_TRUE(root);
  const auto pixel =
      CompileShader("float4 main() : SV_Target { return 1.0.xxxx; }", "ps_5_0");
  ASSERT_EQ(pixel.result, S_OK) << pixel.diagnostic_text();

  for (UINT occurrence = 1; occurrence <= target + 1; ++occurrence) {
    void *output = reinterpret_cast<void *>(static_cast<uintptr_t>(1));
    HRESULT result = E_FAIL;
    if (active->kind == FaultCase::Buffer ||
        active->kind == FaultCase::Texture) {
      D3D12_HEAP_PROPERTIES heap = {};
      heap.Type = D3D12_HEAP_TYPE_DEFAULT;
      D3D12_RESOURCE_DESC desc = {};
      desc.Dimension = active->kind == FaultCase::Buffer
                           ? D3D12_RESOURCE_DIMENSION_BUFFER
                           : D3D12_RESOURCE_DIMENSION_TEXTURE2D;
      desc.Width = active->kind == FaultCase::Buffer ? 256 : 4;
      desc.Height = active->kind == FaultCase::Buffer ? 1 : 4;
      desc.DepthOrArraySize = 1;
      desc.MipLevels = 1;
      desc.Format = active->kind == FaultCase::Buffer
                        ? DXGI_FORMAT_UNKNOWN
                        : DXGI_FORMAT_R8G8B8A8_UNORM;
      desc.SampleDesc.Count = 1;
      desc.Layout = active->kind == FaultCase::Buffer
                        ? D3D12_TEXTURE_LAYOUT_ROW_MAJOR
                        : D3D12_TEXTURE_LAYOUT_UNKNOWN;
      result = context_.device()->CreateCommittedResource(
          &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON,
          nullptr, __uuidof(ID3D12Resource), &output);
    } else if (active->kind == FaultCase::Heap) {
      D3D12_HEAP_DESC desc = {};
      desc.SizeInBytes = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
      desc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
      desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
      desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
      result =
          context_.device()->CreateHeap(&desc, __uuidof(ID3D12Heap), &output);
    } else {
      D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
      desc.pRootSignature = root.get();
      desc.VS = dxmt::test::FullscreenVertexShader();
      desc.PS = {pixel.bytecode->GetBufferPointer(),
                 pixel.bytecode->GetBufferSize()};
      desc.BlendState.RenderTarget[0].RenderTargetWriteMask =
          D3D12_COLOR_WRITE_ENABLE_ALL;
      desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
      desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
      desc.SampleMask = UINT_MAX;
      desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
      desc.NumRenderTargets = 1;
      desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
      desc.SampleDesc.Count = 1;
      result = context_.device()->CreateGraphicsPipelineState(
          &desc, __uuidof(ID3D12PipelineState), &output);
    }

    if (occurrence == target) {
      EXPECT_EQ(result, E_OUTOFMEMORY);
      EXPECT_EQ(output, nullptr);
    } else {
      ASSERT_EQ(result, S_OK);
      ASSERT_NE(output, nullptr);
      static_cast<IUnknown *>(output)->Release();
    }
  }
  EXPECT_EQ(marker.Count(active->environment), 1u);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D12MetalObjectFaultInjectionSpec,
       RepeatedMetalObjectFailuresRecoverAfterFaultIsDisabled) {
  struct FaultCase {
    const char *environment;
    enum Kind { Buffer, Texture, Heap, GraphicsPipeline } kind;
  };
  constexpr FaultCase cases[] = {
      {"DXMT_TEST_FAIL_METAL_BUFFER_CREATION_AT", FaultCase::Buffer},
      {"DXMT_TEST_FAIL_METAL_TEXTURE_CREATION_AT", FaultCase::Texture},
      {"DXMT_TEST_FAIL_METAL_HEAP_CREATION_AT", FaultCase::Heap},
      {"DXMT_TEST_FAIL_METAL_GRAPHICS_PIPELINE_AT",
       FaultCase::GraphicsPipeline},
  };
  const FaultCase *active = nullptr;
  for (const auto &test : cases) {
    if (!ConfiguredInternalAlways(test.environment))
      continue;
    ASSERT_EQ(active, nullptr) << "configure only one repeated Metal fault";
    active = &test;
  }
  if (!active)
    GTEST_SKIP() << "repeated Metal object fault injection is disabled";
  FaultMarker marker;
  ASSERT_TRUE(marker.Initialize());

  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  root_desc.Flags =
      D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
  auto root = context_.CreateRootSignature(root_desc);
  ASSERT_TRUE(root);
  const auto pixel =
      CompileShader("float4 main() : SV_Target { return 1.0.xxxx; }", "ps_5_0");
  ASSERT_EQ(pixel.result, S_OK) << pixel.diagnostic_text();

  auto create = [&](void **output) {
    if (active->kind == FaultCase::Buffer ||
        active->kind == FaultCase::Texture) {
      D3D12_HEAP_PROPERTIES heap = {};
      heap.Type = D3D12_HEAP_TYPE_DEFAULT;
      D3D12_RESOURCE_DESC desc = {};
      desc.Dimension = active->kind == FaultCase::Buffer
                           ? D3D12_RESOURCE_DIMENSION_BUFFER
                           : D3D12_RESOURCE_DIMENSION_TEXTURE2D;
      desc.Width = active->kind == FaultCase::Buffer ? 256 : 4;
      desc.Height = active->kind == FaultCase::Buffer ? 1 : 4;
      desc.DepthOrArraySize = 1;
      desc.MipLevels = 1;
      desc.Format = active->kind == FaultCase::Buffer
                        ? DXGI_FORMAT_UNKNOWN
                        : DXGI_FORMAT_R8G8B8A8_UNORM;
      desc.SampleDesc.Count = 1;
      desc.Layout = active->kind == FaultCase::Buffer
                        ? D3D12_TEXTURE_LAYOUT_ROW_MAJOR
                        : D3D12_TEXTURE_LAYOUT_UNKNOWN;
      return context_.device()->CreateCommittedResource(
          &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON,
          nullptr, __uuidof(ID3D12Resource), output);
    }
    if (active->kind == FaultCase::Heap) {
      D3D12_HEAP_DESC desc = {};
      desc.SizeInBytes = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
      desc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
      desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
      desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
      return context_.device()->CreateHeap(&desc, __uuidof(ID3D12Heap),
                                           output);
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
    desc.pRootSignature = root.get();
    desc.VS = dxmt::test::FullscreenVertexShader();
    desc.PS = {pixel.bytecode->GetBufferPointer(),
               pixel.bytecode->GetBufferSize()};
    desc.BlendState.RenderTarget[0].RenderTargetWriteMask =
        D3D12_COLOR_WRITE_ENABLE_ALL;
    desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    desc.SampleMask = UINT_MAX;
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    return context_.device()->CreateGraphicsPipelineState(
        &desc, __uuidof(ID3D12PipelineState), output);
  };

  for (UINT attempt = 0; attempt < 3; ++attempt) {
    void *output = reinterpret_cast<void *>(uintptr_t{1});
    EXPECT_EQ(create(&output), E_OUTOFMEMORY) << "attempt=" << attempt;
    EXPECT_EQ(output, nullptr) << "attempt=" << attempt;
  }

  ASSERT_TRUE(SetEnvironmentVariableA(active->environment, nullptr));
  void *recovered = reinterpret_cast<void *>(uintptr_t{1});
  ASSERT_EQ(create(&recovered), S_OK);
  ASSERT_NE(recovered, nullptr);
  ASSERT_NE(recovered, reinterpret_cast<void *>(uintptr_t{1}));
  static_cast<IUnknown *>(recovered)->Release();
  EXPECT_EQ(marker.Count(active->environment), 3u);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
