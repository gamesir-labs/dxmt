#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <array>
#include <cstdint>
#include <vector>

// Public command-list creation and immediate-context rejection contracts.
// Every context, list, and resource is test-local, so these cases are safe
// under the default parallel scheduler.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

constexpr std::array<WINBOOL, 2> kRestoreCases = {FALSE, TRUE};
constexpr std::array<WINBOOL, 2> kImmediateWorkCases = {FALSE, TRUE};

constexpr GUID kCommandListDataKey = {
    0x9f149175,
    0xf686,
    0x44e1,
    {0x95, 0xba, 0x35, 0x74, 0xe6, 0xf8, 0x14, 0xcc},
};

const dxmt::test::LogicalCaseFamilyRegistration kCommandListRestoreFamily(
    "D3D11CommandListContractSpec.CreatesEmptyListsForBothRestoreModes",
    "D3D11.CommandList.Empty.Restore.", kRestoreCases.size(), 1,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "DeviceContext",
      "CreateDeferredContext,FinishCommandList,ID3D11CommandList"},
     dxmt::test::kResourceTestCost,
     "one test-local D3D11 device, deferred context, and empty command list "
     "per selected logical case",
     "finish an empty deferred context with RestoreDeferredContextState set "
     "to FALSE and TRUE, then inspect the public command-list object",
     "both modes return a command list with zero context flags, the creating "
     "device, and one canonical COM identity",
     "logical ID, restore mode, HRESULT, flags, owner and interface "
     "identities, and exact replay argument"});

const dxmt::test::LogicalCaseFamilyRegistration kImmediateRejectionFamily(
    "D3D11CommandListContractSpec.ImmediateContextRejectsFinishCommandList",
    "D3D11.CommandList.Immediate.Reject.", kImmediateWorkCases.size(), 1,
    {dxmt::test::TestClass::Robustness,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "DeviceContext",
      "CopyResource,FinishCommandList,DXGI_ERROR_INVALID_CALL"},
     dxmt::test::kResourceTestCost,
     "one test-local D3D11 device and optionally two small buffers per "
     "selected logical case",
     "call FinishCommandList on the immediate context before and after "
     "recording a public CopyResource operation",
     "the immediate context rejects both calls with "
     "DXGI_ERROR_INVALID_CALL",
     "logical ID, whether work was recorded, HRESULT, returned command-list "
     "address, and exact replay argument"});

const dxmt::test::TestCostRegistration kCommandListRestoreCost(
    "D3D11CommandListContractSpec.CreatesEmptyListsForBothRestoreModes",
    dxmt::test::kResourceTestCost);
const dxmt::test::TestCostRegistration kImmediateRejectionCost(
    "D3D11CommandListContractSpec.ImmediateContextRejectsFinishCommandList",
    dxmt::test::kResourceTestCost);

class D3D11CommandListContractSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_NE(context_.context(), nullptr);
  }

  ComPtr<ID3D11Buffer> CreateBuffer() {
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = 64;
    desc.Usage = D3D11_USAGE_DEFAULT;
    ComPtr<ID3D11Buffer> buffer;
    EXPECT_EQ(context_.device()->CreateBuffer(&desc, nullptr, buffer.put()),
              S_OK);
    return buffer;
  }

  D3D11TestContext context_;
};

TEST_F(D3D11CommandListContractSpec, CreatesEmptyListsForBothRestoreModes) {
  std::vector<std::uint32_t> selected_cases;
  for (std::uint32_t logical = 0; logical < kRestoreCases.size(); ++logical) {
    if (dxmt::test::LogicalCaseSelected(kCommandListRestoreFamily.family(),
                                        logical))
      selected_cases.push_back(logical);
  }
  ASSERT_FALSE(selected_cases.empty());

  RecordProperty("logical_cases_executed", selected_cases.size());
  RecordProperty("logical_case_prefix",
                 kCommandListRestoreFamily.family().case_id_prefix);
  for (const std::uint32_t logical : selected_cases) {
    ComPtr<ID3D11DeviceContext> deferred;
    ASSERT_EQ(context_.device()->CreateDeferredContext(0, deferred.put()),
              S_OK);
    ComPtr<ID3D11CommandList> command_list;
    const HRESULT finish_result =
        deferred->FinishCommandList(kRestoreCases[logical], command_list.put());

    UINT context_flags = ~0u;
    ComPtr<ID3D11Device> owner;
    ComPtr<IUnknown> list_identity;
    ComPtr<ID3D11DeviceChild> as_child;
    ComPtr<IUnknown> child_identity;
    HRESULT child_result = E_FAIL;
    HRESULT list_identity_result = E_FAIL;
    HRESULT child_identity_result = E_FAIL;
    if (finish_result == S_OK && command_list) {
      context_flags = command_list->GetContextFlags();
      command_list->GetDevice(owner.put());
      child_result = command_list->QueryInterface(
          __uuidof(ID3D11DeviceChild),
          reinterpret_cast<void **>(as_child.put()));
      list_identity_result = command_list->QueryInterface(
          __uuidof(IUnknown), reinterpret_cast<void **>(list_identity.put()));
      if (child_result == S_OK && as_child) {
        child_identity_result = as_child->QueryInterface(
            __uuidof(IUnknown),
            reinterpret_cast<void **>(child_identity.put()));
      }
    }

    const bool valid = finish_result == S_OK && command_list &&
                       context_flags == 0 && owner.get() == context_.device() &&
                       child_result == S_OK && as_child &&
                       list_identity_result == S_OK && list_identity &&
                       child_identity_result == S_OK && child_identity &&
                       list_identity.get() == child_identity.get();
    if (valid)
      continue;

    const auto case_id =
        dxmt::test::LogicalCaseId(kCommandListRestoreFamily.family(), logical);
    ADD_FAILURE() << "LogicalCaseId: " << case_id << '\n'
                  << "Parameters: logical=" << logical
                  << " restore=" << kRestoreCases[logical]
                  << " selected_cases=" << selected_cases.size() << '\n'
                  << "Expected: finish_hresult=" << S_OK
                  << " context_flags=0 owner=" << context_.device()
                  << " child_hresult=" << S_OK << " identities_equal=true"
                  << '\n'
                  << "Observed: finish_hresult=" << finish_result
                  << " command_list=" << command_list.get()
                  << " context_flags=" << context_flags
                  << " owner=" << owner.get()
                  << " child_hresult=" << child_result
                  << " child=" << as_child.get()
                  << " identity_hresults=" << list_identity_result << ','
                  << child_identity_result
                  << " identities=" << list_identity.get() << ','
                  << child_identity.get() << '\n'
                  << "Replay: --dxmt-case-id=" << case_id;
    return;
  }

  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11CommandListContractSpec,
       CommandListPrivateDataRoundTripsAndDeletes) {
  ComPtr<ID3D11DeviceContext> deferred;
  ASSERT_EQ(context_.device()->CreateDeferredContext(0, deferred.put()), S_OK);
  ComPtr<ID3D11CommandList> command_list;
  ASSERT_EQ(deferred->FinishCommandList(FALSE, command_list.put()), S_OK);
  ASSERT_NE(command_list.get(), nullptr);

  constexpr std::array<std::uint8_t, 5> expected = {2, 3, 5, 7, 11};
  ASSERT_EQ(command_list->SetPrivateData(kCommandListDataKey, expected.size(),
                                         expected.data()),
            S_OK);
  UINT required = 0;
  ASSERT_EQ(
      command_list->GetPrivateData(kCommandListDataKey, &required, nullptr),
      S_OK);
  EXPECT_EQ(required, expected.size());

  std::array<std::uint8_t, expected.size()> actual = {};
  UINT actual_size = actual.size();
  ASSERT_EQ(command_list->GetPrivateData(kCommandListDataKey, &actual_size,
                                         actual.data()),
            S_OK);
  EXPECT_EQ(actual_size, expected.size());
  EXPECT_EQ(actual, expected);

  ASSERT_EQ(command_list->SetPrivateData(kCommandListDataKey, 0, nullptr),
            S_OK);
  actual_size = actual.size();
  EXPECT_EQ(command_list->GetPrivateData(kCommandListDataKey, &actual_size,
                                         actual.data()),
            DXGI_ERROR_NOT_FOUND);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11CommandListContractSpec, ImmediateContextRejectsFinishCommandList) {
  std::vector<std::uint32_t> selected_cases;
  for (std::uint32_t logical = 0; logical < kImmediateWorkCases.size();
       ++logical) {
    if (dxmt::test::LogicalCaseSelected(kImmediateRejectionFamily.family(),
                                        logical))
      selected_cases.push_back(logical);
  }
  ASSERT_FALSE(selected_cases.empty());

  RecordProperty("logical_cases_executed", selected_cases.size());
  RecordProperty("logical_case_prefix",
                 kImmediateRejectionFamily.family().case_id_prefix);
  for (const std::uint32_t logical : selected_cases) {
    if (kImmediateWorkCases[logical]) {
      ComPtr<ID3D11Buffer> source = CreateBuffer();
      ComPtr<ID3D11Buffer> destination = CreateBuffer();
      ASSERT_NE(source.get(), nullptr);
      ASSERT_NE(destination.get(), nullptr);
      context_.context()->CopyResource(destination.get(), source.get());
    }

    ComPtr<ID3D11CommandList> command_list;
    const HRESULT finish_result =
        context_.context()->FinishCommandList(FALSE, command_list.put());
    const auto case_id =
        dxmt::test::LogicalCaseId(kImmediateRejectionFamily.family(), logical);
    EXPECT_EQ(finish_result, DXGI_ERROR_INVALID_CALL)
        << "LogicalCaseId: " << case_id
        << " recorded_work=" << kImmediateWorkCases[logical]
        << " command_list=" << command_list.get()
        << " replay=--dxmt-case-id=" << case_id;
  }

  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
