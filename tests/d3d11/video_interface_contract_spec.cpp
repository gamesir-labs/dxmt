#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <d3d11.h>

#include <array>
#include <cstdint>
#include <vector>

// Public D3D11 video-interface coverage limited to stable object and capability
// contracts. Hardware-dependent decoder execution is intentionally excluded.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

constexpr std::array<const char *, 2> kInterfaceCaseNames = {
    "ID3D11VideoDevice",
    "ID3D11VideoContext",
};

constexpr GUID kUnknownDecoderProfile = {
    0x71d42a1b,
    0x6d57,
    0x4c51,
    {0x91, 0x22, 0xac, 0x37, 0xee, 0xf8, 0x05, 0x49},
};

const dxmt::test::LogicalCaseFamilyRegistration kInterfaceRegistration(
    "D3D11VideoInterfaceContractSpec.ExposesContainerComIdentities",
    "D3D11.Video.Interface.Identity.", kInterfaceCaseNames.size(), 1,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "Device",
      "ID3D11VideoDevice,ID3D11VideoContext,ID3D11DeviceChildGetDevice,"
      "QueryInterface,ComObjectIdentity"},
     dxmt::test::kResourceTestCost,
     "one test-local D3D11 device and its immediate context with one live "
     "video-interface reference per selected logical case",
     "query the public video device from ID3D11Device and video context from "
     "the immediate context",
     "each video interface shares its containing object's canonical IUnknown "
     "identity and the video context returns the creating device",
     "logical ID, selected-case count, interface name, HRESULTs, interface, "
     "canonical identity and owner addresses, and exact replay argument"});

const dxmt::test::TestCostRegistration kInterfaceCost(
    "D3D11VideoInterfaceContractSpec.ExposesContainerComIdentities",
    dxmt::test::kResourceTestCost);

class D3D11VideoInterfaceContractSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_NE(context_.context(), nullptr);
  }

  D3D11TestContext context_;
};

TEST_F(D3D11VideoInterfaceContractSpec, ExposesContainerComIdentities) {
  std::vector<std::uint32_t> selected_cases;
  for (std::uint32_t logical = 0; logical < kInterfaceCaseNames.size();
       ++logical) {
    if (dxmt::test::LogicalCaseSelected(kInterfaceRegistration.family(),
                                        logical))
      selected_cases.push_back(logical);
  }
  ASSERT_FALSE(selected_cases.empty());

  RecordProperty("logical_cases_executed", selected_cases.size());
  RecordProperty("logical_case_prefix",
                 kInterfaceRegistration.family().case_id_prefix);
  for (const std::uint32_t logical : selected_cases) {
    IUnknown *container = logical == 0
                              ? static_cast<IUnknown *>(context_.device())
                              : static_cast<IUnknown *>(context_.context());
    const GUID &interface_id = logical == 0 ? __uuidof(ID3D11VideoDevice)
                                            : __uuidof(ID3D11VideoContext);
    ComPtr<IUnknown> container_identity;
    ASSERT_EQ(container->QueryInterface(
                  __uuidof(IUnknown),
                  reinterpret_cast<void **>(container_identity.put())),
              S_OK);

    ComPtr<IUnknown> video_interface;
    const HRESULT interface_result = container->QueryInterface(
        interface_id, reinterpret_cast<void **>(video_interface.put()));
    ComPtr<IUnknown> video_identity;
    HRESULT identity_result = E_FAIL;
    ComPtr<ID3D11Device> owner;
    if (interface_result == S_OK && video_interface) {
      identity_result = video_interface->QueryInterface(
          __uuidof(IUnknown), reinterpret_cast<void **>(video_identity.put()));
      if (logical == 1) {
        ComPtr<ID3D11VideoContext> video_context;
        if (video_interface->QueryInterface(
                __uuidof(ID3D11VideoContext),
                reinterpret_cast<void **>(video_context.put())) == S_OK &&
            video_context) {
          video_context->GetDevice(owner.put());
        }
      }
    }

    const bool valid = interface_result == S_OK && video_interface &&
                       identity_result == S_OK && video_identity &&
                       video_identity.get() == container_identity.get() &&
                       (logical == 0 || owner.get() == context_.device());
    if (valid)
      continue;

    const auto case_id =
        dxmt::test::LogicalCaseId(kInterfaceRegistration.family(), logical);
    ADD_FAILURE() << "LogicalCaseId: " << case_id << '\n'
                  << "Parameters: logical=" << logical
                  << " interface=" << kInterfaceCaseNames[logical]
                  << " selected_cases=" << selected_cases.size() << '\n'
                  << "Expected: interface_hresult=" << S_OK
                  << " identity_hresult=" << S_OK
                  << " identity=" << container_identity.get()
                  << " owner=" << (logical ? context_.device() : nullptr)
                  << '\n'
                  << "Observed: interface_hresult=" << interface_result
                  << " identity_hresult=" << identity_result
                  << " interface=" << video_interface.get()
                  << " identity=" << video_identity.get()
                  << " owner=" << owner.get() << '\n'
                  << "Replay: --dxmt-case-id=" << case_id;
    break;
  }

  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11VideoInterfaceContractSpec, EnumeratesAdvertisedDecoderProfiles) {
  ComPtr<ID3D11VideoDevice> video_device;
  ASSERT_EQ(context_.device()->QueryInterface(
                __uuidof(ID3D11VideoDevice),
                reinterpret_cast<void **>(video_device.put())),
            S_OK);
  ASSERT_NE(video_device.get(), nullptr);

  const UINT profile_count = video_device->GetVideoDecoderProfileCount();
  for (UINT index = 0; index < profile_count; ++index) {
    GUID profile = {};
    EXPECT_EQ(video_device->GetVideoDecoderProfile(index, &profile), S_OK)
        << "profile_index=" << index << " profile_count=" << profile_count;
  }

  GUID out_of_range_profile = {};
  EXPECT_EQ(video_device->GetVideoDecoderProfile(profile_count + 1,
                                                 &out_of_range_profile),
            E_INVALIDARG);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11VideoInterfaceContractSpec,
       ReportsUnknownDecoderProfileFormatUnsupported) {
  ComPtr<ID3D11VideoDevice> video_device;
  ASSERT_EQ(context_.device()->QueryInterface(
                __uuidof(ID3D11VideoDevice),
                reinterpret_cast<void **>(video_device.put())),
            S_OK);

  BOOL supported = TRUE;
  ASSERT_EQ(video_device->CheckVideoDecoderFormat(&kUnknownDecoderProfile,
                                                  DXGI_FORMAT_NV12, &supported),
            S_OK);
  EXPECT_EQ(supported, FALSE);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
