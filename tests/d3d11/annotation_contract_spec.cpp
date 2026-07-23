#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <array>
#include <cstdint>
#include <vector>

// Public ID3DUserDefinedAnnotation coverage for immediate and deferred
// contexts. Each case owns its contexts and annotation nesting, so it is safe
// under the default parallel scheduler.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

struct AnnotationContextCase {
  ID3D11DeviceContext *context;
  const char *name;
};

constexpr std::uint32_t kAnnotationContextCaseCount = 2;

const dxmt::test::LogicalCaseFamilyRegistration kAnnotationContextCases(
    "D3D11AnnotationContractSpec.ExposesDisabledAnnotationContracts",
    "D3D11.Context.Annotation.", kAnnotationContextCaseCount, 1,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "DeviceContext",
      "CreateDeferredContext,QueryInterface,ID3DUserDefinedAnnotation"},
     dxmt::test::kNormalTestCost,
     "one test-local D3D11 device with immediate and deferred contexts",
     "query the annotation interface from both context types, verify COM "
     "identity, emit an event and marker, and inspect disabled status",
     "both context types expose an aggregated annotation interface; when "
     "annotation capture is disabled BeginEvent and EndEvent return minus "
     "one and GetStatus returns FALSE",
     "logical ID, context type, interface and identity HRESULTs, annotation "
     "return values, status, and exact replay argument"});

const dxmt::test::TestCostRegistration kAnnotationContextCost(
    "D3D11AnnotationContractSpec.ExposesDisabledAnnotationContracts",
    dxmt::test::kNormalTestCost);

class D3D11AnnotationContractSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_NE(context_.context(), nullptr);
    ASSERT_EQ(context_.device()->CreateDeferredContext(0, deferred_.put()),
              S_OK);
    ASSERT_NE(deferred_.get(), nullptr);
  }

  D3D11TestContext context_;
  ComPtr<ID3D11DeviceContext> deferred_;
};

TEST_F(D3D11AnnotationContractSpec, ExposesDisabledAnnotationContracts) {
  const std::array context_cases = {
      AnnotationContextCase{context_.context(), "Immediate"},
      AnnotationContextCase{deferred_.get(), "Deferred"},
  };
  std::vector<std::uint32_t> selected_cases;
  for (std::uint32_t logical = 0; logical < kAnnotationContextCaseCount;
       ++logical) {
    if (dxmt::test::LogicalCaseSelected(kAnnotationContextCases.family(),
                                        logical))
      selected_cases.push_back(logical);
  }
  ASSERT_FALSE(selected_cases.empty());

  RecordProperty("logical_cases_executed", selected_cases.size());
  RecordProperty("logical_case_prefix",
                 kAnnotationContextCases.family().case_id_prefix);
  for (const std::uint32_t logical : selected_cases) {
    const AnnotationContextCase &test_case = context_cases[logical];
    ComPtr<ID3DUserDefinedAnnotation> annotation;
    const HRESULT query_result = test_case.context->QueryInterface(
        __uuidof(ID3DUserDefinedAnnotation),
        reinterpret_cast<void **>(annotation.put()));

    ComPtr<IUnknown> context_identity;
    ComPtr<IUnknown> annotation_identity;
    HRESULT context_identity_result = E_FAIL;
    HRESULT annotation_identity_result = E_FAIL;
    INT begin_result = 0;
    INT end_result = 0;
    WINBOOL status = TRUE;
    if (query_result == S_OK && annotation) {
      context_identity_result = test_case.context->QueryInterface(
          __uuidof(IUnknown),
          reinterpret_cast<void **>(context_identity.put()));
      annotation_identity_result = annotation->QueryInterface(
          __uuidof(IUnknown),
          reinterpret_cast<void **>(annotation_identity.put()));
      begin_result = annotation->BeginEvent(L"DXMT public annotation event");
      annotation->SetMarker(L"DXMT public annotation marker");
      end_result = annotation->EndEvent();
      status = annotation->GetStatus();
    }

    const bool valid =
        query_result == S_OK && annotation && context_identity_result == S_OK &&
        context_identity && annotation_identity_result == S_OK &&
        annotation_identity &&
        context_identity.get() == annotation_identity.get() &&
        begin_result == -1 && end_result == -1 && status == FALSE;
    if (valid)
      continue;

    const auto case_id =
        dxmt::test::LogicalCaseId(kAnnotationContextCases.family(), logical);
    ADD_FAILURE() << "LogicalCaseId: " << case_id << '\n'
                  << "Parameters: logical=" << logical
                  << " context_type=" << test_case.name
                  << " selected_cases=" << selected_cases.size() << '\n'
                  << "Expected: query_hresult=" << S_OK
                  << " identity_hresults=" << S_OK << ',' << S_OK
                  << " identities_equal=true begin=-1 end=-1 status=" << FALSE
                  << '\n'
                  << "Observed: query_hresult=" << query_result
                  << " annotation=" << annotation.get()
                  << " identity_hresults=" << context_identity_result << ','
                  << annotation_identity_result
                  << " identities=" << context_identity.get() << ','
                  << annotation_identity.get() << " begin=" << begin_result
                  << " end=" << end_result << " status=" << status << '\n'
                  << "Replay: --dxmt-case-id=" << case_id;
    return;
  }

  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
