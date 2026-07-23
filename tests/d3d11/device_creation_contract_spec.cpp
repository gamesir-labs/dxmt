#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <array>
#include <cstdint>

// Public D3D11 device-creation coverage. Every logical case creates its own
// device from a fixture-local adapter and does not mutate shared state, so the
// family is safe under the default parallel scheduler.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

struct OutputCase {
  bool request_device;
  bool request_feature_level;
  bool request_context;
  const char *name;
};

constexpr std::array kOutputCases = {
    OutputCase{false, false, false, "NoOutputs"},
    OutputCase{false, false, true, "ContextOnly"},
    OutputCase{false, true, false, "FeatureLevelOnly"},
    OutputCase{false, true, true, "FeatureLevelAndContext"},
    OutputCase{true, false, false, "DeviceOnly"},
    OutputCase{true, false, true, "DeviceAndContext"},
    OutputCase{true, true, false, "DeviceAndFeatureLevel"},
    OutputCase{true, true, true, "EveryOutput"},
};

enum class InvalidAdapterCaseKind {
  NullAdapterUnknownDriver,
  ExplicitAdapterHardwareDriver,
  ExplicitAdapterSoftwareModule,
};

struct InvalidAdapterCase {
  InvalidAdapterCaseKind kind;
  const char *name;
};

constexpr std::array kInvalidAdapterCases = {
    InvalidAdapterCase{InvalidAdapterCaseKind::NullAdapterUnknownDriver,
                       "NullAdapterUnknownDriver"},
    InvalidAdapterCase{InvalidAdapterCaseKind::ExplicitAdapterHardwareDriver,
                       "ExplicitAdapterHardwareDriver"},
    InvalidAdapterCase{InvalidAdapterCaseKind::ExplicitAdapterSoftwareModule,
                       "ExplicitAdapterSoftwareModule"},
};

struct FeatureLevelCase {
  std::array<D3D_FEATURE_LEVEL, 3> requested;
  UINT count;
  D3D_FEATURE_LEVEL expected;
  const char *name;
};

constexpr std::array kFeatureLevelCases = {
    FeatureLevelCase{
        {D3D_FEATURE_LEVEL_11_0}, 1, D3D_FEATURE_LEVEL_11_0, "Level11_0"},
    FeatureLevelCase{
        {D3D_FEATURE_LEVEL_10_1}, 1, D3D_FEATURE_LEVEL_10_1, "Level10_1"},
    FeatureLevelCase{
        {D3D_FEATURE_LEVEL_10_0}, 1, D3D_FEATURE_LEVEL_10_0, "Level10_0"},
    FeatureLevelCase{
        {D3D_FEATURE_LEVEL_9_3}, 1, D3D_FEATURE_LEVEL_9_3, "Level9_3"},
    FeatureLevelCase{
        {D3D_FEATURE_LEVEL_9_2}, 1, D3D_FEATURE_LEVEL_9_2, "Level9_2"},
    FeatureLevelCase{
        {D3D_FEATURE_LEVEL_9_1}, 1, D3D_FEATURE_LEVEL_9_1, "Level9_1"},
    FeatureLevelCase{{D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
                      D3D_FEATURE_LEVEL_10_0},
                     3,
                     D3D_FEATURE_LEVEL_11_0,
                     "HighestFirst"},
    FeatureLevelCase{{D3D_FEATURE_LEVEL_10_0, D3D_FEATURE_LEVEL_10_1,
                      D3D_FEATURE_LEVEL_11_0},
                     3,
                     D3D_FEATURE_LEVEL_10_0,
                     "FirstSupportedWins"},
};

struct CreationFlagsCase {
  UINT flags;
  const char *name;
};

constexpr UINT kObservableCreationFlags =
    D3D11_CREATE_DEVICE_SINGLETHREADED |
    D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS |
    D3D11_CREATE_DEVICE_BGRA_SUPPORT;

constexpr std::array kCreationFlagsCases = {
    CreationFlagsCase{0, "Default"},
    CreationFlagsCase{D3D11_CREATE_DEVICE_SINGLETHREADED, "SingleThreaded"},
    CreationFlagsCase{
        D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS,
        "PreventInternalThreading"},
    CreationFlagsCase{D3D11_CREATE_DEVICE_BGRA_SUPPORT, "BgraSupport"},
    CreationFlagsCase{
        D3D11_CREATE_DEVICE_SINGLETHREADED |
            D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS,
        "SingleThreadedPreventInternalThreading"},
    CreationFlagsCase{D3D11_CREATE_DEVICE_SINGLETHREADED |
                          D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                      "SingleThreadedBgra"},
    CreationFlagsCase{
        D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS |
            D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        "PreventInternalThreadingBgra"},
    CreationFlagsCase{kObservableCreationFlags, "EveryObservableFlag"},
};

const dxmt::test::LogicalCaseFamilyRegistration kOutputRegistration(
    "D3D11DeviceCreationContractSpec.CreatesForEveryOutputCombination",
    "D3D11.DeviceCreation.OutputCombination.", kOutputCases.size(), 1,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "Device",
      "D3D11CreateDevice,ID3D11Device,ID3D11DeviceContext,"
      "ID3D11DeviceContextGetDevice,ID3D11DeviceGetFeatureLevel"},
     dxmt::test::kResourceTestCost,
     "one fixture-local adapter and at most one independently created D3D11 "
     "device and immediate context per selected logical case",
     "request every combination of the optional device, feature-level, and "
     "immediate-context outputs",
     "creation returns S_OK when an owning COM output is requested and "
     "S_FALSE otherwise; every returned object and feature level is valid",
     "logical ID, output mask, HRESULT, chosen feature level, object "
     "addresses, context type, owner identity, and exact replay argument"});

const dxmt::test::LogicalCaseFamilyRegistration kInvalidAdapterRegistration(
    "D3D11DeviceCreationContractSpec.RejectsContradictoryAdapterArguments",
    "D3D11.DeviceCreation.InvalidAdapterArguments.",
    kInvalidAdapterCases.size(), 1,
    {dxmt::test::TestClass::Robustness,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "Device",
      "D3D11CreateDevice,IDXGIAdapter,D3D_DRIVER_TYPE_UNKNOWN,"
      "D3D_DRIVER_TYPE_HARDWARE,SoftwareModule,InvalidArguments"},
     dxmt::test::kResourceTestCost,
     "one fixture-local adapter and no successfully created output objects",
     "combine a null adapter with the adapter-only UNKNOWN driver type, or "
     "an explicit adapter with a non-UNKNOWN driver type or software module",
     "each contradictory argument combination returns E_INVALIDARG and "
     "clears every output",
     "logical ID, invalid case, adapter presence, driver type, software "
     "module, HRESULT, cleared outputs, and exact replay argument"});

const dxmt::test::LogicalCaseFamilyRegistration kFeatureLevelRegistration(
    "D3D11DeviceCreationContractSpec.SelectsRequestedFeatureLevelsInOrder",
    "D3D11.DeviceCreation.FeatureLevelSelection.", kFeatureLevelCases.size(), 1,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"9_1-11_0", "None", "Device",
      "D3D11CreateDevice,ID3D11DeviceGetFeatureLevel,FeatureLevelOrder"},
     dxmt::test::kResourceTestCost,
     "one fixture-local adapter and one independently created D3D11 device "
     "per selected logical case",
     "request each core feature level from 9_1 through 11_0 and exercise "
     "both descending and ascending multi-level preference lists",
     "creation selects the first supported entry exactly and reports it "
     "through both the output parameter and ID3D11Device",
     "logical ID, requested list and count, expected and observed levels, "
     "HRESULT, object addresses, and exact replay argument"});

const dxmt::test::LogicalCaseFamilyRegistration kCreationFlagsRegistration(
    "D3D11DeviceCreationContractSpec.PreservesDocumentedCreationFlags",
    "D3D11.DeviceCreation.Flags.", kCreationFlagsCases.size(), 1,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "Device",
      "D3D11CreateDevice,ID3D11DeviceGetCreationFlags,"
      "D3D11_CREATE_DEVICE_SINGLETHREADED,"
      "D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS,"
      "D3D11_CREATE_DEVICE_BGRA_SUPPORT"},
     dxmt::test::kResourceTestCost,
     "one fixture-local adapter and one independently created D3D11 device "
     "and immediate context per selected logical case",
     "create devices with every combination of three documented, "
     "environment-independent creation flags",
     "creation succeeds and GetCreationFlags preserves every requested bit",
     "logical ID, requested and observed flags, HRESULT, feature level, "
     "object addresses, and exact replay argument"});

const dxmt::test::TestCostRegistration kOutputCost(
    "D3D11DeviceCreationContractSpec.CreatesForEveryOutputCombination",
    dxmt::test::kResourceTestCost);
const dxmt::test::TestCostRegistration kInvalidAdapterCost(
    "D3D11DeviceCreationContractSpec.RejectsContradictoryAdapterArguments",
    dxmt::test::kResourceTestCost);
const dxmt::test::TestCostRegistration kFeatureLevelCost(
    "D3D11DeviceCreationContractSpec.SelectsRequestedFeatureLevelsInOrder",
    dxmt::test::kResourceTestCost);
const dxmt::test::TestCostRegistration kCreationFlagsCost(
    "D3D11DeviceCreationContractSpec.PreservesDocumentedCreationFlags",
    dxmt::test::kResourceTestCost);

class D3D11DeviceCreationContractSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.InitializeAdapter(), S_OK);
    ASSERT_NE(context_.adapter(), nullptr);
  }

  D3D11TestContext context_;
};

TEST_F(D3D11DeviceCreationContractSpec, CreatesForEveryOutputCombination) {
  constexpr D3D_FEATURE_LEVEL kRequestedLevel = D3D_FEATURE_LEVEL_11_0;
  constexpr D3D_FEATURE_LEVEL kPoisonLevel = D3D_FEATURE_LEVEL_9_1;
  std::uint32_t selected_count = 0;

  for (std::uint32_t logical = 0; logical < kOutputCases.size(); ++logical) {
    if (!dxmt::test::LogicalCaseSelected(kOutputRegistration.family(), logical))
      continue;
    ++selected_count;
    const OutputCase &test_case = kOutputCases[logical];
    const auto case_id =
        dxmt::test::LogicalCaseId(kOutputRegistration.family(), logical);
    SCOPED_TRACE(::testing::Message()
                 << "LogicalCaseId: " << case_id << " case=" << test_case.name
                 << " device=" << test_case.request_device
                 << " feature_level=" << test_case.request_feature_level
                 << " context=" << test_case.request_context
                 << " Replay: --dxmt-case-id=" << case_id);

    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> immediate_context;
    D3D_FEATURE_LEVEL chosen_level = kPoisonLevel;
    const HRESULT expected =
        test_case.request_device || test_case.request_context ? S_OK : S_FALSE;
    const HRESULT hr = D3D11CreateDevice(
        context_.adapter(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0,
        &kRequestedLevel, 1, D3D11_SDK_VERSION,
        test_case.request_device ? device.put() : nullptr,
        test_case.request_feature_level ? &chosen_level : nullptr,
        test_case.request_context ? immediate_context.put() : nullptr);

    EXPECT_EQ(hr, expected);
    EXPECT_EQ(static_cast<bool>(device), test_case.request_device);
    EXPECT_EQ(static_cast<bool>(immediate_context), test_case.request_context);
    if (test_case.request_feature_level)
      EXPECT_EQ(chosen_level, kRequestedLevel);
    else
      EXPECT_EQ(chosen_level, kPoisonLevel);

    if (device) {
      EXPECT_EQ(device->GetFeatureLevel(), kRequestedLevel);
    }
    if (immediate_context) {
      EXPECT_EQ(immediate_context->GetType(), D3D11_DEVICE_CONTEXT_IMMEDIATE);
      ComPtr<ID3D11Device> owner;
      immediate_context->GetDevice(owner.put());
      ASSERT_TRUE(owner);
      EXPECT_EQ(owner->GetFeatureLevel(), kRequestedLevel);
      if (device) {
        EXPECT_EQ(owner.get(), device.get());
      }
    }
  }

  ASSERT_NE(selected_count, 0u);
  RecordProperty("logical_cases_executed", selected_count);
  RecordProperty("logical_case_prefix",
                 kOutputRegistration.family().case_id_prefix);
}

TEST_F(D3D11DeviceCreationContractSpec, RejectsContradictoryAdapterArguments) {
  constexpr D3D_FEATURE_LEVEL kRequestedLevel = D3D_FEATURE_LEVEL_11_0;
  constexpr D3D_FEATURE_LEVEL kPoisonLevel = D3D_FEATURE_LEVEL_9_1;
  auto *const kPoisonDevice =
      reinterpret_cast<ID3D11Device *>(static_cast<std::uintptr_t>(1));
  auto *const kPoisonContext =
      reinterpret_cast<ID3D11DeviceContext *>(static_cast<std::uintptr_t>(1));
  std::uint32_t selected_count = 0;

  for (std::uint32_t logical = 0; logical < kInvalidAdapterCases.size();
       ++logical) {
    if (!dxmt::test::LogicalCaseSelected(kInvalidAdapterRegistration.family(),
                                         logical))
      continue;
    ++selected_count;
    const InvalidAdapterCase &test_case = kInvalidAdapterCases[logical];
    const auto case_id = dxmt::test::LogicalCaseId(
        kInvalidAdapterRegistration.family(), logical);

    IDXGIAdapter *adapter = context_.adapter();
    D3D_DRIVER_TYPE driver_type = D3D_DRIVER_TYPE_UNKNOWN;
    HMODULE software = nullptr;
    switch (test_case.kind) {
    case InvalidAdapterCaseKind::NullAdapterUnknownDriver:
      adapter = nullptr;
      break;
    case InvalidAdapterCaseKind::ExplicitAdapterHardwareDriver:
      driver_type = D3D_DRIVER_TYPE_HARDWARE;
      break;
    case InvalidAdapterCaseKind::ExplicitAdapterSoftwareModule:
      software = reinterpret_cast<HMODULE>(static_cast<std::uintptr_t>(1));
      break;
    }

    SCOPED_TRACE(::testing::Message()
                 << "LogicalCaseId: " << case_id << " case=" << test_case.name
                 << " adapter=" << adapter << " driver_type=" << driver_type
                 << " software=" << software
                 << " Replay: --dxmt-case-id=" << case_id);

    ID3D11Device *device = kPoisonDevice;
    ID3D11DeviceContext *immediate_context = kPoisonContext;
    D3D_FEATURE_LEVEL chosen_level = kPoisonLevel;
    const HRESULT hr = D3D11CreateDevice(
        adapter, driver_type, software, 0, &kRequestedLevel, 1,
        D3D11_SDK_VERSION, &device, &chosen_level, &immediate_context);

    EXPECT_EQ(hr, E_INVALIDARG);
    EXPECT_EQ(device, nullptr);
    EXPECT_EQ(chosen_level, static_cast<D3D_FEATURE_LEVEL>(0));
    EXPECT_EQ(immediate_context, nullptr);

    if (device && device != kPoisonDevice)
      device->Release();
    if (immediate_context && immediate_context != kPoisonContext)
      immediate_context->Release();
  }

  ASSERT_NE(selected_count, 0u);
  RecordProperty("logical_cases_executed", selected_count);
  RecordProperty("logical_case_prefix",
                 kInvalidAdapterRegistration.family().case_id_prefix);
}

TEST_F(D3D11DeviceCreationContractSpec, SelectsRequestedFeatureLevelsInOrder) {
  std::uint32_t selected_count = 0;
  for (std::uint32_t logical = 0; logical < kFeatureLevelCases.size();
       ++logical) {
    if (!dxmt::test::LogicalCaseSelected(kFeatureLevelRegistration.family(),
                                         logical))
      continue;
    ++selected_count;
    const FeatureLevelCase &test_case = kFeatureLevelCases[logical];
    const auto case_id =
        dxmt::test::LogicalCaseId(kFeatureLevelRegistration.family(), logical);
    SCOPED_TRACE(::testing::Message()
                 << "LogicalCaseId: " << case_id << " case=" << test_case.name
                 << " count=" << test_case.count
                 << " expected=" << test_case.expected
                 << " Replay: --dxmt-case-id=" << case_id);

    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> immediate_context;
    D3D_FEATURE_LEVEL chosen_level = D3D_FEATURE_LEVEL_9_1;
    const HRESULT hr = D3D11CreateDevice(
        context_.adapter(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0,
        test_case.requested.data(), test_case.count, D3D11_SDK_VERSION,
        device.put(), &chosen_level, immediate_context.put());

    EXPECT_EQ(hr, S_OK);
    ASSERT_TRUE(device);
    ASSERT_TRUE(immediate_context);
    EXPECT_EQ(chosen_level, test_case.expected);
    EXPECT_EQ(device->GetFeatureLevel(), test_case.expected);
    ComPtr<ID3D11Device> owner;
    immediate_context->GetDevice(owner.put());
    EXPECT_EQ(owner.get(), device.get());
  }

  ASSERT_NE(selected_count, 0u);
  RecordProperty("logical_cases_executed", selected_count);
  RecordProperty("logical_case_prefix",
                 kFeatureLevelRegistration.family().case_id_prefix);
}

TEST_F(D3D11DeviceCreationContractSpec, PreservesDocumentedCreationFlags) {
  constexpr D3D_FEATURE_LEVEL kRequestedLevel = D3D_FEATURE_LEVEL_11_0;
  std::uint32_t selected_count = 0;
  for (std::uint32_t logical = 0; logical < kCreationFlagsCases.size();
       ++logical) {
    if (!dxmt::test::LogicalCaseSelected(kCreationFlagsRegistration.family(),
                                         logical))
      continue;
    ++selected_count;
    const CreationFlagsCase &test_case = kCreationFlagsCases[logical];
    const auto case_id =
        dxmt::test::LogicalCaseId(kCreationFlagsRegistration.family(), logical);
    SCOPED_TRACE(::testing::Message()
                 << "LogicalCaseId: " << case_id << " case=" << test_case.name
                 << " flags=" << test_case.flags
                 << " Replay: --dxmt-case-id=" << case_id);

    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> immediate_context;
    D3D_FEATURE_LEVEL chosen_level = D3D_FEATURE_LEVEL_9_1;
    const HRESULT hr = D3D11CreateDevice(
        context_.adapter(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, test_case.flags,
        &kRequestedLevel, 1, D3D11_SDK_VERSION, device.put(), &chosen_level,
        immediate_context.put());

    EXPECT_EQ(hr, S_OK);
    ASSERT_TRUE(device);
    ASSERT_TRUE(immediate_context);
    EXPECT_EQ(chosen_level, kRequestedLevel);
    EXPECT_EQ(device->GetCreationFlags() & kObservableCreationFlags,
              test_case.flags);
  }

  ASSERT_NE(selected_count, 0u);
  RecordProperty("logical_cases_executed", selected_count);
  RecordProperty("logical_case_prefix",
                 kCreationFlagsRegistration.family().case_id_prefix);
}

} // namespace
