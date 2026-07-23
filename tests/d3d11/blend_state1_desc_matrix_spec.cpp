#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <array>
#include <cstdint>
#include <vector>

// Public D3D11.1 blend-state coverage for every logic operation. The matrix
// adapts to the device's advertised OutputMergerLogicOp capability and checks
// the corresponding enabled or normalized-disabled descriptor contract.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

constexpr std::array<D3D11_LOGIC_OP, 16> kLogicOperations = {
    D3D11_LOGIC_OP_CLEAR,       D3D11_LOGIC_OP_SET,
    D3D11_LOGIC_OP_COPY,        D3D11_LOGIC_OP_COPY_INVERTED,
    D3D11_LOGIC_OP_NOOP,        D3D11_LOGIC_OP_INVERT,
    D3D11_LOGIC_OP_AND,         D3D11_LOGIC_OP_NAND,
    D3D11_LOGIC_OP_OR,          D3D11_LOGIC_OP_NOR,
    D3D11_LOGIC_OP_XOR,         D3D11_LOGIC_OP_EQUIV,
    D3D11_LOGIC_OP_AND_REVERSE, D3D11_LOGIC_OP_AND_INVERTED,
    D3D11_LOGIC_OP_OR_REVERSE,  D3D11_LOGIC_OP_OR_INVERTED,
};

constexpr std::uint32_t kBlendState1CaseCount = kLogicOperations.size();

const dxmt::test::LogicalCaseFamilyRegistration kBlendState1Cases(
    "D3D11BlendState1DescMatrixSpec."
    "RoundTripsEveryAdvertisedLogicOperation",
    "D3D11.BlendState1.LogicOp.Description.", kBlendState1CaseCount, 2,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "Device",
      "ID3D11Device1,CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS),"
      "CreateBlendState1,ID3D11BlendState1GetDesc1,"
      "ID3D11DeviceChildGetDevice,ComObjectIdentity"},
     dxmt::test::kResourceTestCost,
     "one test-local D3D11 device and one live pair of BlendState1 COM "
     "references per selected logical case",
     "create all 16 public D3D11 logic operations when advertised, otherwise "
     "supply each operation while logic operations remain disabled, then "
     "query the normalized Desc1 and recreate the state",
     "enabled operations round-trip exactly; disabled operation fields "
     "normalize to NOOP; identical descriptions reuse the same COM object",
     "logical ID, selected-case count, advertised capability, logic operation, "
     "expected and returned descriptors, state and owner addresses, HRESULTs, "
     "failure phase, and exact replay argument"});

const dxmt::test::TestCostRegistration
    kBlendState1Cost("D3D11BlendState1DescMatrixSpec."
                     "RoundTripsEveryAdvertisedLogicOperation",
                     dxmt::test::kResourceTestCost);

D3D11_RENDER_TARGET_BLEND_DESC1 DefaultTarget() {
  D3D11_RENDER_TARGET_BLEND_DESC1 target = {};
  target.BlendEnable = FALSE;
  target.LogicOpEnable = FALSE;
  target.SrcBlend = D3D11_BLEND_ONE;
  target.DestBlend = D3D11_BLEND_ZERO;
  target.BlendOp = D3D11_BLEND_OP_ADD;
  target.SrcBlendAlpha = D3D11_BLEND_ONE;
  target.DestBlendAlpha = D3D11_BLEND_ZERO;
  target.BlendOpAlpha = D3D11_BLEND_OP_ADD;
  target.LogicOp = D3D11_LOGIC_OP_NOOP;
  target.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
  return target;
}

D3D11_BLEND_DESC1 DescForLogical(std::uint32_t logical,
                                 bool logic_ops_supported) {
  D3D11_BLEND_DESC1 desc = {};
  desc.AlphaToCoverageEnable = FALSE;
  desc.IndependentBlendEnable = FALSE;
  for (auto &target : desc.RenderTarget)
    target = DefaultTarget();
  desc.RenderTarget[0].LogicOpEnable = logic_ops_supported ? TRUE : FALSE;
  desc.RenderTarget[0].LogicOp = kLogicOperations[logical];
  return desc;
}

D3D11_BLEND_DESC1 ExpectedDesc(const D3D11_BLEND_DESC1 &created) {
  D3D11_BLEND_DESC1 expected = created;
  D3D11_RENDER_TARGET_BLEND_DESC1 target = created.RenderTarget[0];
  if (!target.LogicOpEnable)
    target.LogicOp = D3D11_LOGIC_OP_NOOP;
  for (auto &output : expected.RenderTarget)
    output = target;
  return expected;
}

bool TargetDescsEqual(const D3D11_RENDER_TARGET_BLEND_DESC1 &actual,
                      const D3D11_RENDER_TARGET_BLEND_DESC1 &expected) {
  return actual.BlendEnable == expected.BlendEnable &&
         actual.LogicOpEnable == expected.LogicOpEnable &&
         actual.SrcBlend == expected.SrcBlend &&
         actual.DestBlend == expected.DestBlend &&
         actual.BlendOp == expected.BlendOp &&
         actual.SrcBlendAlpha == expected.SrcBlendAlpha &&
         actual.DestBlendAlpha == expected.DestBlendAlpha &&
         actual.BlendOpAlpha == expected.BlendOpAlpha &&
         actual.LogicOp == expected.LogicOp &&
         actual.RenderTargetWriteMask == expected.RenderTargetWriteMask;
}

bool BlendDescsEqual(const D3D11_BLEND_DESC1 &actual,
                     const D3D11_BLEND_DESC1 &expected) {
  if (actual.AlphaToCoverageEnable != expected.AlphaToCoverageEnable ||
      actual.IndependentBlendEnable != expected.IndependentBlendEnable)
    return false;
  for (UINT index = 0; index < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT;
       ++index) {
    if (!TargetDescsEqual(actual.RenderTarget[index],
                          expected.RenderTarget[index]))
      return false;
  }
  return true;
}

class D3D11BlendState1DescMatrixSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_EQ(
        context_.device()->QueryInterface(
            __uuidof(ID3D11Device1), reinterpret_cast<void **>(device1_.put())),
        S_OK);
    ASSERT_NE(device1_.get(), nullptr);

    D3D11_FEATURE_DATA_D3D11_OPTIONS options = {};
    ASSERT_EQ(context_.device()->CheckFeatureSupport(
                  D3D11_FEATURE_D3D11_OPTIONS, &options, sizeof(options)),
              S_OK);
    logic_ops_supported_ = options.OutputMergerLogicOp != FALSE;
  }

  D3D11TestContext context_;
  ComPtr<ID3D11Device1> device1_;
  bool logic_ops_supported_ = false;
};

TEST_F(D3D11BlendState1DescMatrixSpec,
       RoundTripsEveryAdvertisedLogicOperation) {
  std::vector<std::uint32_t> selected_cases;
  for (std::uint32_t logical = 0; logical < kBlendState1CaseCount; ++logical) {
    if (dxmt::test::LogicalCaseSelected(kBlendState1Cases.family(), logical))
      selected_cases.push_back(logical);
  }
  ASSERT_FALSE(selected_cases.empty());

  RecordProperty("logical_cases_executed", selected_cases.size());
  RecordProperty("logical_case_prefix",
                 kBlendState1Cases.family().case_id_prefix);
  for (const std::uint32_t logical : selected_cases) {
    const D3D11_BLEND_DESC1 created =
        DescForLogical(logical, logic_ops_supported_);
    const D3D11_BLEND_DESC1 expected = ExpectedDesc(created);
    ComPtr<ID3D11BlendState1> state;
    ComPtr<ID3D11BlendState1> duplicate;
    const HRESULT create_result =
        device1_->CreateBlendState1(&created, state.put());
    HRESULT duplicate_result = E_FAIL;
    D3D11_BLEND_DESC1 actual = {};
    ComPtr<ID3D11BlendState> legacy;
    ComPtr<ID3D11Device> owner;
    if (create_result == S_OK && state) {
      state->GetDesc1(&actual);
      state->GetDevice(owner.put());
      state->QueryInterface(__uuidof(ID3D11BlendState),
                            reinterpret_cast<void **>(legacy.put()));
      duplicate_result = device1_->CreateBlendState1(&created, duplicate.put());
    }

    const bool valid =
        create_result == S_OK && state && BlendDescsEqual(actual, expected) &&
        legacy && owner.get() == context_.device() &&
        duplicate_result == S_OK && duplicate.get() == state.get();
    if (valid)
      continue;

    const auto case_id =
        dxmt::test::LogicalCaseId(kBlendState1Cases.family(), logical);
    ADD_FAILURE()
        << "LogicalCaseId: " << case_id << '\n'
        << "Class: "
        << dxmt::test::TestClassName(
               kBlendState1Cases.family().traits.test_class)
        << '\n'
        << "Parameters: logical=" << logical
        << " logic_op=" << static_cast<UINT>(kLogicOperations[logical])
        << " advertised=" << logic_ops_supported_
        << " selected_cases=" << selected_cases.size() << '\n'
        << "Expected: create_hresult=" << S_OK << " duplicate_hresult=" << S_OK
        << " logic_enabled=" << expected.RenderTarget[0].LogicOpEnable
        << " logic_op=" << static_cast<UINT>(expected.RenderTarget[0].LogicOp)
        << " owner=" << context_.device() << '\n'
        << "Observed: create_hresult=" << create_result
        << " duplicate_hresult=" << duplicate_result
        << " logic_enabled=" << actual.RenderTarget[0].LogicOpEnable
        << " logic_op=" << static_cast<UINT>(actual.RenderTarget[0].LogicOp)
        << " legacy=" << legacy.get() << " owner=" << owner.get()
        << " state=" << state.get() << " duplicate=" << duplicate.get() << '\n'
        << "Replay: --dxmt-case-id=" << case_id;
    break;
  }

  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11BlendState1DescMatrixSpec,
       ValidatesNullOutputAndMutuallyExclusiveOperations) {
  ID3D11BlendState1 *state =
      reinterpret_cast<ID3D11BlendState1 *>(static_cast<std::uintptr_t>(1));
  EXPECT_EQ(device1_->CreateBlendState1(nullptr, &state), E_INVALIDARG);
  EXPECT_EQ(state, nullptr);

  D3D11_BLEND_DESC1 desc = DescForLogical(0, false);
  EXPECT_EQ(device1_->CreateBlendState1(&desc, nullptr), S_FALSE);

  desc.RenderTarget[0].BlendEnable = TRUE;
  desc.RenderTarget[0].LogicOpEnable = TRUE;
  state = reinterpret_cast<ID3D11BlendState1 *>(static_cast<std::uintptr_t>(1));
  EXPECT_EQ(device1_->CreateBlendState1(&desc, &state), E_INVALIDARG);
  EXPECT_EQ(state, nullptr);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
