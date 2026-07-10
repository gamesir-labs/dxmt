#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iomanip>
#include <string>

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

DXMT_SLOW_TEST_PATTERN("*D3D11Unreal*");

::testing::AssertionResult HResultSucceeded(HRESULT hr) {
  if (SUCCEEDED(hr))
    return ::testing::AssertionSuccess();
  return ::testing::AssertionFailure()
         << "HRESULT failed: 0x" << std::hex << static_cast<unsigned long>(hr);
}

bool SupportsAsyncCreation(BOOL driver_concurrent_creates,
                           UINT creation_flags) {
  return driver_concurrent_creates != FALSE &&
         !(creation_flags & D3D11_CREATE_DEVICE_SINGLETHREADED);
}

bool SupportsMapNoOverwrite(HRESULT query_result,
                            const D3D11_FEATURE_DATA_D3D11_OPTIONS &options) {
  return SUCCEEDED(query_result) && options.MapNoOverwriteOnDynamicBufferSRV;
}

bool SupportsArrayIndex(HRESULT query_result,
                        const D3D11_FEATURE_DATA_D3D11_OPTIONS3 &options) {
  return SUCCEEDED(query_result) &&
         options.VPAndRTArrayIndexFromAnyShaderFeedingRasterizer;
}

bool IsUma(bool has_device3, HRESULT query_result,
           const D3D11_FEATURE_DATA_D3D11_OPTIONS2 &options) {
  return has_device3 && SUCCEEDED(query_result) &&
         options.UnifiedMemoryArchitecture;
}

struct MsaaProbeResult {
  HRESULT result = E_FAIL;
  UINT quality_levels = 0;
};

UINT SelectBestMsaaCount(UINT requested,
                         const std::array<MsaaProbeResult, 9> &probes) {
  for (UINT sample_count = requested; sample_count > 0; --sample_count) {
    if (SUCCEEDED(probes[sample_count].result) &&
        probes[sample_count].quality_levels > 0) {
      return sample_count;
    }
  }
  return 0;
}

std::uint64_t SelectGraphicsMemory(bool is_uma, bool is_amd,
                                   HRESULT adapter3_result,
                                   HRESULT budget_result, std::uint64_t budget,
                                   std::uint64_t dedicated_video_memory,
                                   std::uint64_t dedicated_system_memory,
                                   std::uint64_t shared_system_memory,
                                   std::uint64_t total_physical_memory) {
  const auto considered_shared_memory =
      std::min(shared_system_memory / 2, total_physical_memory / 4);
  if (is_uma) {
    return dedicated_video_memory + dedicated_system_memory +
           considered_shared_memory;
  }
  if (is_amd && SUCCEEDED(adapter3_result) && SUCCEEDED(budget_result)) {
    return budget;
  }

  constexpr std::uint64_t mib = 1024ull * 1024ull;
  if (dedicated_video_memory >= 200 * mib)
    return dedicated_video_memory;
  if (dedicated_system_memory >= 200 * mib)
    return dedicated_system_memory;
  if (shared_system_memory >= 400 * mib)
    return considered_shared_memory;
  return total_physical_memory / 4;
}

TEST(UnrealD3D11PolicySpec, MapsOptionalCapabilityFallbacks) {
  D3D11_FEATURE_DATA_D3D11_OPTIONS options = {};
  EXPECT_FALSE(SupportsMapNoOverwrite(E_FAIL, options));
  options.MapNoOverwriteOnDynamicBufferSRV = TRUE;
  EXPECT_TRUE(SupportsMapNoOverwrite(S_OK, options));

  D3D11_FEATURE_DATA_D3D11_OPTIONS3 options3 = {};
  EXPECT_FALSE(SupportsArrayIndex(E_NOINTERFACE, options3));
  options3.VPAndRTArrayIndexFromAnyShaderFeedingRasterizer = TRUE;
  EXPECT_TRUE(SupportsArrayIndex(S_OK, options3));

  D3D11_FEATURE_DATA_D3D11_OPTIONS2 options2 = {};
  options2.UnifiedMemoryArchitecture = TRUE;
  EXPECT_FALSE(IsUma(false, S_OK, options2));
  EXPECT_FALSE(IsUma(true, E_INVALIDARG, options2));
  EXPECT_TRUE(IsUma(true, S_OK, options2));
}

TEST(UnrealD3D11PolicySpec, SelectsMsaaByDescendingFallback) {
  std::array<MsaaProbeResult, 9> probes = {};
  probes[8] = {S_OK, 0};
  probes[7] = {E_FAIL, 4};
  probes[4] = {S_OK, 2};
  probes[1] = {S_OK, 1};
  EXPECT_EQ(SelectBestMsaaCount(8, probes), 4u);

  probes[8] = {S_OK, 1};
  EXPECT_EQ(SelectBestMsaaCount(8, probes), 8u);

  probes[8] = {E_FAIL, 1};
  probes[4] = {S_OK, 0};
  EXPECT_EQ(SelectBestMsaaCount(8, probes), 1u);
}

TEST(UnrealD3D11PolicySpec, SelectsGraphicsMemoryWithOptionalAmdBudget) {
  constexpr std::uint64_t mib = 1024ull * 1024ull;
  constexpr std::uint64_t gib = 1024ull * mib;

  EXPECT_EQ(SelectGraphicsMemory(true, true, S_OK, S_OK, 3 * gib, 128 * mib,
                                 64 * mib, 4 * gib, 8 * gib),
            128 * mib + 64 * mib + 2 * gib);
  EXPECT_EQ(SelectGraphicsMemory(false, true, S_OK, S_OK, 3 * gib, 2 * gib, 0,
                                 0, 8 * gib),
            3 * gib);
  EXPECT_EQ(SelectGraphicsMemory(false, true, S_OK, E_FAIL, 3 * gib, 2 * gib, 0,
                                 0, 8 * gib),
            2 * gib);
  EXPECT_EQ(SelectGraphicsMemory(false, false, S_OK, S_OK, 3 * gib, 2 * gib, 0,
                                 0, 8 * gib),
            2 * gib);
  EXPECT_EQ(SelectGraphicsMemory(false, false, E_NOINTERFACE, E_FAIL, 0,
                                 128 * mib, 512 * mib, 0, 8 * gib),
            512 * mib);
  EXPECT_EQ(SelectGraphicsMemory(false, false, E_NOINTERFACE, E_FAIL, 0,
                                 128 * mib, 128 * mib, 2 * gib, 8 * gib),
            1 * gib);
  EXPECT_EQ(SelectGraphicsMemory(false, false, E_NOINTERFACE, E_FAIL, 0,
                                 128 * mib, 128 * mib, 128 * mib, 8 * gib),
            2 * gib);
}

class D3D11UnrealCapabilitySpec : public ::testing::Test {
protected:
  void SetUp() override {
    const HRESULT hr = context_.Initialize();
    ASSERT_TRUE(HResultSucceeded(hr));
    ASSERT_NE(context_.factory(), nullptr);
    ASSERT_NE(context_.adapter(), nullptr);
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_NE(context_.context(), nullptr);
  }

  D3D11TestContext context_;
};

struct FormatExpectation {
  DXGI_FORMAT format;
  UINT support;
  const char *name;
};

class D3D11UnrealFormatSpec
    : public D3D11UnrealCapabilitySpec,
      public ::testing::WithParamInterface<FormatExpectation> {};

class D3D11UnrealMsaaSpec : public D3D11UnrealCapabilitySpec,
                            public ::testing::WithParamInterface<UINT> {};

TEST_F(D3D11UnrealCapabilitySpec,
       EnumeratesHighPerformanceAdapterWithStableIdentity) {
  const auto &desc = context_.adapter_desc();
  EXPECT_NE(desc.Description[0], L'\0');
  EXPECT_NE(desc.VendorId, 0u);

  ComPtr<IDXGIFactory6> factory6;
  const HRESULT factory6_hr = context_.factory()->QueryInterface(
      __uuidof(IDXGIFactory6), reinterpret_cast<void **>(factory6.put()));
  if (SUCCEEDED(factory6_hr)) {
    ComPtr<IDXGIAdapter> preferred_adapter;
    ASSERT_TRUE(factory6);
    ASSERT_TRUE(HResultSucceeded(factory6->EnumAdapterByGpuPreference(
        0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, __uuidof(IDXGIAdapter),
        reinterpret_cast<void **>(preferred_adapter.put()))));
    ASSERT_TRUE(preferred_adapter);
    DXGI_ADAPTER_DESC preferred_desc = {};
    ASSERT_TRUE(HResultSucceeded(preferred_adapter->GetDesc(&preferred_desc)));
    EXPECT_EQ(preferred_desc.VendorId, desc.VendorId);
    EXPECT_EQ(preferred_desc.DeviceId, desc.DeviceId);
    EXPECT_EQ(preferred_desc.SubSysId, desc.SubSysId);
    EXPECT_EQ(preferred_desc.Revision, desc.Revision);
  }
}

TEST_F(D3D11UnrealCapabilitySpec, CreatesFinalDeviceAtSelectedFeatureLevel) {
  EXPECT_NE(context_.device(), nullptr);
  EXPECT_NE(context_.context(), nullptr);
  EXPECT_GE(context_.feature_level(), D3D_FEATURE_LEVEL_11_0);
  EXPECT_LE(context_.feature_level(), D3D_FEATURE_LEVEL_11_1);
  EXPECT_EQ(context_.device()->GetFeatureLevel(), context_.feature_level());
  EXPECT_NE(context_.device()->GetCreationFlags() &
                D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            0u);
}

TEST(D3D11UnrealProbeSpec, RecreatesFinalDeviceAtExactProbeFeatureLevel) {
  D3D11TestContext adapter_context;
  ASSERT_TRUE(HResultSucceeded(adapter_context.InitializeAdapter()));
  ASSERT_NE(adapter_context.adapter(), nullptr);

  constexpr std::array probe_levels = {
      D3D_FEATURE_LEVEL_11_1,
      D3D_FEATURE_LEVEL_11_0,
  };
  ComPtr<ID3D11Device> probe_device;
  ComPtr<ID3D11DeviceContext> probe_context;
  D3D_FEATURE_LEVEL probed_level = D3D_FEATURE_LEVEL(0);
  ASSERT_TRUE(HResultSucceeded(D3D11CreateDevice(
      adapter_context.adapter(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
      D3D11_CREATE_DEVICE_SINGLETHREADED | D3D11_CREATE_DEVICE_BGRA_SUPPORT,
      probe_levels.data(), static_cast<UINT>(probe_levels.size()),
      D3D11_SDK_VERSION, probe_device.put(), &probed_level,
      probe_context.put())));
  ASSERT_TRUE(probe_device);
  ASSERT_TRUE(probe_context);
  EXPECT_GE(probed_level, D3D_FEATURE_LEVEL_11_0);
  EXPECT_NE(probe_device->GetCreationFlags() &
                D3D11_CREATE_DEVICE_SINGLETHREADED,
            0u);
  EXPECT_NE(probe_device->GetCreationFlags() & D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            0u);

  probe_context.reset();
  probe_device.reset();

  ComPtr<ID3D11Device> final_device;
  ComPtr<ID3D11DeviceContext> final_context;
  D3D_FEATURE_LEVEL final_level = D3D_FEATURE_LEVEL(0);
  ASSERT_TRUE(HResultSucceeded(D3D11CreateDevice(
      adapter_context.adapter(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
      D3D11_CREATE_DEVICE_BGRA_SUPPORT, &probed_level, 1, D3D11_SDK_VERSION,
      final_device.put(), &final_level, final_context.put())));
  ASSERT_TRUE(final_device);
  ASSERT_TRUE(final_context);
  EXPECT_EQ(final_level, probed_level);
  EXPECT_EQ(final_device->GetFeatureLevel(), probed_level);
  EXPECT_EQ(final_device->GetCreationFlags() &
                D3D11_CREATE_DEVICE_SINGLETHREADED,
            0u);
}

TEST_F(D3D11UnrealCapabilitySpec,
       RejectsHardwareDriverTypeWithExplicitAdapter) {
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> immediate_context;
  D3D_FEATURE_LEVEL actual_feature_level = D3D_FEATURE_LEVEL(0);
  const auto requested_feature_level = D3D_FEATURE_LEVEL_11_0;

  const HRESULT hr = D3D11CreateDevice(
      context_.adapter(), D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
      &requested_feature_level, 1, D3D11_SDK_VERSION, device.put(),
      &actual_feature_level, immediate_context.put());

  EXPECT_EQ(hr, E_INVALIDARG);
  EXPECT_EQ(device.get(), nullptr);
  EXPECT_EQ(immediate_context.get(), nullptr);
}

TEST_F(D3D11UnrealCapabilitySpec, ReportsThreadSafeResourceCreation) {
  D3D11_FEATURE_DATA_THREADING threading = {};
  ASSERT_TRUE(HResultSucceeded(context_.device()->CheckFeatureSupport(
      D3D11_FEATURE_THREADING, &threading, sizeof(threading))));

  EXPECT_TRUE(threading.DriverConcurrentCreates == TRUE ||
              threading.DriverConcurrentCreates == FALSE);
  EXPECT_TRUE(threading.DriverCommandLists == TRUE ||
              threading.DriverCommandLists == FALSE);
}

TEST_F(D3D11UnrealCapabilitySpec,
       ComputesUnrealAsyncCreationDecisionFromFlagsAndThreading) {
  D3D11_FEATURE_DATA_THREADING threading = {};
  ASSERT_TRUE(HResultSucceeded(context_.device()->CheckFeatureSupport(
      D3D11_FEATURE_THREADING, &threading, sizeof(threading))));

  const bool concurrent_creation = SupportsAsyncCreation(
      threading.DriverConcurrentCreates, context_.device()->GetCreationFlags());
  EXPECT_EQ(concurrent_creation, threading.DriverConcurrentCreates != FALSE);
}

TEST_F(D3D11UnrealCapabilitySpec, ReportsBufferMappingOptionsUsedByUnreal) {
  D3D11_FEATURE_DATA_D3D11_OPTIONS options = {};
  const HRESULT hr = context_.device()->CheckFeatureSupport(
      D3D11_FEATURE_D3D11_OPTIONS, &options, sizeof(options));
  const bool supports_map_no_overwrite = SupportsMapNoOverwrite(hr, options);
  if (SUCCEEDED(hr)) {
    EXPECT_EQ(supports_map_no_overwrite,
              options.MapNoOverwriteOnDynamicBufferSRV != FALSE);
  } else {
    EXPECT_FALSE(supports_map_no_overwrite);
  }
}

TEST_F(D3D11UnrealCapabilitySpec, ReportsDevice3ArchitectureOptions) {
  ComPtr<ID3D11Device3> device3;
  const HRESULT device3_hr = context_.device()->QueryInterface(
      __uuidof(ID3D11Device3), reinterpret_cast<void **>(device3.put()));

  D3D11_FEATURE_DATA_D3D11_OPTIONS2 options = {};
  HRESULT options_hr = E_NOINTERFACE;
  if (SUCCEEDED(device3_hr) && device3) {
    options_hr = device3->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS2,
                                              &options, sizeof(options));
  }
  const bool is_uma = IsUma(device3.get() != nullptr, options_hr, options);
  if (SUCCEEDED(options_hr)) {
    EXPECT_EQ(is_uma, options.UnifiedMemoryArchitecture != FALSE);
  } else {
    EXPECT_FALSE(is_uma);
  }
}

TEST_F(D3D11UnrealCapabilitySpec, ReportsArrayIndexFromRasterizerInputs) {
  D3D11_FEATURE_DATA_D3D11_OPTIONS3 options = {};
  const HRESULT hr = context_.device()->CheckFeatureSupport(
      D3D11_FEATURE_D3D11_OPTIONS3, &options, sizeof(options));
  const bool supports_array_index = SupportsArrayIndex(hr, options);
  if (SUCCEEDED(hr)) {
    EXPECT_EQ(supports_array_index,
              options.VPAndRTArrayIndexFromAnyShaderFeedingRasterizer != FALSE);
  } else {
    EXPECT_FALSE(supports_array_index);
  }
}

TEST_F(D3D11UnrealCapabilitySpec, TreatsLocalVideoMemoryBudgetAsOptional) {
  ComPtr<IDXGIAdapter3> adapter3;
  if (FAILED(context_.adapter()->QueryInterface(
          __uuidof(IDXGIAdapter3),
          reinterpret_cast<void **>(adapter3.put())))) {
    SUCCEED() << "IDXGIAdapter3 is optional in the UE D3D11 path";
    return;
  }
  ASSERT_TRUE(adapter3);

  DXGI_QUERY_VIDEO_MEMORY_INFO memory_info = {};
  const HRESULT hr = adapter3->QueryVideoMemoryInfo(
      0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &memory_info);
  if (FAILED(hr)) {
    SUCCEED() << "UE falls back to DXGI_ADAPTER_DESC memory values";
    return;
  }
  if (memory_info.Budget != 0) {
    EXPECT_LE(memory_info.CurrentReservation, memory_info.Budget);
  }
}

TEST_F(D3D11UnrealCapabilitySpec, PreservesDeviceAdapterFactoryIdentity) {
  ComPtr<IDXGIDevice> dxgi_device;
  ASSERT_TRUE(HResultSucceeded(context_.device()->QueryInterface(
      __uuidof(IDXGIDevice), reinterpret_cast<void **>(dxgi_device.put()))));
  ASSERT_TRUE(dxgi_device);

  ComPtr<IDXGIAdapter> device_adapter;
  ASSERT_TRUE(HResultSucceeded(dxgi_device->GetAdapter(device_adapter.put())));
  ASSERT_TRUE(device_adapter);
  DXGI_ADAPTER_DESC device_desc = {};
  ASSERT_TRUE(HResultSucceeded(device_adapter->GetDesc(&device_desc)));
  EXPECT_EQ(device_desc.AdapterLuid.HighPart,
            context_.adapter_desc().AdapterLuid.HighPart);
  EXPECT_EQ(device_desc.AdapterLuid.LowPart,
            context_.adapter_desc().AdapterLuid.LowPart);

  ComPtr<IDXGIFactory1> parent_factory;
  ASSERT_TRUE(HResultSucceeded(device_adapter->GetParent(
      __uuidof(IDXGIFactory1),
      reinterpret_cast<void **>(parent_factory.put()))));
  ASSERT_TRUE(parent_factory);
  ComPtr<IUnknown> expected_identity;
  ComPtr<IUnknown> actual_identity;
  ASSERT_TRUE(HResultSucceeded(context_.factory()->QueryInterface(
      __uuidof(IUnknown), reinterpret_cast<void **>(expected_identity.put()))));
  ASSERT_TRUE(expected_identity);
  ASSERT_TRUE(HResultSucceeded(parent_factory->QueryInterface(
      __uuidof(IUnknown), reinterpret_cast<void **>(actual_identity.put()))));
  ASSERT_TRUE(actual_identity);
  EXPECT_EQ(actual_identity.get(), expected_identity.get());
}

TEST_F(D3D11UnrealCapabilitySpec, EnumeratesOutputsUntilNotFound) {
  HRESULT terminal = S_OK;
  for (UINT index = 0; index < 32; ++index) {
    ComPtr<IDXGIOutput> output;
    terminal = context_.adapter()->EnumOutputs(index, output.put());
    if (terminal == DXGI_ERROR_NOT_FOUND)
      break;
    ASSERT_TRUE(HResultSucceeded(terminal));
    ASSERT_TRUE(output);
  }
  EXPECT_EQ(terminal, DXGI_ERROR_NOT_FOUND);
}

TEST_P(D3D11UnrealFormatSpec, ReportsRequiredPixelFormatCapabilities) {
  const auto expectation = GetParam();
  D3D11_FEATURE_DATA_FORMAT_SUPPORT support = {};
  support.InFormat = expectation.format;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CheckFeatureSupport(
      D3D11_FEATURE_FORMAT_SUPPORT, &support, sizeof(support))));
  EXPECT_EQ(support.OutFormatSupport & expectation.support,
            expectation.support);
}

TEST_F(D3D11UnrealCapabilitySpec,
       MapsTypedUavSupportWithoutRequiringOptionalFlags) {
  D3D11_FEATURE_DATA_FORMAT_SUPPORT support = {};
  support.InFormat = DXGI_FORMAT_R32_UINT;
  const HRESULT support_hr = context_.device()->CheckFeatureSupport(
      D3D11_FEATURE_FORMAT_SUPPORT, &support, sizeof(support));
  D3D11_FEATURE_DATA_FORMAT_SUPPORT2 support2 = {};
  support2.InFormat = DXGI_FORMAT_R32_UINT;
  const HRESULT support2_hr = context_.device()->CheckFeatureSupport(
      D3D11_FEATURE_FORMAT_SUPPORT2, &support2, sizeof(support2));

  const bool has_uav = SUCCEEDED(support_hr) &&
                       (support.OutFormatSupport &
                        D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW);
  const bool has_store =
      SUCCEEDED(support2_hr) &&
      (support2.OutFormatSupport2 & D3D11_FORMAT_SUPPORT2_UAV_TYPED_STORE);
  const bool has_atomics =
      SUCCEEDED(support2_hr) &&
      (support2.OutFormatSupport2 & D3D11_FORMAT_SUPPORT2_UAV_ATOMIC_EXCHANGE);
  EXPECT_FALSE(has_store && !has_uav);
  EXPECT_FALSE(has_atomics && !has_uav);
}

INSTANTIATE_TEST_SUITE_P(
    UnrealBootstrapFormats, D3D11UnrealFormatSpec,
    ::testing::Values(
        FormatExpectation{DXGI_FORMAT_R8G8B8A8_UNORM,
                          D3D11_FORMAT_SUPPORT_TEXTURE2D |
                              D3D11_FORMAT_SUPPORT_RENDER_TARGET |
                              D3D11_FORMAT_SUPPORT_SHADER_SAMPLE |
                              D3D11_FORMAT_SUPPORT_BLENDABLE,
                          "Color8"},
        FormatExpectation{DXGI_FORMAT_B8G8R8A8_UNORM,
                          D3D11_FORMAT_SUPPORT_TEXTURE2D |
                              D3D11_FORMAT_SUPPORT_RENDER_TARGET |
                              D3D11_FORMAT_SUPPORT_SHADER_SAMPLE |
                              D3D11_FORMAT_SUPPORT_DISPLAY,
                          "BackBuffer"},
        FormatExpectation{DXGI_FORMAT_R16G16B16A16_FLOAT,
                          D3D11_FORMAT_SUPPORT_TEXTURE2D |
                              D3D11_FORMAT_SUPPORT_RENDER_TARGET |
                              D3D11_FORMAT_SUPPORT_SHADER_SAMPLE,
                          "SceneColor"},
        FormatExpectation{DXGI_FORMAT_R32G8X24_TYPELESS,
                          D3D11_FORMAT_SUPPORT_TEXTURE2D, "Depth32Resource"},
        FormatExpectation{DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS,
                          D3D11_FORMAT_SUPPORT_TEXTURE2D |
                              D3D11_FORMAT_SUPPORT_SHADER_SAMPLE,
                          "Depth32Srv"},
        FormatExpectation{DXGI_FORMAT_D32_FLOAT_S8X24_UINT,
                          D3D11_FORMAT_SUPPORT_TEXTURE2D |
                              D3D11_FORMAT_SUPPORT_DEPTH_STENCIL,
                          "Depth32Dsv"},
        FormatExpectation{DXGI_FORMAT_R24G8_TYPELESS,
                          D3D11_FORMAT_SUPPORT_TEXTURE2D, "Depth24Resource"},
        FormatExpectation{DXGI_FORMAT_R24_UNORM_X8_TYPELESS,
                          D3D11_FORMAT_SUPPORT_TEXTURE2D |
                              D3D11_FORMAT_SUPPORT_SHADER_SAMPLE,
                          "Depth24Srv"},
        FormatExpectation{DXGI_FORMAT_D24_UNORM_S8_UINT,
                          D3D11_FORMAT_SUPPORT_TEXTURE2D |
                              D3D11_FORMAT_SUPPORT_DEPTH_STENCIL,
                          "Depth24Dsv"},
        FormatExpectation{DXGI_FORMAT_R16_TYPELESS,
                          D3D11_FORMAT_SUPPORT_TEXTURE2D, "Depth16Resource"},
        FormatExpectation{DXGI_FORMAT_R16_UNORM,
                          D3D11_FORMAT_SUPPORT_TEXTURE2D |
                              D3D11_FORMAT_SUPPORT_SHADER_SAMPLE,
                          "Depth16Srv"},
        FormatExpectation{DXGI_FORMAT_D16_UNORM,
                          D3D11_FORMAT_SUPPORT_TEXTURE2D |
                              D3D11_FORMAT_SUPPORT_DEPTH_STENCIL,
                          "Depth16Dsv"},
        FormatExpectation{DXGI_FORMAT_R32_UINT,
                          D3D11_FORMAT_SUPPORT_BUFFER |
                              D3D11_FORMAT_SUPPORT_TEXTURE2D,
                          "UintUav"}),
    [](const ::testing::TestParamInfo<FormatExpectation> &info) {
      return std::string(info.param.name);
    });

TEST_P(D3D11UnrealMsaaSpec, CreatesEveryReportedRenderTargetConfiguration) {
  const UINT sample_count = GetParam();
  UINT quality_levels = 0;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CheckMultisampleQualityLevels(
      DXGI_FORMAT_R8G8B8A8_UNORM, sample_count, &quality_levels)));

  if (sample_count == 1) {
    ASSERT_GT(quality_levels, 0u);
  }
  if (quality_levels == 0)
    GTEST_SKIP() << sample_count << "x MSAA is not supported by this device";

  D3D11_TEXTURE2D_DESC desc = {};
  desc.Width = 64;
  desc.Height = 64;
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = sample_count;
  desc.SampleDesc.Quality = 0;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

  ComPtr<ID3D11Texture2D> texture;
  EXPECT_TRUE(HResultSucceeded(
      context_.device()->CreateTexture2D(&desc, nullptr, texture.put())));
}

INSTANTIATE_TEST_SUITE_P(UnrealFallbackOrder, D3D11UnrealMsaaSpec,
                         ::testing::Values(1u, 2u, 4u, 8u));

TEST_F(D3D11UnrealCapabilitySpec, RejectsIncorrectFeatureStructureSize) {
  D3D11_FEATURE_DATA_THREADING threading = {};
  EXPECT_EQ(context_.device()->CheckFeatureSupport(
                D3D11_FEATURE_THREADING, &threading, sizeof(threading) - 1),
            E_INVALIDARG);
}

} // namespace
