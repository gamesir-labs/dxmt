#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <cstdint>
#include <vector>

// Public D3D11 Texture2DMSArray RTV descriptor coverage. First array slices
// and array sizes form exactly 4096 distinct valid multisampled views.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

constexpr std::uint32_t kTexture2dmsRtvDescCaseCount = 4096;

const dxmt::test::LogicalCaseFamilyRegistration kTexture2dmsRtvDescCases(
    "D3D11Texture2DMSRenderTargetViewDescMatrixSpec."
    "RoundTrips4096ArrayRanges",
    "D3D11.RTV.Texture2DMSArray.Description.", kTexture2dmsRtvDescCaseCount, 4,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "Device",
      "CheckMultisampleQualityLevels,CreateTexture2D,CreateRenderTargetView,"
      "ID3D11RenderTargetViewGetDesc,ID3D11ViewGetResource,"
      "ID3D11DeviceChildGetDevice"},
     dxmt::test::kResourceTestCost,
     "one test-local D3D11 device, one 4x multisampled Texture2D array, and "
     "one live render-target view per selected logical case",
     "create every combination of 64 first array slices and 64 array sizes, "
     "then query the complete public descriptor and relationships",
     "every view preserves its multisampled array range and returns the "
     "source texture and creating device through public COM interfaces",
     "logical ID, selected-case count, combination indexes, expected and "
     "returned descriptors, resource and owner addresses, HRESULT, failure "
     "phase, and exact replay argument"});

const dxmt::test::TestCostRegistration
    kTexture2dmsRtvDescCost("D3D11Texture2DMSRenderTargetViewDescMatrixSpec."
                            "RoundTrips4096ArrayRanges",
                            dxmt::test::kResourceTestCost);

struct Texture2dmsRtvDescCase {
  UINT array_start_index;
  UINT array_count_index;
  D3D11_RENDER_TARGET_VIEW_DESC desc;
};

Texture2dmsRtvDescCase CaseForLogical(std::uint32_t logical) {
  Texture2dmsRtvDescCase test_case = {};
  test_case.array_start_index = logical & 63u;
  test_case.array_count_index = logical >> 6u;

  test_case.desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  test_case.desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY;
  test_case.desc.Texture2DMSArray.FirstArraySlice = test_case.array_start_index;
  test_case.desc.Texture2DMSArray.ArraySize = test_case.array_count_index + 1u;
  return test_case;
}

bool Texture2dmsRtvDescsEqual(const D3D11_RENDER_TARGET_VIEW_DESC &actual,
                              const D3D11_RENDER_TARGET_VIEW_DESC &expected) {
  return actual.Format == expected.Format &&
         actual.ViewDimension == expected.ViewDimension &&
         actual.Texture2DMSArray.FirstArraySlice ==
             expected.Texture2DMSArray.FirstArraySlice &&
         actual.Texture2DMSArray.ArraySize ==
             expected.Texture2DMSArray.ArraySize;
}

class D3D11Texture2DMSRenderTargetViewDescMatrixSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_NE(context_.context(), nullptr);

    UINT quality_levels = 0;
    ASSERT_EQ(context_.device()->CheckMultisampleQualityLevels(
                  DXGI_FORMAT_R8G8B8A8_UNORM, 4, &quality_levels),
              S_OK);
    ASSERT_GT(quality_levels, 0u);

    D3D11_TEXTURE2D_DESC texture_desc = {};
    texture_desc.Width = 4;
    texture_desc.Height = 4;
    texture_desc.MipLevels = 1;
    texture_desc.ArraySize = 127;
    texture_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texture_desc.SampleDesc.Count = 4;
    texture_desc.SampleDesc.Quality = 0;
    texture_desc.Usage = D3D11_USAGE_DEFAULT;
    texture_desc.BindFlags = D3D11_BIND_RENDER_TARGET;
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

TEST_F(D3D11Texture2DMSRenderTargetViewDescMatrixSpec,
       RoundTrips4096ArrayRanges) {
  std::vector<std::uint32_t> selected_cases;
  selected_cases.reserve(kTexture2dmsRtvDescCaseCount);
  for (std::uint32_t logical = 0; logical < kTexture2dmsRtvDescCaseCount;
       ++logical) {
    if (dxmt::test::LogicalCaseSelected(kTexture2dmsRtvDescCases.family(),
                                        logical))
      selected_cases.push_back(logical);
  }
  ASSERT_FALSE(selected_cases.empty());

  RecordProperty("logical_cases_executed", selected_cases.size());
  RecordProperty("logical_case_prefix",
                 kTexture2dmsRtvDescCases.family().case_id_prefix);
  for (const std::uint32_t logical : selected_cases) {
    const Texture2dmsRtvDescCase test_case = CaseForLogical(logical);
    ComPtr<ID3D11RenderTargetView> view;
    const HRESULT create_result = context_.device()->CreateRenderTargetView(
        texture_.get(), &test_case.desc, view.put());
    D3D11_RENDER_TARGET_VIEW_DESC actual = {};
    ComPtr<ID3D11Resource> resource;
    ComPtr<ID3D11Device> owner;
    if (create_result == S_OK && view) {
      view->GetDesc(&actual);
      view->GetResource(resource.put());
      view->GetDevice(owner.put());
    }

    const bool desc_matches = Texture2dmsRtvDescsEqual(actual, test_case.desc);
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
        dxmt::test::LogicalCaseId(kTexture2dmsRtvDescCases.family(), logical);
    ADD_FAILURE()
        << "LogicalCaseId: " << case_id << '\n'
        << "Class: "
        << dxmt::test::TestClassName(
               kTexture2dmsRtvDescCases.family().traits.test_class)
        << '\n'
        << "Requirements: feature_level=11_0 shader_model=None queue=Device "
           "capability=CheckMultisampleQualityLevels,CreateTexture2D,"
           "CreateRenderTargetView,RenderTargetViewGetDesc,ViewGetResource,"
           "DeviceChildGetDevice\n"
        << "ExecutionPath: "
        << dxmt::test::ExecutionPathName(
               kTexture2dmsRtvDescCases.family().traits.execution_path)
        << '\n'
        << "Parameters: logical=" << logical
        << " array_start_index=" << test_case.array_start_index
        << " array_count_index=" << test_case.array_count_index
        << " selected_cases=" << selected_cases.size() << '\n'
        << "Expected: format=" << static_cast<UINT>(test_case.desc.Format)
        << " dimension=" << static_cast<UINT>(test_case.desc.ViewDimension)
        << " array_start=" << test_case.desc.Texture2DMSArray.FirstArraySlice
        << " array_count=" << test_case.desc.Texture2DMSArray.ArraySize
        << " resource=" << resource_identity_.get()
        << " owner=" << context_.device() << '\n'
        << "Observed: create_hresult=" << create_result
        << " failure_phase=" << failure_phase
        << " format=" << static_cast<UINT>(actual.Format)
        << " dimension=" << static_cast<UINT>(actual.ViewDimension)
        << " array_start=" << actual.Texture2DMSArray.FirstArraySlice
        << " array_count=" << actual.Texture2DMSArray.ArraySize
        << " resource=" << resource.get() << " owner=" << owner.get()
        << " view=" << view.get() << '\n'
        << "Replay: --dxmt-case-id=" << case_id;
    break;
  }

  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11Texture2DMSRenderTargetViewDescMatrixSpec,
       DefaultsToTheCompleteArrayAndRejectsInvalidRanges) {
  ID3D11RenderTargetView *view = reinterpret_cast<ID3D11RenderTargetView *>(
      static_cast<std::uintptr_t>(1));
  EXPECT_EQ(context_.device()->CreateRenderTargetView(nullptr, nullptr, &view),
            E_INVALIDARG);
  EXPECT_EQ(view, nullptr);

  ComPtr<ID3D11RenderTargetView> default_view;
  ASSERT_EQ(context_.device()->CreateRenderTargetView(texture_.get(), nullptr,
                                                      default_view.put()),
            S_OK);
  ASSERT_NE(default_view.get(), nullptr);
  D3D11_RENDER_TARGET_VIEW_DESC default_desc = {};
  default_view->GetDesc(&default_desc);
  EXPECT_EQ(default_desc.Format, DXGI_FORMAT_R8G8B8A8_UNORM);
  EXPECT_EQ(default_desc.ViewDimension, D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY);
  EXPECT_EQ(default_desc.Texture2DMSArray.FirstArraySlice, 0u);
  EXPECT_EQ(default_desc.Texture2DMSArray.ArraySize, 127u);

  const D3D11_RENDER_TARGET_VIEW_DESC valid = CaseForLogical(0).desc;
  EXPECT_EQ(context_.device()->CreateRenderTargetView(texture_.get(), &valid,
                                                      nullptr),
            S_FALSE);

  D3D11_RENDER_TARGET_VIEW_DESC past_end = valid;
  past_end.Texture2DMSArray.FirstArraySlice = 64;
  past_end.Texture2DMSArray.ArraySize = 64;
  EXPECT_EQ(context_.device()->CreateRenderTargetView(texture_.get(), &past_end,
                                                      nullptr),
            E_INVALIDARG);

  D3D11_RENDER_TARGET_VIEW_DESC empty = valid;
  empty.Texture2DMSArray.ArraySize = 0;
  view = reinterpret_cast<ID3D11RenderTargetView *>(
      static_cast<std::uintptr_t>(1));
  EXPECT_EQ(
      context_.device()->CreateRenderTargetView(texture_.get(), &empty, &view),
      E_INVALIDARG);
  EXPECT_EQ(view, nullptr);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
