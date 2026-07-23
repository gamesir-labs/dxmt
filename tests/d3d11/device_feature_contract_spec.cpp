#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <d3d11_4.h>

#include <array>
#include <cstdint>
#include <vector>

// Public ID3D11Device::CheckFeatureSupport coverage for device-wide capability
// structures. Queries are read-only on a test-local device and parallel-safe.

namespace {

using dxmt::test::D3D11TestContext;

struct FeatureCase {
  D3D11_FEATURE feature;
  const char *name;
};

constexpr std::array kFeatureCases = {
    FeatureCase{D3D11_FEATURE_THREADING, "Threading"},
    FeatureCase{D3D11_FEATURE_DOUBLES, "Doubles"},
    FeatureCase{D3D11_FEATURE_D3D10_X_HARDWARE_OPTIONS,
                "D3D10XHardwareOptions"},
    FeatureCase{D3D11_FEATURE_D3D11_OPTIONS, "D3D11Options"},
    FeatureCase{D3D11_FEATURE_ARCHITECTURE_INFO, "ArchitectureInfo"},
    FeatureCase{D3D11_FEATURE_D3D9_OPTIONS, "D3D9Options"},
    FeatureCase{D3D11_FEATURE_SHADER_MIN_PRECISION_SUPPORT,
                "ShaderMinPrecision"},
    FeatureCase{D3D11_FEATURE_D3D9_SHADOW_SUPPORT, "D3D9ShadowSupport"},
    FeatureCase{D3D11_FEATURE_D3D11_OPTIONS1, "D3D11Options1"},
    FeatureCase{D3D11_FEATURE_D3D11_OPTIONS2, "D3D11Options2"},
    FeatureCase{D3D11_FEATURE_D3D11_OPTIONS3, "D3D11Options3"},
    FeatureCase{D3D11_FEATURE_GPU_VIRTUAL_ADDRESS_SUPPORT,
                "GpuVirtualAddressSupport"},
    FeatureCase{D3D11_FEATURE_D3D11_OPTIONS5, "D3D11Options5"},
    FeatureCase{D3D11_FEATURE_SHADER_CACHE, "ShaderCache"},
};

const dxmt::test::LogicalCaseFamilyRegistration kFeatureRegistration(
    "D3D11DeviceFeatureContractSpec.ReturnsWellFormedFeatureStructures",
    "D3D11.Device.FeatureSupport.Structure.", kFeatureCases.size(), 1,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "Device", "ID3D11DeviceCheckFeatureSupport"},
     dxmt::test::kResourceTestCost,
     "one test-local D3D11 device and one small stack capability structure per "
     "selected logical case",
     "query every implemented device-wide feature structure through the "
     "public CheckFeatureSupport API",
     "each query succeeds and returns BOOLs, enums, bitmasks, and address-bit "
     "counts within their documented domains",
     "logical ID, selected-case count, feature name and selector, HRESULT, "
     "validation detail, and exact replay argument"});

const dxmt::test::TestCostRegistration kFeatureCost(
    "D3D11DeviceFeatureContractSpec.ReturnsWellFormedFeatureStructures",
    dxmt::test::kResourceTestCost);

bool IsBool(WINBOOL value) { return value == FALSE || value == TRUE; }

::testing::AssertionResult ValidateFeature(ID3D11Device *device,
                                           std::uint32_t logical) {
  const D3D11_FEATURE feature = kFeatureCases[logical].feature;
  HRESULT result = E_FAIL;

  switch (feature) {
  case D3D11_FEATURE_THREADING: {
    D3D11_FEATURE_DATA_THREADING data = {};
    result = device->CheckFeatureSupport(feature, &data, sizeof(data));
    if (result == S_OK && IsBool(data.DriverConcurrentCreates) &&
        IsBool(data.DriverCommandLists))
      return ::testing::AssertionSuccess();
    return ::testing::AssertionFailure()
           << "HRESULT=" << result
           << " DriverConcurrentCreates=" << data.DriverConcurrentCreates
           << " DriverCommandLists=" << data.DriverCommandLists;
  }
  case D3D11_FEATURE_DOUBLES: {
    D3D11_FEATURE_DATA_DOUBLES data = {};
    result = device->CheckFeatureSupport(feature, &data, sizeof(data));
    if (result == S_OK && IsBool(data.DoublePrecisionFloatShaderOps))
      return ::testing::AssertionSuccess();
    return ::testing::AssertionFailure()
           << "HRESULT=" << result << " DoublePrecisionFloatShaderOps="
           << data.DoublePrecisionFloatShaderOps;
  }
  case D3D11_FEATURE_D3D10_X_HARDWARE_OPTIONS: {
    D3D11_FEATURE_DATA_D3D10_X_HARDWARE_OPTIONS data = {};
    result = device->CheckFeatureSupport(feature, &data, sizeof(data));
    if (result == S_OK &&
        IsBool(data.ComputeShaders_Plus_RawAndStructuredBuffers_Via_Shader_4_x))
      return ::testing::AssertionSuccess();
    return ::testing::AssertionFailure()
           << "HRESULT=" << result << " ComputeShaders="
           << data.ComputeShaders_Plus_RawAndStructuredBuffers_Via_Shader_4_x;
  }
  case D3D11_FEATURE_D3D11_OPTIONS: {
    D3D11_FEATURE_DATA_D3D11_OPTIONS data = {};
    result = device->CheckFeatureSupport(feature, &data, sizeof(data));
    const WINBOOL values[] = {
        data.OutputMergerLogicOp,
        data.UAVOnlyRenderingForcedSampleCount,
        data.DiscardAPIsSeenByDriver,
        data.FlagsForUpdateAndCopySeenByDriver,
        data.ClearView,
        data.CopyWithOverlap,
        data.ConstantBufferPartialUpdate,
        data.ConstantBufferOffsetting,
        data.MapNoOverwriteOnDynamicConstantBuffer,
        data.MapNoOverwriteOnDynamicBufferSRV,
        data.MultisampleRTVWithForcedSampleCountOne,
        data.SAD4ShaderInstructions,
        data.ExtendedDoublesShaderInstructions,
        data.ExtendedResourceSharing,
    };
    bool valid = result == S_OK;
    for (const WINBOOL value : values)
      valid = valid && IsBool(value);
    if (valid)
      return ::testing::AssertionSuccess();
    return ::testing::AssertionFailure()
           << "HRESULT=" << result << " malformed BOOL field";
  }
  case D3D11_FEATURE_ARCHITECTURE_INFO: {
    D3D11_FEATURE_DATA_ARCHITECTURE_INFO data = {};
    result = device->CheckFeatureSupport(feature, &data, sizeof(data));
    if (result == S_OK && IsBool(data.TileBasedDeferredRenderer))
      return ::testing::AssertionSuccess();
    return ::testing::AssertionFailure()
           << "HRESULT=" << result
           << " TileBasedDeferredRenderer=" << data.TileBasedDeferredRenderer;
  }
  case D3D11_FEATURE_D3D9_OPTIONS: {
    D3D11_FEATURE_DATA_D3D9_OPTIONS data = {};
    result = device->CheckFeatureSupport(feature, &data, sizeof(data));
    if (result == S_OK && IsBool(data.FullNonPow2TextureSupport))
      return ::testing::AssertionSuccess();
    return ::testing::AssertionFailure()
           << "HRESULT=" << result
           << " FullNonPow2TextureSupport=" << data.FullNonPow2TextureSupport;
  }
  case D3D11_FEATURE_SHADER_MIN_PRECISION_SUPPORT: {
    D3D11_FEATURE_DATA_SHADER_MIN_PRECISION_SUPPORT data = {};
    result = device->CheckFeatureSupport(feature, &data, sizeof(data));
    constexpr UINT kKnown =
        D3D11_SHADER_MIN_PRECISION_10_BIT | D3D11_SHADER_MIN_PRECISION_16_BIT;
    if (result == S_OK && !(data.PixelShaderMinPrecision & ~kKnown) &&
        !(data.AllOtherShaderStagesMinPrecision & ~kKnown))
      return ::testing::AssertionSuccess();
    return ::testing::AssertionFailure()
           << "HRESULT=" << result
           << " PixelShaderMinPrecision=" << data.PixelShaderMinPrecision
           << " AllOtherShaderStagesMinPrecision="
           << data.AllOtherShaderStagesMinPrecision;
  }
  case D3D11_FEATURE_D3D9_SHADOW_SUPPORT: {
    D3D11_FEATURE_DATA_D3D9_SHADOW_SUPPORT data = {};
    result = device->CheckFeatureSupport(feature, &data, sizeof(data));
    if (result == S_OK &&
        IsBool(data.SupportsDepthAsTextureWithLessEqualComparisonFilter))
      return ::testing::AssertionSuccess();
    return ::testing::AssertionFailure()
           << "HRESULT=" << result << " SupportsDepthAsTexture="
           << data.SupportsDepthAsTextureWithLessEqualComparisonFilter;
  }
  case D3D11_FEATURE_D3D11_OPTIONS1: {
    D3D11_FEATURE_DATA_D3D11_OPTIONS1 data = {};
    result = device->CheckFeatureSupport(feature, &data, sizeof(data));
    const bool valid =
        result == S_OK &&
        data.TiledResourcesTier <= D3D11_TILED_RESOURCES_TIER_3 &&
        IsBool(data.MinMaxFiltering) &&
        IsBool(data.ClearViewAlsoSupportsDepthOnlyFormats) &&
        IsBool(data.MapOnDefaultBuffers);
    if (valid)
      return ::testing::AssertionSuccess();
    return ::testing::AssertionFailure()
           << "HRESULT=" << result
           << " TiledResourcesTier=" << data.TiledResourcesTier
           << " MinMaxFiltering=" << data.MinMaxFiltering
           << " ClearViewDepthOnly="
           << data.ClearViewAlsoSupportsDepthOnlyFormats
           << " MapOnDefaultBuffers=" << data.MapOnDefaultBuffers;
  }
  case D3D11_FEATURE_D3D11_OPTIONS2: {
    D3D11_FEATURE_DATA_D3D11_OPTIONS2 data = {};
    result = device->CheckFeatureSupport(feature, &data, sizeof(data));
    const bool valid =
        result == S_OK && IsBool(data.PSSpecifiedStencilRefSupported) &&
        IsBool(data.TypedUAVLoadAdditionalFormats) &&
        IsBool(data.ROVsSupported) &&
        data.ConservativeRasterizationTier <=
            D3D11_CONSERVATIVE_RASTERIZATION_TIER_3 &&
        data.TiledResourcesTier <= D3D11_TILED_RESOURCES_TIER_3 &&
        IsBool(data.MapOnDefaultTextures) && IsBool(data.StandardSwizzle) &&
        IsBool(data.UnifiedMemoryArchitecture);
    if (valid)
      return ::testing::AssertionSuccess();
    return ::testing::AssertionFailure()
           << "HRESULT=" << result << " ConservativeRasterizationTier="
           << data.ConservativeRasterizationTier
           << " TiledResourcesTier=" << data.TiledResourcesTier;
  }
  case D3D11_FEATURE_D3D11_OPTIONS3: {
    D3D11_FEATURE_DATA_D3D11_OPTIONS3 data = {};
    result = device->CheckFeatureSupport(feature, &data, sizeof(data));
    if (result == S_OK &&
        IsBool(data.VPAndRTArrayIndexFromAnyShaderFeedingRasterizer))
      return ::testing::AssertionSuccess();
    return ::testing::AssertionFailure()
           << "HRESULT=" << result << " VPAndRTArrayIndex="
           << data.VPAndRTArrayIndexFromAnyShaderFeedingRasterizer;
  }
  case D3D11_FEATURE_GPU_VIRTUAL_ADDRESS_SUPPORT: {
    D3D11_FEATURE_DATA_GPU_VIRTUAL_ADDRESS_SUPPORT data = {};
    result = device->CheckFeatureSupport(feature, &data, sizeof(data));
    if (result == S_OK && data.MaxGPUVirtualAddressBitsPerProcess <= 64 &&
        data.MaxGPUVirtualAddressBitsPerResource <=
            data.MaxGPUVirtualAddressBitsPerProcess)
      return ::testing::AssertionSuccess();
    return ::testing::AssertionFailure()
           << "HRESULT=" << result
           << " ProcessBits=" << data.MaxGPUVirtualAddressBitsPerProcess
           << " ResourceBits=" << data.MaxGPUVirtualAddressBitsPerResource;
  }
  case D3D11_FEATURE_D3D11_OPTIONS5: {
    D3D11_FEATURE_DATA_D3D11_OPTIONS5 data = {};
    result = device->CheckFeatureSupport(feature, &data, sizeof(data));
    if (result == S_OK &&
        data.SharedResourceTier <= D3D11_SHARED_RESOURCE_TIER_3)
      return ::testing::AssertionSuccess();
    return ::testing::AssertionFailure()
           << "HRESULT=" << result
           << " SharedResourceTier=" << data.SharedResourceTier;
  }
  case D3D11_FEATURE_SHADER_CACHE: {
    D3D11_FEATURE_DATA_SHADER_CACHE data = {};
    result = device->CheckFeatureSupport(feature, &data, sizeof(data));
    constexpr UINT kKnown = D3D11_SHADER_CACHE_SUPPORT_AUTOMATIC_INPROC_CACHE |
                            D3D11_SHADER_CACHE_SUPPORT_AUTOMATIC_DISK_CACHE;
    if (result == S_OK && !(data.SupportFlags & ~kKnown))
      return ::testing::AssertionSuccess();
    return ::testing::AssertionFailure()
           << "HRESULT=" << result << " SupportFlags=" << data.SupportFlags;
  }
  default:
    return ::testing::AssertionFailure() << "unhandled feature=" << feature;
  }
}

class D3D11DeviceFeatureContractSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_NE(context_.device(), nullptr);
  }

  D3D11TestContext context_;
};

TEST_F(D3D11DeviceFeatureContractSpec, ReturnsWellFormedFeatureStructures) {
  std::vector<std::uint32_t> selected_cases;
  for (std::uint32_t logical = 0; logical < kFeatureCases.size(); ++logical) {
    if (dxmt::test::LogicalCaseSelected(kFeatureRegistration.family(), logical))
      selected_cases.push_back(logical);
  }
  ASSERT_FALSE(selected_cases.empty());

  RecordProperty("logical_cases_executed", selected_cases.size());
  RecordProperty("logical_case_prefix",
                 kFeatureRegistration.family().case_id_prefix);
  for (const std::uint32_t logical : selected_cases) {
    const auto validation = ValidateFeature(context_.device(), logical);
    if (validation)
      continue;

    const auto case_id =
        dxmt::test::LogicalCaseId(kFeatureRegistration.family(), logical);
    ADD_FAILURE() << "LogicalCaseId: " << case_id << '\n'
                  << "Parameters: logical=" << logical
                  << " feature_name=" << kFeatureCases[logical].name
                  << " feature=" << kFeatureCases[logical].feature
                  << " selected_cases=" << selected_cases.size() << '\n'
                  << "Observed: " << validation.message() << '\n'
                  << "Replay: --dxmt-case-id=" << case_id;
    break;
  }

  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
