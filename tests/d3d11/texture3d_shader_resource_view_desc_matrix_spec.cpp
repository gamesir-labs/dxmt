#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <array>
#include <cstdint>
#include <vector>

// Public D3D11 Texture3D SRV descriptor coverage. Four compatible formats and
// every nonempty contiguous range of 11 mips form 264 distinct valid views.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

constexpr UINT kMipLevelCount = 11;
constexpr std::uint32_t kTexture3dSrvDescCaseCount = 264;

const dxmt::test::LogicalCaseFamilyRegistration kTexture3dSrvDescCases(
    "D3D11Texture3DShaderResourceViewDescMatrixSpec."
    "RoundTrips264CompleteMipRanges",
    "D3D11.SRV.Texture3D.Description.", kTexture3dSrvDescCaseCount, 3,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "Device",
      "CreateTexture3D,CreateShaderResourceView,"
      "ID3D11ShaderResourceViewGetDesc,ID3D11ViewGetResource,"
      "ID3D11DeviceChildGetDevice"},
     dxmt::test::kResourceTestCost,
     "one test-local D3D11 device, one typeless Texture3D, and one live "
     "shader-resource view per selected logical case",
     "create every combination of four compatible formats and all 66 "
     "nonempty contiguous ranges of 11 mip levels, then query the complete "
     "public descriptor and relationships",
     "every view preserves its format and mip range and returns the source "
     "texture and creating device through public COM interfaces",
     "logical ID, selected-case count, format and mip-range indexes, expected "
     "and returned descriptors, resource and owner addresses, HRESULT, "
     "failure phase, and exact replay argument"});

const dxmt::test::TestCostRegistration
    kTexture3dSrvDescCost("D3D11Texture3DShaderResourceViewDescMatrixSpec."
                          "RoundTrips264CompleteMipRanges",
                          dxmt::test::kResourceTestCost);

constexpr std::array<DXGI_FORMAT, 4> kViewFormats = {
    DXGI_FORMAT_R8G8B8A8_UNORM,
    DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
    DXGI_FORMAT_R8G8B8A8_UINT,
    DXGI_FORMAT_R8G8B8A8_SNORM,
};

struct Texture3dSrvDescCase {
  UINT format_index;
  UINT mip_range_index;
  D3D11_SHADER_RESOURCE_VIEW_DESC desc;
};

Texture3dSrvDescCase CaseForLogical(std::uint32_t logical) {
  Texture3dSrvDescCase test_case = {};
  test_case.format_index = logical & 3u;
  test_case.mip_range_index = logical >> 2u;

  UINT range = test_case.mip_range_index;
  for (UINT first = 0; first < kMipLevelCount; ++first) {
    const UINT count_at_first = kMipLevelCount - first;
    if (range < count_at_first) {
      test_case.desc.Format = kViewFormats[test_case.format_index];
      test_case.desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
      test_case.desc.Texture3D.MostDetailedMip = first;
      test_case.desc.Texture3D.MipLevels = range + 1u;
      return test_case;
    }
    range -= count_at_first;
  }
  return test_case;
}

bool Texture3dSrvDescsEqual(const D3D11_SHADER_RESOURCE_VIEW_DESC &actual,
                            const D3D11_SHADER_RESOURCE_VIEW_DESC &expected) {
  return actual.Format == expected.Format &&
         actual.ViewDimension == expected.ViewDimension &&
         actual.Texture3D.MostDetailedMip ==
             expected.Texture3D.MostDetailedMip &&
         actual.Texture3D.MipLevels == expected.Texture3D.MipLevels;
}

class D3D11Texture3DShaderResourceViewDescMatrixSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_NE(context_.context(), nullptr);

    D3D11_TEXTURE3D_DESC texture_desc = {};
    texture_desc.Width = 1024;
    texture_desc.Height = 1;
    texture_desc.Depth = 1;
    texture_desc.MipLevels = kMipLevelCount;
    texture_desc.Format = DXGI_FORMAT_R8G8B8A8_TYPELESS;
    texture_desc.Usage = D3D11_USAGE_DEFAULT;
    texture_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
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

TEST_F(D3D11Texture3DShaderResourceViewDescMatrixSpec,
       RoundTrips264CompleteMipRanges) {
  std::vector<std::uint32_t> selected_cases;
  selected_cases.reserve(kTexture3dSrvDescCaseCount);
  for (std::uint32_t logical = 0; logical < kTexture3dSrvDescCaseCount;
       ++logical) {
    if (dxmt::test::LogicalCaseSelected(kTexture3dSrvDescCases.family(),
                                        logical))
      selected_cases.push_back(logical);
  }
  ASSERT_FALSE(selected_cases.empty());

  RecordProperty("logical_cases_executed", selected_cases.size());
  RecordProperty("logical_case_prefix",
                 kTexture3dSrvDescCases.family().case_id_prefix);
  for (const std::uint32_t logical : selected_cases) {
    const Texture3dSrvDescCase test_case = CaseForLogical(logical);
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

    const bool desc_matches = Texture3dSrvDescsEqual(actual, test_case.desc);
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
        dxmt::test::LogicalCaseId(kTexture3dSrvDescCases.family(), logical);
    ADD_FAILURE()
        << "LogicalCaseId: " << case_id << '\n'
        << "Class: "
        << dxmt::test::TestClassName(
               kTexture3dSrvDescCases.family().traits.test_class)
        << '\n'
        << "Requirements: feature_level=11_0 shader_model=None queue=Device "
           "capability=CreateTexture3D,CreateShaderResourceView,"
           "ShaderResourceViewGetDesc,ViewGetResource,DeviceChildGetDevice\n"
        << "ExecutionPath: "
        << dxmt::test::ExecutionPathName(
               kTexture3dSrvDescCases.family().traits.execution_path)
        << '\n'
        << "Parameters: logical=" << logical
        << " format_index=" << test_case.format_index
        << " mip_range_index=" << test_case.mip_range_index
        << " selected_cases=" << selected_cases.size() << '\n'
        << "Expected: format=" << static_cast<UINT>(test_case.desc.Format)
        << " dimension=" << static_cast<UINT>(test_case.desc.ViewDimension)
        << " mip_start=" << test_case.desc.Texture3D.MostDetailedMip
        << " mip_count=" << test_case.desc.Texture3D.MipLevels
        << " resource=" << resource_identity_.get()
        << " owner=" << context_.device() << '\n'
        << "Observed: create_hresult=" << create_result
        << " failure_phase=" << failure_phase
        << " format=" << static_cast<UINT>(actual.Format)
        << " dimension=" << static_cast<UINT>(actual.ViewDimension)
        << " mip_start=" << actual.Texture3D.MostDetailedMip
        << " mip_count=" << actual.Texture3D.MipLevels
        << " resource=" << resource.get() << " owner=" << owner.get()
        << " view=" << view.get() << '\n'
        << "Replay: --dxmt-case-id=" << case_id;
    break;
  }

  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11Texture3DShaderResourceViewDescMatrixSpec,
       RejectsNullResourceAndInvalidMipRanges) {
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

  D3D11_SHADER_RESOURCE_VIEW_DESC invalid_mip = valid;
  invalid_mip.Texture3D.MostDetailedMip = kMipLevelCount;
  EXPECT_EQ(context_.device()->CreateShaderResourceView(texture_.get(),
                                                        &invalid_mip, nullptr),
            E_INVALIDARG);

  D3D11_SHADER_RESOURCE_VIEW_DESC past_end = valid;
  past_end.Texture3D.MostDetailedMip = kMipLevelCount - 1u;
  past_end.Texture3D.MipLevels = 2;
  EXPECT_EQ(context_.device()->CreateShaderResourceView(texture_.get(),
                                                        &past_end, nullptr),
            E_INVALIDARG);

  D3D11_SHADER_RESOURCE_VIEW_DESC empty = valid;
  empty.Texture3D.MipLevels = 0;
  view = reinterpret_cast<ID3D11ShaderResourceView *>(
      static_cast<std::uintptr_t>(1));
  EXPECT_EQ(context_.device()->CreateShaderResourceView(texture_.get(), &empty,
                                                        &view),
            E_INVALIDARG);
  EXPECT_EQ(view, nullptr);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
