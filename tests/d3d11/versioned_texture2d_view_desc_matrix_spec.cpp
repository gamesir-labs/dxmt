#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <array>
#include <cstdint>
#include <vector>

// Public D3D11.3 versioned-view coverage for Texture2D arrays. The three view
// kinds exercise their Desc1-only PlaneSlice member while retaining legacy
// interface and descriptor compatibility.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

enum class ViewKind : std::uint32_t {
  ShaderResource,
  RenderTarget,
  UnorderedAccess,
};

constexpr std::array<const char *, 3> kViewKindNames = {
    "ShaderResource",
    "RenderTarget",
    "UnorderedAccess",
};

constexpr std::uint32_t kViewKindCount = kViewKindNames.size();

const dxmt::test::LogicalCaseFamilyRegistration kVersionedViewDescCases(
    "D3D11VersionedTexture2dViewDescMatrixSpec."
    "RoundTripsArrayViewDescriptions",
    "D3D11.Texture2DArray.View1.Description.", kViewKindCount, 1,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "Device",
      "ID3D11Device3,CreateShaderResourceView1,CreateRenderTargetView1,"
      "CreateUnorderedAccessView1,ID3D11ShaderResourceView1GetDesc1,"
      "ID3D11RenderTargetView1GetDesc1,"
      "ID3D11UnorderedAccessView1GetDesc1,ID3D11ViewGetResource,"
      "ID3D11DeviceChildGetDevice,QueryInterface,ComObjectIdentity"},
     dxmt::test::kResourceTestCost,
     "one test-local D3D11 device, one Texture2D array, and one live versioned "
     "view per selected logical case",
     "create explicit Texture2D-array SRV1, RTV1, and UAV1 descriptions with "
     "PlaneSlice zero, then inspect their versioned and legacy public "
     "interfaces",
     "each versioned view preserves its full Desc1, exposes the matching "
     "legacy descriptor and interface, and returns the source resource and "
     "creating device identities",
     "logical ID, selected-case count, view kind, expected and returned "
     "descriptors, resource and owner addresses, interface HRESULTs and "
     "addresses, failure phase, and exact replay argument"});

const dxmt::test::LogicalCaseFamilyRegistration kInvalidPlaneSliceCases(
    "D3D11VersionedTexture2dViewDescMatrixSpec."
    "RejectsNonzeroPlaneSliceForSinglePlaneFormat",
    "D3D11.Texture2DArray.View1.InvalidPlaneSlice.", kViewKindCount, 1,
    {dxmt::test::TestClass::Robustness,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "Device",
      "ID3D11Device3,CreateShaderResourceView1,CreateRenderTargetView1,"
      "CreateUnorderedAccessView1,PlaneSlice"},
     dxmt::test::kResourceTestCost,
     "one test-local D3D11 device, one Texture2D array, and at most one "
     "rejected versioned view per selected logical case",
     "request PlaneSlice one for a single-plane R8G8B8A8 texture through each "
     "versioned view creation API",
     "every API returns E_INVALIDARG and clears its output pointer",
     "logical ID, selected-case count, view kind, plane slice, HRESULT, output "
     "address, and exact replay argument"});

const dxmt::test::TestCostRegistration
    kVersionedViewDescCost("D3D11VersionedTexture2dViewDescMatrixSpec."
                           "RoundTripsArrayViewDescriptions",
                           dxmt::test::kResourceTestCost);
const dxmt::test::TestCostRegistration
    kInvalidPlaneSliceCost("D3D11VersionedTexture2dViewDescMatrixSpec."
                           "RejectsNonzeroPlaneSliceForSinglePlaneFormat",
                           dxmt::test::kResourceTestCost);

D3D11_SHADER_RESOURCE_VIEW_DESC1 ShaderResourceDesc(UINT plane_slice = 0) {
  D3D11_SHADER_RESOURCE_VIEW_DESC1 desc = {};
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
  desc.Texture2DArray.MostDetailedMip = 1;
  desc.Texture2DArray.MipLevels = 2;
  desc.Texture2DArray.FirstArraySlice = 1;
  desc.Texture2DArray.ArraySize = 2;
  desc.Texture2DArray.PlaneSlice = plane_slice;
  return desc;
}

D3D11_RENDER_TARGET_VIEW_DESC1 RenderTargetDesc(UINT plane_slice = 0) {
  D3D11_RENDER_TARGET_VIEW_DESC1 desc = {};
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
  desc.Texture2DArray.MipSlice = 1;
  desc.Texture2DArray.FirstArraySlice = 1;
  desc.Texture2DArray.ArraySize = 2;
  desc.Texture2DArray.PlaneSlice = plane_slice;
  return desc;
}

D3D11_UNORDERED_ACCESS_VIEW_DESC1 UnorderedAccessDesc(UINT plane_slice = 0) {
  D3D11_UNORDERED_ACCESS_VIEW_DESC1 desc = {};
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
  desc.Texture2DArray.MipSlice = 1;
  desc.Texture2DArray.FirstArraySlice = 1;
  desc.Texture2DArray.ArraySize = 2;
  desc.Texture2DArray.PlaneSlice = plane_slice;
  return desc;
}

class D3D11VersionedTexture2dViewDescMatrixSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_EQ(
        context_.device()->QueryInterface(
            __uuidof(ID3D11Device3), reinterpret_cast<void **>(device3_.put())),
        S_OK);
    ASSERT_NE(device3_.get(), nullptr);

    D3D11_TEXTURE2D_DESC1 desc = {};
    desc.Width = 16;
    desc.Height = 8;
    desc.MipLevels = 3;
    desc.ArraySize = 4;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET |
                     D3D11_BIND_UNORDERED_ACCESS;
    desc.TextureLayout = D3D11_TEXTURE_LAYOUT_UNDEFINED;
    ASSERT_EQ(device3_->CreateTexture2D1(&desc, nullptr, texture_.put()), S_OK);
    ASSERT_NE(texture_.get(), nullptr);
    ASSERT_EQ(
        texture_->QueryInterface(__uuidof(ID3D11Resource),
                                 reinterpret_cast<void **>(resource_.put())),
        S_OK);
    ASSERT_NE(resource_.get(), nullptr);
  }

  D3D11TestContext context_;
  ComPtr<ID3D11Device3> device3_;
  ComPtr<ID3D11Texture2D1> texture_;
  ComPtr<ID3D11Resource> resource_;
};

TEST_F(D3D11VersionedTexture2dViewDescMatrixSpec,
       RoundTripsArrayViewDescriptions) {
  std::vector<std::uint32_t> selected_cases;
  for (std::uint32_t logical = 0; logical < kViewKindCount; ++logical) {
    if (dxmt::test::LogicalCaseSelected(kVersionedViewDescCases.family(),
                                        logical))
      selected_cases.push_back(logical);
  }
  ASSERT_FALSE(selected_cases.empty());

  RecordProperty("logical_cases_executed", selected_cases.size());
  RecordProperty("logical_case_prefix",
                 kVersionedViewDescCases.family().case_id_prefix);
  for (const std::uint32_t logical : selected_cases) {
    const ViewKind kind = static_cast<ViewKind>(logical);
    HRESULT create_result = E_FAIL;
    HRESULT versioned_interface_result = E_FAIL;
    HRESULT legacy_interface_result = E_FAIL;
    bool versioned_desc_matches = false;
    bool legacy_desc_matches = false;
    IUnknown *view_address = nullptr;
    IUnknown *versioned_address = nullptr;
    IUnknown *legacy_address = nullptr;
    ComPtr<ID3D11Resource> returned_resource;
    ComPtr<ID3D11Device> owner;

    if (kind == ViewKind::ShaderResource) {
      const auto expected = ShaderResourceDesc();
      ComPtr<ID3D11ShaderResourceView1> view;
      create_result = device3_->CreateShaderResourceView1(
          resource_.get(), &expected, view.put());
      if (create_result == S_OK && view) {
        D3D11_SHADER_RESOURCE_VIEW_DESC1 actual = {};
        D3D11_SHADER_RESOURCE_VIEW_DESC legacy_desc = {};
        view->GetDesc1(&actual);
        view->GetDesc(&legacy_desc);
        view->GetResource(returned_resource.put());
        view->GetDevice(owner.put());
        ComPtr<ID3D11ShaderResourceView1> queried_versioned;
        ComPtr<ID3D11ShaderResourceView> queried_legacy;
        versioned_interface_result = view->QueryInterface(
            __uuidof(ID3D11ShaderResourceView1),
            reinterpret_cast<void **>(queried_versioned.put()));
        legacy_interface_result = view->QueryInterface(
            __uuidof(ID3D11ShaderResourceView),
            reinterpret_cast<void **>(queried_legacy.put()));
        view_address = view.get();
        versioned_address = queried_versioned.get();
        legacy_address = queried_legacy.get();
        versioned_desc_matches =
            actual.Format == expected.Format &&
            actual.ViewDimension == expected.ViewDimension &&
            actual.Texture2DArray.MostDetailedMip ==
                expected.Texture2DArray.MostDetailedMip &&
            actual.Texture2DArray.MipLevels ==
                expected.Texture2DArray.MipLevels &&
            actual.Texture2DArray.FirstArraySlice ==
                expected.Texture2DArray.FirstArraySlice &&
            actual.Texture2DArray.ArraySize ==
                expected.Texture2DArray.ArraySize &&
            actual.Texture2DArray.PlaneSlice ==
                expected.Texture2DArray.PlaneSlice;
        legacy_desc_matches =
            legacy_desc.Format == expected.Format &&
            legacy_desc.ViewDimension == expected.ViewDimension &&
            legacy_desc.Texture2DArray.MostDetailedMip ==
                expected.Texture2DArray.MostDetailedMip &&
            legacy_desc.Texture2DArray.MipLevels ==
                expected.Texture2DArray.MipLevels &&
            legacy_desc.Texture2DArray.FirstArraySlice ==
                expected.Texture2DArray.FirstArraySlice &&
            legacy_desc.Texture2DArray.ArraySize ==
                expected.Texture2DArray.ArraySize;
      }
    } else if (kind == ViewKind::RenderTarget) {
      const auto expected = RenderTargetDesc();
      ComPtr<ID3D11RenderTargetView1> view;
      create_result = device3_->CreateRenderTargetView1(resource_.get(),
                                                        &expected, view.put());
      if (create_result == S_OK && view) {
        D3D11_RENDER_TARGET_VIEW_DESC1 actual = {};
        D3D11_RENDER_TARGET_VIEW_DESC legacy_desc = {};
        view->GetDesc1(&actual);
        view->GetDesc(&legacy_desc);
        view->GetResource(returned_resource.put());
        view->GetDevice(owner.put());
        ComPtr<ID3D11RenderTargetView1> queried_versioned;
        ComPtr<ID3D11RenderTargetView> queried_legacy;
        versioned_interface_result = view->QueryInterface(
            __uuidof(ID3D11RenderTargetView1),
            reinterpret_cast<void **>(queried_versioned.put()));
        legacy_interface_result = view->QueryInterface(
            __uuidof(ID3D11RenderTargetView),
            reinterpret_cast<void **>(queried_legacy.put()));
        view_address = view.get();
        versioned_address = queried_versioned.get();
        legacy_address = queried_legacy.get();
        versioned_desc_matches =
            actual.Format == expected.Format &&
            actual.ViewDimension == expected.ViewDimension &&
            actual.Texture2DArray.MipSlice ==
                expected.Texture2DArray.MipSlice &&
            actual.Texture2DArray.FirstArraySlice ==
                expected.Texture2DArray.FirstArraySlice &&
            actual.Texture2DArray.ArraySize ==
                expected.Texture2DArray.ArraySize &&
            actual.Texture2DArray.PlaneSlice ==
                expected.Texture2DArray.PlaneSlice;
        legacy_desc_matches =
            legacy_desc.Format == expected.Format &&
            legacy_desc.ViewDimension == expected.ViewDimension &&
            legacy_desc.Texture2DArray.MipSlice ==
                expected.Texture2DArray.MipSlice &&
            legacy_desc.Texture2DArray.FirstArraySlice ==
                expected.Texture2DArray.FirstArraySlice &&
            legacy_desc.Texture2DArray.ArraySize ==
                expected.Texture2DArray.ArraySize;
      }
    } else {
      const auto expected = UnorderedAccessDesc();
      ComPtr<ID3D11UnorderedAccessView1> view;
      create_result = device3_->CreateUnorderedAccessView1(
          resource_.get(), &expected, view.put());
      if (create_result == S_OK && view) {
        D3D11_UNORDERED_ACCESS_VIEW_DESC1 actual = {};
        D3D11_UNORDERED_ACCESS_VIEW_DESC legacy_desc = {};
        view->GetDesc1(&actual);
        view->GetDesc(&legacy_desc);
        view->GetResource(returned_resource.put());
        view->GetDevice(owner.put());
        ComPtr<ID3D11UnorderedAccessView1> queried_versioned;
        ComPtr<ID3D11UnorderedAccessView> queried_legacy;
        versioned_interface_result = view->QueryInterface(
            __uuidof(ID3D11UnorderedAccessView1),
            reinterpret_cast<void **>(queried_versioned.put()));
        legacy_interface_result = view->QueryInterface(
            __uuidof(ID3D11UnorderedAccessView),
            reinterpret_cast<void **>(queried_legacy.put()));
        view_address = view.get();
        versioned_address = queried_versioned.get();
        legacy_address = queried_legacy.get();
        versioned_desc_matches =
            actual.Format == expected.Format &&
            actual.ViewDimension == expected.ViewDimension &&
            actual.Texture2DArray.MipSlice ==
                expected.Texture2DArray.MipSlice &&
            actual.Texture2DArray.FirstArraySlice ==
                expected.Texture2DArray.FirstArraySlice &&
            actual.Texture2DArray.ArraySize ==
                expected.Texture2DArray.ArraySize &&
            actual.Texture2DArray.PlaneSlice ==
                expected.Texture2DArray.PlaneSlice;
        legacy_desc_matches =
            legacy_desc.Format == expected.Format &&
            legacy_desc.ViewDimension == expected.ViewDimension &&
            legacy_desc.Texture2DArray.MipSlice ==
                expected.Texture2DArray.MipSlice &&
            legacy_desc.Texture2DArray.FirstArraySlice ==
                expected.Texture2DArray.FirstArraySlice &&
            legacy_desc.Texture2DArray.ArraySize ==
                expected.Texture2DArray.ArraySize;
      }
    }

    const bool valid =
        create_result == S_OK && view_address && versioned_desc_matches &&
        legacy_desc_matches && returned_resource.get() == resource_.get() &&
        owner.get() == context_.device() &&
        versioned_interface_result == S_OK &&
        versioned_address == view_address && legacy_interface_result == S_OK &&
        legacy_address == view_address;
    if (valid)
      continue;

    const auto case_id =
        dxmt::test::LogicalCaseId(kVersionedViewDescCases.family(), logical);
    ADD_FAILURE() << "LogicalCaseId: " << case_id << '\n'
                  << "Class: "
                  << dxmt::test::TestClassName(
                         kVersionedViewDescCases.family().traits.test_class)
                  << '\n'
                  << "Parameters: logical=" << logical
                  << " view_kind=" << kViewKindNames[logical]
                  << " selected_cases=" << selected_cases.size() << '\n'
                  << "Expected: create_hresult=" << S_OK
                  << " versioned_desc_match=true legacy_desc_match=true"
                  << " versioned_interface_hresult=" << S_OK
                  << " legacy_interface_hresult=" << S_OK
                  << " resource=" << resource_.get()
                  << " owner=" << context_.device() << '\n'
                  << "Observed: create_hresult=" << create_result
                  << " versioned_desc_match=" << versioned_desc_matches
                  << " legacy_desc_match=" << legacy_desc_matches
                  << " versioned_interface_hresult="
                  << versioned_interface_result
                  << " legacy_interface_hresult=" << legacy_interface_result
                  << " resource=" << returned_resource.get()
                  << " owner=" << owner.get() << " view=" << view_address
                  << " versioned=" << versioned_address
                  << " legacy=" << legacy_address << '\n'
                  << "Replay: --dxmt-case-id=" << case_id;
    break;
  }

  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11VersionedTexture2dViewDescMatrixSpec,
       RejectsNonzeroPlaneSliceForSinglePlaneFormat) {
  std::vector<std::uint32_t> selected_cases;
  for (std::uint32_t logical = 0; logical < kViewKindCount; ++logical) {
    if (dxmt::test::LogicalCaseSelected(kInvalidPlaneSliceCases.family(),
                                        logical))
      selected_cases.push_back(logical);
  }
  ASSERT_FALSE(selected_cases.empty());

  RecordProperty("logical_cases_executed", selected_cases.size());
  RecordProperty("logical_case_prefix",
                 kInvalidPlaneSliceCases.family().case_id_prefix);
  for (const std::uint32_t logical : selected_cases) {
    const ViewKind kind = static_cast<ViewKind>(logical);
    HRESULT create_result = E_FAIL;
    IUnknown *output =
        reinterpret_cast<IUnknown *>(static_cast<std::uintptr_t>(1));
    if (kind == ViewKind::ShaderResource) {
      const auto desc = ShaderResourceDesc(1);
      ID3D11ShaderResourceView1 *view =
          reinterpret_cast<ID3D11ShaderResourceView1 *>(
              static_cast<std::uintptr_t>(1));
      create_result =
          device3_->CreateShaderResourceView1(resource_.get(), &desc, &view);
      output = view;
    } else if (kind == ViewKind::RenderTarget) {
      const auto desc = RenderTargetDesc(1);
      ID3D11RenderTargetView1 *view =
          reinterpret_cast<ID3D11RenderTargetView1 *>(
              static_cast<std::uintptr_t>(1));
      create_result =
          device3_->CreateRenderTargetView1(resource_.get(), &desc, &view);
      output = view;
    } else {
      const auto desc = UnorderedAccessDesc(1);
      ID3D11UnorderedAccessView1 *view =
          reinterpret_cast<ID3D11UnorderedAccessView1 *>(
              static_cast<std::uintptr_t>(1));
      create_result =
          device3_->CreateUnorderedAccessView1(resource_.get(), &desc, &view);
      output = view;
    }

    if (create_result == E_INVALIDARG && output == nullptr)
      continue;

    const auto case_id =
        dxmt::test::LogicalCaseId(kInvalidPlaneSliceCases.family(), logical);
    ADD_FAILURE() << "LogicalCaseId: " << case_id << '\n'
                  << "Class: "
                  << dxmt::test::TestClassName(
                         kInvalidPlaneSliceCases.family().traits.test_class)
                  << '\n'
                  << "Parameters: logical=" << logical
                  << " view_kind=" << kViewKindNames[logical]
                  << " plane_slice=1 selected_cases=" << selected_cases.size()
                  << '\n'
                  << "Expected: create_hresult=" << E_INVALIDARG
                  << " output=null" << '\n'
                  << "Observed: create_hresult=" << create_result
                  << " output=" << output << '\n'
                  << "Replay: --dxmt-case-id=" << case_id;
    if (SUCCEEDED(create_result) && output)
      output->Release();
    break;
  }

  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11VersionedTexture2dViewDescMatrixSpec,
       ValidatesResourceAndOutputPointers) {
  const auto srv_desc = ShaderResourceDesc();
  ID3D11ShaderResourceView1 *srv =
      reinterpret_cast<ID3D11ShaderResourceView1 *>(
          static_cast<std::uintptr_t>(1));
  EXPECT_EQ(device3_->CreateShaderResourceView1(nullptr, &srv_desc, &srv),
            E_INVALIDARG);
  EXPECT_EQ(srv, nullptr);
  EXPECT_EQ(
      device3_->CreateShaderResourceView1(resource_.get(), &srv_desc, nullptr),
      S_FALSE);

  const auto rtv_desc = RenderTargetDesc();
  ID3D11RenderTargetView1 *rtv = reinterpret_cast<ID3D11RenderTargetView1 *>(
      static_cast<std::uintptr_t>(1));
  EXPECT_EQ(device3_->CreateRenderTargetView1(nullptr, &rtv_desc, &rtv),
            E_INVALIDARG);
  EXPECT_EQ(rtv, nullptr);
  EXPECT_EQ(
      device3_->CreateRenderTargetView1(resource_.get(), &rtv_desc, nullptr),
      S_FALSE);

  const auto uav_desc = UnorderedAccessDesc();
  ID3D11UnorderedAccessView1 *uav =
      reinterpret_cast<ID3D11UnorderedAccessView1 *>(
          static_cast<std::uintptr_t>(1));
  EXPECT_EQ(device3_->CreateUnorderedAccessView1(nullptr, &uav_desc, &uav),
            E_INVALIDARG);
  EXPECT_EQ(uav, nullptr);
  EXPECT_EQ(
      device3_->CreateUnorderedAccessView1(resource_.get(), &uav_desc, nullptr),
      S_FALSE);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
