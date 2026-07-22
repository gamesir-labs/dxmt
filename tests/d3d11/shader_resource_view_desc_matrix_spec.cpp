#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <array>
#include <cstdint>
#include <vector>

// Public D3D11 shader-resource-view descriptor coverage. Compatible formats,
// mip ranges, and array ranges form exactly 4096 distinct valid views.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

constexpr std::uint32_t kSrvDescCaseCount = 4096;

const dxmt::test::LogicalCaseFamilyRegistration kSrvDescCases(
    "D3D11ShaderResourceViewDescMatrixSpec."
    "RoundTrips4096Texture2DArrayRanges",
    "D3D11.SRV.Texture2DArray.Description.", kSrvDescCaseCount, 4,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "Device",
      "CreateShaderResourceView,ID3D11ShaderResourceViewGetDesc,"
      "ID3D11ViewGetResource,ID3D11DeviceChildGetDevice"},
     dxmt::test::kResourceTestCost,
     "one test-local D3D11 device, one typeless Texture2D array, and one live "
     "shader-resource view per selected logical case",
     "create every combination of four compatible formats, four most-"
     "detailed mip levels, four mip counts, eight first array slices, and "
     "eight array sizes, then query the public descriptor and relationships",
     "every view preserves its complete descriptor and returns the source "
     "resource and creating device through their public COM interfaces",
     "logical ID, selected-case count, combination indexes, expected and "
     "returned descriptors, resource and owner addresses, HRESULT, failure "
     "phase, and exact replay argument"});

const dxmt::test::TestCostRegistration
    kSrvDescCost("D3D11ShaderResourceViewDescMatrixSpec."
                 "RoundTrips4096Texture2DArrayRanges",
                 dxmt::test::kResourceTestCost);

constexpr std::array<DXGI_FORMAT, 4> kViewFormats = {
    DXGI_FORMAT_R8G8B8A8_UNORM,
    DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
    DXGI_FORMAT_R8G8B8A8_UINT,
    DXGI_FORMAT_R8G8B8A8_SNORM,
};

struct SrvDescCase {
  UINT format_index;
  UINT mip_start_index;
  UINT mip_count_index;
  UINT array_start_index;
  UINT array_count_index;
  D3D11_SHADER_RESOURCE_VIEW_DESC desc;
};

SrvDescCase CaseForLogical(std::uint32_t logical) {
  SrvDescCase test_case = {};
  std::uint32_t encoded = logical;
  test_case.format_index = encoded & 3u;
  encoded >>= 2u;
  test_case.mip_start_index = encoded & 3u;
  encoded >>= 2u;
  test_case.mip_count_index = encoded & 3u;
  encoded >>= 2u;
  test_case.array_start_index = encoded & 7u;
  encoded >>= 3u;
  test_case.array_count_index = encoded & 7u;

  test_case.desc.Format = kViewFormats[test_case.format_index];
  test_case.desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
  test_case.desc.Texture2DArray.MostDetailedMip = test_case.mip_start_index;
  test_case.desc.Texture2DArray.MipLevels = test_case.mip_count_index + 1u;
  test_case.desc.Texture2DArray.FirstArraySlice = test_case.array_start_index;
  test_case.desc.Texture2DArray.ArraySize = test_case.array_count_index + 1u;
  return test_case;
}

bool SrvDescsEqual(const D3D11_SHADER_RESOURCE_VIEW_DESC &actual,
                   const D3D11_SHADER_RESOURCE_VIEW_DESC &expected) {
  return actual.Format == expected.Format &&
         actual.ViewDimension == expected.ViewDimension &&
         actual.Texture2DArray.MostDetailedMip ==
             expected.Texture2DArray.MostDetailedMip &&
         actual.Texture2DArray.MipLevels == expected.Texture2DArray.MipLevels &&
         actual.Texture2DArray.FirstArraySlice ==
             expected.Texture2DArray.FirstArraySlice &&
         actual.Texture2DArray.ArraySize == expected.Texture2DArray.ArraySize;
}

class D3D11ShaderResourceViewDescMatrixSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_NE(context_.context(), nullptr);

    D3D11_TEXTURE2D_DESC texture_desc = {};
    texture_desc.Width = 128;
    texture_desc.Height = 128;
    texture_desc.MipLevels = 8;
    texture_desc.ArraySize = 16;
    texture_desc.Format = DXGI_FORMAT_R8G8B8A8_TYPELESS;
    texture_desc.SampleDesc.Count = 1;
    texture_desc.Usage = D3D11_USAGE_DEFAULT;
    texture_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
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

TEST_F(D3D11ShaderResourceViewDescMatrixSpec,
       RoundTrips4096Texture2DArrayRanges) {
  std::vector<std::uint32_t> selected_cases;
  selected_cases.reserve(kSrvDescCaseCount);
  for (std::uint32_t logical = 0; logical < kSrvDescCaseCount; ++logical) {
    if (dxmt::test::LogicalCaseSelected(kSrvDescCases.family(), logical))
      selected_cases.push_back(logical);
  }
  ASSERT_FALSE(selected_cases.empty());

  RecordProperty("logical_cases_executed", selected_cases.size());
  RecordProperty("logical_case_prefix", kSrvDescCases.family().case_id_prefix);
  for (const std::uint32_t logical : selected_cases) {
    const SrvDescCase test_case = CaseForLogical(logical);
    ComPtr<ID3D11ShaderResourceView> view;
    const HRESULT create_result = context_.device()->CreateShaderResourceView(
        texture_.get(), &test_case.desc, view.put());
    D3D11_SHADER_RESOURCE_VIEW_DESC actual = {};
    ComPtr<ID3D11Resource> resource;
    ComPtr<ID3D11Device> owner;
    if (create_result == S_OK && view) {
      view->GetDesc(&actual);
      view->GetResource(resource.put());
      view->GetDevice(owner.put());
    }

    const bool desc_matches = SrvDescsEqual(actual, test_case.desc);
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
        dxmt::test::LogicalCaseId(kSrvDescCases.family(), logical);
    ADD_FAILURE()
        << "LogicalCaseId: " << case_id << '\n'
        << "Class: "
        << dxmt::test::TestClassName(kSrvDescCases.family().traits.test_class)
        << '\n'
        << "Requirements: feature_level=11_0 shader_model=None queue=Device "
           "capability=CreateShaderResourceView,ShaderResourceViewGetDesc,"
           "ViewGetResource,DeviceChildGetDevice\n"
        << "ExecutionPath: "
        << dxmt::test::ExecutionPathName(
               kSrvDescCases.family().traits.execution_path)
        << '\n'
        << "Parameters: logical=" << logical
        << " format_index=" << test_case.format_index
        << " mip_start_index=" << test_case.mip_start_index
        << " mip_count_index=" << test_case.mip_count_index
        << " array_start_index=" << test_case.array_start_index
        << " array_count_index=" << test_case.array_count_index
        << " selected_cases=" << selected_cases.size() << '\n'
        << "Expected: format=" << static_cast<UINT>(test_case.desc.Format)
        << " dimension=" << static_cast<UINT>(test_case.desc.ViewDimension)
        << " mip_start=" << test_case.desc.Texture2DArray.MostDetailedMip
        << " mip_count=" << test_case.desc.Texture2DArray.MipLevels
        << " array_start=" << test_case.desc.Texture2DArray.FirstArraySlice
        << " array_count=" << test_case.desc.Texture2DArray.ArraySize
        << " resource=" << resource_identity_.get()
        << " owner=" << context_.device() << '\n'
        << "Observed: create_hresult=" << create_result
        << " failure_phase=" << failure_phase
        << " format=" << static_cast<UINT>(actual.Format)
        << " dimension=" << static_cast<UINT>(actual.ViewDimension)
        << " mip_start=" << actual.Texture2DArray.MostDetailedMip
        << " mip_count=" << actual.Texture2DArray.MipLevels
        << " array_start=" << actual.Texture2DArray.FirstArraySlice
        << " array_count=" << actual.Texture2DArray.ArraySize
        << " resource=" << resource.get() << " owner=" << owner.get()
        << " view=" << view.get() << '\n'
        << "Replay: --dxmt-case-id=" << case_id;
    break;
  }

  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11ShaderResourceViewDescMatrixSpec,
       ValidatesNullResourceOutputAndSubresourceRange) {
  ID3D11ShaderResourceView *view = reinterpret_cast<ID3D11ShaderResourceView *>(
      static_cast<std::uintptr_t>(1));
  EXPECT_EQ(
      context_.device()->CreateShaderResourceView(nullptr, nullptr, &view),
      E_INVALIDARG);
  EXPECT_EQ(view, nullptr);

  const D3D11_SHADER_RESOURCE_VIEW_DESC valid = CaseForLogical(0).desc;
  EXPECT_EQ(context_.device()->CreateShaderResourceView(texture_.get(), &valid,
                                                        nullptr),
            S_FALSE);

  D3D11_SHADER_RESOURCE_VIEW_DESC invalid = valid;
  invalid.Texture2DArray.MostDetailedMip = 8;
  EXPECT_EQ(context_.device()->CreateShaderResourceView(texture_.get(),
                                                        &invalid, nullptr),
            E_INVALIDARG);

  view = reinterpret_cast<ID3D11ShaderResourceView *>(
      static_cast<std::uintptr_t>(1));
  EXPECT_EQ(context_.device()->CreateShaderResourceView(texture_.get(),
                                                        &invalid, &view),
            E_INVALIDARG);
  EXPECT_EQ(view, nullptr);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
