#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <array>
#include <cstdint>
#include <vector>

// Public D3D11 Texture3D UAV descriptor coverage. Compatible formats, mip
// slices, and W sizes form exactly 4096 distinct valid views.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

constexpr std::uint32_t kTexture3dUavDescCaseCount = 4096;

const dxmt::test::LogicalCaseFamilyRegistration kTexture3dUavDescCases(
    "D3D11Texture3DUnorderedAccessViewDescMatrixSpec."
    "RoundTrips4096MipDepthRanges",
    "D3D11.UAV.Texture3D.Description.", kTexture3dUavDescCaseCount, 4,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "Device",
      "CreateTexture3D,CreateUnorderedAccessView,"
      "ID3D11UnorderedAccessViewGetDesc,ID3D11ViewGetResource,"
      "ID3D11DeviceChildGetDevice"},
     dxmt::test::kResourceTestCost,
     "one test-local D3D11 device, one typeless Texture3D, and one live "
     "unordered-access view per selected logical case",
     "create every combination of two compatible formats, two mip slices, "
     "and 1024 W sizes beginning at W slice zero, then query the complete "
     "public descriptor and relationships",
     "every view preserves its format and mip-depth range and returns the "
     "source texture and creating device through public COM interfaces",
     "logical ID, selected-case count, combination indexes, expected and "
     "returned descriptors, resource and owner addresses, HRESULT, failure "
     "phase, and exact replay argument"});

const dxmt::test::TestCostRegistration
    kTexture3dUavDescCost("D3D11Texture3DUnorderedAccessViewDescMatrixSpec."
                          "RoundTrips4096MipDepthRanges",
                          dxmt::test::kResourceTestCost);

constexpr std::array<DXGI_FORMAT, 2> kViewFormats = {
    DXGI_FORMAT_R32_FLOAT,
    DXGI_FORMAT_R32_UINT,
};

struct Texture3dUavDescCase {
  UINT format_index;
  UINT mip_index;
  UINT depth_count_index;
  D3D11_UNORDERED_ACCESS_VIEW_DESC desc;
};

Texture3dUavDescCase CaseForLogical(std::uint32_t logical) {
  Texture3dUavDescCase test_case = {};
  test_case.format_index = logical & 1u;
  test_case.mip_index = (logical >> 1u) & 1u;
  test_case.depth_count_index = logical >> 2u;

  test_case.desc.Format = kViewFormats[test_case.format_index];
  test_case.desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE3D;
  test_case.desc.Texture3D.MipSlice = test_case.mip_index;
  test_case.desc.Texture3D.FirstWSlice = 0;
  test_case.desc.Texture3D.WSize = test_case.depth_count_index + 1u;
  return test_case;
}

bool Texture3dUavDescsEqual(const D3D11_UNORDERED_ACCESS_VIEW_DESC &actual,
                            const D3D11_UNORDERED_ACCESS_VIEW_DESC &expected) {
  return actual.Format == expected.Format &&
         actual.ViewDimension == expected.ViewDimension &&
         actual.Texture3D.MipSlice == expected.Texture3D.MipSlice &&
         actual.Texture3D.FirstWSlice == expected.Texture3D.FirstWSlice &&
         actual.Texture3D.WSize == expected.Texture3D.WSize;
}

class D3D11Texture3DUnorderedAccessViewDescMatrixSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_NE(context_.context(), nullptr);

    D3D11_TEXTURE3D_DESC texture_desc = {};
    texture_desc.Width = 4;
    texture_desc.Height = 4;
    texture_desc.Depth = 2048;
    texture_desc.MipLevels = 2;
    texture_desc.Format = DXGI_FORMAT_R32_TYPELESS;
    texture_desc.Usage = D3D11_USAGE_DEFAULT;
    texture_desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    ASSERT_EQ(context_.device()->CreateTexture3D(&texture_desc, nullptr,
                                                 texture_.put()),
              S_OK);
    ASSERT_NE(texture_.get(), nullptr);
    ASSERT_EQ(texture_->QueryInterface(
                  __uuidof(ID3D11Resource),
                  reinterpret_cast<void **>(resource_identity_.put())),
              S_OK);
  }

  D3D11TestContext context_;
  ComPtr<ID3D11Texture3D> texture_;
  ComPtr<ID3D11Resource> resource_identity_;
};

TEST_F(D3D11Texture3DUnorderedAccessViewDescMatrixSpec,
       RoundTrips4096MipDepthRanges) {
  std::vector<std::uint32_t> selected_cases;
  selected_cases.reserve(kTexture3dUavDescCaseCount);
  for (std::uint32_t logical = 0; logical < kTexture3dUavDescCaseCount;
       ++logical) {
    if (dxmt::test::LogicalCaseSelected(kTexture3dUavDescCases.family(),
                                        logical))
      selected_cases.push_back(logical);
  }
  ASSERT_FALSE(selected_cases.empty());

  RecordProperty("logical_cases_executed", selected_cases.size());
  RecordProperty("logical_case_prefix",
                 kTexture3dUavDescCases.family().case_id_prefix);
  for (const std::uint32_t logical : selected_cases) {
    const Texture3dUavDescCase test_case = CaseForLogical(logical);
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

    const bool desc_matches = Texture3dUavDescsEqual(actual, test_case.desc);
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
        dxmt::test::LogicalCaseId(kTexture3dUavDescCases.family(), logical);
    ADD_FAILURE()
        << "LogicalCaseId: " << case_id << '\n'
        << "Class: "
        << dxmt::test::TestClassName(
               kTexture3dUavDescCases.family().traits.test_class)
        << '\n'
        << "Requirements: feature_level=11_0 shader_model=None queue=Device "
           "capability=CreateTexture3D,CreateUnorderedAccessView,"
           "UnorderedAccessViewGetDesc,ViewGetResource,DeviceChildGetDevice\n"
        << "ExecutionPath: "
        << dxmt::test::ExecutionPathName(
               kTexture3dUavDescCases.family().traits.execution_path)
        << '\n'
        << "Parameters: logical=" << logical
        << " format_index=" << test_case.format_index
        << " mip_index=" << test_case.mip_index
        << " depth_count_index=" << test_case.depth_count_index
        << " selected_cases=" << selected_cases.size() << '\n'
        << "Expected: format=" << static_cast<UINT>(test_case.desc.Format)
        << " dimension=" << static_cast<UINT>(test_case.desc.ViewDimension)
        << " mip=" << test_case.desc.Texture3D.MipSlice
        << " depth_start=" << test_case.desc.Texture3D.FirstWSlice
        << " depth_count=" << test_case.desc.Texture3D.WSize
        << " resource=" << resource_identity_.get()
        << " owner=" << context_.device() << '\n'
        << "Observed: create_hresult=" << create_result
        << " failure_phase=" << failure_phase
        << " format=" << static_cast<UINT>(actual.Format)
        << " dimension=" << static_cast<UINT>(actual.ViewDimension)
        << " mip=" << actual.Texture3D.MipSlice
        << " depth_start=" << actual.Texture3D.FirstWSlice
        << " depth_count=" << actual.Texture3D.WSize
        << " resource=" << resource.get() << " owner=" << owner.get()
        << " view=" << view.get() << '\n'
        << "Replay: --dxmt-case-id=" << case_id;
    break;
  }

  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11Texture3DUnorderedAccessViewDescMatrixSpec,
       RejectsNullResourceAndInvalidMipDepthRanges) {
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

  D3D11_UNORDERED_ACCESS_VIEW_DESC invalid_mip = valid;
  invalid_mip.Texture3D.MipSlice = 2;
  EXPECT_EQ(context_.device()->CreateUnorderedAccessView(texture_.get(),
                                                         &invalid_mip, nullptr),
            E_INVALIDARG);

  D3D11_UNORDERED_ACCESS_VIEW_DESC past_end = valid;
  past_end.Texture3D.MipSlice = 1;
  past_end.Texture3D.WSize = 1025;
  EXPECT_EQ(context_.device()->CreateUnorderedAccessView(texture_.get(),
                                                         &past_end, nullptr),
            E_INVALIDARG);

  D3D11_UNORDERED_ACCESS_VIEW_DESC empty = valid;
  empty.Texture3D.WSize = 0;
  view = reinterpret_cast<ID3D11UnorderedAccessView *>(
      static_cast<std::uintptr_t>(1));
  EXPECT_EQ(context_.device()->CreateUnorderedAccessView(texture_.get(), &empty,
                                                         &view),
            E_INVALIDARG);
  EXPECT_EQ(view, nullptr);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
