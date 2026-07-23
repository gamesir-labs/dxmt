#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <dxgi1_2.h>

#include <array>
#include <cstdint>

// Public legacy shared-resource coverage. Every case uses an unnamed KMT
// handle owned by one test-local texture and a second test-local device, so no
// fixed name, filesystem path, or process-global synchronization is shared
// with parallel workers.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

struct SharedInterfaceCase {
  const GUID *iid;
  HRESULT expected;
  const char *name;
};

constexpr std::array kSharedInterfaceCases = {
    SharedInterfaceCase{&__uuidof(IUnknown), S_OK, "IUnknown"},
    SharedInterfaceCase{&__uuidof(ID3D11DeviceChild), S_OK, "DeviceChild"},
    SharedInterfaceCase{&__uuidof(ID3D11Resource), S_OK, "Resource"},
    SharedInterfaceCase{&__uuidof(ID3D11Texture2D), S_OK, "Texture2D"},
    SharedInterfaceCase{&__uuidof(IDXGIResource), S_OK, "DxgiResource"},
    SharedInterfaceCase{&__uuidof(ID3D11Buffer), E_NOINTERFACE,
                        "UnsupportedBuffer"},
};

const dxmt::test::LogicalCaseFamilyRegistration kSharedInterfaceRegistration(
    "D3D11SharedResourceContractSpec.OpensAcrossDevicesByPublicInterface",
    "D3D11.SharedResource.Open.Interface.", kSharedInterfaceCases.size(), 1,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "Device",
      "CreateTexture2D,D3D11_RESOURCE_MISC_SHARED,IDXGIResource,"
      "GetSharedHandle,OpenSharedResource,ID3D11Resource,ID3D11Texture2D,"
      "ID3D11DeviceChildGetDevice,QueryInterface,ComObjectIdentity"},
     dxmt::test::kResourceTestCost,
     "two test-local D3D11 devices, one shared texture, and one unnamed KMT "
     "handle per physical test",
     "create a legacy shared texture on one device and open its unique handle "
     "on a second device through each public resource interface plus one "
     "unrelated interface",
     "supported interfaces open one texture identity with the original "
     "description and second-device ownership; the unrelated interface "
     "returns E_NOINTERFACE and no object",
     "logical ID, interface, creation and open HRESULTs, shared handle, "
     "descriptions, identities, owner, and exact replay argument"});

const dxmt::test::TestCostRegistration kSharedInterfaceCost(
    "D3D11SharedResourceContractSpec.OpensAcrossDevicesByPublicInterface",
    dxmt::test::kResourceTestCost);
const dxmt::test::TestCostRegistration kLegacyHandleTypeCost(
    "D3D11SharedResourceContractSpec.RejectsLegacyHandleInNtOpenMethod",
    dxmt::test::kResourceTestCost);

class D3D11SharedResourceContractSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    constexpr D3D_FEATURE_LEVEL kFeatureLevel = D3D_FEATURE_LEVEL_11_0;
    D3D_FEATURE_LEVEL chosen_level = D3D_FEATURE_LEVEL_9_1;
    ASSERT_EQ(D3D11CreateDevice(context_.adapter(), D3D_DRIVER_TYPE_UNKNOWN,
                                nullptr, 0, &kFeatureLevel, 1,
                                D3D11_SDK_VERSION, second_device_.put(),
                                &chosen_level, second_context_.put()),
              S_OK);
    ASSERT_TRUE(second_device_);
    ASSERT_TRUE(second_context_);
    ASSERT_EQ(chosen_level, kFeatureLevel);
    ASSERT_EQ(second_device_->QueryInterface(
                  __uuidof(ID3D11Device1),
                  reinterpret_cast<void **>(second_device1_.put())),
              S_OK);

    desc_.Width = 32;
    desc_.Height = 16;
    desc_.MipLevels = 1;
    desc_.ArraySize = 1;
    desc_.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc_.SampleDesc.Count = 1;
    desc_.Usage = D3D11_USAGE_DEFAULT;
    desc_.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    desc_.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
    ASSERT_EQ(context_.device()->CreateTexture2D(&desc_, nullptr,
                                                 shared_texture_.put()),
              S_OK);
    ASSERT_TRUE(shared_texture_);

    ComPtr<IDXGIResource> dxgi_resource;
    ASSERT_EQ(shared_texture_->QueryInterface(
                  __uuidof(IDXGIResource),
                  reinterpret_cast<void **>(dxgi_resource.put())),
              S_OK);
    ASSERT_EQ(dxgi_resource->GetSharedHandle(&shared_handle_), S_OK);
    ASSERT_NE(shared_handle_, nullptr);
  }

  D3D11TestContext context_;
  ComPtr<ID3D11Device> second_device_;
  ComPtr<ID3D11Device1> second_device1_;
  ComPtr<ID3D11DeviceContext> second_context_;
  ComPtr<ID3D11Texture2D> shared_texture_;
  D3D11_TEXTURE2D_DESC desc_ = {};
  HANDLE shared_handle_ = nullptr;
};

TEST_F(D3D11SharedResourceContractSpec, OpensAcrossDevicesByPublicInterface) {
  std::uint32_t selected_count = 0;
  for (std::uint32_t logical = 0; logical < kSharedInterfaceCases.size();
       ++logical) {
    if (!dxmt::test::LogicalCaseSelected(kSharedInterfaceRegistration.family(),
                                         logical))
      continue;
    ++selected_count;
    const SharedInterfaceCase &test_case = kSharedInterfaceCases[logical];
    const auto case_id = dxmt::test::LogicalCaseId(
        kSharedInterfaceRegistration.family(), logical);
    SCOPED_TRACE(::testing::Message() << "LogicalCaseId: " << case_id
                                      << " interface=" << test_case.name
                                      << " handle=" << shared_handle_
                                      << " Replay: --dxmt-case-id=" << case_id);

    ComPtr<IUnknown> opened;
    const HRESULT open_result = second_device_->OpenSharedResource(
        shared_handle_, *test_case.iid,
        reinterpret_cast<void **>(opened.put()));
    EXPECT_EQ(open_result, test_case.expected);
    if (test_case.expected == E_NOINTERFACE) {
      EXPECT_EQ(opened.get(), nullptr);
      continue;
    }
    ASSERT_TRUE(opened);

    ComPtr<ID3D11Texture2D> opened_texture;
    ASSERT_EQ(
        opened->QueryInterface(__uuidof(ID3D11Texture2D),
                               reinterpret_cast<void **>(opened_texture.put())),
        S_OK);
    D3D11_TEXTURE2D_DESC opened_desc = {};
    opened_texture->GetDesc(&opened_desc);
    EXPECT_EQ(opened_desc.Width, desc_.Width);
    EXPECT_EQ(opened_desc.Height, desc_.Height);
    EXPECT_EQ(opened_desc.MipLevels, desc_.MipLevels);
    EXPECT_EQ(opened_desc.ArraySize, desc_.ArraySize);
    EXPECT_EQ(opened_desc.Format, desc_.Format);
    EXPECT_EQ(opened_desc.SampleDesc.Count, desc_.SampleDesc.Count);
    EXPECT_EQ(opened_desc.BindFlags, desc_.BindFlags);
    EXPECT_EQ(opened_desc.MiscFlags & D3D11_RESOURCE_MISC_SHARED,
              D3D11_RESOURCE_MISC_SHARED);

    ComPtr<ID3D11Device> owner;
    opened_texture->GetDevice(owner.put());
    EXPECT_EQ(owner.get(), second_device_.get());
    ComPtr<IUnknown> opened_identity;
    ComPtr<IUnknown> texture_identity;
    ASSERT_EQ(opened->QueryInterface(
                  __uuidof(IUnknown),
                  reinterpret_cast<void **>(opened_identity.put())),
              S_OK);
    ASSERT_EQ(opened_texture->QueryInterface(
                  __uuidof(IUnknown),
                  reinterpret_cast<void **>(texture_identity.put())),
              S_OK);
    EXPECT_EQ(opened_identity.get(), texture_identity.get());
  }

  ASSERT_NE(selected_count, 0u);
  RecordProperty("logical_cases_executed", selected_count);
  RecordProperty("logical_case_prefix",
                 kSharedInterfaceRegistration.family().case_id_prefix);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
  EXPECT_EQ(second_device_->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11SharedResourceContractSpec, RejectsLegacyHandleInNtOpenMethod) {
  ComPtr<ID3D11Texture2D> opened;
  EXPECT_EQ(second_device1_->OpenSharedResource1(
                shared_handle_, __uuidof(ID3D11Texture2D),
                reinterpret_cast<void **>(opened.put())),
            E_INVALIDARG);
  EXPECT_EQ(opened.get(), nullptr);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
  EXPECT_EQ(second_device_->GetDeviceRemovedReason(), S_OK);
}

} // namespace
