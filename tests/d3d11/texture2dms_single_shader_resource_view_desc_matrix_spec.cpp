#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <cstdint>
#include <vector>

// Public D3D11 non-array Texture2DMS SRV descriptor coverage. Explicit and
// default descriptors exercise both public creation paths.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

constexpr std::uint32_t kTexture2dmsSrvDescCaseCount = 2;

const dxmt::test::LogicalCaseFamilyRegistration kTexture2dmsSrvDescCases(
    "D3D11Texture2DMSSingleShaderResourceViewDescMatrixSpec."
    "RoundTripsExplicitAndDefaultDescriptions",
    "D3D11.SRV.Texture2DMS.Description.", kTexture2dmsSrvDescCaseCount, 1,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "Device",
      "CheckMultisampleQualityLevels,CreateTexture2D,"
      "CreateShaderResourceView,ID3D11ShaderResourceViewGetDesc,"
      "ID3D11ViewGetResource,ID3D11DeviceChildGetDevice"},
     dxmt::test::kResourceTestCost,
     "one test-local D3D11 device, one 4x multisampled non-array Texture2D, "
     "and one live shader-resource view per selected logical case",
     "create the non-array multisampled view once with an explicit public "
     "descriptor and once through default descriptor inference",
     "both paths report the typed Texture2DMS descriptor and return the "
     "source texture and creating device through public COM interfaces",
     "logical ID, selected-case count, descriptor mode, expected and returned "
     "descriptors, resource and owner addresses, HRESULT, failure phase, and "
     "exact replay argument"});

const dxmt::test::TestCostRegistration kTexture2dmsSrvDescCost(
    "D3D11Texture2DMSSingleShaderResourceViewDescMatrixSpec."
    "RoundTripsExplicitAndDefaultDescriptions",
    dxmt::test::kResourceTestCost);

D3D11_SHADER_RESOURCE_VIEW_DESC ExpectedDesc() {
  D3D11_SHADER_RESOURCE_VIEW_DESC desc = {};
  desc.Format = DXGI_FORMAT_R32_FLOAT;
  desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DMS;
  return desc;
}

bool Texture2dmsSrvDescsEqual(const D3D11_SHADER_RESOURCE_VIEW_DESC &actual,
                              const D3D11_SHADER_RESOURCE_VIEW_DESC &expected) {
  return actual.Format == expected.Format &&
         actual.ViewDimension == expected.ViewDimension;
}

class D3D11Texture2DMSSingleShaderResourceViewDescMatrixSpec
    : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_NE(context_.context(), nullptr);

    UINT quality_levels = 0;
    ASSERT_EQ(context_.device()->CheckMultisampleQualityLevels(
                  DXGI_FORMAT_R32_FLOAT, 4, &quality_levels),
              S_OK);
    ASSERT_GT(quality_levels, 0u);

    D3D11_TEXTURE2D_DESC texture_desc = {};
    texture_desc.Width = 4;
    texture_desc.Height = 4;
    texture_desc.MipLevels = 1;
    texture_desc.ArraySize = 1;
    texture_desc.Format = DXGI_FORMAT_R32_FLOAT;
    texture_desc.SampleDesc.Count = 4;
    texture_desc.SampleDesc.Quality = 0;
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

TEST_F(D3D11Texture2DMSSingleShaderResourceViewDescMatrixSpec,
       RoundTripsExplicitAndDefaultDescriptions) {
  std::vector<std::uint32_t> selected_cases;
  selected_cases.reserve(kTexture2dmsSrvDescCaseCount);
  for (std::uint32_t logical = 0; logical < kTexture2dmsSrvDescCaseCount;
       ++logical) {
    if (dxmt::test::LogicalCaseSelected(kTexture2dmsSrvDescCases.family(),
                                        logical))
      selected_cases.push_back(logical);
  }
  ASSERT_FALSE(selected_cases.empty());

  const D3D11_SHADER_RESOURCE_VIEW_DESC expected = ExpectedDesc();
  RecordProperty("logical_cases_executed", selected_cases.size());
  RecordProperty("logical_case_prefix",
                 kTexture2dmsSrvDescCases.family().case_id_prefix);
  for (const std::uint32_t logical : selected_cases) {
    const bool use_default_desc = logical != 0;
    ComPtr<ID3D11ShaderResourceView> view;
    const HRESULT create_result = context_.device()->CreateShaderResourceView(
        texture_.get(), use_default_desc ? nullptr : &expected, view.put());
    D3D11_SHADER_RESOURCE_VIEW_DESC actual = {};
    ComPtr<ID3D11Resource> resource;
    ComPtr<ID3D11Device> owner;
    if (create_result == S_OK && view) {
      view->GetDesc(&actual);
      view->GetResource(resource.put());
      view->GetDevice(owner.put());
    }

    const bool desc_matches = Texture2dmsSrvDescsEqual(actual, expected);
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
        dxmt::test::LogicalCaseId(kTexture2dmsSrvDescCases.family(), logical);
    ADD_FAILURE()
        << "LogicalCaseId: " << case_id << '\n'
        << "Class: "
        << dxmt::test::TestClassName(
               kTexture2dmsSrvDescCases.family().traits.test_class)
        << '\n'
        << "Requirements: feature_level=11_0 shader_model=None queue=Device "
           "capability=CheckMultisampleQualityLevels,CreateTexture2D,"
           "CreateShaderResourceView,ShaderResourceViewGetDesc,"
           "ViewGetResource,DeviceChildGetDevice\n"
        << "ExecutionPath: "
        << dxmt::test::ExecutionPathName(
               kTexture2dmsSrvDescCases.family().traits.execution_path)
        << '\n'
        << "Parameters: logical=" << logical
        << " descriptor_mode=" << (use_default_desc ? "default" : "explicit")
        << " selected_cases=" << selected_cases.size() << '\n'
        << "Expected: format=" << static_cast<UINT>(expected.Format)
        << " dimension=" << static_cast<UINT>(expected.ViewDimension)
        << " resource=" << resource_identity_.get()
        << " owner=" << context_.device() << '\n'
        << "Observed: create_hresult=" << create_result
        << " failure_phase=" << failure_phase
        << " format=" << static_cast<UINT>(actual.Format)
        << " dimension=" << static_cast<UINT>(actual.ViewDimension)
        << " resource=" << resource.get() << " owner=" << owner.get()
        << " view=" << view.get() << '\n'
        << "Replay: --dxmt-case-id=" << case_id;
    break;
  }

  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11Texture2DMSSingleShaderResourceViewDescMatrixSpec,
       RejectsNullResourceAndNonMultisampledDimension) {
  ID3D11ShaderResourceView *view = reinterpret_cast<ID3D11ShaderResourceView *>(
      static_cast<std::uintptr_t>(1));
  EXPECT_EQ(
      context_.device()->CreateShaderResourceView(nullptr, nullptr, &view),
      E_INVALIDARG);
  EXPECT_EQ(view, nullptr);

  const D3D11_SHADER_RESOURCE_VIEW_DESC valid = ExpectedDesc();
  EXPECT_EQ(context_.device()->CreateShaderResourceView(texture_.get(), &valid,
                                                        nullptr),
            S_FALSE);

  D3D11_SHADER_RESOURCE_VIEW_DESC invalid = {};
  invalid.Format = DXGI_FORMAT_R32_FLOAT;
  invalid.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
  invalid.Texture2D.MostDetailedMip = 0;
  invalid.Texture2D.MipLevels = 1;
  view = reinterpret_cast<ID3D11ShaderResourceView *>(
      static_cast<std::uintptr_t>(1));
  EXPECT_EQ(context_.device()->CreateShaderResourceView(texture_.get(),
                                                        &invalid, &view),
            E_INVALIDARG);
  EXPECT_EQ(view, nullptr);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
