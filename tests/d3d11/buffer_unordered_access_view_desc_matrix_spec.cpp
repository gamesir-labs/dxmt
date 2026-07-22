#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <array>
#include <cstdint>
#include <vector>

// Public D3D11 buffer unordered-access-view descriptor coverage. Typed formats
// and element ranges form exactly 4096 distinct valid views.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

constexpr std::uint32_t kBufferUavDescCaseCount = 4096;

const dxmt::test::LogicalCaseFamilyRegistration kBufferUavDescCases(
    "D3D11BufferUnorderedAccessViewDescMatrixSpec."
    "RoundTrips4096TypedElementRanges",
    "D3D11.UAV.Buffer.Description.", kBufferUavDescCaseCount, 4,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "Device",
      "CreateBuffer,CreateUnorderedAccessView,"
      "ID3D11UnorderedAccessViewGetDesc,ID3D11ViewGetResource,"
      "ID3D11DeviceChildGetDevice"},
     dxmt::test::kResourceTestCost,
     "one test-local D3D11 device, one typed-view buffer, and one live "
     "unordered-access view per selected logical case",
     "create every combination of two core R32 typed formats, 32 first "
     "elements, and 64 element counts, then query the complete public "
     "descriptor and relationships",
     "every view preserves its descriptor and returns the source buffer and "
     "creating device through their public COM interfaces",
     "logical ID, selected-case count, combination indexes, expected and "
     "returned descriptors, resource and owner addresses, HRESULT, failure "
     "phase, and exact replay argument"});

const dxmt::test::TestCostRegistration
    kBufferUavDescCost("D3D11BufferUnorderedAccessViewDescMatrixSpec."
                       "RoundTrips4096TypedElementRanges",
                       dxmt::test::kResourceTestCost);

constexpr std::array<DXGI_FORMAT, 2> kViewFormats = {
    DXGI_FORMAT_R32_FLOAT,
    DXGI_FORMAT_R32_UINT,
};

struct BufferUavDescCase {
  UINT format_index;
  UINT first_element_index;
  UINT element_count_index;
  D3D11_UNORDERED_ACCESS_VIEW_DESC desc;
};

BufferUavDescCase CaseForLogical(std::uint32_t logical) {
  BufferUavDescCase test_case = {};
  std::uint32_t encoded = logical;
  test_case.format_index = encoded & 1u;
  encoded >>= 1u;
  test_case.first_element_index = encoded & 31u;
  encoded >>= 5u;
  test_case.element_count_index = encoded & 63u;

  test_case.desc.Format = kViewFormats[test_case.format_index];
  test_case.desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
  test_case.desc.Buffer.FirstElement = test_case.first_element_index;
  test_case.desc.Buffer.NumElements = test_case.element_count_index + 1u;
  test_case.desc.Buffer.Flags = 0;
  return test_case;
}

bool BufferUavDescsEqual(const D3D11_UNORDERED_ACCESS_VIEW_DESC &actual,
                         const D3D11_UNORDERED_ACCESS_VIEW_DESC &expected) {
  return actual.Format == expected.Format &&
         actual.ViewDimension == expected.ViewDimension &&
         actual.Buffer.FirstElement == expected.Buffer.FirstElement &&
         actual.Buffer.NumElements == expected.Buffer.NumElements &&
         actual.Buffer.Flags == expected.Buffer.Flags;
}

class D3D11BufferUnorderedAccessViewDescMatrixSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_NE(context_.context(), nullptr);

    D3D11_BUFFER_DESC buffer_desc = {};
    buffer_desc.ByteWidth = 95u * sizeof(std::uint32_t);
    buffer_desc.Usage = D3D11_USAGE_DEFAULT;
    buffer_desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    ASSERT_EQ(
        context_.device()->CreateBuffer(&buffer_desc, nullptr, buffer_.put()),
        S_OK);
    ASSERT_NE(buffer_.get(), nullptr);
    ASSERT_EQ(buffer_->QueryInterface(
                  __uuidof(ID3D11Resource),
                  reinterpret_cast<void **>(resource_identity_.put())),
              S_OK);
  }

  D3D11TestContext context_;
  ComPtr<ID3D11Buffer> buffer_;
  ComPtr<ID3D11Resource> resource_identity_;
};

TEST_F(D3D11BufferUnorderedAccessViewDescMatrixSpec,
       RoundTrips4096TypedElementRanges) {
  std::vector<std::uint32_t> selected_cases;
  selected_cases.reserve(kBufferUavDescCaseCount);
  for (std::uint32_t logical = 0; logical < kBufferUavDescCaseCount;
       ++logical) {
    if (dxmt::test::LogicalCaseSelected(kBufferUavDescCases.family(), logical))
      selected_cases.push_back(logical);
  }
  ASSERT_FALSE(selected_cases.empty());

  RecordProperty("logical_cases_executed", selected_cases.size());
  RecordProperty("logical_case_prefix",
                 kBufferUavDescCases.family().case_id_prefix);
  for (const std::uint32_t logical : selected_cases) {
    const BufferUavDescCase test_case = CaseForLogical(logical);
    ComPtr<ID3D11UnorderedAccessView> view;
    const HRESULT create_result = context_.device()->CreateUnorderedAccessView(
        buffer_.get(), &test_case.desc, view.put());
    D3D11_UNORDERED_ACCESS_VIEW_DESC actual = {};
    ComPtr<ID3D11Resource> resource;
    ComPtr<ID3D11Device> owner;
    if (create_result == S_OK && view) {
      view->GetDesc(&actual);
      view->GetResource(resource.put());
      view->GetDevice(owner.put());
    }

    const bool desc_matches = BufferUavDescsEqual(actual, test_case.desc);
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
        dxmt::test::LogicalCaseId(kBufferUavDescCases.family(), logical);
    ADD_FAILURE()
        << "LogicalCaseId: " << case_id << '\n'
        << "Class: "
        << dxmt::test::TestClassName(
               kBufferUavDescCases.family().traits.test_class)
        << '\n'
        << "Requirements: feature_level=11_0 shader_model=None queue=Device "
           "capability=CreateBuffer,CreateUnorderedAccessView,"
           "UnorderedAccessViewGetDesc,ViewGetResource,DeviceChildGetDevice\n"
        << "ExecutionPath: "
        << dxmt::test::ExecutionPathName(
               kBufferUavDescCases.family().traits.execution_path)
        << '\n'
        << "Parameters: logical=" << logical
        << " format_index=" << test_case.format_index
        << " first_element_index=" << test_case.first_element_index
        << " element_count_index=" << test_case.element_count_index
        << " selected_cases=" << selected_cases.size() << '\n'
        << "Expected: format=" << static_cast<UINT>(test_case.desc.Format)
        << " dimension=" << static_cast<UINT>(test_case.desc.ViewDimension)
        << " first_element=" << test_case.desc.Buffer.FirstElement
        << " element_count=" << test_case.desc.Buffer.NumElements
        << " flags=" << test_case.desc.Buffer.Flags
        << " resource=" << resource_identity_.get()
        << " owner=" << context_.device() << '\n'
        << "Observed: create_hresult=" << create_result
        << " failure_phase=" << failure_phase
        << " format=" << static_cast<UINT>(actual.Format)
        << " dimension=" << static_cast<UINT>(actual.ViewDimension)
        << " first_element=" << actual.Buffer.FirstElement
        << " element_count=" << actual.Buffer.NumElements
        << " flags=" << actual.Buffer.Flags << " resource=" << resource.get()
        << " owner=" << owner.get() << " view=" << view.get() << '\n'
        << "Replay: --dxmt-case-id=" << case_id;
    break;
  }

  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11BufferUnorderedAccessViewDescMatrixSpec,
       RejectsNullResourceAndInvalidElementRanges) {
  ID3D11UnorderedAccessView *view =
      reinterpret_cast<ID3D11UnorderedAccessView *>(
          static_cast<std::uintptr_t>(1));
  EXPECT_EQ(
      context_.device()->CreateUnorderedAccessView(nullptr, nullptr, &view),
      E_INVALIDARG);
  EXPECT_EQ(view, nullptr);

  const D3D11_UNORDERED_ACCESS_VIEW_DESC valid = CaseForLogical(0).desc;
  EXPECT_EQ(context_.device()->CreateUnorderedAccessView(buffer_.get(), &valid,
                                                         nullptr),
            S_FALSE);

  D3D11_UNORDERED_ACCESS_VIEW_DESC past_end = valid;
  past_end.Buffer.FirstElement = 32;
  past_end.Buffer.NumElements = 64;
  EXPECT_EQ(context_.device()->CreateUnorderedAccessView(buffer_.get(),
                                                         &past_end, nullptr),
            E_INVALIDARG);

  D3D11_UNORDERED_ACCESS_VIEW_DESC empty = valid;
  empty.Buffer.NumElements = 0;
  view = reinterpret_cast<ID3D11UnorderedAccessView *>(
      static_cast<std::uintptr_t>(1));
  EXPECT_EQ(context_.device()->CreateUnorderedAccessView(buffer_.get(), &empty,
                                                         &view),
            E_INVALIDARG);
  EXPECT_EQ(view, nullptr);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
