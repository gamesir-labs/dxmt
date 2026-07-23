#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <dxgi1_6.h>

#include <array>
#include <cstdint>
#include <limits>
#include <set>
#include <string>
#include <utility>
#include <vector>

// Public DXGI output enumeration and COM-object coverage. The tests only read
// display topology and never take output ownership or change display state, so
// they remain safe under the default parallel scheduler.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

constexpr GUID kOutputPrivateDataKey = {
    0xe6b8d8c4,
    0x6452,
    0x46d7,
    {0xa3, 0x8e, 0x2b, 0x75, 0x9f, 0xa0, 0xc3, 0x18}};

struct OutputInterfaceCase {
  const GUID *iid;
  HRESULT expected;
  const char *name;
};

constexpr std::array kOutputInterfaceCases = {
    OutputInterfaceCase{&__uuidof(IUnknown), S_OK, "IUnknown"},
    OutputInterfaceCase{&__uuidof(IDXGIObject), S_OK, "IDXGIObject"},
    OutputInterfaceCase{&__uuidof(IDXGIOutput), S_OK, "IDXGIOutput"},
    OutputInterfaceCase{&__uuidof(IDXGIOutput1), S_OK, "IDXGIOutput1"},
    OutputInterfaceCase{&__uuidof(IDXGIOutput2), S_OK, "IDXGIOutput2"},
    OutputInterfaceCase{&__uuidof(IDXGIOutput3), S_OK, "IDXGIOutput3"},
    OutputInterfaceCase{&__uuidof(IDXGIOutput4), S_OK, "IDXGIOutput4"},
    OutputInterfaceCase{&__uuidof(IDXGIOutput5), S_OK, "IDXGIOutput5"},
    OutputInterfaceCase{&__uuidof(IDXGIOutput6), S_OK, "IDXGIOutput6"},
    OutputInterfaceCase{&__uuidof(ID3D11Device), E_NOINTERFACE,
                        "UnsupportedDevice"},
};

struct OverlayFormatCase {
  DXGI_FORMAT format;
  const char *name;
};

constexpr std::array kOverlayFormatCases = {
    OverlayFormatCase{DXGI_FORMAT_R8G8B8A8_UNORM, "Rgba8"},
    OverlayFormatCase{DXGI_FORMAT_B8G8R8A8_UNORM, "Bgra8"},
    OverlayFormatCase{DXGI_FORMAT_R10G10B10A2_UNORM, "Rgb10A2"},
    OverlayFormatCase{DXGI_FORMAT_NV12, "Nv12"},
};

struct OverlayColorSpaceCase {
  DXGI_COLOR_SPACE_TYPE color_space;
  const char *name;
};

constexpr std::array kOverlayColorSpaceCases = {
    OverlayColorSpaceCase{DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709, "Srgb709"},
    OverlayColorSpaceCase{DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709, "Linear709"},
    OverlayColorSpaceCase{DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020,
                          "Hdr10P2020"},
};

const dxmt::test::LogicalCaseFamilyRegistration kOutputInterfaceRegistration(
    "D3D11DxgiOutputContractSpec.QueriesEveryPublicOutputInterface",
    "D3D11.DXGI.Output.Interface.", kOutputInterfaceCases.size(), 1,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "Device",
      "IDXGIAdapterEnumOutputs,IDXGIOutput,IDXGIOutput1,IDXGIOutput2,"
      "IDXGIOutput3,IDXGIOutput4,IDXGIOutput5,IDXGIOutput6,IDXGIObject,"
      "QueryInterface,ComObjectIdentity"},
     dxmt::test::kResourceTestCost,
     "one fixture-local adapter and at most one read-only output object per "
     "selected logical case",
     "enumerate the first available output and query every public output "
     "interface version plus one unrelated interface",
     "systems without an attached output report DXGI_ERROR_NOT_FOUND; an "
     "available output exposes all runtime interface versions under one COM "
     "identity and rejects the unrelated interface",
     "logical ID, interface name, enumeration and QueryInterface HRESULTs, "
     "object identities, output availability, and exact replay argument"});

const dxmt::test::LogicalCaseFamilyRegistration kOverlayFormatRegistration(
    "D3D11DxgiOutputContractSpec.QueriesOverlayFormatCapabilities",
    "D3D11.DXGI.Output.OverlayFormat.", kOverlayFormatCases.size(), 1,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "Device",
      "IDXGIOutput3,CheckOverlaySupport,ID3D11Device,DXGI_FORMAT"},
     dxmt::test::kResourceTestCost,
     "one fixture-local D3D11 device and at most one read-only output",
     "query overlay support for common RGBA, BGRA, 10-bit, and YUV formats",
     "each query succeeds and returns only documented overlay support flags, "
     "or the adapter reports no attached output",
     "logical ID, format, HRESULT, support flags, output availability, and "
     "exact replay argument"});

const dxmt::test::LogicalCaseFamilyRegistration kOverlayColorRegistration(
    "D3D11DxgiOutputContractSpec.QueriesOverlayColorSpaceCapabilities",
    "D3D11.DXGI.Output.OverlayColorSpace.", kOverlayColorSpaceCases.size(), 1,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "Device",
      "IDXGIOutput4,CheckOverlayColorSpaceSupport,ID3D11Device,"
      "DXGI_COLOR_SPACE_TYPE"},
     dxmt::test::kResourceTestCost,
     "one fixture-local D3D11 device and at most one read-only output",
     "query overlay color-space support for sRGB, linear RGB, and HDR10",
     "each query succeeds and returns only documented color-space support "
     "flags, or the adapter reports no attached output",
     "logical ID, color space, HRESULT, support flags, output availability, "
     "and exact replay argument"});

const dxmt::test::TestCostRegistration kOutputInterfaceCost(
    "D3D11DxgiOutputContractSpec.QueriesEveryPublicOutputInterface",
    dxmt::test::kResourceTestCost);
const dxmt::test::TestCostRegistration kOutputEnumerationCost(
    "D3D11DxgiOutputContractSpec.EnumeratesStableObjectsUntilNotFound",
    dxmt::test::kResourceTestCost);
const dxmt::test::TestCostRegistration kOutputDescriptionCost(
    "D3D11DxgiOutputContractSpec.ReportsVersionedDescriptionAndComposition",
    dxmt::test::kResourceTestCost);
const dxmt::test::TestCostRegistration kOverlayFormatCost(
    "D3D11DxgiOutputContractSpec.QueriesOverlayFormatCapabilities",
    dxmt::test::kResourceTestCost);
const dxmt::test::TestCostRegistration kOverlayColorCost(
    "D3D11DxgiOutputContractSpec.QueriesOverlayColorSpaceCapabilities",
    dxmt::test::kResourceTestCost);

class D3D11DxgiOutputContractSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.InitializeAdapter(), S_OK);
    ASSERT_NE(context_.adapter(), nullptr);
  }

  HRESULT GetFirstOutput(ComPtr<IDXGIOutput6> &output6) {
    ComPtr<IDXGIOutput> output;
    const HRESULT hr = context_.adapter()->EnumOutputs(0, output.put());
    if (hr != S_OK)
      return hr;
    return output->QueryInterface(__uuidof(IDXGIOutput6),
                                  reinterpret_cast<void **>(output6.put()));
  }

  D3D11TestContext context_;
};

TEST_F(D3D11DxgiOutputContractSpec, QueriesEveryPublicOutputInterface) {
  std::uint32_t selected_count = 0;
  for (std::uint32_t logical = 0; logical < kOutputInterfaceCases.size();
       ++logical) {
    if (!dxmt::test::LogicalCaseSelected(kOutputInterfaceRegistration.family(),
                                         logical))
      continue;
    ++selected_count;
    const OutputInterfaceCase &test_case = kOutputInterfaceCases[logical];
    const auto case_id = dxmt::test::LogicalCaseId(
        kOutputInterfaceRegistration.family(), logical);
    SCOPED_TRACE(::testing::Message() << "LogicalCaseId: " << case_id
                                      << " interface=" << test_case.name
                                      << " Replay: --dxmt-case-id=" << case_id);

    ComPtr<IDXGIOutput> output;
    const HRESULT enumeration_result =
        context_.adapter()->EnumOutputs(0, output.put());
    ASSERT_TRUE(enumeration_result == S_OK ||
                enumeration_result == DXGI_ERROR_NOT_FOUND);
    if (enumeration_result == DXGI_ERROR_NOT_FOUND) {
      EXPECT_EQ(output.get(), nullptr);
      continue;
    }
    ASSERT_TRUE(output);

    ComPtr<IUnknown> output_identity;
    ASSERT_EQ(output->QueryInterface(
                  __uuidof(IUnknown),
                  reinterpret_cast<void **>(output_identity.put())),
              S_OK);
    ComPtr<IUnknown> queried;
    const HRESULT interface_result = output->QueryInterface(
        *test_case.iid, reinterpret_cast<void **>(queried.put()));
    EXPECT_EQ(interface_result, test_case.expected);
    if (test_case.expected == E_NOINTERFACE) {
      EXPECT_EQ(queried.get(), nullptr);
      continue;
    }
    ASSERT_TRUE(queried);
    ComPtr<IUnknown> queried_identity;
    ASSERT_EQ(queried->QueryInterface(
                  __uuidof(IUnknown),
                  reinterpret_cast<void **>(queried_identity.put())),
              S_OK);
    EXPECT_EQ(queried_identity.get(), output_identity.get());
  }

  ASSERT_NE(selected_count, 0u);
  RecordProperty("logical_cases_executed", selected_count);
  RecordProperty("logical_case_prefix",
                 kOutputInterfaceRegistration.family().case_id_prefix);
}

TEST_F(D3D11DxgiOutputContractSpec, EnumeratesStableObjectsUntilNotFound) {
  EXPECT_EQ(context_.adapter()->EnumOutputs(0, nullptr), E_INVALIDARG);

  ComPtr<IUnknown> adapter_identity;
  ASSERT_EQ(context_.adapter()->QueryInterface(
                __uuidof(IUnknown),
                reinterpret_cast<void **>(adapter_identity.put())),
            S_OK);
  std::set<IUnknown *> output_identities;
  std::vector<ComPtr<IDXGIOutput>> live_outputs;
  bool reached_boundary = false;
  UINT output_count = 0;
  for (UINT index = 0; index < 32; ++index) {
    ComPtr<IDXGIOutput> output;
    const HRESULT hr = context_.adapter()->EnumOutputs(index, output.put());
    if (hr == DXGI_ERROR_NOT_FOUND) {
      EXPECT_EQ(output.get(), nullptr);
      reached_boundary = true;
      break;
    }
    ASSERT_EQ(hr, S_OK) << "output_index=" << index;
    ASSERT_TRUE(output);
    ++output_count;

    DXGI_OUTPUT_DESC desc = {};
    ASSERT_EQ(output->GetDesc(&desc), S_OK);
    EXPECT_LE(desc.DesktopCoordinates.left, desc.DesktopCoordinates.right);
    EXPECT_LE(desc.DesktopCoordinates.top, desc.DesktopCoordinates.bottom);

    ComPtr<IDXGIAdapter> parent;
    ASSERT_EQ(output->GetParent(__uuidof(IDXGIAdapter),
                                reinterpret_cast<void **>(parent.put())),
              S_OK);
    ComPtr<IUnknown> parent_identity;
    ASSERT_EQ(parent->QueryInterface(
                  __uuidof(IUnknown),
                  reinterpret_cast<void **>(parent_identity.put())),
              S_OK);
    EXPECT_EQ(parent_identity.get(), adapter_identity.get());

    ComPtr<IUnknown> output_identity;
    ASSERT_EQ(output->QueryInterface(
                  __uuidof(IUnknown),
                  reinterpret_cast<void **>(output_identity.put())),
              S_OK);
    EXPECT_TRUE(output_identities.insert(output_identity.get()).second);

    constexpr std::array<std::uint8_t, 5> kPrivateValue = {2, 3, 5, 7, 11};
    ASSERT_EQ(output->SetPrivateData(kOutputPrivateDataKey,
                                     kPrivateValue.size(),
                                     kPrivateValue.data()),
              S_OK);
    std::array<std::uint8_t, kPrivateValue.size()> returned = {};
    UINT returned_size = returned.size();
    ASSERT_EQ(output->GetPrivateData(kOutputPrivateDataKey, &returned_size,
                                     returned.data()),
              S_OK);
    EXPECT_EQ(returned_size, kPrivateValue.size());
    EXPECT_EQ(returned, kPrivateValue);
    EXPECT_EQ(output->SetPrivateData(kOutputPrivateDataKey, 0, nullptr), S_OK);
    live_outputs.push_back(std::move(output));
  }

  EXPECT_TRUE(reached_boundary);
  RecordProperty("outputs_enumerated", output_count);
}

TEST_F(D3D11DxgiOutputContractSpec, ReportsVersionedDescriptionAndComposition) {
  ComPtr<IDXGIOutput6> output6;
  const HRESULT output_result = GetFirstOutput(output6);
  ASSERT_TRUE(output_result == S_OK || output_result == DXGI_ERROR_NOT_FOUND);
  if (output_result == DXGI_ERROR_NOT_FOUND) {
    EXPECT_EQ(output6.get(), nullptr);
    return;
  }
  ASSERT_TRUE(output6);

  DXGI_OUTPUT_DESC desc = {};
  DXGI_OUTPUT_DESC1 desc1 = {};
  ASSERT_EQ(output6->GetDesc(&desc), S_OK);
  ASSERT_EQ(output6->GetDesc1(&desc1), S_OK);
  EXPECT_EQ(std::wstring(desc1.DeviceName), std::wstring(desc.DeviceName));
  EXPECT_EQ(desc1.DesktopCoordinates.left, desc.DesktopCoordinates.left);
  EXPECT_EQ(desc1.DesktopCoordinates.top, desc.DesktopCoordinates.top);
  EXPECT_EQ(desc1.DesktopCoordinates.right, desc.DesktopCoordinates.right);
  EXPECT_EQ(desc1.DesktopCoordinates.bottom, desc.DesktopCoordinates.bottom);
  EXPECT_EQ(desc1.AttachedToDesktop, desc.AttachedToDesktop);
  EXPECT_EQ(desc1.Rotation, desc.Rotation);
  EXPECT_EQ(desc1.Monitor, desc.Monitor);
  if (desc1.AttachedToDesktop) {
    EXPECT_GT(desc1.BitsPerColor, 0u);
  }

  const BOOL supports_overlays = output6->SupportsOverlays();
  EXPECT_TRUE(supports_overlays == TRUE || supports_overlays == FALSE);
  UINT composition_flags = std::numeric_limits<UINT>::max();
  ASSERT_EQ(output6->CheckHardwareCompositionSupport(&composition_flags), S_OK);
  constexpr UINT kCompositionMask =
      DXGI_HARDWARE_COMPOSITION_SUPPORT_FLAG_FULLSCREEN |
      DXGI_HARDWARE_COMPOSITION_SUPPORT_FLAG_WINDOWED |
      DXGI_HARDWARE_COMPOSITION_SUPPORT_FLAG_CURSOR_STRETCHED;
  EXPECT_EQ(composition_flags & ~kCompositionMask, 0u);
}

TEST_F(D3D11DxgiOutputContractSpec, QueriesOverlayFormatCapabilities) {
  ASSERT_EQ(context_.InitializeDevice(), S_OK);
  ComPtr<IDXGIOutput6> output6;
  const HRESULT output_result = GetFirstOutput(output6);
  ASSERT_TRUE(output_result == S_OK || output_result == DXGI_ERROR_NOT_FOUND);
  std::uint32_t selected_count = 0;
  for (std::uint32_t logical = 0; logical < kOverlayFormatCases.size();
       ++logical) {
    if (!dxmt::test::LogicalCaseSelected(kOverlayFormatRegistration.family(),
                                         logical))
      continue;
    ++selected_count;
    const OverlayFormatCase &test_case = kOverlayFormatCases[logical];
    const auto case_id =
        dxmt::test::LogicalCaseId(kOverlayFormatRegistration.family(), logical);
    SCOPED_TRACE(::testing::Message()
                 << "LogicalCaseId: " << case_id << " case=" << test_case.name
                 << " format=" << static_cast<UINT>(test_case.format)
                 << " Replay: --dxmt-case-id=" << case_id);
    if (output_result == DXGI_ERROR_NOT_FOUND) {
      EXPECT_EQ(output6.get(), nullptr);
      continue;
    }
    ASSERT_TRUE(output6);

    UINT flags = std::numeric_limits<UINT>::max();
    ASSERT_EQ(output6->CheckOverlaySupport(test_case.format, context_.device(),
                                           &flags),
              S_OK);
    constexpr UINT kOverlayMask =
        DXGI_OVERLAY_SUPPORT_FLAG_DIRECT | DXGI_OVERLAY_SUPPORT_FLAG_SCALING;
    EXPECT_EQ(flags & ~kOverlayMask, 0u);
  }

  ASSERT_NE(selected_count, 0u);
  RecordProperty("logical_cases_executed", selected_count);
  RecordProperty("logical_case_prefix",
                 kOverlayFormatRegistration.family().case_id_prefix);
}

TEST_F(D3D11DxgiOutputContractSpec, QueriesOverlayColorSpaceCapabilities) {
  ASSERT_EQ(context_.InitializeDevice(), S_OK);
  ComPtr<IDXGIOutput6> output6;
  const HRESULT output_result = GetFirstOutput(output6);
  ASSERT_TRUE(output_result == S_OK || output_result == DXGI_ERROR_NOT_FOUND);
  std::uint32_t selected_count = 0;
  for (std::uint32_t logical = 0; logical < kOverlayColorSpaceCases.size();
       ++logical) {
    if (!dxmt::test::LogicalCaseSelected(kOverlayColorRegistration.family(),
                                         logical))
      continue;
    ++selected_count;
    const OverlayColorSpaceCase &test_case = kOverlayColorSpaceCases[logical];
    const auto case_id =
        dxmt::test::LogicalCaseId(kOverlayColorRegistration.family(), logical);
    SCOPED_TRACE(::testing::Message()
                 << "LogicalCaseId: " << case_id << " case=" << test_case.name
                 << " color_space=" << static_cast<UINT>(test_case.color_space)
                 << " Replay: --dxmt-case-id=" << case_id);
    if (output_result == DXGI_ERROR_NOT_FOUND) {
      EXPECT_EQ(output6.get(), nullptr);
      continue;
    }
    ASSERT_TRUE(output6);

    UINT flags = std::numeric_limits<UINT>::max();
    ASSERT_EQ(output6->CheckOverlayColorSpaceSupport(DXGI_FORMAT_R8G8B8A8_UNORM,
                                                     test_case.color_space,
                                                     context_.device(), &flags),
              S_OK);
    EXPECT_EQ(flags & ~DXGI_OVERLAY_COLOR_SPACE_SUPPORT_FLAG_PRESENT, 0u);
  }

  ASSERT_NE(selected_count, 0u);
  RecordProperty("logical_cases_executed", selected_count);
  RecordProperty("logical_case_prefix",
                 kOverlayColorRegistration.family().case_id_prefix);
}

} // namespace
