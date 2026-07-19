#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

constexpr GUID kHardwareProtectedSession = {
    0x62b0084e,
    0xc70e,
    0x4daa,
    {0xa1, 0x09, 0x30, 0xff, 0x8d, 0x5a, 0x04, 0x82}};

void ExpectCopyExecution(D3D12TestContext &context) {
  constexpr std::array<std::uint32_t, 4> expected = {
      0x10203040u, 0x50607080u, 0x90a0b0c0u, 0xd0e0f001u};
  auto upload = context.CreateUploadBuffer(sizeof(expected), expected.data(),
                                            sizeof(expected));
  auto destination = context.CreateBuffer(
      sizeof(expected), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(upload);
  ASSERT_TRUE(destination);
  context.list()->CopyBufferRegion(destination.get(), 0, upload.get(), 0,
                                   sizeof(expected));
  D3D12TestContext::Transition(context.list(), destination.get(),
                               D3D12_RESOURCE_STATE_COPY_DEST,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
  std::vector<std::uint8_t> actual;
  ASSERT_EQ(context.ReadbackBuffer(destination.get(), sizeof(expected),
                                   &actual),
            S_OK);
  ASSERT_EQ(actual.size(), sizeof(expected));
  EXPECT_EQ(std::memcmp(actual.data(), expected.data(), sizeof(expected)), 0);
  EXPECT_EQ(context.device()->GetDeviceRemovedReason(), S_OK);
}

class OptionalCommandContractSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};
    ASSERT_EQ(context_.device()->CheckFeatureSupport(
                  D3D12_FEATURE_D3D12_OPTIONS5, &options5,
                  sizeof(options5)),
              S_OK);
    ASSERT_EQ(options5.RaytracingTier,
              D3D12_RAYTRACING_TIER_NOT_SUPPORTED);
  }

  template <typename Interface> ComPtr<Interface> QueryDevice() {
    ComPtr<Interface> result;
    EXPECT_EQ(context_.device()->QueryInterface(
                  __uuidof(Interface),
                  reinterpret_cast<void **>(result.put())),
              S_OK);
    return result;
  }

  template <typename Interface> ComPtr<Interface> QueryList() {
    ComPtr<Interface> result;
    EXPECT_EQ(context_.list()->QueryInterface(
                  __uuidof(Interface),
                  reinterpret_cast<void **>(result.put())),
              S_OK);
    return result;
  }

  D3D12TestContext context_;
};

TEST_F(OptionalCommandContractSpec,
       RaytracingPrebuildInfoIsZeroWhenTierIsUnsupported) {
  auto device5 = QueryDevice<ID3D12Device5>();
  ASSERT_TRUE(device5);
  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
  inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
  inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
  D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info;
  std::memset(&info, 0xff, sizeof(info));

  device5->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);
  EXPECT_EQ(info.ResultDataMaxSizeInBytes, 0u);
  EXPECT_EQ(info.ScratchDataSizeInBytes, 0u);
  EXPECT_EQ(info.UpdateScratchDataSizeInBytes, 0u);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(OptionalCommandContractSpec,
       RaytracingBuildFailsCloseBlocksResetAndFreshContextRecovers) {
  auto list4 = QueryList<ID3D12GraphicsCommandList4>();
  ASSERT_TRUE(list4);
  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC desc = {};
  desc.Inputs.Type =
      D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
  desc.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
  list4->BuildRaytracingAccelerationStructure(&desc, 0, nullptr);
  EXPECT_EQ(list4->Close(), E_NOTIMPL);
  EXPECT_EQ(list4->Reset(context_.allocator(), nullptr), E_FAIL);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);

  D3D12TestContext recovered;
  ASSERT_EQ(recovered.Initialize(), S_OK);
  ExpectCopyExecution(recovered);
}

TEST_F(OptionalCommandContractSpec,
       RaytracingPostbuildEmissionFailsClose) {
  auto list4 = QueryList<ID3D12GraphicsCommandList4>();
  ASSERT_TRUE(list4);
  D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC desc = {};
  desc.InfoType =
      D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_CURRENT_SIZE;
  const D3D12_GPU_VIRTUAL_ADDRESS source = 0;
  list4->EmitRaytracingAccelerationStructurePostbuildInfo(&desc, 1, &source);
  EXPECT_EQ(list4->Close(), E_NOTIMPL);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(OptionalCommandContractSpec, RaytracingCopyFailsClose) {
  auto list4 = QueryList<ID3D12GraphicsCommandList4>();
  ASSERT_TRUE(list4);
  list4->CopyRaytracingAccelerationStructure(
      0, 0, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE_CLONE);
  EXPECT_EQ(list4->Close(), E_NOTIMPL);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(OptionalCommandContractSpec,
       NullRaytracingPipelineStateIsHarmlessToLaterExecution) {
  auto list4 = QueryList<ID3D12GraphicsCommandList4>();
  ASSERT_TRUE(list4);
  list4->SetPipelineState1(nullptr);
  ExpectCopyExecution(context_);
}

TEST_F(OptionalCommandContractSpec,
       ShadingRateImageFailsClosedWhileNullDefaultsRemainHarmless) {
  D3D12_FEATURE_DATA_D3D12_OPTIONS6 options6 = {};
  ASSERT_EQ(context_.device()->CheckFeatureSupport(
                D3D12_FEATURE_D3D12_OPTIONS6, &options6,
                sizeof(options6)),
            S_OK);
  ASSERT_EQ(options6.VariableShadingRateTier,
            D3D12_VARIABLE_SHADING_RATE_TIER_NOT_SUPPORTED);
  auto list5 = QueryList<ID3D12GraphicsCommandList5>();
  ASSERT_TRUE(list5);
  auto image = context_.CreateTexture2D(
      4, 4, 1, DXGI_FORMAT_R8_UINT, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COMMON);
  ASSERT_TRUE(image);
  list5->RSSetShadingRateImage(image.get());
  EXPECT_EQ(list5->Close(), E_NOTIMPL);

  D3D12TestContext recovered;
  ASSERT_EQ(recovered.Initialize(), S_OK);
  ComPtr<ID3D12GraphicsCommandList5> recovered_list5;
  ASSERT_EQ(recovered.list()->QueryInterface(
                __uuidof(ID3D12GraphicsCommandList5),
                reinterpret_cast<void **>(recovered_list5.put())),
            S_OK);
  recovered_list5->RSSetShadingRate(D3D12_SHADING_RATE_1X1, nullptr);
  recovered_list5->RSSetShadingRateImage(nullptr);
  ExpectCopyExecution(recovered);
}

TEST_F(OptionalCommandContractSpec,
       ProtectedSession1AndTypeQueriesRemainCapabilityCoherent) {
  D3D12_FEATURE_DATA_PROTECTED_RESOURCE_SESSION_SUPPORT support = {};
  ASSERT_EQ(context_.device()->CheckFeatureSupport(
                D3D12_FEATURE_PROTECTED_RESOURCE_SESSION_SUPPORT, &support,
                sizeof(support)),
            S_OK);
  ASSERT_EQ(support.Support,
            D3D12_PROTECTED_RESOURCE_SESSION_SUPPORT_FLAG_NONE);

  D3D12_FEATURE_DATA_PROTECTED_RESOURCE_SESSION_TYPE_COUNT type_count = {};
  type_count.Count = 123;
  ASSERT_EQ(context_.device()->CheckFeatureSupport(
                D3D12_FEATURE_PROTECTED_RESOURCE_SESSION_TYPE_COUNT,
                &type_count, sizeof(type_count)),
            S_OK);
  EXPECT_EQ(type_count.Count, 0u);

  GUID type = kHardwareProtectedSession;
  D3D12_FEATURE_DATA_PROTECTED_RESOURCE_SESSION_TYPES types = {};
  types.Count = 1;
  types.pTypes = &type;
  ASSERT_EQ(context_.device()->CheckFeatureSupport(
                D3D12_FEATURE_PROTECTED_RESOURCE_SESSION_TYPES, &types,
                sizeof(types)),
            S_OK);
  EXPECT_EQ(types.Count, 0u);

  auto device7 = QueryDevice<ID3D12Device7>();
  ASSERT_TRUE(device7);
  D3D12_PROTECTED_RESOURCE_SESSION_DESC1 desc = {};
  desc.ProtectionType = kHardwareProtectedSession;
  auto *session = reinterpret_cast<ID3D12ProtectedResourceSession1 *>(
      uintptr_t{1});
  EXPECT_EQ(device7->CreateProtectedResourceSession1(
                &desc, __uuidof(ID3D12ProtectedResourceSession1),
                reinterpret_cast<void **>(&session)),
            E_NOTIMPL);
  EXPECT_EQ(session, nullptr);

  session = reinterpret_cast<ID3D12ProtectedResourceSession1 *>(uintptr_t{1});
  desc.NodeMask = 2;
  EXPECT_EQ(device7->CreateProtectedResourceSession1(
                &desc, __uuidof(ID3D12ProtectedResourceSession1),
                reinterpret_cast<void **>(&session)),
            E_INVALIDARG);
  EXPECT_EQ(session, nullptr);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(OptionalCommandContractSpec,
       StateObjectGrowthValidationClearsOutputs) {
  auto device5 = QueryDevice<ID3D12Device5>();
  auto device7 = QueryDevice<ID3D12Device7>();
  ASSERT_TRUE(device5);
  ASSERT_TRUE(device7);

  auto *state_object =
      reinterpret_cast<ID3D12StateObject *>(uintptr_t{1});
  EXPECT_EQ(device5->CreateStateObject(
                nullptr, __uuidof(ID3D12StateObject),
                reinterpret_cast<void **>(&state_object)),
            E_INVALIDARG);
  EXPECT_EQ(state_object, nullptr);

  D3D12_STATE_OBJECT_DESC addition = {};
  state_object = reinterpret_cast<ID3D12StateObject *>(uintptr_t{1});
  EXPECT_EQ(device7->AddToStateObject(
                &addition, nullptr, __uuidof(ID3D12StateObject),
                reinterpret_cast<void **>(&state_object)),
            E_INVALIDARG);
  EXPECT_EQ(state_object, nullptr);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(OptionalCommandContractSpec,
       SamplerFeedbackDescriptorOverwritesExistingUavWithInertBinding) {
  D3D12_FEATURE_DATA_D3D12_OPTIONS7 options7 = {};
  ASSERT_EQ(context_.device()->CheckFeatureSupport(
                D3D12_FEATURE_D3D12_OPTIONS7, &options7, sizeof(options7)),
            S_OK);
  ASSERT_EQ(options7.SamplerFeedbackTier,
            D3D12_SAMPLER_FEEDBACK_TIER_NOT_SUPPORTED);
  auto device8 = QueryDevice<ID3D12Device8>();
  ASSERT_TRUE(device8);

  const auto shader = CompileShader(R"(
    RWByteAddressBuffer output : register(u0);
    [numthreads(1, 1, 1)]
    void main() { output.Store(0, 0xdeadbeef); }
  )",
                                    "cs_5_0");
  ASSERT_EQ(shader.result, S_OK) << shader.diagnostic_text();

  D3D12_DESCRIPTOR_RANGE range = {};
  range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  range.NumDescriptors = 1;
  D3D12_ROOT_PARAMETER parameter = {};
  parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameter.DescriptorTable.NumDescriptorRanges = 1;
  parameter.DescriptorTable.pDescriptorRanges = &range;
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  root_desc.NumParameters = 1;
  root_desc.pParameters = &parameter;
  auto root = context_.CreateRootSignature(root_desc);
  ASSERT_TRUE(root);
  const D3D12_SHADER_BYTECODE bytecode = {
      shader.bytecode->GetBufferPointer(), shader.bytecode->GetBufferSize()};
  auto pipeline = context_.CreateComputePipeline(root.get(), bytecode);
  ASSERT_TRUE(pipeline);

  constexpr std::array<std::uint32_t, 4> initial = {
      0x10203040u, 0x50607080u, 0x90a0b0c0u, 0xd0e0f001u};
  auto upload = context_.CreateUploadBuffer(sizeof(initial), initial.data(),
                                             sizeof(initial));
  auto output = context_.CreateBuffer(
      sizeof(initial), D3D12_HEAP_TYPE_DEFAULT,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_COPY_DEST);
  auto heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true);
  ASSERT_TRUE(upload);
  ASSERT_TRUE(output);
  ASSERT_TRUE(heap);

  context_.list()->CopyBufferRegion(output.get(), 0, upload.get(), 0,
                                    sizeof(initial));
  D3D12TestContext::Transition(context_.list(), output.get(),
                               D3D12_RESOURCE_STATE_COPY_DEST,
                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
  uav.Format = DXGI_FORMAT_R32_TYPELESS;
  uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav.Buffer.NumElements = initial.size();
  uav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
  const auto cpu = heap->GetCPUDescriptorHandleForHeapStart();
  context_.device()->CreateUnorderedAccessView(output.get(), nullptr, &uav,
                                                cpu);

  device8->CreateSamplerFeedbackUnorderedAccessView(nullptr, nullptr, cpu);
  ID3D12DescriptorHeap *heaps[] = {heap.get()};
  context_.list()->SetDescriptorHeaps(1, heaps);
  context_.list()->SetComputeRootSignature(root.get());
  context_.list()->SetPipelineState(pipeline.get());
  context_.list()->SetComputeRootDescriptorTable(
      0, heap->GetGPUDescriptorHandleForHeapStart());
  context_.list()->Dispatch(1, 1, 1);
  D3D12TestContext::Transition(context_.list(), output.get(),
                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);

  std::vector<std::uint8_t> actual;
  ASSERT_EQ(context_.ReadbackBuffer(output.get(), sizeof(initial), &actual),
            S_OK);
  ASSERT_EQ(actual.size(), sizeof(initial));
  EXPECT_EQ(std::memcmp(actual.data(), initial.data(), sizeof(initial)), 0);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(OptionalCommandContractSpec,
       EmptyStreamOutputTargetResetIsHarmless) {
  context_.list()->SOSetTargets(0, 0, nullptr);
  ExpectCopyExecution(context_);
}

} // namespace
