#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <array>
#include <cstdint>
#include <vector>

// Public D3D11 TextureCubeArray SRV descriptor coverage. Compatible formats,
// mip ranges, and cube ranges form exactly 4096 distinct valid views.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

constexpr std::uint32_t kCubeArraySrvDescCaseCount = 4096;

const dxmt::test::LogicalCaseFamilyRegistration kCubeArraySrvDescCases(
    "D3D11TextureCubeArrayShaderResourceViewDescMatrixSpec."
    "RoundTrips4096MipCubeRanges",
    "D3D11.SRV.TextureCubeArray.Description.", kCubeArraySrvDescCaseCount, 4,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "Device",
      "CreateTexture2D,CreateShaderResourceView,"
      "ID3D11ShaderResourceViewGetDesc,ID3D11ViewGetResource,"
      "ID3D11DeviceChildGetDevice"},
     dxmt::test::kResourceTestCost,
     "one test-local D3D11 device, one typeless TextureCubeArray, and one "
     "live shader-resource view per selected logical case",
     "create every combination of four compatible formats, four most-"
     "detailed mip levels, four mip counts, eight first cubes, and eight cube "
     "counts, then query the complete public descriptor and relationships",
     "every view preserves its descriptor and returns the source texture and "
     "creating device through their public COM interfaces",
     "logical ID, selected-case count, combination indexes, expected and "
     "returned descriptors, resource and owner addresses, HRESULT, failure "
     "phase, and exact replay argument"});

const dxmt::test::TestCostRegistration kCubeArraySrvDescCost(
    "D3D11TextureCubeArrayShaderResourceViewDescMatrixSpec."
    "RoundTrips4096MipCubeRanges",
    dxmt::test::kResourceTestCost);

constexpr std::array<DXGI_FORMAT, 4> kViewFormats = {
    DXGI_FORMAT_R8G8B8A8_UNORM,
    DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
    DXGI_FORMAT_R8G8B8A8_UINT,
    DXGI_FORMAT_R8G8B8A8_SNORM,
};

struct CubeArraySrvDescCase {
  UINT format_index;
  UINT mip_start_index;
  UINT mip_count_index;
  UINT first_cube_index;
  UINT cube_count_index;
  D3D11_SHADER_RESOURCE_VIEW_DESC desc;
};

CubeArraySrvDescCase CaseForLogical(std::uint32_t logical) {
  CubeArraySrvDescCase test_case = {};
  std::uint32_t encoded = logical;
  test_case.format_index = encoded & 3u;
  encoded >>= 2u;
  test_case.mip_start_index = encoded & 3u;
  encoded >>= 2u;
  test_case.mip_count_index = encoded & 3u;
  encoded >>= 2u;
  test_case.first_cube_index = encoded & 7u;
  encoded >>= 3u;
  test_case.cube_count_index = encoded & 7u;

  test_case.desc.Format = kViewFormats[test_case.format_index];
  test_case.desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBEARRAY;
  test_case.desc.TextureCubeArray.MostDetailedMip = test_case.mip_start_index;
  test_case.desc.TextureCubeArray.MipLevels = test_case.mip_count_index + 1u;
  test_case.desc.TextureCubeArray.First2DArrayFace =
      test_case.first_cube_index * 6u;
  test_case.desc.TextureCubeArray.NumCubes = test_case.cube_count_index + 1u;
  return test_case;
}

bool CubeArraySrvDescsEqual(const D3D11_SHADER_RESOURCE_VIEW_DESC &actual,
                            const D3D11_SHADER_RESOURCE_VIEW_DESC &expected) {
  return actual.Format == expected.Format &&
         actual.ViewDimension == expected.ViewDimension &&
         actual.TextureCubeArray.MostDetailedMip ==
             expected.TextureCubeArray.MostDetailedMip &&
         actual.TextureCubeArray.MipLevels ==
             expected.TextureCubeArray.MipLevels &&
         actual.TextureCubeArray.First2DArrayFace ==
             expected.TextureCubeArray.First2DArrayFace &&
         actual.TextureCubeArray.NumCubes == expected.TextureCubeArray.NumCubes;
}

class D3D11TextureCubeArrayShaderResourceViewDescMatrixSpec
    : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_NE(context_.context(), nullptr);

    D3D11_TEXTURE2D_DESC texture_desc = {};
    texture_desc.Width = 128;
    texture_desc.Height = 128;
    texture_desc.MipLevels = 8;
    texture_desc.ArraySize = 90;
    texture_desc.Format = DXGI_FORMAT_R8G8B8A8_TYPELESS;
    texture_desc.SampleDesc.Count = 1;
    texture_desc.Usage = D3D11_USAGE_DEFAULT;
    texture_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    texture_desc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;
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

TEST_F(D3D11TextureCubeArrayShaderResourceViewDescMatrixSpec,
       RoundTrips4096MipCubeRanges) {
  std::vector<std::uint32_t> selected_cases;
  selected_cases.reserve(kCubeArraySrvDescCaseCount);
  for (std::uint32_t logical = 0; logical < kCubeArraySrvDescCaseCount;
       ++logical) {
    if (dxmt::test::LogicalCaseSelected(kCubeArraySrvDescCases.family(),
                                        logical))
      selected_cases.push_back(logical);
  }
  ASSERT_FALSE(selected_cases.empty());

  RecordProperty("logical_cases_executed", selected_cases.size());
  RecordProperty("logical_case_prefix",
                 kCubeArraySrvDescCases.family().case_id_prefix);
  for (const std::uint32_t logical : selected_cases) {
    const CubeArraySrvDescCase test_case = CaseForLogical(logical);
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

    const bool desc_matches = CubeArraySrvDescsEqual(actual, test_case.desc);
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
        dxmt::test::LogicalCaseId(kCubeArraySrvDescCases.family(), logical);
    ADD_FAILURE()
        << "LogicalCaseId: " << case_id << '\n'
        << "Class: "
        << dxmt::test::TestClassName(
               kCubeArraySrvDescCases.family().traits.test_class)
        << '\n'
        << "Requirements: feature_level=11_0 shader_model=None queue=Device "
           "capability=CreateTexture2D,CreateShaderResourceView,"
           "ShaderResourceViewGetDesc,ViewGetResource,DeviceChildGetDevice\n"
        << "ExecutionPath: "
        << dxmt::test::ExecutionPathName(
               kCubeArraySrvDescCases.family().traits.execution_path)
        << '\n'
        << "Parameters: logical=" << logical
        << " format_index=" << test_case.format_index
        << " mip_start_index=" << test_case.mip_start_index
        << " mip_count_index=" << test_case.mip_count_index
        << " first_cube_index=" << test_case.first_cube_index
        << " cube_count_index=" << test_case.cube_count_index
        << " selected_cases=" << selected_cases.size() << '\n'
        << "Expected: format=" << static_cast<UINT>(test_case.desc.Format)
        << " dimension=" << static_cast<UINT>(test_case.desc.ViewDimension)
        << " mip_start=" << test_case.desc.TextureCubeArray.MostDetailedMip
        << " mip_count=" << test_case.desc.TextureCubeArray.MipLevels
        << " first_face=" << test_case.desc.TextureCubeArray.First2DArrayFace
        << " cube_count=" << test_case.desc.TextureCubeArray.NumCubes
        << " resource=" << resource_identity_.get()
        << " owner=" << context_.device() << '\n'
        << "Observed: create_hresult=" << create_result
        << " failure_phase=" << failure_phase
        << " format=" << static_cast<UINT>(actual.Format)
        << " dimension=" << static_cast<UINT>(actual.ViewDimension)
        << " mip_start=" << actual.TextureCubeArray.MostDetailedMip
        << " mip_count=" << actual.TextureCubeArray.MipLevels
        << " first_face=" << actual.TextureCubeArray.First2DArrayFace
        << " cube_count=" << actual.TextureCubeArray.NumCubes
        << " resource=" << resource.get() << " owner=" << owner.get()
        << " view=" << view.get() << '\n'
        << "Replay: --dxmt-case-id=" << case_id;
    break;
  }

  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11TextureCubeArrayShaderResourceViewDescMatrixSpec,
       RejectsNullResourceAndInvalidCubeRanges) {
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

  D3D11_SHADER_RESOURCE_VIEW_DESC past_end = valid;
  past_end.TextureCubeArray.First2DArrayFace = 48;
  past_end.TextureCubeArray.NumCubes = 8;
  EXPECT_EQ(context_.device()->CreateShaderResourceView(texture_.get(),
                                                        &past_end, nullptr),
            E_INVALIDARG);

  D3D11_SHADER_RESOURCE_VIEW_DESC empty = valid;
  empty.TextureCubeArray.NumCubes = 0;
  view = reinterpret_cast<ID3D11ShaderResourceView *>(
      static_cast<std::uintptr_t>(1));
  EXPECT_EQ(context_.device()->CreateShaderResourceView(texture_.get(), &empty,
                                                        &view),
            E_INVALIDARG);
  EXPECT_EQ(view, nullptr);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
