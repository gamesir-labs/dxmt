#include <dxmt_test.hpp>
#include <dxmt_test_com.hpp>

#include <d3d12_agility_public.hpp>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <array>
#include <cstdint>

namespace {

using dxmt::test::ComPtr;
using D3D12GetInterfaceProc = HRESULT(WINAPI *)(REFCLSID, REFIID, void **);

constexpr UINT kAgilitySdkVersion = 616;

D3D12GetInterfaceProc GetInterfaceProc() {
  HMODULE module = GetModuleHandleW(L"d3d12.dll");
  if (!module)
    module = LoadLibraryW(L"d3d12.dll");
  return module ? reinterpret_cast<D3D12GetInterfaceProc>(
                      GetProcAddress(module, "D3D12GetInterface"))
                : nullptr;
}

ComPtr<ID3D12SDKConfiguration1> CreateSdkConfiguration() {
  const auto get_interface = GetInterfaceProc();
  if (!get_interface)
    return {};

  ComPtr<ID3D12SDKConfiguration1> configuration;
  const HRESULT hr = get_interface(
      kCLSID_D3D12SDKConfiguration, __uuidof(ID3D12SDKConfiguration1),
      reinterpret_cast<void **>(configuration.put()));
  return SUCCEEDED(hr) ? std::move(configuration)
                       : ComPtr<ID3D12SDKConfiguration1>{};
}

ComPtr<ID3D12DeviceFactory>
CreateDeviceFactory(ID3D12SDKConfiguration1 *configuration) {
  ComPtr<ID3D12DeviceFactory> factory;
  const HRESULT hr = configuration->CreateDeviceFactory(
      kAgilitySdkVersion, ".\\D3D12\\", __uuidof(ID3D12DeviceFactory),
      reinterpret_cast<void **>(factory.put()));
  return SUCCEEDED(hr) ? std::move(factory) : ComPtr<ID3D12DeviceFactory>{};
}

DXMT_SERIAL_TEST(D3D12AgilityPublicApiSpec,
                 ConfigurationAndFactoryUsePublicInterfaces) {
  const auto get_interface = GetInterfaceProc();
  if (!get_interface)
    GTEST_SKIP() << "D3D12GetInterface is unavailable";

  EXPECT_EQ(get_interface(kCLSID_D3D12SDKConfiguration,
                          __uuidof(ID3D12SDKConfiguration), nullptr),
            S_FALSE);

  auto configuration = CreateSdkConfiguration();
  if (!configuration)
    GTEST_SKIP() << "ID3D12SDKConfiguration1 is unavailable";

  ComPtr<ID3D12SDKConfiguration> base_configuration;
  ASSERT_EQ(configuration->QueryInterface(
                __uuidof(ID3D12SDKConfiguration),
                reinterpret_cast<void **>(base_configuration.put())),
            S_OK);
  ASSERT_TRUE(base_configuration);

  const HRESULT set_version =
      base_configuration->SetSDKVersion(kAgilitySdkVersion, ".\\D3D12\\");
  auto factory = CreateDeviceFactory(configuration.get());
  if (FAILED(set_version) || !factory)
    GTEST_SKIP() << "an app-local Agility SDK is not staged";

  const auto flags = static_cast<D3D12_DEVICE_FACTORY_FLAGS>(
      D3D12_DEVICE_FACTORY_FLAG_ALLOW_RETURNING_EXISTING_DEVICE |
      D3D12_DEVICE_FACTORY_FLAG_DISALLOW_STORING_NEW_DEVICE_AS_SINGLETON);
  ASSERT_EQ(factory->SetFlags(flags), S_OK);
  EXPECT_EQ(factory->GetFlags(), flags);
  EXPECT_EQ(factory->ApplyToGlobalState(), S_OK);
  EXPECT_EQ(factory->InitializeFromGlobalState(), S_OK);

  const HRESULT feature_hr =
      factory->EnableExperimentalFeatures(0, nullptr, nullptr, nullptr);
  EXPECT_TRUE(feature_hr == S_OK || feature_hr == DXGI_ERROR_UNSUPPORTED);

  ComPtr<ID3D12Device> device;
  ASSERT_EQ(factory->CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                                  __uuidof(ID3D12Device),
                                  reinterpret_cast<void **>(device.put())),
            S_OK);
  EXPECT_TRUE(device);
  device.reset();
  factory.reset();
  base_configuration.reset();
  configuration->FreeUnusedSDKs();
}

DXMT_SERIAL_TEST(D3D12AgilityPublicApiSpec,
                 DeviceConfigurationExposesRootSignatureAndDredContracts) {
  auto sdk_configuration = CreateSdkConfiguration();
  if (!sdk_configuration)
    GTEST_SKIP() << "ID3D12SDKConfiguration1 is unavailable";
  auto factory = CreateDeviceFactory(sdk_configuration.get());
  if (!factory)
    GTEST_SKIP() << "an app-local Agility SDK is not staged";

  ComPtr<ID3D12DeviceConfiguration1> configuration;
  ASSERT_EQ(factory->QueryInterface(
                __uuidof(ID3D12DeviceConfiguration1),
                reinterpret_cast<void **>(configuration.put())),
            S_OK);
  const auto desc = configuration->GetDesc();
  EXPECT_GT(desc.SDKVersion, 0u);
  EXPECT_EQ(configuration->GetEnabledExperimentalFeatures(
                nullptr, desc.NumEnabledExperimentalFeatures),
            S_OK);

  D3D12_VERSIONED_ROOT_SIGNATURE_DESC root_desc = {};
  root_desc.Version = D3D_ROOT_SIGNATURE_VERSION_1_0;
  root_desc.Desc_1_0.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
  ComPtr<ID3DBlob> blob;
  ComPtr<ID3DBlob> error;
  ASSERT_EQ(configuration->SerializeVersionedRootSignature(
                &root_desc, blob.put(), error.put()),
            S_OK);
  ASSERT_TRUE(blob);

  ComPtr<ID3D12VersionedRootSignatureDeserializer> deserializer;
  ASSERT_EQ(configuration->CreateVersionedRootSignatureDeserializer(
                blob->GetBufferPointer(), blob->GetBufferSize(),
                __uuidof(ID3D12VersionedRootSignatureDeserializer),
                reinterpret_cast<void **>(deserializer.put())),
            S_OK);
  ASSERT_TRUE(deserializer);

  const std::array<std::uint8_t, 4> invalid_library = {0xde, 0xad, 0xbe, 0xef};
  void *invalid_output = reinterpret_cast<void *>(std::uintptr_t{1});
  EXPECT_TRUE(FAILED(
      configuration->CreateVersionedRootSignatureDeserializerFromSubobjectInLibrary(
          invalid_library.data(), invalid_library.size(), L"MissingRootSignature",
          __uuidof(ID3D12VersionedRootSignatureDeserializer), &invalid_output)));
  EXPECT_EQ(invalid_output, nullptr);

  ComPtr<ID3D12DeviceRemovedExtendedDataSettings1> dred;
  const HRESULT dred_hr = factory->GetConfigurationInterface(
      kCLSID_D3D12DeviceRemovedExtendedData,
      __uuidof(ID3D12DeviceRemovedExtendedDataSettings1),
      reinterpret_cast<void **>(dred.put()));
  if (SUCCEEDED(dred_hr) && dred) {
    dred->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_SYSTEM_CONTROLLED);
    dred->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_SYSTEM_CONTROLLED);
    dred->SetWatsonDumpEnablement(D3D12_DRED_ENABLEMENT_SYSTEM_CONTROLLED);
    dred->SetBreadcrumbContextEnablement(
        D3D12_DRED_ENABLEMENT_SYSTEM_CONTROLLED);
  }
  dred.reset();
  deserializer.reset();
  error.reset();
  blob.reset();
  configuration.reset();
  factory.reset();
  sdk_configuration->FreeUnusedSDKs();
}

} // namespace
