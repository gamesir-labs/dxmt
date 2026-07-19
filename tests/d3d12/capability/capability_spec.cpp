#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>

#include <algorithm>
#include <array>
#include <iomanip>
#include <string>

namespace {

using dxmt::test::ComPtr;

DXMT_SLOW_TEST_PATTERN("*D3D12Unreal*");

::testing::AssertionResult HResultSucceeded(HRESULT hr) {
  if (SUCCEEDED(hr))
    return ::testing::AssertionSuccess();
  return ::testing::AssertionFailure()
         << "HRESULT failed: 0x" << std::hex << static_cast<unsigned long>(hr);
}

HRESULT CreateMsaaRenderTarget(ID3D12Device *device, DXGI_FORMAT format,
                               UINT sample_count,
                               ComPtr<ID3D12Resource> *texture) {
  D3D12_HEAP_PROPERTIES heap = {};
  heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width = 64;
  desc.Height = 64;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.Format = format;
  desc.SampleDesc.Count = sample_count;
  desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  return device->CreateCommittedResource(
      &heap, D3D12_HEAP_FLAG_NONE, &desc,
      D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, __uuidof(ID3D12Resource),
      reinterpret_cast<void **>(texture->put()));
}

HRESULT CreateReservedMsaaRenderTarget(ID3D12Device *device,
                                       DXGI_FORMAT format,
                                       UINT sample_count,
                                       ComPtr<ID3D12Resource> *texture) {
  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width = 64;
  desc.Height = 64;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.Format = format;
  desc.SampleDesc.Count = sample_count;
  desc.Layout = D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE;
  desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  return device->CreateReservedResource(
      &desc, D3D12_RESOURCE_STATE_COMMON, nullptr,
      __uuidof(ID3D12Resource), reinterpret_cast<void **>(texture->put()));
}

D3D_FEATURE_LEVEL ResolveFeatureLevel(HRESULT query_result,
                                      D3D_FEATURE_LEVEL reported,
                                      D3D_FEATURE_LEVEL minimum) {
  return SUCCEEDED(query_result) ? reported : minimum;
}

D3D_FEATURE_LEVEL FindHighestFeatureLevel(ID3D12Device *device,
                                          D3D_FEATURE_LEVEL minimum) {
  constexpr std::array requested_levels = {
      D3D_FEATURE_LEVEL_12_2, D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0,
      D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
  };
  D3D12_FEATURE_DATA_FEATURE_LEVELS levels = {};
  levels.NumFeatureLevels = static_cast<UINT>(requested_levels.size());
  levels.pFeatureLevelsRequested = requested_levels.data();
  const HRESULT hr = device->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS,
                                                 &levels, sizeof(levels));
  return ResolveFeatureLevel(hr, levels.MaxSupportedFeatureLevel, minimum);
}

D3D_SHADER_MODEL FindHighestShaderModel(ID3D12Device *device) {
  constexpr std::array shader_models = {
      D3D_SHADER_MODEL_6_7, D3D_SHADER_MODEL_6_6, D3D_SHADER_MODEL_6_5,
      D3D_SHADER_MODEL_6_4, D3D_SHADER_MODEL_6_3, D3D_SHADER_MODEL_6_2,
      D3D_SHADER_MODEL_6_1, D3D_SHADER_MODEL_6_0,
  };
  for (const D3D_SHADER_MODEL requested : shader_models) {
    D3D12_FEATURE_DATA_SHADER_MODEL shader_model = {};
    shader_model.HighestShaderModel = requested;
    if (SUCCEEDED(device->CheckFeatureSupport(
            D3D12_FEATURE_SHADER_MODEL, &shader_model, sizeof(shader_model)))) {
      return shader_model.HighestShaderModel;
    }
  }
  return D3D_SHADER_MODEL_5_1;
}

struct Sm6PolicyCase {
  D3D_FEATURE_LEVEL feature_level;
  D3D_SHADER_MODEL shader_model;
  D3D12_RESOURCE_BINDING_TIER binding_tier;
  bool wave_ops;
  bool typed_atomic64;
  bool emulated_atomic64;
  bool fully_bindless;
  bool expected;
  const char *name;
};

bool SupportsUnrealSm6(const Sm6PolicyCase &capabilities) {
  const auto required_binding_tier = capabilities.fully_bindless
                                         ? D3D12_RESOURCE_BINDING_TIER_3
                                         : D3D12_RESOURCE_BINDING_TIER_2;
  return capabilities.feature_level >= D3D_FEATURE_LEVEL_12_0 &&
         capabilities.shader_model >= D3D_SHADER_MODEL_6_6 &&
         capabilities.wave_ops &&
         capabilities.binding_tier >= required_binding_tier &&
         (capabilities.typed_atomic64 || capabilities.emulated_atomic64);
}

enum class UnrealRhiFeatureLevel {
  Unsupported,
  Sm5,
  Sm6,
};

UnrealRhiFeatureLevel
ResolveUnrealRhiFeatureLevel(const Sm6PolicyCase &capabilities) {
  if (SupportsUnrealSm6(capabilities))
    return UnrealRhiFeatureLevel::Sm6;
  if (capabilities.feature_level >= D3D_FEATURE_LEVEL_11_0)
    return UnrealRhiFeatureLevel::Sm5;
  return UnrealRhiFeatureLevel::Unsupported;
}

class UnrealD3D12Sm6PolicySpec
    : public ::testing::Test,
      public ::testing::WithParamInterface<Sm6PolicyCase> {};

TEST_P(UnrealD3D12Sm6PolicySpec, AppliesEveryFeatureGate) {
  EXPECT_EQ(SupportsUnrealSm6(GetParam()), GetParam().expected);
}

INSTANTIATE_TEST_SUITE_P(
    CapabilityMatrix, UnrealD3D12Sm6PolicySpec,
    ::testing::Values(
        Sm6PolicyCase{D3D_FEATURE_LEVEL_12_0, D3D_SHADER_MODEL_6_6,
                      D3D12_RESOURCE_BINDING_TIER_2, true, true, false, false,
                      true, "Baseline"},
        Sm6PolicyCase{D3D_FEATURE_LEVEL_11_1, D3D_SHADER_MODEL_6_6,
                      D3D12_RESOURCE_BINDING_TIER_2, true, true, false, false,
                      false, "FeatureLevel"},
        Sm6PolicyCase{D3D_FEATURE_LEVEL_12_0, D3D_SHADER_MODEL_6_5,
                      D3D12_RESOURCE_BINDING_TIER_2, true, true, false, false,
                      false, "ShaderModel"},
        Sm6PolicyCase{D3D_FEATURE_LEVEL_12_0, D3D_SHADER_MODEL_6_6,
                      D3D12_RESOURCE_BINDING_TIER_2, false, true, false, false,
                      false, "WaveOps"},
        Sm6PolicyCase{D3D_FEATURE_LEVEL_12_0, D3D_SHADER_MODEL_6_6,
                      D3D12_RESOURCE_BINDING_TIER_1, true, true, false, false,
                      false, "BindingTier"},
        Sm6PolicyCase{D3D_FEATURE_LEVEL_12_0, D3D_SHADER_MODEL_6_6,
                      D3D12_RESOURCE_BINDING_TIER_2, true, false, false, false,
                      false, "Atomic64"},
        Sm6PolicyCase{D3D_FEATURE_LEVEL_12_0, D3D_SHADER_MODEL_6_6,
                      D3D12_RESOURCE_BINDING_TIER_2, true, false, true, false,
                      true, "EmulatedAtomic64"},
        Sm6PolicyCase{D3D_FEATURE_LEVEL_12_0, D3D_SHADER_MODEL_6_6,
                      D3D12_RESOURCE_BINDING_TIER_2, true, true, false, true,
                      false, "BindlessTier2"},
        Sm6PolicyCase{D3D_FEATURE_LEVEL_12_0, D3D_SHADER_MODEL_6_6,
                      D3D12_RESOURCE_BINDING_TIER_3, true, true, false, true,
                      true, "BindlessTier3"}),
    [](const ::testing::TestParamInfo<Sm6PolicyCase> &info) {
      return std::string(info.param.name);
    });

TEST(UnrealD3D12FeaturePolicySpec, FallsBackToCreationFeatureLevel) {
  EXPECT_EQ(ResolveFeatureLevel(E_INVALIDARG, D3D_FEATURE_LEVEL(0),
                                D3D_FEATURE_LEVEL_11_0),
            D3D_FEATURE_LEVEL_11_0);
  EXPECT_EQ(
      ResolveFeatureLevel(S_OK, D3D_FEATURE_LEVEL_12_0, D3D_FEATURE_LEVEL_11_0),
      D3D_FEATURE_LEVEL_12_0);
}

TEST(UnrealD3D12FeaturePolicySpec, FallsBackFromSm6ToSm5) {
  Sm6PolicyCase capabilities = {D3D_FEATURE_LEVEL_12_0,
                                D3D_SHADER_MODEL_6_6,
                                D3D12_RESOURCE_BINDING_TIER_2,
                                true,
                                true,
                                false,
                                false,
                                true,
                                "Sm6"};
  EXPECT_EQ(ResolveUnrealRhiFeatureLevel(capabilities),
            UnrealRhiFeatureLevel::Sm6);

  capabilities.typed_atomic64 = false;
  EXPECT_EQ(ResolveUnrealRhiFeatureLevel(capabilities),
            UnrealRhiFeatureLevel::Sm5);

  capabilities.feature_level = D3D_FEATURE_LEVEL_10_0;
  EXPECT_EQ(ResolveUnrealRhiFeatureLevel(capabilities),
            UnrealRhiFeatureLevel::Unsupported);
}

class D3D12UnrealCapabilitySpec : public ::testing::Test {
protected:
  void SetUp() override {
    HRESULT hr = CreateDXGIFactory2(0, __uuidof(IDXGIFactory4),
                                    reinterpret_cast<void **>(factory_.put()));
    ASSERT_TRUE(HResultSucceeded(hr));
    ASSERT_TRUE(factory_);

    ComPtr<IDXGIFactory6> factory6;
    const HRESULT factory6_hr = factory_->QueryInterface(
        __uuidof(IDXGIFactory6), reinterpret_cast<void **>(factory6.put()));
    if (SUCCEEDED(factory6_hr)) {
      ASSERT_TRUE(factory6);
    }
    if (factory6) {
      hr = factory6->EnumAdapterByGpuPreference(
          0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, __uuidof(IDXGIAdapter1),
          reinterpret_cast<void **>(adapter_.put()));
    } else {
      hr = factory_->EnumAdapters1(0, adapter_.put());
    }
    ASSERT_TRUE(HResultSucceeded(hr));
    ASSERT_TRUE(adapter_);
    ASSERT_TRUE(HResultSucceeded(adapter_->GetDesc1(&adapter_desc_)));

    hr = D3D12CreateDevice(adapter_.get(), D3D_FEATURE_LEVEL_11_0,
                           __uuidof(ID3D12Device),
                           reinterpret_cast<void **>(device_.put()));
    ASSERT_TRUE(HResultSucceeded(hr));
    ASSERT_TRUE(device_);
  }

  ComPtr<IDXGIFactory4> factory_;
  ComPtr<IDXGIAdapter1> adapter_;
  ComPtr<ID3D12Device> device_;
  DXGI_ADAPTER_DESC1 adapter_desc_ = {};
};

struct FormatExpectation {
  DXGI_FORMAT format;
  D3D12_FORMAT_SUPPORT1 support1;
  const char *name;
};

class D3D12UnrealFormatSpec
    : public D3D12UnrealCapabilitySpec,
      public ::testing::WithParamInterface<FormatExpectation> {};

class D3D12UnrealQueueSpec
    : public D3D12UnrealCapabilitySpec,
      public ::testing::WithParamInterface<D3D12_COMMAND_LIST_TYPE> {};

TEST_F(D3D12UnrealCapabilitySpec,
       EnumeratesHighPerformanceAdapterWithStableIdentity) {
  EXPECT_NE(adapter_desc_.Description[0], L'\0');
  EXPECT_NE(adapter_desc_.VendorId, 0u);
  EXPECT_EQ(adapter_desc_.Flags & DXGI_ADAPTER_FLAG_SOFTWARE, 0u);
}

TEST_F(D3D12UnrealCapabilitySpec, RecreatedFactoryFindsTheSameAdapterIdentity) {
  ComPtr<IDXGIFactory4> recreated_factory;
  ASSERT_TRUE(HResultSucceeded(
      CreateDXGIFactory2(0, __uuidof(IDXGIFactory4),
                         reinterpret_cast<void **>(recreated_factory.put()))));
  ASSERT_TRUE(recreated_factory);

  ComPtr<IDXGIAdapter1> recreated_adapter;
  ComPtr<IDXGIFactory6> recreated_factory6;
  HRESULT hr = recreated_factory->QueryInterface(
      __uuidof(IDXGIFactory6),
      reinterpret_cast<void **>(recreated_factory6.put()));
  if (SUCCEEDED(hr)) {
    ASSERT_TRUE(recreated_factory6);
  }
  if (SUCCEEDED(hr) && recreated_factory6) {
    hr = recreated_factory6->EnumAdapterByGpuPreference(
        0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, __uuidof(IDXGIAdapter1),
        reinterpret_cast<void **>(recreated_adapter.put()));
  } else {
    hr = recreated_factory->EnumAdapters1(0, recreated_adapter.put());
  }
  ASSERT_TRUE(HResultSucceeded(hr));
  ASSERT_TRUE(recreated_adapter);

  DXGI_ADAPTER_DESC1 recreated_desc = {};
  ASSERT_TRUE(HResultSucceeded(recreated_adapter->GetDesc1(&recreated_desc)));
  EXPECT_EQ(recreated_desc.VendorId, adapter_desc_.VendorId);
  EXPECT_EQ(recreated_desc.DeviceId, adapter_desc_.DeviceId);
  EXPECT_EQ(recreated_desc.SubSysId, adapter_desc_.SubSysId);
  EXPECT_EQ(recreated_desc.Revision, adapter_desc_.Revision);
}

TEST_F(D3D12UnrealCapabilitySpec, CreatesExplicitFeatureLevel11Device) {
  EXPECT_NE(device_.get(), nullptr);
  EXPECT_GE(device_->GetNodeCount(), 1u);
}

TEST_F(D3D12UnrealCapabilitySpec, ExposesDeviceInterfacesRequiredByUnreal) {
  ComPtr<ID3D12Device1> device1;
  ComPtr<ID3D12Device2> device2;
  ASSERT_TRUE(HResultSucceeded(device_->QueryInterface(
      __uuidof(ID3D12Device1), reinterpret_cast<void **>(device1.put()))));
  ASSERT_TRUE(device1);
  ASSERT_TRUE(HResultSucceeded(device_->QueryInterface(
      __uuidof(ID3D12Device2), reinterpret_cast<void **>(device2.put()))));
  ASSERT_TRUE(device2);
}

TEST_F(D3D12UnrealCapabilitySpec, SelectsHighestSupportedFeatureLevel) {
  const D3D_FEATURE_LEVEL highest =
      FindHighestFeatureLevel(device_.get(), D3D_FEATURE_LEVEL_11_0);
  EXPECT_GE(highest, D3D_FEATURE_LEVEL_11_0);
  EXPECT_LE(highest, D3D_FEATURE_LEVEL_12_2);
}

TEST_F(D3D12UnrealCapabilitySpec, CreatesFinalDeviceAtReportedFeatureLevel) {
  const D3D_FEATURE_LEVEL highest =
      FindHighestFeatureLevel(device_.get(), D3D_FEATURE_LEVEL_11_0);
  device_.reset();

  ComPtr<ID3D12Device> final_device;
  ASSERT_TRUE(HResultSucceeded(
      D3D12CreateDevice(adapter_.get(), highest, __uuidof(ID3D12Device),
                        reinterpret_cast<void **>(final_device.put()))));
  ASSERT_TRUE(final_device);
  EXPECT_GE(final_device->GetNodeCount(), 1u);
}

TEST_F(D3D12UnrealCapabilitySpec, SelectsHighestSupportedShaderModel) {
  const D3D_SHADER_MODEL shader_model = FindHighestShaderModel(device_.get());
  EXPECT_GE(shader_model, D3D_SHADER_MODEL_5_1);
  EXPECT_LE(shader_model, D3D_SHADER_MODEL_6_7);
}

TEST_F(D3D12UnrealCapabilitySpec, ResolvesLiveRhiFeatureLevelFromEverySm6Gate) {
  const D3D_FEATURE_LEVEL feature_level =
      FindHighestFeatureLevel(device_.get(), D3D_FEATURE_LEVEL_11_0);
  const D3D_SHADER_MODEL shader_model = FindHighestShaderModel(device_.get());

  D3D12_FEATURE_DATA_D3D12_OPTIONS options = {};
  const HRESULT options_hr = device_->CheckFeatureSupport(
      D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options));
  D3D12_FEATURE_DATA_D3D12_OPTIONS1 options1 = {};
  const HRESULT options1_hr = device_->CheckFeatureSupport(
      D3D12_FEATURE_D3D12_OPTIONS1, &options1, sizeof(options1));
  D3D12_FEATURE_DATA_D3D12_OPTIONS9 options9 = {};
  const HRESULT options9_hr = device_->CheckFeatureSupport(
      D3D12_FEATURE_D3D12_OPTIONS9, &options9, sizeof(options9));

  const Sm6PolicyCase live_capabilities = {
      feature_level,
      shader_model,
      SUCCEEDED(options_hr) ? options.ResourceBindingTier
                            : D3D12_RESOURCE_BINDING_TIER(0),
      SUCCEEDED(options1_hr) && options1.WaveOps,
      SUCCEEDED(options9_hr) && options9.AtomicInt64OnTypedResourceSupported,
      // DXMT's macOS Wine path has no Intel Windows driver-extension provider.
      false,
      false,
      false,
      "LiveDevice",
  };
  const UnrealRhiFeatureLevel selected =
      ResolveUnrealRhiFeatureLevel(live_capabilities);

  ASSERT_NE(selected, UnrealRhiFeatureLevel::Unsupported);
  if (selected == UnrealRhiFeatureLevel::Sm6) {
    EXPECT_GE(feature_level, D3D_FEATURE_LEVEL_12_0);
    EXPECT_GE(shader_model, D3D_SHADER_MODEL_6_6);
    EXPECT_TRUE(SUCCEEDED(options_hr));
    EXPECT_GE(options.ResourceBindingTier, D3D12_RESOURCE_BINDING_TIER_2);
    EXPECT_TRUE(SUCCEEDED(options1_hr));
    EXPECT_TRUE(options1.WaveOps);
    EXPECT_TRUE(SUCCEEDED(options9_hr));
    EXPECT_TRUE(options9.AtomicInt64OnTypedResourceSupported ||
                live_capabilities.emulated_atomic64);
  } else {
    EXPECT_EQ(selected, UnrealRhiFeatureLevel::Sm5);
    EXPECT_FALSE(SupportsUnrealSm6(live_capabilities));
  }
}

TEST_F(D3D12UnrealCapabilitySpec, ReportsResourceAndHeapTiers) {
  D3D12_FEATURE_DATA_D3D12_OPTIONS options = {};
  ASSERT_TRUE(HResultSucceeded(device_->CheckFeatureSupport(
      D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options))));

  EXPECT_GE(options.ResourceBindingTier, D3D12_RESOURCE_BINDING_TIER_1);
  EXPECT_LE(options.ResourceBindingTier, D3D12_RESOURCE_BINDING_TIER_3);
  EXPECT_GE(options.ResourceHeapTier, D3D12_RESOURCE_HEAP_TIER_1);
  EXPECT_LE(options.ResourceHeapTier, D3D12_RESOURCE_HEAP_TIER_2);
  EXPECT_GE(options.MaxGPUVirtualAddressBitsPerResource, 32u);
}

TEST_F(D3D12UnrealCapabilitySpec, ReportsUnifiedMemoryArchitecture) {
  D3D12_FEATURE_DATA_ARCHITECTURE architecture = {};
  architecture.NodeIndex = 0;
  const HRESULT hr = device_->CheckFeatureSupport(
      D3D12_FEATURE_ARCHITECTURE, &architecture, sizeof(architecture));
  const bool is_uma = SUCCEEDED(hr) && architecture.UMA;

  if (SUCCEEDED(hr)) {
    EXPECT_EQ(is_uma, architecture.UMA != FALSE);
  } else {
    EXPECT_FALSE(is_uma);
  }
  if (SUCCEEDED(hr) && !architecture.UMA) {
    EXPECT_FALSE(architecture.CacheCoherentUMA);
  }
}

TEST_F(D3D12UnrealCapabilitySpec, ReportsShaderWaveOperationContract) {
  D3D12_FEATURE_DATA_D3D12_OPTIONS1 options = {};
  const HRESULT hr = device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS1,
                                                  &options, sizeof(options));

  if (SUCCEEDED(hr) && options.WaveOps) {
    EXPECT_GT(options.WaveLaneCountMin, 0u);
    EXPECT_GE(options.WaveLaneCountMax, options.WaveLaneCountMin);
  } else {
    EXPECT_EQ(options.WaveLaneCountMin, 0u);
    EXPECT_EQ(options.WaveLaneCountMax, 0u);
  }
}


TEST_F(D3D12UnrealCapabilitySpec, NegotiatesRootSignatureVersion) {
  D3D12_FEATURE_DATA_ROOT_SIGNATURE root_signature = {};
  root_signature.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
  HRESULT hr = device_->CheckFeatureSupport(
      D3D12_FEATURE_ROOT_SIGNATURE, &root_signature, sizeof(root_signature));
  if (FAILED(hr)) {
    root_signature.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
    hr = device_->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE,
                                      &root_signature, sizeof(root_signature));
  }
  ASSERT_TRUE(HResultSucceeded(hr));
  EXPECT_GE(root_signature.HighestVersion, D3D_ROOT_SIGNATURE_VERSION_1_0);
  EXPECT_LE(root_signature.HighestVersion, D3D_ROOT_SIGNATURE_VERSION_1_1);
}

TEST_F(D3D12UnrealCapabilitySpec, ReportsBarrierAndQueueCapabilities) {
  D3D12_FEATURE_DATA_D3D12_OPTIONS3 options3 = {};
  ASSERT_TRUE(HResultSucceeded(device_->CheckFeatureSupport(
      D3D12_FEATURE_D3D12_OPTIONS3, &options3, sizeof(options3))));
  const auto write_buffer_support =
      D3D12_COMMAND_LIST_SUPPORT_FLAG_DIRECT |
      D3D12_COMMAND_LIST_SUPPORT_FLAG_BUNDLE |
      D3D12_COMMAND_LIST_SUPPORT_FLAG_COMPUTE |
      D3D12_COMMAND_LIST_SUPPORT_FLAG_COPY;
  EXPECT_EQ(options3.WriteBufferImmediateSupportFlags, write_buffer_support);

  D3D12_FEATURE_DATA_COMMAND_QUEUE_PRIORITY queue_priority = {};
  queue_priority.CommandListType = D3D12_COMMAND_LIST_TYPE_DIRECT;
  queue_priority.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
  ASSERT_TRUE(HResultSucceeded(
      device_->CheckFeatureSupport(D3D12_FEATURE_COMMAND_QUEUE_PRIORITY,
                                   &queue_priority, sizeof(queue_priority))));
  EXPECT_TRUE(queue_priority.PriorityForTypeIsSupported);
}

TEST_F(D3D12UnrealCapabilitySpec,
       KeepsEnhancedBarrierAdvertisementConsistentWithDeviceInterface) {
  D3D12_FEATURE_DATA_D3D12_OPTIONS12 options12 = {};
  const HRESULT options12_hr = device_->CheckFeatureSupport(
      D3D12_FEATURE_D3D12_OPTIONS12, &options12, sizeof(options12));

  ComPtr<ID3D12Device10> device10;
  const HRESULT device10_hr = device_->QueryInterface(
      __uuidof(ID3D12Device10), reinterpret_cast<void **>(device10.put()));
  if (SUCCEEDED(options12_hr) && options12.EnhancedBarriersSupported) {
    EXPECT_TRUE(HResultSucceeded(device10_hr));
    EXPECT_TRUE(device10);
  } else {
    EXPECT_FALSE(SUCCEEDED(options12_hr) &&
                 options12.EnhancedBarriersSupported);
  }
}

TEST_F(D3D12UnrealCapabilitySpec, ReportsOptionalRenderingTiers) {
  D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};
  const HRESULT options5_hr = device_->CheckFeatureSupport(
      D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5));
  if (SUCCEEDED(options5_hr)) {
    EXPECT_GE(options5.RenderPassesTier, D3D12_RENDER_PASS_TIER_0);
    EXPECT_GE(options5.RaytracingTier, D3D12_RAYTRACING_TIER_NOT_SUPPORTED);
  }

  D3D12_FEATURE_DATA_D3D12_OPTIONS7 options7 = {};
  const HRESULT options7_hr = device_->CheckFeatureSupport(
      D3D12_FEATURE_D3D12_OPTIONS7, &options7, sizeof(options7));
  if (SUCCEEDED(options7_hr)) {
    EXPECT_GE(options7.MeshShaderTier, D3D12_MESH_SHADER_TIER_NOT_SUPPORTED);
  }
}

TEST_F(D3D12UnrealCapabilitySpec, ReportsGpuVirtualAddressLimits) {
  D3D12_FEATURE_DATA_GPU_VIRTUAL_ADDRESS_SUPPORT support = {};
  ASSERT_TRUE(HResultSucceeded(device_->CheckFeatureSupport(
      D3D12_FEATURE_GPU_VIRTUAL_ADDRESS_SUPPORT, &support, sizeof(support))));
  EXPECT_GE(support.MaxGPUVirtualAddressBitsPerResource, 32u);
  EXPECT_GE(support.MaxGPUVirtualAddressBitsPerProcess,
            support.MaxGPUVirtualAddressBitsPerResource);
}

TEST_F(D3D12UnrealCapabilitySpec, ReportsLocalVideoMemoryBudget) {
  ComPtr<IDXGIAdapter3> adapter3;
  ASSERT_TRUE(HResultSucceeded(adapter_->QueryInterface(
      __uuidof(IDXGIAdapter3), reinterpret_cast<void **>(adapter3.put()))));
  ASSERT_TRUE(adapter3);

  DXGI_QUERY_VIDEO_MEMORY_INFO memory_info = {};
  ASSERT_TRUE(HResultSucceeded(adapter3->QueryVideoMemoryInfo(
      0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &memory_info)));
  EXPECT_GT(memory_info.Budget, 0ull);
  EXPECT_LE(memory_info.CurrentReservation, memory_info.Budget);
}

TEST_F(D3D12UnrealCapabilitySpec, AppliesAndRestoresUnrealMemoryReservation) {
  ComPtr<IDXGIAdapter3> adapter3;
  ASSERT_TRUE(HResultSucceeded(adapter_->QueryInterface(
      __uuidof(IDXGIAdapter3), reinterpret_cast<void **>(adapter3.put()))));
  ASSERT_TRUE(adapter3);

  DXGI_QUERY_VIDEO_MEMORY_INFO before = {};
  ASSERT_TRUE(HResultSucceeded(adapter3->QueryVideoMemoryInfo(
      0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &before)));
  const UINT64 reservation =
      std::min(before.AvailableForReservation, before.Budget * 9 / 10);
  ASSERT_TRUE(HResultSucceeded(adapter3->SetVideoMemoryReservation(
      0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, reservation)));

  struct RestoreGuard {
    IDXGIAdapter3 *adapter;
    UINT64 reservation;
    bool active = true;
    ~RestoreGuard() {
      if (active)
        adapter->SetVideoMemoryReservation(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL,
                                           reservation);
    }
  } restore{adapter3.get(), before.CurrentReservation};

  DXGI_QUERY_VIDEO_MEMORY_INFO during = {};
  const HRESULT during_hr = adapter3->QueryVideoMemoryInfo(
      0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &during);
  const HRESULT restore_hr = adapter3->SetVideoMemoryReservation(
      0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, before.CurrentReservation);
  if (SUCCEEDED(restore_hr))
    restore.active = false;

  EXPECT_TRUE(HResultSucceeded(during_hr));
  if (SUCCEEDED(during_hr)) {
    EXPECT_EQ(during.CurrentReservation, reservation);
  }
  EXPECT_TRUE(HResultSucceeded(restore_hr));

  DXGI_QUERY_VIDEO_MEMORY_INFO restored = {};
  ASSERT_TRUE(HResultSucceeded(adapter3->QueryVideoMemoryInfo(
      0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &restored)));
  EXPECT_EQ(restored.CurrentReservation, before.CurrentReservation);
}

TEST_P(D3D12UnrealQueueSpec, CreatesAndSignalsEveryUnrealQueueType) {
  D3D12_COMMAND_QUEUE_DESC queue_desc = {};
  queue_desc.Type = GetParam();
  queue_desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
  queue_desc.NodeMask = 1;
  ComPtr<ID3D12CommandQueue> queue;
  ASSERT_TRUE(HResultSucceeded(
      device_->CreateCommandQueue(&queue_desc, __uuidof(ID3D12CommandQueue),
                                  reinterpret_cast<void **>(queue.put()))));
  ASSERT_TRUE(queue);

  ComPtr<ID3D12Fence> fence;
  ASSERT_TRUE(HResultSucceeded(
      device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence),
                           reinterpret_cast<void **>(fence.put()))));
  ASSERT_TRUE(fence);
  ASSERT_TRUE(HResultSucceeded(queue->Signal(fence.get(), 1)));
  if (fence->GetCompletedValue() < 1) {
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    ASSERT_NE(event, nullptr);
    const HRESULT event_hr = fence->SetEventOnCompletion(1, event);
    DWORD wait_result = WAIT_FAILED;
    if (SUCCEEDED(event_hr))
      wait_result = WaitForSingleObject(event, 10000);
    CloseHandle(event);
    EXPECT_TRUE(HResultSucceeded(event_hr));
    if (SUCCEEDED(event_hr)) {
      EXPECT_EQ(wait_result, WAIT_OBJECT_0);
    }
  }
  EXPECT_GE(fence->GetCompletedValue(), 1ull);
}

INSTANTIATE_TEST_SUITE_P(
    UnrealDeviceQueues, D3D12UnrealQueueSpec,
    ::testing::Values(D3D12_COMMAND_LIST_TYPE_DIRECT,
                      D3D12_COMMAND_LIST_TYPE_COMPUTE,
                      D3D12_COMMAND_LIST_TYPE_COPY),
    [](const ::testing::TestParamInfo<D3D12_COMMAND_LIST_TYPE> &info) {
      switch (info.param) {
      case D3D12_COMMAND_LIST_TYPE_DIRECT:
        return "Direct";
      case D3D12_COMMAND_LIST_TYPE_COMPUTE:
        return "Compute";
      case D3D12_COMMAND_LIST_TYPE_COPY:
        return "Copy";
      default:
        return "Unknown";
      }
    });

TEST_P(D3D12UnrealFormatSpec, ReportsRequiredPixelFormatCapabilities) {
  const auto expectation = GetParam();
  D3D12_FEATURE_DATA_FORMAT_SUPPORT support = {};
  support.Format = expectation.format;
  ASSERT_TRUE(HResultSucceeded(device_->CheckFeatureSupport(
      D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support))));
  EXPECT_EQ(support.Support1 & expectation.support1, expectation.support1);

  D3D12_FEATURE_DATA_FORMAT_INFO info = {};
  info.Format = expectation.format;
  ASSERT_TRUE(HResultSucceeded(device_->CheckFeatureSupport(
      D3D12_FEATURE_FORMAT_INFO, &info, sizeof(info))));
  EXPECT_GE(info.PlaneCount, 1u);
}

TEST_F(D3D12UnrealCapabilitySpec,
       MapsTypedUavSupportWithoutRequiringOptionalFlags) {
  D3D12_FEATURE_DATA_FORMAT_SUPPORT support = {};
  support.Format = DXGI_FORMAT_R32_UINT;
  const HRESULT hr = device_->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT,
                                                  &support, sizeof(support));

  const bool has_uav =
      SUCCEEDED(hr) &&
      (support.Support1 & D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW);
  const bool has_store =
      SUCCEEDED(hr) &&
      (support.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE);
  const bool has_atomics =
      SUCCEEDED(hr) &&
      (support.Support2 & D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_EXCHANGE);
  EXPECT_FALSE(has_store && !has_uav);
  EXPECT_FALSE(has_atomics && !has_uav);
}

INSTANTIATE_TEST_SUITE_P(
    UnrealBootstrapFormats, D3D12UnrealFormatSpec,
    ::testing::Values(
        FormatExpectation{DXGI_FORMAT_R8G8B8A8_UNORM,
                          D3D12_FORMAT_SUPPORT1_TEXTURE2D |
                              D3D12_FORMAT_SUPPORT1_RENDER_TARGET |
                              D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE |
                              D3D12_FORMAT_SUPPORT1_BLENDABLE,
                          "Color8"},
        FormatExpectation{DXGI_FORMAT_B8G8R8A8_UNORM,
                          D3D12_FORMAT_SUPPORT1_TEXTURE2D |
                              D3D12_FORMAT_SUPPORT1_RENDER_TARGET |
                              D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE,
                          "BackBuffer"},
        FormatExpectation{DXGI_FORMAT_R16G16B16A16_FLOAT,
                          D3D12_FORMAT_SUPPORT1_TEXTURE2D |
                              D3D12_FORMAT_SUPPORT1_RENDER_TARGET |
                              D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE,
                          "SceneColor"},
        FormatExpectation{DXGI_FORMAT_R32G8X24_TYPELESS,
                          D3D12_FORMAT_SUPPORT1_TEXTURE2D, "Depth32Resource"},
        FormatExpectation{DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS,
                          D3D12_FORMAT_SUPPORT1_TEXTURE2D, "Depth32Srv"},
        FormatExpectation{DXGI_FORMAT_D32_FLOAT_S8X24_UINT,
                          D3D12_FORMAT_SUPPORT1_TEXTURE2D |
                              D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL,
                          "Depth32Dsv"},
        FormatExpectation{DXGI_FORMAT_R24G8_TYPELESS,
                          D3D12_FORMAT_SUPPORT1_TEXTURE2D, "Depth24Resource"},
        FormatExpectation{DXGI_FORMAT_R24_UNORM_X8_TYPELESS,
                          D3D12_FORMAT_SUPPORT1_TEXTURE2D, "Depth24Srv"},
        FormatExpectation{DXGI_FORMAT_D24_UNORM_S8_UINT,
                          D3D12_FORMAT_SUPPORT1_TEXTURE2D |
                              D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL,
                          "Depth24Dsv"},
        FormatExpectation{DXGI_FORMAT_R16_TYPELESS,
                          D3D12_FORMAT_SUPPORT1_TEXTURE2D, "Depth16Resource"},
        FormatExpectation{DXGI_FORMAT_R16_UNORM,
                          D3D12_FORMAT_SUPPORT1_TEXTURE2D |
                              D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE,
                          "Depth16Srv"},
        FormatExpectation{DXGI_FORMAT_D16_UNORM,
                          D3D12_FORMAT_SUPPORT1_TEXTURE2D |
                              D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL,
                          "Depth16Dsv"},
        FormatExpectation{DXGI_FORMAT_R32_UINT,
                          D3D12_FORMAT_SUPPORT1_BUFFER |
                              D3D12_FORMAT_SUPPORT1_TEXTURE2D,
                          "UintUav"}),
    [](const ::testing::TestParamInfo<FormatExpectation> &info) {
      return std::string(info.param.name);
    });

TEST_F(D3D12UnrealCapabilitySpec,
       CreatesEveryReportedRenderTargetConfiguration) {
  for (const UINT sample_count : {1u, 2u, 4u, 8u}) {
    D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS levels = {};
    levels.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    levels.SampleCount = sample_count;
    ASSERT_TRUE(HResultSucceeded(device_->CheckFeatureSupport(
        D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &levels, sizeof(levels))))
        << sample_count << "x MSAA query failed";
    if (sample_count == 1) {
      ASSERT_GT(levels.NumQualityLevels, 0u);
    }
    if (levels.NumQualityLevels == 0)
      continue;

    ComPtr<ID3D12Resource> texture;
    EXPECT_TRUE(HResultSucceeded(CreateMsaaRenderTarget(
        device_.get(), levels.Format, sample_count, &texture)))
        << sample_count << "x MSAA was reported but could not be created";
  }
}



TEST_F(D3D12UnrealCapabilitySpec,
       ReportsConsistentMultisamplingAcrossTypelessFormatFamily) {
  constexpr std::array formats = {
      DXGI_FORMAT_R8G8B8A8_TYPELESS, DXGI_FORMAT_R8G8B8A8_UNORM,
      DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_R8G8B8A8_UINT,
      DXGI_FORMAT_R8G8B8A8_SNORM, DXGI_FORMAT_R8G8B8A8_SINT,
  };

  for (const UINT sample_count : {2u, 4u, 8u}) {
    UINT reference_quality_levels = UINT_MAX;
    for (const DXGI_FORMAT format : formats) {
      SCOPED_TRACE(::testing::Message()
                   << "format=" << static_cast<UINT>(format)
                   << " sample_count=" << sample_count);
      D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS levels = {};
      levels.Format = format;
      levels.SampleCount = sample_count;
      ASSERT_EQ(device_->CheckFeatureSupport(
                    D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &levels,
                    sizeof(levels)),
                S_OK);
      if (reference_quality_levels == UINT_MAX)
        reference_quality_levels = levels.NumQualityLevels;
      EXPECT_EQ(levels.NumQualityLevels, reference_quality_levels);
    }
  }
}

TEST_F(D3D12UnrealCapabilitySpec,
       MatchesMsaaReportToCommittedRenderTargetCreation) {
  constexpr std::array formats = {
      DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM,
      DXGI_FORMAT_R10G10B10A2_UNORM, DXGI_FORMAT_R16G16B16A16_FLOAT,
      DXGI_FORMAT_R32G32B32A32_FLOAT,
  };

  for (const DXGI_FORMAT format : formats) {
    for (const UINT sample_count : {2u, 4u, 8u}) {
      SCOPED_TRACE(::testing::Message()
                   << "format=" << static_cast<UINT>(format)
                   << " sample_count=" << sample_count);
      D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS levels = {};
      levels.Format = format;
      levels.SampleCount = sample_count;
      ASSERT_EQ(device_->CheckFeatureSupport(
                    D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &levels,
                    sizeof(levels)),
                S_OK);

      ComPtr<ID3D12Resource> texture;
      const HRESULT create_hr = CreateMsaaRenderTarget(
          device_.get(), format, sample_count, &texture);
      if (levels.NumQualityLevels) {
        EXPECT_TRUE(SUCCEEDED(create_hr));
        EXPECT_TRUE(texture);
      } else {
        EXPECT_TRUE(FAILED(create_hr));
        EXPECT_FALSE(texture);
      }
    }
  }
}

TEST_F(D3D12UnrealCapabilitySpec,
       MatchesTiledMsaaReportToReservedResourceCreation) {
  for (const UINT sample_count : {2u, 4u, 8u}) {
    SCOPED_TRACE(::testing::Message() << "sample_count=" << sample_count);
    D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS levels = {};
    levels.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    levels.SampleCount = sample_count;
    levels.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_TILED_RESOURCE;
    ASSERT_EQ(device_->CheckFeatureSupport(
                  D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &levels,
                  sizeof(levels)),
              S_OK);

    ComPtr<ID3D12Resource> texture;
    const HRESULT create_hr = CreateReservedMsaaRenderTarget(
        device_.get(), levels.Format, sample_count, &texture);
    if (levels.NumQualityLevels) {
      EXPECT_TRUE(SUCCEEDED(create_hr));
      EXPECT_TRUE(texture);
    } else {
      EXPECT_TRUE(FAILED(create_hr));
      EXPECT_FALSE(texture);
    }
  }
}

TEST_F(D3D12UnrealCapabilitySpec, ReportsEveryUnrealMsaaFallbackCandidate) {
  for (const UINT sample_count : {1u, 2u, 4u, 8u}) {
    D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS levels = {};
    levels.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    levels.SampleCount = sample_count;
    ASSERT_TRUE(HResultSucceeded(device_->CheckFeatureSupport(
        D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &levels, sizeof(levels))))
        << sample_count << "x MSAA query failed";
    if (sample_count == 1) {
      EXPECT_GT(levels.NumQualityLevels, 0u);
    }
  }
}

TEST_F(D3D12UnrealCapabilitySpec, CreatesBootstrapDescriptorHeaps) {
  for (const auto type :
       {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
        D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
        D3D12_DESCRIPTOR_HEAP_TYPE_DSV}) {
    EXPECT_GT(device_->GetDescriptorHandleIncrementSize(type), 0u);
    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.Type = type;
    desc.NumDescriptors =
        type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV ? 1024 : 64;
    desc.Flags = (type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV ||
                  type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER)
                     ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE
                     : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    ComPtr<ID3D12DescriptorHeap> heap;
    EXPECT_TRUE(HResultSucceeded(
        device_->CreateDescriptorHeap(&desc, __uuidof(ID3D12DescriptorHeap),
                                      reinterpret_cast<void **>(heap.put()))));
  }
}


} // namespace
