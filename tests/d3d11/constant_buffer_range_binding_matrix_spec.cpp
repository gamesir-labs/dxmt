#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <cstdint>
#include <vector>

// Public-D3D11.1 constant-buffer window state coverage. The 4096 logical cases
// are mapped injectively to aligned FirstConstant / NumConstants pairs that
// remain inside one maximum-size D3D11 constant buffer.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

constexpr std::uint32_t kConstantBufferRangeCaseCount = 4096;
constexpr UINT kConstantsPerAlignmentBlock = 16;
constexpr UINT kConstantBufferConstants = 4096;

const dxmt::test::LogicalCaseFamilyRegistration kConstantBufferRangeCases(
    "D3D11ConstantBufferRangeBindingMatrixSpec."
    "RoundTrips4096ComputeWindowsAndClearsState",
    "D3D11.CSConstantBuffer1.Window.", kConstantBufferRangeCaseCount, 4,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "Immediate", "Context1",
      "CSSetConstantBuffers1,CSGetConstantBuffers1,ComReferenceState"},
     dxmt::test::kResourceTestCost,
     "one maximum-size 64 KiB constant buffer and an "
     "ID3D11DeviceContext1 immediate context",
     "bind each selected unique 16-constant-aligned compute-stage window, read "
     "its buffer and range through CSGetConstantBuffers1, then unbind it",
     "the getter returns the exact COM object, FirstConstant and NumConstants, "
     "and the slot is null after unbinding",
     "logical ID, selected-case count, requested and returned range, expected "
     "and actual buffer addresses, failing phase, and exact replay argument"});

const dxmt::test::TestCostRegistration
    kConstantBufferRangeCost("D3D11ConstantBufferRangeBindingMatrixSpec."
                             "RoundTrips4096ComputeWindowsAndClearsState",
                             dxmt::test::kResourceTestCost);

struct ConstantBufferRange {
  UINT first_constant;
  UINT num_constants;
};

ConstantBufferRange RangeForCase(std::uint32_t logical) {
  std::uint32_t remaining = logical;
  for (UINT first_block = 0;; ++first_block) {
    const UINT available_blocks =
        kConstantBufferConstants / kConstantsPerAlignmentBlock - first_block;
    if (remaining < available_blocks) {
      return {first_block * kConstantsPerAlignmentBlock,
              (remaining + 1u) * kConstantsPerAlignmentBlock};
    }
    remaining -= available_blocks;
  }
}

class D3D11ConstantBufferRangeBindingMatrixSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_NE(context_.context(), nullptr);
    ASSERT_EQ(context_.context()->QueryInterface(
                  __uuidof(ID3D11DeviceContext1),
                  reinterpret_cast<void **>(context1_.put())),
              S_OK);
  }

  D3D11TestContext context_;
  ComPtr<ID3D11DeviceContext1> context1_;
};

TEST_F(D3D11ConstantBufferRangeBindingMatrixSpec,
       RoundTrips4096ComputeWindowsAndClearsState) {
  std::vector<std::uint32_t> selected_cases;
  selected_cases.reserve(kConstantBufferRangeCaseCount);
  for (std::uint32_t logical = 0; logical < kConstantBufferRangeCaseCount;
       ++logical) {
    if (dxmt::test::LogicalCaseSelected(kConstantBufferRangeCases.family(),
                                        logical))
      selected_cases.push_back(logical);
  }
  ASSERT_FALSE(selected_cases.empty());

  D3D11_BUFFER_DESC buffer_desc = {};
  buffer_desc.ByteWidth = kConstantBufferConstants * 4u * sizeof(std::uint32_t);
  buffer_desc.Usage = D3D11_USAGE_DEFAULT;
  buffer_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  ComPtr<ID3D11Buffer> buffer;
  ASSERT_EQ(
      context_.device()->CreateBuffer(&buffer_desc, nullptr, buffer.put()),
      S_OK);

  RecordProperty("logical_cases_executed", selected_cases.size());
  RecordProperty("logical_case_prefix",
                 kConstantBufferRangeCases.family().case_id_prefix);
  for (const std::uint32_t logical : selected_cases) {
    const ConstantBufferRange requested = RangeForCase(logical);
    ID3D11Buffer *bound_buffer = buffer.get();
    context1_->CSSetConstantBuffers1(0, 1, &bound_buffer,
                                     &requested.first_constant,
                                     &requested.num_constants);

    ID3D11Buffer *returned_buffer = nullptr;
    UINT returned_first = ~0u;
    UINT returned_count = ~0u;
    context1_->CSGetConstantBuffers1(0, 1, &returned_buffer, &returned_first,
                                     &returned_count);
    const void *returned_address = returned_buffer;
    const bool range_matches = returned_first == requested.first_constant &&
                               returned_count == requested.num_constants;
    const bool buffer_matches = returned_buffer == buffer.get();
    if (returned_buffer)
      returned_buffer->Release();

    ID3D11Buffer *null_buffer = nullptr;
    context1_->CSSetConstantBuffers1(0, 1, &null_buffer, nullptr, nullptr);
    ID3D11Buffer *after_unbind = nullptr;
    context1_->CSGetConstantBuffers1(0, 1, &after_unbind, nullptr, nullptr);
    const bool unbound = after_unbind == nullptr;
    if (after_unbind)
      after_unbind->Release();

    if (range_matches && buffer_matches && unbound)
      continue;

    const auto case_id =
        dxmt::test::LogicalCaseId(kConstantBufferRangeCases.family(), logical);
    ADD_FAILURE()
        << "LogicalCaseId: " << case_id << '\n'
        << "Class: "
        << dxmt::test::TestClassName(
               kConstantBufferRangeCases.family().traits.test_class)
        << '\n'
        << "Requirements: feature_level=11_0 queue=Immediate "
           "capability=Context1,CSSetConstantBuffers1,CSGetConstantBuffers1,"
           "ComReferenceState\n"
        << "ExecutionPath: "
        << dxmt::test::ExecutionPathName(
               kConstantBufferRangeCases.family().traits.execution_path)
        << '\n'
        << "Parameters: logical=" << logical
        << " first_constant=" << requested.first_constant
        << " num_constants=" << requested.num_constants
        << " selected_cases=" << selected_cases.size() << '\n'
        << "Observed: returned_first=" << returned_first
        << " returned_count=" << returned_count
        << " expected_buffer=" << buffer.get()
        << " returned_buffer=" << returned_address << " unbound=" << unbound
        << '\n'
        << "Replay: --dxmt-case-id=" << case_id;
    break;
  }
  ID3D11Buffer *null_buffer = nullptr;
  context1_->CSSetConstantBuffers1(0, 1, &null_buffer, nullptr, nullptr);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
