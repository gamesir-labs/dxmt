#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <cstdint>
#include <vector>

// Public-D3D11 IASetIndexBuffer / IAGetIndexBuffer state coverage. Every
// logical case uses an aligned byte offset and alternates the two legal D3D11
// index formats, then verifies complete unbinding.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

constexpr std::uint32_t kIndexBufferBindingCaseCount = 4096;
constexpr UINT kIndexBufferSize = kIndexBufferBindingCaseCount * 4u;

const dxmt::test::LogicalCaseFamilyRegistration kIndexBufferBindingCases(
    "D3D11IndexBufferBindingMatrixSpec."
    "RoundTrips4096OffsetsFormatsAndClearsState",
    "D3D11.IAGetIndexBuffer.Binding.", kIndexBufferBindingCaseCount, 4,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "Immediate",
      "IASetIndexBuffer,IAGetIndexBuffer,ComReferenceState"},
     dxmt::test::kResourceTestCost,
     "one 16 KiB test-local index buffer that accommodates every aligned "
     "R16_UINT and R32_UINT byte offset",
     "bind every selected offset and alternating index format, read the state, "
     "release the getter reference, then bind null with UNKNOWN format",
     "the getter returns the exact buffer, format and byte offset, followed by "
     "a null buffer with UNKNOWN format and zero offset",
     "logical ID, selected-case count, requested and returned format and byte "
     "offset, expected and actual addresses, phase, and exact replay "
     "argument"});

const dxmt::test::TestCostRegistration
    kIndexBufferBindingCost("D3D11IndexBufferBindingMatrixSpec."
                            "RoundTrips4096OffsetsFormatsAndClearsState",
                            dxmt::test::kResourceTestCost);

struct IndexBinding {
  DXGI_FORMAT format;
  UINT offset;
};

IndexBinding BindingForCase(std::uint32_t logical) {
  if ((logical & 1u) != 0)
    return {DXGI_FORMAT_R32_UINT, logical * 4u};
  return {DXGI_FORMAT_R16_UINT, logical * 2u};
}

class D3D11IndexBufferBindingMatrixSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_NE(context_.context(), nullptr);
  }

  D3D11TestContext context_;
};

TEST_F(D3D11IndexBufferBindingMatrixSpec,
       RoundTrips4096OffsetsFormatsAndClearsState) {
  std::vector<std::uint32_t> selected_cases;
  selected_cases.reserve(kIndexBufferBindingCaseCount);
  for (std::uint32_t logical = 0; logical < kIndexBufferBindingCaseCount;
       ++logical) {
    if (dxmt::test::LogicalCaseSelected(kIndexBufferBindingCases.family(),
                                        logical))
      selected_cases.push_back(logical);
  }
  ASSERT_FALSE(selected_cases.empty());

  D3D11_BUFFER_DESC buffer_desc = {};
  buffer_desc.ByteWidth = kIndexBufferSize;
  buffer_desc.Usage = D3D11_USAGE_DEFAULT;
  buffer_desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
  ComPtr<ID3D11Buffer> buffer;
  ASSERT_EQ(
      context_.device()->CreateBuffer(&buffer_desc, nullptr, buffer.put()),
      S_OK);

  RecordProperty("logical_cases_executed", selected_cases.size());
  RecordProperty("logical_case_prefix",
                 kIndexBufferBindingCases.family().case_id_prefix);
  for (const std::uint32_t logical : selected_cases) {
    const IndexBinding requested = BindingForCase(logical);
    context_.context()->IASetIndexBuffer(buffer.get(), requested.format,
                                         requested.offset);

    ID3D11Buffer *returned_buffer = nullptr;
    DXGI_FORMAT returned_format = DXGI_FORMAT_UNKNOWN;
    UINT returned_offset = ~0u;
    context_.context()->IAGetIndexBuffer(&returned_buffer, &returned_format,
                                         &returned_offset);
    const void *returned_address = returned_buffer;
    const bool bound_matches = returned_buffer == buffer.get() &&
                               returned_format == requested.format &&
                               returned_offset == requested.offset;
    if (returned_buffer)
      returned_buffer->Release();

    context_.context()->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
    ID3D11Buffer *after_unbind = nullptr;
    DXGI_FORMAT unbound_format = DXGI_FORMAT_R32_UINT;
    UINT unbound_offset = ~0u;
    context_.context()->IAGetIndexBuffer(&after_unbind, &unbound_format,
                                         &unbound_offset);
    const void *unbound_address = after_unbind;
    const bool unbound_matches = after_unbind == nullptr &&
                                 unbound_format == DXGI_FORMAT_UNKNOWN &&
                                 unbound_offset == 0;
    if (after_unbind)
      after_unbind->Release();

    if (bound_matches && unbound_matches)
      continue;

    const auto case_id =
        dxmt::test::LogicalCaseId(kIndexBufferBindingCases.family(), logical);
    ADD_FAILURE()
        << "LogicalCaseId: " << case_id << '\n'
        << "Class: "
        << dxmt::test::TestClassName(
               kIndexBufferBindingCases.family().traits.test_class)
        << '\n'
        << "Requirements: feature_level=11_0 queue=Immediate "
           "capability=IASetIndexBuffer,IAGetIndexBuffer,ComReferenceState\n"
        << "ExecutionPath: "
        << dxmt::test::ExecutionPathName(
               kIndexBufferBindingCases.family().traits.execution_path)
        << '\n'
        << "Parameters: logical=" << logical
        << " requested_format=" << static_cast<unsigned>(requested.format)
        << " requested_offset=" << requested.offset
        << " selected_cases=" << selected_cases.size() << '\n'
        << "Observed: returned_format="
        << static_cast<unsigned>(returned_format)
        << " returned_offset=" << returned_offset
        << " expected_buffer=" << buffer.get()
        << " returned_buffer=" << returned_address
        << " unbound_format=" << static_cast<unsigned>(unbound_format)
        << " unbound_offset=" << unbound_offset
        << " unbound_buffer=" << unbound_address << '\n'
        << "Replay: --dxmt-case-id=" << case_id;
    break;
  }
  context_.context()->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
