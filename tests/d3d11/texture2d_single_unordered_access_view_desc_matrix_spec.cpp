#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <array>
#include <cstdint>
#include <vector>

// Public D3D11 non-array Texture2D UAV descriptor coverage. Two compatible
// formats across eight mip slices form 16 distinct valid views.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

constexpr UINT kMipLevelCount = 8;
constexpr std::uint32_t kTexture2dUavDescCaseCount = 16;

const dxmt::test::LogicalCaseFamilyRegistration kTexture2dUavDescCases(
    "D3D11Texture2DSingleUnorderedAccessViewDescMatrixSpec."
    "RoundTrips16FormatMipSlices",
    "D3D11.UAV.Texture2D.Description.", kTexture2dUavDescCaseCount, 2,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "Device",
      "CreateTexture2D,CreateUnorderedAccessView,"
      "ID3D11UnorderedAccessViewGetDesc,ID3D11ViewGetResource,"
      "ID3D11DeviceChildGetDevice"},
     dxmt::test::kResourceTestCost,
     "one test-local D3D11 device, one typeless non-array Texture2D, and one "
     "live unordered-access view per selected logical case",
     "create every combination of two compatible formats and eight mip "
     "slices, then query the complete public descriptor and relationships",
     "every view preserves its format and mip slice and returns the source "
     "texture and creating device through public COM interfaces",
     "logical ID, selected-case count, format and mip indexes, expected and "
     "returned descriptors, resource and owner addresses, HRESULT, failure "
     "phase, and exact replay argument"});

const dxmt::test::TestCostRegistration kTexture2dUavDescCost(
    "D3D11Texture2DSingleUnorderedAccessViewDescMatrixSpec."
    "RoundTrips16FormatMipSlices",
    dxmt::test::kResourceTestCost);

constexpr std::array<DXGI_FORMAT, 2> kViewFormats = {
    DXGI_FORMAT_R32_FLOAT,
    DXGI_FORMAT_R32_UINT,
};

struct Texture2dUavDescCase {
  UINT format_index;
  UINT mip_index;
  D3D11_UNORDERED_ACCESS_VIEW_DESC desc;
};

Texture2dUavDescCase CaseForLogical(std::uint32_t logical) {
  Texture2dUavDescCase test_case = {};
  test_case.format_index = logical & 1u;
  test_case.mip_index = logical >> 1u;
  test_case.desc.Format = kViewFormats[test_case.format_index];
  test_case.desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
  test_case.desc.Texture2D.MipSlice = test_case.mip_index;
  return test_case;
}

bool Texture2dUavDescsEqual(const D3D11_UNORDERED_ACCESS_VIEW_DESC &actual,
                            const D3D11_UNORDERED_ACCESS_VIEW_DESC &expected) {
  return actual.Format == expected.Format &&
         actual.ViewDimension == expected.ViewDimension &&
         actual.Texture2D.MipSlice == expected.Texture2D.MipSlice;
}

class D3D11Texture2DSingleUnorderedAccessViewDescMatrixSpec
    : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_NE(context_.context(), nullptr);

    D3D11_TEXTURE2D_DESC texture_desc = {};
    texture_desc.Width = 128;
    texture_desc.Height = 128;
    texture_desc.MipLevels = kMipLevelCount;
    texture_desc.ArraySize = 1;
    texture_desc.Format = DXGI_FORMAT_R32_TYPELESS;
    texture_desc.SampleDesc.Count = 1;
    texture_desc.Usage = D3D11_USAGE_DEFAULT;
    texture_desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    ASSERT_EQ(context_.device()->CreateTexture2D(&texture_desc, nullptr,
                                                 texture_.put()),
              S_OK);
    ASSERT_NE(texture_.get(), nullptr);
    ASSERT_EQ(texture_->QueryInterface(
                  __uuidof(ID3D11Resource),
                  reinterpret_cast<void **>(resource_identity_.put())),
              S_OK);
  }

  D3D11TestContext context_;
  ComPtr<ID3D11Texture2D> texture_;
  ComPtr<ID3D11Resource> resource_identity_;
};

TEST_F(D3D11Texture2DSingleUnorderedAccessViewDescMatrixSpec,
       RoundTrips16FormatMipSlices) {
  std::vector<std::uint32_t> selected_cases;
  selected_cases.reserve(kTexture2dUavDescCaseCount);
  for (std::uint32_t logical = 0; logical < kTexture2dUavDescCaseCount;
       ++logical) {
    if (dxmt::test::LogicalCaseSelected(kTexture2dUavDescCases.family(),
                                        logical))
      selected_cases.push_back(logical);
  }
  ASSERT_FALSE(selected_cases.empty());

  RecordProperty("logical_cases_executed", selected_cases.size());
  RecordProperty("logical_case_prefix",
                 kTexture2dUavDescCases.family().case_id_prefix);
  for (const std::uint32_t logical : selected_cases) {
    const Texture2dUavDescCase test_case = CaseForLogical(logical);
    ComPtr<ID3D11UnorderedAccessView> view;
    const HRESULT create_result = context_.device()->CreateUnorderedAccessView(
        texture_.get(), &test_case.desc, view.put());
    D3D11_UNORDERED_ACCESS_VIEW_DESC actual = {};
    ComPtr<ID3D11Resource> resource;
    ComPtr<ID3D11Device> owner;
    if (create_result == S_OK && view) {
      view->GetDesc(&actual);
      view->GetResource(resource.put());
      view->GetDevice(owner.put());
    }

    const bool desc_matches = Texture2dUavDescsEqual(actual, test_case.desc);
    const bool resource_matches = resource.get() == resource_identity_.get();
    const bool owner_matches = owner.get() == context_.device();
    if (create_result == S_OK && view && desc_matches && resource_matches &&
        owner_matches)
      continue;

    const char *failure_phase = "owner";
    if (create_result != S_OK || !view)
      failure_phase = "create";
    else if (!desc_matches)
      failure_phase = "get_desc";
    else if (!resource_matches)
      failure_phase = "get_resource";

    const auto case_id =
        dxmt::test::LogicalCaseId(kTexture2dUavDescCases.family(), logical);
    ADD_FAILURE()
        << "LogicalCaseId: " << case_id << '\n'
        << "Class: "
        << dxmt::test::TestClassName(
               kTexture2dUavDescCases.family().traits.test_class)
        << '\n'
        << "Requirements: feature_level=11_0 shader_model=None queue=Device "
           "capability=CreateTexture2D,CreateUnorderedAccessView,"
           "UnorderedAccessViewGetDesc,ViewGetResource,DeviceChildGetDevice\n"
        << "ExecutionPath: "
        << dxmt::test::ExecutionPathName(
               kTexture2dUavDescCases.family().traits.execution_path)
        << '\n'
        << "Parameters: logical=" << logical
        << " format_index=" << test_case.format_index
        << " mip_index=" << test_case.mip_index
        << " selected_cases=" << selected_cases.size() << '\n'
        << "Expected: format=" << static_cast<UINT>(test_case.desc.Format)
        << " dimension=" << static_cast<UINT>(test_case.desc.ViewDimension)
        << " mip=" << test_case.desc.Texture2D.MipSlice
        << " resource=" << resource_identity_.get()
        << " owner=" << context_.device() << '\n'
        << "Observed: create_hresult=" << create_result
        << " failure_phase=" << failure_phase
        << " format=" << static_cast<UINT>(actual.Format)
        << " dimension=" << static_cast<UINT>(actual.ViewDimension)
        << " mip=" << actual.Texture2D.MipSlice
        << " resource=" << resource.get() << " owner=" << owner.get()
        << " view=" << view.get() << '\n'
        << "Replay: --dxmt-case-id=" << case_id;
    break;
  }

  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11Texture2DSingleUnorderedAccessViewDescMatrixSpec,
       RejectsNullResourceAndInvalidMipSlice) {
  ID3D11UnorderedAccessView *view =
      reinterpret_cast<ID3D11UnorderedAccessView *>(
          static_cast<std::uintptr_t>(1));
  EXPECT_EQ(
      context_.device()->CreateUnorderedAccessView(nullptr, nullptr, &view),
      E_INVALIDARG);
  EXPECT_EQ(view, nullptr);

  const D3D11_UNORDERED_ACCESS_VIEW_DESC valid = CaseForLogical(0).desc;
  EXPECT_EQ(context_.device()->CreateUnorderedAccessView(texture_.get(), &valid,
                                                         nullptr),
            S_FALSE);

  D3D11_UNORDERED_ACCESS_VIEW_DESC invalid = valid;
  invalid.Texture2D.MipSlice = kMipLevelCount;
  view = reinterpret_cast<ID3D11UnorderedAccessView *>(
      static_cast<std::uintptr_t>(1));
  EXPECT_EQ(context_.device()->CreateUnorderedAccessView(texture_.get(),
                                                         &invalid, &view),
            E_INVALIDARG);
  EXPECT_EQ(view, nullptr);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
