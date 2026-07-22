#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d11_test_context.hpp"

#include <array>
#include <cstdint>
#include <vector>

// Public-D3D11 input-layout coverage. Every one of 64 layouts is replaced by
// every layout, exercising 4096 ordered input-assembler state transitions.

namespace {

using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

constexpr std::uint32_t kInputLayoutBindingCaseCount = 4096;
constexpr std::uint32_t kInputLayoutCount = 64;

const dxmt::test::LogicalCaseFamilyRegistration kInputLayoutBindingCases(
    "D3D11InputLayoutBindingMatrixSpec."
    "RoundTrips4096OrderedLayoutTransitions",
    "D3D11.IAGetInputLayout.Binding.", kInputLayoutBindingCaseCount, 4,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "5_0", "Immediate",
      "D3DCompile,CreateInputLayout,IASetInputLayout,IAGetInputLayout,"
      "ComReferenceState"},
     dxmt::test::kResourceTestCost,
     "one public D3DCompile vertex signature and sixty-four test-local input "
     "layouts with distinct aligned byte offsets",
     "bind and query every ordered pair of previous and replacement input "
     "layouts, release each getter reference, then bind a null layout",
     "each getter returns the exact most recently bound input-layout object, "
     "and the getter returns null after unbinding",
     "logical ID, selected-case count, previous and replacement layout "
     "indices and byte offsets, expected and actual addresses, failure phase, "
     "and exact replay argument"});

const dxmt::test::TestCostRegistration
    kInputLayoutBindingCost("D3D11InputLayoutBindingMatrixSpec."
                            "RoundTrips4096OrderedLayoutTransitions",
                            dxmt::test::kResourceTestCost);

struct InputLayoutTransition {
  std::uint32_t previous_layout_index;
  std::uint32_t replacement_layout_index;
};

InputLayoutTransition TransitionForCase(std::uint32_t logical) {
  return {logical & 63u, (logical >> 6u) & 63u};
}

class D3D11InputLayoutBindingMatrixSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_NE(context_.context(), nullptr);
  }

  D3D11TestContext context_;
};

TEST_F(D3D11InputLayoutBindingMatrixSpec,
       RoundTrips4096OrderedLayoutTransitions) {
  std::vector<std::uint32_t> selected_cases;
  selected_cases.reserve(kInputLayoutBindingCaseCount);
  for (std::uint32_t logical = 0; logical < kInputLayoutBindingCaseCount;
       ++logical) {
    if (dxmt::test::LogicalCaseSelected(kInputLayoutBindingCases.family(),
                                        logical))
      selected_cases.push_back(logical);
  }
  ASSERT_FALSE(selected_cases.empty());

  const auto vertex = CompileShader(
      "float4 main(float4 position : POSITION) : SV_Position { return "
      "position; }",
      "vs_5_0");
  ASSERT_EQ(vertex.result, S_OK) << vertex.diagnostic_text();

  std::array<ComPtr<ID3D11InputLayout>, kInputLayoutCount> layouts;
  std::array<UINT, kInputLayoutCount> byte_offsets;
  for (std::uint32_t index = 0; index < kInputLayoutCount; ++index) {
    byte_offsets[index] = index * sizeof(FLOAT);
    const D3D11_INPUT_ELEMENT_DESC element = {"POSITION",
                                              0,
                                              DXGI_FORMAT_R32G32B32A32_FLOAT,
                                              0,
                                              byte_offsets[index],
                                              D3D11_INPUT_PER_VERTEX_DATA,
                                              0};
    ASSERT_EQ(context_.device()->CreateInputLayout(
                  &element, 1, vertex.bytecode->GetBufferPointer(),
                  vertex.bytecode->GetBufferSize(), layouts[index].put()),
              S_OK)
        << "layout_index=" << index << " byte_offset=" << byte_offsets[index];
    for (std::uint32_t previous = 0; previous < index; ++previous) {
      ASSERT_NE(layouts[index].get(), layouts[previous].get())
          << "layout_index=" << index << " previous_layout_index=" << previous
          << " byte_offset=" << byte_offsets[index]
          << " previous_byte_offset=" << byte_offsets[previous];
    }
  }

  RecordProperty("logical_cases_executed", selected_cases.size());
  RecordProperty("logical_case_prefix",
                 kInputLayoutBindingCases.family().case_id_prefix);
  for (const std::uint32_t logical : selected_cases) {
    const InputLayoutTransition transition = TransitionForCase(logical);

    context_.context()->IASetInputLayout(
        layouts[transition.previous_layout_index].get());
    ID3D11InputLayout *actual_previous = nullptr;
    context_.context()->IAGetInputLayout(&actual_previous);
    const void *actual_previous_address = actual_previous;
    const bool previous_matches =
        actual_previous == layouts[transition.previous_layout_index].get();
    if (actual_previous)
      actual_previous->Release();

    context_.context()->IASetInputLayout(
        layouts[transition.replacement_layout_index].get());
    ID3D11InputLayout *actual_replacement = nullptr;
    context_.context()->IAGetInputLayout(&actual_replacement);
    const void *actual_replacement_address = actual_replacement;
    const bool replacement_matches =
        actual_replacement ==
        layouts[transition.replacement_layout_index].get();
    if (actual_replacement)
      actual_replacement->Release();

    context_.context()->IASetInputLayout(nullptr);
    ID3D11InputLayout *actual_unbound = nullptr;
    context_.context()->IAGetInputLayout(&actual_unbound);
    const void *actual_unbound_address = actual_unbound;
    const bool unbound_matches = actual_unbound == nullptr;
    if (actual_unbound)
      actual_unbound->Release();

    if (previous_matches && replacement_matches && unbound_matches)
      continue;

    const auto case_id =
        dxmt::test::LogicalCaseId(kInputLayoutBindingCases.family(), logical);
    ADD_FAILURE()
        << "LogicalCaseId: " << case_id << '\n'
        << "Class: "
        << dxmt::test::TestClassName(
               kInputLayoutBindingCases.family().traits.test_class)
        << '\n'
        << "Requirements: feature_level=11_0 shader_model=5_0 "
           "queue=Immediate capability=D3DCompile,CreateInputLayout,"
           "IASetInputLayout,IAGetInputLayout,ComReferenceState\n"
        << "ExecutionPath: "
        << dxmt::test::ExecutionPathName(
               kInputLayoutBindingCases.family().traits.execution_path)
        << '\n'
        << "Parameters: logical=" << logical
        << " previous_layout_index=" << transition.previous_layout_index
        << " previous_byte_offset="
        << byte_offsets[transition.previous_layout_index]
        << " replacement_layout_index=" << transition.replacement_layout_index
        << " replacement_byte_offset="
        << byte_offsets[transition.replacement_layout_index]
        << " selected_cases=" << selected_cases.size() << '\n'
        << "Observed: expected_previous="
        << layouts[transition.previous_layout_index].get()
        << " actual_previous=" << actual_previous_address
        << " expected_replacement="
        << layouts[transition.replacement_layout_index].get()
        << " actual_replacement=" << actual_replacement_address
        << " unbound_layout=" << actual_unbound_address << '\n'
        << "Replay: --dxmt-case-id=" << case_id;
    break;
  }

  context_.context()->IASetInputLayout(nullptr);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
