#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

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

TEST_F(OptionalCommandContractSpec, AtomicCopyUint64FailsClose) {
  auto list1 = QueryList<ID3D12GraphicsCommandList1>();
  ASSERT_TRUE(list1);
  constexpr std::array<std::uint64_t, 4> data = {
      0x1020304050607080ull, 0x90a0b0c0d0e0f001ull,
      0x13579bdf2468ace0ull, 0xfeedc0de0badf00dull};
  auto source = context_.CreateUploadBuffer(sizeof(data), data.data(),
                                             sizeof(data));
  auto destination = context_.CreateBuffer(
      sizeof(data), D3D12_HEAP_TYPE_DEFAULT,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(source);
  ASSERT_TRUE(destination);
  list1->AtomicCopyBufferUINT64(destination.get(), 0, source.get(), 0, 0,
                                nullptr, nullptr);
  EXPECT_EQ(list1->Close(), E_NOTIMPL);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
