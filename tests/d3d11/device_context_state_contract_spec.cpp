#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <array>
#include <cstdint>
#include <vector>

// Public D3D11.1 device-context-state creation coverage. Context-state
// activation is deliberately separate: this contract verifies creation,
// feature-level selection, ownership, and COM identity without changing the
// immediate context's process-visible compatibility mode.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

struct ContextStateCase {
  const IID *emulated_interface;
  UINT flags;
  const char *name;
};

const std::array<ContextStateCase, 4> kContextStateCases = {
    ContextStateCase{&__uuidof(ID3D11Device), 0, "DeviceMultithreaded"},
    ContextStateCase{&__uuidof(ID3D11Device1), 0, "Device1Multithreaded"},
    ContextStateCase{&__uuidof(ID3D11Device),
                     D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED,
                     "DeviceSinglethreaded"},
    ContextStateCase{&__uuidof(ID3D11Device1),
                     D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED,
                     "Device1Singlethreaded"},
};

constexpr std::uint32_t kContextStateCaseCount = kContextStateCases.size();

const dxmt::test::LogicalCaseFamilyRegistration kContextStateCasesRegistration(
    "D3D11DeviceContextStateContractSpec.CreatesSupportedD3D11Modes",
    "D3D11.DeviceContextState.Create.Mode.", kContextStateCaseCount, 1,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "Device",
      "ID3D11Device1,CreateDeviceContextState,ID3DDeviceContextState,"
      "ID3D11DeviceChildGetDevice,QueryInterface,ComObjectIdentity"},
     dxmt::test::kResourceTestCost,
     "one test-local D3D11 device and one live device-context-state object per "
     "selected logical case",
     "create context-state objects for ID3D11Device and ID3D11Device1 "
     "emulation on devices with matching multithreaded and singlethreaded "
     "creation modes",
     "each creation chooses the requested device feature level, returns an "
     "ID3D11DeviceChild owned by the creating device, and preserves one COM "
     "identity across its public interfaces",
     "logical ID, selected-case count, emulated interface and mode, requested "
     "and chosen feature levels, HRESULTs, object and owner addresses, COM "
     "identities, failure phase, and exact replay argument"});

const dxmt::test::TestCostRegistration kContextStateCost(
    "D3D11DeviceContextStateContractSpec.CreatesSupportedD3D11Modes",
    dxmt::test::kResourceTestCost);

class D3D11DeviceContextStateContractSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_EQ(
        context_.device()->QueryInterface(
            __uuidof(ID3D11Device1), reinterpret_cast<void **>(device1_.put())),
        S_OK);
    ASSERT_NE(device1_.get(), nullptr);
  }

  D3D11TestContext context_;
  ComPtr<ID3D11Device1> device1_;
};

TEST_F(D3D11DeviceContextStateContractSpec, CreatesSupportedD3D11Modes) {
  std::vector<std::uint32_t> selected_cases;
  for (std::uint32_t logical = 0; logical < kContextStateCaseCount; ++logical) {
    if (dxmt::test::LogicalCaseSelected(kContextStateCasesRegistration.family(),
                                        logical))
      selected_cases.push_back(logical);
  }
  ASSERT_FALSE(selected_cases.empty());

  RecordProperty("logical_cases_executed", selected_cases.size());
  RecordProperty("logical_case_prefix",
                 kContextStateCasesRegistration.family().case_id_prefix);
  for (const std::uint32_t logical : selected_cases) {
    const ContextStateCase &test_case = kContextStateCases[logical];
    const D3D_FEATURE_LEVEL requested_level = context_.feature_level();
    ComPtr<ID3D11Device> singlethreaded_device;
    ComPtr<ID3D11DeviceContext> singlethreaded_context;
    ComPtr<ID3D11Device1> singlethreaded_device1;
    ID3D11Device *expected_owner = context_.device();
    ID3D11Device1 *creation_device = device1_.get();
    if (test_case.flags & D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED) {
      D3D_FEATURE_LEVEL actual_level = D3D_FEATURE_LEVEL(0);
      ASSERT_EQ(D3D11CreateDevice(context_.adapter(), D3D_DRIVER_TYPE_UNKNOWN,
                                  nullptr, D3D11_CREATE_DEVICE_SINGLETHREADED,
                                  &requested_level, 1, D3D11_SDK_VERSION,
                                  singlethreaded_device.put(), &actual_level,
                                  singlethreaded_context.put()),
                S_OK);
      ASSERT_EQ(actual_level, requested_level);
      ASSERT_EQ(singlethreaded_device->QueryInterface(
                    __uuidof(ID3D11Device1),
                    reinterpret_cast<void **>(singlethreaded_device1.put())),
                S_OK);
      expected_owner = singlethreaded_device.get();
      creation_device = singlethreaded_device1.get();
    }

    D3D_FEATURE_LEVEL chosen_level = D3D_FEATURE_LEVEL(0);
    ComPtr<ID3DDeviceContextState> state;
    const HRESULT create_result = creation_device->CreateDeviceContextState(
        test_case.flags, &requested_level, 1, D3D11_SDK_VERSION,
        *test_case.emulated_interface, &chosen_level, state.put());

    ComPtr<ID3D11Device> owner;
    ComPtr<ID3D11DeviceChild> child;
    ComPtr<IUnknown> state_identity;
    ComPtr<IUnknown> child_identity;
    HRESULT child_result = E_FAIL;
    HRESULT state_identity_result = E_FAIL;
    HRESULT child_identity_result = E_FAIL;
    if (create_result == S_OK && state) {
      state->GetDevice(owner.put());
      child_result = state->QueryInterface(
          __uuidof(ID3D11DeviceChild), reinterpret_cast<void **>(child.put()));
      state_identity_result = state->QueryInterface(
          __uuidof(IUnknown), reinterpret_cast<void **>(state_identity.put()));
      if (child_result == S_OK && child) {
        child_identity_result = child->QueryInterface(
            __uuidof(IUnknown),
            reinterpret_cast<void **>(child_identity.put()));
      }
    }

    const bool valid = create_result == S_OK && state &&
                       chosen_level == requested_level &&
                       owner.get() == expected_owner && child_result == S_OK &&
                       child && state_identity_result == S_OK &&
                       state_identity && child_identity_result == S_OK &&
                       child_identity.get() == state_identity.get();
    if (valid)
      continue;

    const auto case_id = dxmt::test::LogicalCaseId(
        kContextStateCasesRegistration.family(), logical);
    ADD_FAILURE()
        << "LogicalCaseId: " << case_id << '\n'
        << "Class: "
        << dxmt::test::TestClassName(
               kContextStateCasesRegistration.family().traits.test_class)
        << '\n'
        << "Parameters: logical=" << logical << " name=" << test_case.name
        << " flags=" << test_case.flags
        << " requested_level=" << requested_level
        << " selected_cases=" << selected_cases.size() << '\n'
        << "Expected: create_hresult=" << S_OK << " child_hresult=" << S_OK
        << " identity_hresult=" << S_OK << " chosen_level=" << requested_level
        << " owner=" << expected_owner << '\n'
        << "Observed: create_hresult=" << create_result
        << " child_hresult=" << child_result
        << " state_identity_hresult=" << state_identity_result
        << " child_identity_hresult=" << child_identity_result
        << " chosen_level=" << chosen_level << " state=" << state.get()
        << " child=" << child.get()
        << " state_identity=" << state_identity.get()
        << " child_identity=" << child_identity.get()
        << " owner=" << owner.get() << '\n'
        << "Replay: --dxmt-case-id=" << case_id;
    break;
  }

  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11DeviceContextStateContractSpec, RejectsInvalidCreationInputs) {
  const D3D_FEATURE_LEVEL requested_level = context_.feature_level();

  const auto expect_invalid = [&](UINT flags,
                                  const D3D_FEATURE_LEVEL *feature_levels,
                                  UINT feature_level_count, UINT sdk_version,
                                  REFIID emulated_interface) {
    D3D_FEATURE_LEVEL chosen_level = D3D_FEATURE_LEVEL(0xdead);
    ID3DDeviceContextState *state = reinterpret_cast<ID3DDeviceContextState *>(
        static_cast<std::uintptr_t>(1));
    EXPECT_EQ(device1_->CreateDeviceContextState(
                  flags, feature_levels, feature_level_count, sdk_version,
                  emulated_interface, &chosen_level, &state),
              E_INVALIDARG);
    EXPECT_EQ(chosen_level, D3D_FEATURE_LEVEL(0));
    EXPECT_EQ(state, nullptr);
  };

  expect_invalid(0, &requested_level, 0, D3D11_SDK_VERSION,
                 __uuidof(ID3D11Device));
  expect_invalid(0, &requested_level, 1, 0, __uuidof(ID3D11Device));
  expect_invalid(0, &requested_level, 1, D3D11_SDK_VERSION, __uuidof(IUnknown));
  expect_invalid(0x80000000u, &requested_level, 1, D3D11_SDK_VERSION,
                 __uuidof(ID3D11Device));
  expect_invalid(D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED,
                 &requested_level, 1, D3D11_SDK_VERSION,
                 __uuidof(ID3D11Device));

  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
