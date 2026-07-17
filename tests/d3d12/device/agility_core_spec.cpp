#include <dxmt_test.hpp>
#include <dxmt_test_com.hpp>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>

#include "../../../src/d3d12/d3d12_agility.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <thread>
#include <utility>
#include <vector>

namespace {

using dxmt::test::ComPtr;

using D3D12GetDebugInterfaceProc = HRESULT(WINAPI *)(REFIID, void **);
using D3D12GetInterfaceProc = HRESULT(WINAPI *)(REFCLSID, REFIID, void **);
using D3D12EnableExperimentalFeaturesProc = HRESULT(WINAPI *)(
    UINT, const IID *, void *, UINT *);
using DXMTCreateD3D12DeviceFactoryProc = HRESULT(WINAPI *)(REFIID, void **);

constexpr GUID kUnsupportedClass = {
    0x44b2118a,
    0x4b57,
    0x42a2,
    {0xb7, 0xf2, 0x15, 0xc3, 0xb8, 0x79, 0x9c, 0x07}};
constexpr GUID kUnsupportedInterface = {
    0x24fbfca0,
    0xbefa,
    0x4c5d,
    {0x89, 0x64, 0xb6, 0xda, 0x75, 0x8a, 0x5d, 0xa2}};

template <typename Proc>
Proc LoadD3D12Proc(const char *name) {
  HMODULE module = GetModuleHandleW(L"d3d12.dll");
  if (!module)
    module = LoadLibraryW(L"d3d12.dll");
  return module ? reinterpret_cast<Proc>(GetProcAddress(module, name))
                : nullptr;
}

template <typename Proc>
Proc LoadD3D12CoreProc(const char *name) {
  HMODULE module = GetModuleHandleW(L"d3d12core.dll");
  if (!module)
    module = LoadLibraryW(L"d3d12core.dll");
  return module ? reinterpret_cast<Proc>(GetProcAddress(module, name))
                : nullptr;
}

D3D12GetInterfaceProc GetInterfaceProc() {
  return LoadD3D12Proc<D3D12GetInterfaceProc>("D3D12GetInterface");
}

ComPtr<ID3D12SDKConfiguration1> CreateSdkConfiguration() {
  const auto get_interface = GetInterfaceProc();
  if (!get_interface)
    return {};

  ComPtr<ID3D12SDKConfiguration1> configuration;
  const HRESULT hr = get_interface(
      dxmt::d3d12::kCLSID_D3D12SDKConfiguration,
      __uuidof(ID3D12SDKConfiguration1),
      reinterpret_cast<void **>(configuration.put()));
  return SUCCEEDED(hr) ? std::move(configuration)
                       : ComPtr<ID3D12SDKConfiguration1>{};
}

ComPtr<ID3D12DeviceFactory> CreateDeviceFactory() {
  auto configuration = CreateSdkConfiguration();
  if (!configuration)
    return {};

  ComPtr<ID3D12DeviceFactory> factory;
  const HRESULT hr = configuration->CreateDeviceFactory(
      dxmt::d3d12::kAgilitySdkVersion, ".\\D3D12\\",
      __uuidof(ID3D12DeviceFactory),
      reinterpret_cast<void **>(factory.put()));
  return SUCCEEDED(hr) ? std::move(factory) : ComPtr<ID3D12DeviceFactory>{};
}

TEST(D3D12AgilityCoreSpec, DebugInterfaceIsUnsupportedAndClearsOutput) {
  const auto get_debug =
      LoadD3D12Proc<D3D12GetDebugInterfaceProc>("D3D12GetDebugInterface");
  ASSERT_NE(get_debug, nullptr);

  void *output = reinterpret_cast<void *>(std::uintptr_t{1});
  EXPECT_EQ(get_debug(__uuidof(IUnknown), &output), E_NOINTERFACE);
  EXPECT_EQ(output, nullptr);
  EXPECT_EQ(get_debug(__uuidof(IUnknown), nullptr), E_NOINTERFACE);
}

TEST(D3D12AgilityCoreSpec, GetInterfaceRejectsNullAndUnknownRequests) {
  const auto get_interface = GetInterfaceProc();
  ASSERT_NE(get_interface, nullptr);

  EXPECT_EQ(get_interface(dxmt::d3d12::kCLSID_D3D12SDKConfiguration,
                          __uuidof(ID3D12SDKConfiguration1), nullptr),
            E_POINTER);

  void *output = reinterpret_cast<void *>(std::uintptr_t{1});
  EXPECT_EQ(get_interface(kUnsupportedClass, __uuidof(IUnknown), &output),
            E_NOINTERFACE);
  EXPECT_EQ(output, nullptr);

  output = reinterpret_cast<void *>(std::uintptr_t{1});
  EXPECT_EQ(get_interface(dxmt::d3d12::kCLSID_D3D12SDKConfiguration,
                          kUnsupportedInterface, &output),
            E_NOINTERFACE);
  EXPECT_EQ(output, nullptr);
}

TEST(D3D12AgilityCoreSpec, SdkConfigurationSupportsBothInterfaceVersions) {
  auto configuration1 = CreateSdkConfiguration();
  ASSERT_TRUE(configuration1);

  ComPtr<ID3D12SDKConfiguration> configuration;
  ComPtr<IUnknown> identity1;
  ComPtr<IUnknown> identity;
  EXPECT_EQ(configuration1->QueryInterface(
                __uuidof(ID3D12SDKConfiguration),
                reinterpret_cast<void **>(configuration.put())),
            S_OK);
  ASSERT_TRUE(configuration);
  EXPECT_EQ(configuration1->QueryInterface(
                __uuidof(IUnknown),
                reinterpret_cast<void **>(identity1.put())),
            S_OK);
  EXPECT_EQ(configuration->QueryInterface(
                __uuidof(IUnknown), reinterpret_cast<void **>(identity.put())),
            S_OK);
  EXPECT_EQ(identity1.get(), identity.get());
}

TEST(D3D12AgilityCoreSpec, SdkConfigurationAcceptsVersionAndPathRequests) {
  auto configuration = CreateSdkConfiguration();
  ASSERT_TRUE(configuration);

  EXPECT_EQ(configuration->SetSDKVersion(dxmt::d3d12::kAgilitySdkVersion,
                                         ".\\D3D12\\"),
            S_OK);
  EXPECT_EQ(configuration->SetSDKVersion(0, nullptr), S_OK);
  configuration->FreeUnusedSDKs();
}

TEST(D3D12AgilityCoreSpec,
     SdkConfigurationCreatesFactoryAndValidatesOutput) {
  auto configuration = CreateSdkConfiguration();
  ASSERT_TRUE(configuration);

  EXPECT_EQ(configuration->CreateDeviceFactory(
                dxmt::d3d12::kAgilitySdkVersion, nullptr,
                __uuidof(ID3D12DeviceFactory), nullptr),
            E_POINTER);

  void *output = reinterpret_cast<void *>(std::uintptr_t{1});
  EXPECT_EQ(configuration->CreateDeviceFactory(
                dxmt::d3d12::kAgilitySdkVersion, nullptr,
                kUnsupportedInterface, &output),
            E_NOINTERFACE);
  EXPECT_EQ(output, nullptr);

  auto factory = CreateDeviceFactory();
  EXPECT_TRUE(factory);
}

TEST(D3D12AgilityCoreSpec, GetInterfaceCreatesDeviceFactoryDirectly) {
  const auto get_interface = GetInterfaceProc();
  ASSERT_NE(get_interface, nullptr);

  ComPtr<ID3D12DeviceFactory> factory;
  EXPECT_EQ(get_interface(
                dxmt::d3d12::kCLSID_D3D12DeviceFactory,
                __uuidof(ID3D12DeviceFactory),
                reinterpret_cast<void **>(factory.put())),
            S_OK);
  EXPECT_TRUE(factory);
}

TEST(D3D12AgilityCoreSpec, CoreFactoryExportValidatesRequests) {
  const auto create_factory =
      LoadD3D12CoreProc<DXMTCreateD3D12DeviceFactoryProc>(
          "DXMTCreateD3D12DeviceFactory");
  ASSERT_NE(create_factory, nullptr);

  EXPECT_EQ(create_factory(__uuidof(ID3D12DeviceFactory), nullptr), E_POINTER);

  void *output = reinterpret_cast<void *>(std::uintptr_t{1});
  EXPECT_EQ(create_factory(kUnsupportedInterface, &output), E_NOINTERFACE);
  EXPECT_EQ(output, nullptr);

  ComPtr<ID3D12DeviceFactory> factory;
  EXPECT_EQ(create_factory(__uuidof(ID3D12DeviceFactory),
                           reinterpret_cast<void **>(factory.put())),
            S_OK);
  EXPECT_TRUE(factory);
}

TEST(D3D12AgilityCoreSpec, FactoryInterfacesShareComIdentity) {
  auto factory = CreateDeviceFactory();
  ASSERT_TRUE(factory);

  ComPtr<ID3D12DeviceConfiguration> configuration;
  ComPtr<ID3D12DeviceConfiguration1> configuration1;
  ComPtr<IUnknown> factory_identity;
  ComPtr<IUnknown> configuration_identity;
  EXPECT_EQ(factory->QueryInterface(
                __uuidof(ID3D12DeviceConfiguration),
                reinterpret_cast<void **>(configuration.put())),
            S_OK);
  EXPECT_EQ(factory->QueryInterface(
                __uuidof(ID3D12DeviceConfiguration1),
                reinterpret_cast<void **>(configuration1.put())),
            S_OK);
  ASSERT_TRUE(configuration);
  ASSERT_TRUE(configuration1);
  EXPECT_EQ(factory->QueryInterface(
                __uuidof(IUnknown),
                reinterpret_cast<void **>(factory_identity.put())),
            S_OK);
  EXPECT_EQ(configuration->QueryInterface(
                __uuidof(IUnknown),
                reinterpret_cast<void **>(configuration_identity.put())),
            S_OK);
  EXPECT_EQ(factory_identity.get(), configuration_identity.get());
}

TEST(D3D12AgilityCoreSpec, FactoryFlagsRoundTripAndGlobalReset) {
  auto factory = CreateDeviceFactory();
  ASSERT_TRUE(factory);

  const auto flags = static_cast<D3D12_DEVICE_FACTORY_FLAGS>(
      D3D12_DEVICE_FACTORY_FLAG_ALLOW_RETURNING_EXISTING_DEVICE |
      D3D12_DEVICE_FACTORY_FLAG_DISALLOW_STORING_NEW_DEVICE_AS_SINGLETON);
  EXPECT_EQ(factory->SetFlags(flags), S_OK);
  EXPECT_EQ(factory->GetFlags(), flags);
  EXPECT_EQ(factory->ApplyToGlobalState(), S_OK);
  EXPECT_EQ(factory->GetFlags(), flags);
  EXPECT_EQ(factory->InitializeFromGlobalState(), S_OK);
  EXPECT_EQ(factory->GetFlags(), D3D12_DEVICE_FACTORY_FLAG_NONE);
}

TEST(D3D12AgilityCoreSpec, FactoriesKeepFlagsIndependent) {
  auto first = CreateDeviceFactory();
  auto second = CreateDeviceFactory();
  ASSERT_TRUE(first);
  ASSERT_TRUE(second);

  const auto first_flags = static_cast<D3D12_DEVICE_FACTORY_FLAGS>(
      D3D12_DEVICE_FACTORY_FLAG_ALLOW_RETURNING_EXISTING_DEVICE |
      D3D12_DEVICE_FACTORY_FLAG_DISALLOW_STORING_NEW_DEVICE_AS_SINGLETON);
  const auto second_flags = static_cast<D3D12_DEVICE_FACTORY_FLAGS>(
      D3D12_DEVICE_FACTORY_FLAG_ALLOW_RETURNING_INCOMPATIBLE_EXISTING_DEVICE);
  ASSERT_EQ(first->SetFlags(first_flags), S_OK);
  EXPECT_EQ(second->GetFlags(), D3D12_DEVICE_FACTORY_FLAG_NONE);
  ASSERT_EQ(second->SetFlags(second_flags), S_OK);
  EXPECT_EQ(first->GetFlags(), first_flags);
  EXPECT_EQ(second->GetFlags(), second_flags);
}

TEST(D3D12AgilityCoreSpec, DeviceConfigurationDescAdvertisesAgilitySdk) {
  auto factory = CreateDeviceFactory();
  ASSERT_TRUE(factory);

  ComPtr<ID3D12DeviceConfiguration> configuration;
  ASSERT_EQ(factory->QueryInterface(
                __uuidof(ID3D12DeviceConfiguration),
                reinterpret_cast<void **>(configuration.put())),
            S_OK);
  const D3D12_DEVICE_CONFIGURATION_DESC desc = configuration->GetDesc();
  EXPECT_EQ(desc.Flags, D3D12_DEVICE_FLAG_NONE);
  EXPECT_EQ(desc.GpuBasedValidationFlags, 0u);
  EXPECT_EQ(desc.SDKVersion, dxmt::d3d12::kAgilitySdkVersion);
  EXPECT_EQ(desc.NumEnabledExperimentalFeatures, 0u);
}

TEST(D3D12AgilityCoreSpec, FactoryExperimentalFeaturesFailClosed) {
  auto factory = CreateDeviceFactory();
  ASSERT_TRUE(factory);

  EXPECT_EQ(factory->EnableExperimentalFeatures(1, nullptr, nullptr, nullptr),
            E_INVALIDARG);
  EXPECT_EQ(factory->EnableExperimentalFeatures(0, nullptr, nullptr, nullptr),
            DXGI_ERROR_UNSUPPORTED);
  EXPECT_EQ(factory->EnableExperimentalFeatures(
                1, &kUnsupportedInterface, nullptr, nullptr),
            DXGI_ERROR_UNSUPPORTED);
}

TEST(D3D12AgilityCoreSpec, ConfigurationReportsNoExperimentalFeatures) {
  auto factory = CreateDeviceFactory();
  ASSERT_TRUE(factory);

  ComPtr<ID3D12DeviceConfiguration> configuration;
  ASSERT_EQ(factory->QueryInterface(
                __uuidof(ID3D12DeviceConfiguration),
                reinterpret_cast<void **>(configuration.put())),
            S_OK);
  EXPECT_EQ(configuration->GetEnabledExperimentalFeatures(nullptr, 0), S_OK);
  EXPECT_EQ(configuration->GetEnabledExperimentalFeatures(nullptr, 1),
            E_INVALIDARG);
}

TEST(D3D12AgilityCoreSpec, GlobalExperimentalFeaturesFailClosed) {
  const auto enable = LoadD3D12Proc<D3D12EnableExperimentalFeaturesProc>(
      "D3D12EnableExperimentalFeatures");
  ASSERT_NE(enable, nullptr);

  EXPECT_EQ(enable(1, nullptr, nullptr, nullptr), E_INVALIDARG);
  EXPECT_EQ(enable(0, nullptr, nullptr, nullptr), DXGI_ERROR_UNSUPPORTED);
  EXPECT_EQ(enable(1, &kUnsupportedInterface, nullptr, nullptr),
            DXGI_ERROR_UNSUPPORTED);
}

TEST(D3D12AgilityCoreSpec, FactoryProvidesBothDredSettingsVersions) {
  auto factory = CreateDeviceFactory();
  ASSERT_TRUE(factory);

  ComPtr<ID3D12DeviceRemovedExtendedDataSettings1> dred1;
  ASSERT_EQ(factory->GetConfigurationInterface(
                dxmt::d3d12::kCLSID_D3D12DeviceRemovedExtendedData,
                __uuidof(ID3D12DeviceRemovedExtendedDataSettings1),
                reinterpret_cast<void **>(dred1.put())),
            S_OK);
  ASSERT_TRUE(dred1);

  ComPtr<ID3D12DeviceRemovedExtendedDataSettings> dred;
  ComPtr<IUnknown> identity1;
  ComPtr<IUnknown> identity;
  EXPECT_EQ(dred1->QueryInterface(
                __uuidof(ID3D12DeviceRemovedExtendedDataSettings),
                reinterpret_cast<void **>(dred.put())),
            S_OK);
  ASSERT_TRUE(dred);
  EXPECT_EQ(dred1->QueryInterface(
                __uuidof(IUnknown),
                reinterpret_cast<void **>(identity1.put())),
            S_OK);
  EXPECT_EQ(dred->QueryInterface(
                __uuidof(IUnknown), reinterpret_cast<void **>(identity.put())),
            S_OK);
  EXPECT_EQ(identity1.get(), identity.get());

  dred->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
  dred->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_OFF);
  dred->SetWatsonDumpEnablement(D3D12_DRED_ENABLEMENT_SYSTEM_CONTROLLED);
  dred1->SetBreadcrumbContextEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
}

TEST(D3D12AgilityCoreSpec,
     DredSettingsOutliveFactoryAndAcceptEveryEnablementValue) {
  auto factory = CreateDeviceFactory();
  ASSERT_TRUE(factory);

  ComPtr<ID3D12DeviceRemovedExtendedDataSettings1> dred1;
  ASSERT_EQ(factory->GetConfigurationInterface(
                dxmt::d3d12::kCLSID_D3D12DeviceRemovedExtendedData,
                __uuidof(ID3D12DeviceRemovedExtendedDataSettings1),
                reinterpret_cast<void **>(dred1.put())),
            S_OK);
  ASSERT_TRUE(dred1);
  factory.reset();

  ComPtr<ID3D12DeviceRemovedExtendedDataSettings> dred;
  ASSERT_EQ(dred1->QueryInterface(
                __uuidof(ID3D12DeviceRemovedExtendedDataSettings),
                reinterpret_cast<void **>(dred.put())),
            S_OK);
  ASSERT_TRUE(dred);
  for (const auto enablement :
       std::array{D3D12_DRED_ENABLEMENT_SYSTEM_CONTROLLED,
                  D3D12_DRED_ENABLEMENT_FORCED_OFF,
                  D3D12_DRED_ENABLEMENT_FORCED_ON}) {
    dred->SetAutoBreadcrumbsEnablement(enablement);
    dred->SetPageFaultEnablement(enablement);
    dred->SetWatsonDumpEnablement(enablement);
    dred1->SetBreadcrumbContextEnablement(enablement);
  }
}

TEST(D3D12AgilityCoreSpec, DredSettingsRequestsReturnIndependentObjects) {
  auto factory = CreateDeviceFactory();
  ASSERT_TRUE(factory);

  ComPtr<ID3D12DeviceRemovedExtendedDataSettings1> first;
  ComPtr<ID3D12DeviceRemovedExtendedDataSettings1> second;
  ASSERT_EQ(factory->GetConfigurationInterface(
                dxmt::d3d12::kCLSID_D3D12DeviceRemovedExtendedData,
                __uuidof(ID3D12DeviceRemovedExtendedDataSettings1),
                reinterpret_cast<void **>(first.put())),
            S_OK);
  ASSERT_EQ(factory->GetConfigurationInterface(
                dxmt::d3d12::kCLSID_D3D12DeviceRemovedExtendedData,
                __uuidof(ID3D12DeviceRemovedExtendedDataSettings1),
                reinterpret_cast<void **>(second.put())),
            S_OK);
  ASSERT_TRUE(first);
  ASSERT_TRUE(second);

  ComPtr<IUnknown> first_identity;
  ComPtr<IUnknown> second_identity;
  ASSERT_EQ(first->QueryInterface(
                __uuidof(IUnknown),
                reinterpret_cast<void **>(first_identity.put())),
            S_OK);
  ASSERT_EQ(second->QueryInterface(
                __uuidof(IUnknown),
                reinterpret_cast<void **>(second_identity.put())),
            S_OK);
  EXPECT_NE(first_identity.get(), second_identity.get());
}

TEST(D3D12AgilityCoreSpec, DredSettingsRejectUnknownInterfaceAndClearOutput) {
  auto factory = CreateDeviceFactory();
  ASSERT_TRUE(factory);

  ComPtr<ID3D12DeviceRemovedExtendedDataSettings1> dred1;
  ASSERT_EQ(factory->GetConfigurationInterface(
                dxmt::d3d12::kCLSID_D3D12DeviceRemovedExtendedData,
                __uuidof(ID3D12DeviceRemovedExtendedDataSettings1),
                reinterpret_cast<void **>(dred1.put())),
            S_OK);
  ASSERT_TRUE(dred1);

  void *output = reinterpret_cast<void *>(std::uintptr_t{1});
  EXPECT_EQ(dred1->QueryInterface(kUnsupportedInterface, &output),
            E_NOINTERFACE);
  EXPECT_EQ(output, nullptr);
  EXPECT_EQ(dred1->QueryInterface(__uuidof(IUnknown), nullptr), E_POINTER);
}

TEST(D3D12AgilityCoreSpec, FactoryRejectsUnknownConfigurationRequests) {
  auto factory = CreateDeviceFactory();
  ASSERT_TRUE(factory);

  EXPECT_EQ(factory->GetConfigurationInterface(
                dxmt::d3d12::kCLSID_D3D12DeviceRemovedExtendedData,
                __uuidof(ID3D12DeviceRemovedExtendedDataSettings1), nullptr),
            S_FALSE);

  void *output = reinterpret_cast<void *>(std::uintptr_t{1});
  EXPECT_EQ(factory->GetConfigurationInterface(
                kUnsupportedClass, __uuidof(IUnknown), &output),
            E_NOINTERFACE);
  EXPECT_EQ(output, nullptr);

  output = reinterpret_cast<void *>(std::uintptr_t{1});
  EXPECT_EQ(factory->GetConfigurationInterface(
                dxmt::d3d12::kCLSID_D3D12DeviceRemovedExtendedData,
                kUnsupportedInterface, &output),
            E_NOINTERFACE);
  EXPECT_EQ(output, nullptr);
}

TEST(D3D12AgilityCoreSpec, FactoryCreatesIndependentDevice) {
  auto factory = CreateDeviceFactory();
  ASSERT_TRUE(factory);

  ComPtr<ID3D12Device> device;
  EXPECT_EQ(factory->CreateDevice(
                nullptr, D3D_FEATURE_LEVEL_12_0, __uuidof(ID3D12Device),
                reinterpret_cast<void **>(device.put())),
            S_OK);
  EXPECT_TRUE(device);
}

TEST(D3D12AgilityCoreSpec, DeviceConfigurationProxiesRootSignatureHelpers) {
  auto factory = CreateDeviceFactory();
  ASSERT_TRUE(factory);

  ComPtr<ID3D12DeviceConfiguration> configuration;
  ASSERT_EQ(factory->QueryInterface(
                __uuidof(ID3D12DeviceConfiguration),
                reinterpret_cast<void **>(configuration.put())),
            S_OK);

  D3D12_VERSIONED_ROOT_SIGNATURE_DESC desc = {};
  desc.Version = D3D_ROOT_SIGNATURE_VERSION_1_0;
  desc.Desc_1_0.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
  ComPtr<ID3DBlob> blob;
  ComPtr<ID3DBlob> error;
  ASSERT_EQ(configuration->SerializeVersionedRootSignature(
                &desc, blob.put(), error.put()),
            S_OK);
  ASSERT_TRUE(blob);

  ComPtr<ID3D12VersionedRootSignatureDeserializer> deserializer;
  EXPECT_EQ(configuration->CreateVersionedRootSignatureDeserializer(
                blob->GetBufferPointer(), blob->GetBufferSize(),
                __uuidof(ID3D12VersionedRootSignatureDeserializer),
                reinterpret_cast<void **>(deserializer.put())),
            S_OK);
  ASSERT_TRUE(deserializer);
  const auto *round_trip = deserializer->GetUnconvertedRootSignatureDesc();
  ASSERT_NE(round_trip, nullptr);
  EXPECT_EQ(round_trip->Version, D3D_ROOT_SIGNATURE_VERSION_1_0);
  EXPECT_EQ(round_trip->Desc_1_0.NumParameters, 0u);
  EXPECT_EQ(round_trip->Desc_1_0.NumStaticSamplers, 0u);
  EXPECT_EQ(round_trip->Desc_1_0.Flags, D3D12_ROOT_SIGNATURE_FLAG_NONE);
}

TEST(D3D12AgilityCoreSpec,
     LibrarySubobjectDeserializerFailuresClearOutput) {
  auto factory = CreateDeviceFactory();
  ASSERT_TRUE(factory);

  ComPtr<ID3D12DeviceConfiguration1> configuration1;
  ASSERT_EQ(factory->QueryInterface(
                __uuidof(ID3D12DeviceConfiguration1),
                reinterpret_cast<void **>(configuration1.put())),
            S_OK);
  ASSERT_TRUE(configuration1);

  void *output = reinterpret_cast<void *>(std::uintptr_t{1});
  EXPECT_EQ(
      configuration1->CreateVersionedRootSignatureDeserializerFromSubobjectInLibrary(
          nullptr, 1, L"MissingRootSignature",
          __uuidof(ID3D12VersionedRootSignatureDeserializer), &output),
      E_INVALIDARG);
  EXPECT_EQ(output, nullptr);

  const std::array<std::uint8_t, 4> invalid_library = {0xde, 0xad, 0xbe, 0xef};
  output = reinterpret_cast<void *>(std::uintptr_t{1});
  EXPECT_EQ(
      configuration1->CreateVersionedRootSignatureDeserializerFromSubobjectInLibrary(
          invalid_library.data(), invalid_library.size(),
          L"MissingRootSignature",
          __uuidof(ID3D12VersionedRootSignatureDeserializer), &output),
      E_INVALIDARG);
  EXPECT_EQ(output, nullptr);
}

TEST(D3D12AgilityCoreSpec, ConcurrentInterfaceAndFactoryCreationIsStable) {
  constexpr unsigned int kThreadCount = 8;
  constexpr unsigned int kIterations = 16;
  std::atomic_uint failures = 0;
  std::vector<std::thread> threads;
  threads.reserve(kThreadCount);
  for (unsigned int thread_index = 0; thread_index < kThreadCount;
       ++thread_index) {
    threads.emplace_back([&] {
      for (unsigned int iteration = 0; iteration < kIterations; ++iteration) {
        auto configuration = CreateSdkConfiguration();
        auto factory = CreateDeviceFactory();
        if (!configuration || !factory)
          ++failures;
      }
    });
  }
  for (auto &thread : threads)
    thread.join();
  EXPECT_EQ(failures.load(), 0u);
}

} // namespace
