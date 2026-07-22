#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <array>
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
     {"11_0", "None", "Immediate",
      "Context1,CSSetConstantBuffers1,CSGetConstantBuffers1,ComReferenceState"},
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

const dxmt::test::LogicalCaseFamilyRegistration
    kGraphicsConstantBufferRangeCases(
        "D3D11ConstantBufferRangeBindingMatrixSpec."
        "RoundTrips4096FiveGraphicsStageWindowCombinations",
        "D3D11.GraphicsConstantBuffer1.Window.", kConstantBufferRangeCaseCount,
        4,
        {dxmt::test::TestClass::Conformance,
         dxmt::test::ExecutionPath::Auto,
         {"11_0", "None", "Immediate",
          "Context1,VSSetConstantBuffers1,VSGetConstantBuffers1,"
          "PSSetConstantBuffers1,PSGetConstantBuffers1,"
          "GSSetConstantBuffers1,GSGetConstantBuffers1,"
          "HSSetConstantBuffers1,HSGetConstantBuffers1,"
          "DSSetConstantBuffers1,DSGetConstantBuffers1,ComReferenceState"},
         dxmt::test::kResourceTestCost,
         "one maximum-size 64 KiB constant buffer and an "
         "ID3D11DeviceContext1 immediate context",
         "bind five independently permuted aligned windows at varying slots "
         "across every graphics shader stage, query every stage while all "
         "are simultaneously bound, then unbind them",
         "every getter returns the stage-local COM object, FirstConstant and "
         "NumConstants without cross-stage contamination, then null after "
         "unbinding",
         "logical ID, selected-case count, failing stage and slot, requested "
         "and returned range, expected and actual addresses, unbind state, "
         "and exact replay argument"});

const dxmt::test::TestCostRegistration kGraphicsConstantBufferRangeCost(
    "D3D11ConstantBufferRangeBindingMatrixSpec."
    "RoundTrips4096FiveGraphicsStageWindowCombinations",
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

enum class GraphicsShaderStage : UINT {
  Vertex,
  Pixel,
  Geometry,
  Hull,
  Domain,
  Count
};

constexpr UINT kGraphicsShaderStageCount =
    static_cast<UINT>(GraphicsShaderStage::Count);

const char *StageName(GraphicsShaderStage stage) {
  switch (stage) {
  case GraphicsShaderStage::Vertex:
    return "VS";
  case GraphicsShaderStage::Pixel:
    return "PS";
  case GraphicsShaderStage::Geometry:
    return "GS";
  case GraphicsShaderStage::Hull:
    return "HS";
  case GraphicsShaderStage::Domain:
    return "DS";
  case GraphicsShaderStage::Count:
    break;
  }
  return "unknown";
}

void SetConstantBufferRange(ID3D11DeviceContext1 *context,
                            GraphicsShaderStage stage, UINT slot,
                            ID3D11Buffer *const *buffer,
                            const UINT *first_constant,
                            const UINT *num_constants) {
  switch (stage) {
  case GraphicsShaderStage::Vertex:
    context->VSSetConstantBuffers1(slot, 1, buffer, first_constant,
                                   num_constants);
    return;
  case GraphicsShaderStage::Pixel:
    context->PSSetConstantBuffers1(slot, 1, buffer, first_constant,
                                   num_constants);
    return;
  case GraphicsShaderStage::Geometry:
    context->GSSetConstantBuffers1(slot, 1, buffer, first_constant,
                                   num_constants);
    return;
  case GraphicsShaderStage::Hull:
    context->HSSetConstantBuffers1(slot, 1, buffer, first_constant,
                                   num_constants);
    return;
  case GraphicsShaderStage::Domain:
    context->DSSetConstantBuffers1(slot, 1, buffer, first_constant,
                                   num_constants);
    return;
  case GraphicsShaderStage::Count:
    return;
  }
}

void GetConstantBufferRange(ID3D11DeviceContext1 *context,
                            GraphicsShaderStage stage, UINT slot,
                            ID3D11Buffer **buffer, UINT *first_constant,
                            UINT *num_constants) {
  switch (stage) {
  case GraphicsShaderStage::Vertex:
    context->VSGetConstantBuffers1(slot, 1, buffer, first_constant,
                                   num_constants);
    return;
  case GraphicsShaderStage::Pixel:
    context->PSGetConstantBuffers1(slot, 1, buffer, first_constant,
                                   num_constants);
    return;
  case GraphicsShaderStage::Geometry:
    context->GSGetConstantBuffers1(slot, 1, buffer, first_constant,
                                   num_constants);
    return;
  case GraphicsShaderStage::Hull:
    context->HSGetConstantBuffers1(slot, 1, buffer, first_constant,
                                   num_constants);
    return;
  case GraphicsShaderStage::Domain:
    context->DSGetConstantBuffers1(slot, 1, buffer, first_constant,
                                   num_constants);
    return;
  case GraphicsShaderStage::Count:
    return;
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

TEST_F(D3D11ConstantBufferRangeBindingMatrixSpec,
       RoundTrips4096FiveGraphicsStageWindowCombinations) {
  std::vector<std::uint32_t> selected_cases;
  selected_cases.reserve(kConstantBufferRangeCaseCount);
  for (std::uint32_t logical = 0; logical < kConstantBufferRangeCaseCount;
       ++logical) {
    if (dxmt::test::LogicalCaseSelected(
            kGraphicsConstantBufferRangeCases.family(), logical))
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

  constexpr std::array<std::uint32_t, kGraphicsShaderStageCount> multipliers = {
      1u, 5u, 9u, 13u, 17u};
  constexpr std::array<std::uint32_t, kGraphicsShaderStageCount> offsets = {
      0u, 0x11du, 0x233u, 0x34bu, 0x461u};

  RecordProperty("logical_cases_executed", selected_cases.size());
  RecordProperty("logical_case_prefix",
                 kGraphicsConstantBufferRangeCases.family().case_id_prefix);
  for (const std::uint32_t logical : selected_cases) {
    std::array<ConstantBufferRange, kGraphicsShaderStageCount> requested;
    std::array<UINT, kGraphicsShaderStageCount> slots;
    for (UINT stage_index = 0; stage_index < kGraphicsShaderStageCount;
         ++stage_index) {
      const std::uint32_t permuted =
          (logical * multipliers[stage_index] + offsets[stage_index]) & 4095u;
      requested[stage_index] = RangeForCase(permuted);
      slots[stage_index] = (logical + stage_index * 3u) %
                           D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT;
      ID3D11Buffer *bound_buffer = buffer.get();
      SetConstantBufferRange(context1_.get(),
                             static_cast<GraphicsShaderStage>(stage_index),
                             slots[stage_index], &bound_buffer,
                             &requested[stage_index].first_constant,
                             &requested[stage_index].num_constants);
    }

    std::array<const void *, kGraphicsShaderStageCount> returned_addresses = {};
    std::array<UINT, kGraphicsShaderStageCount> returned_first = {};
    std::array<UINT, kGraphicsShaderStageCount> returned_count = {};
    std::array<bool, kGraphicsShaderStageCount> bound_matches = {};
    bool all_matches = true;
    for (UINT stage_index = 0; stage_index < kGraphicsShaderStageCount;
         ++stage_index) {
      ID3D11Buffer *returned_buffer = nullptr;
      returned_first[stage_index] = ~0u;
      returned_count[stage_index] = ~0u;
      GetConstantBufferRange(
          context1_.get(), static_cast<GraphicsShaderStage>(stage_index),
          slots[stage_index], &returned_buffer, &returned_first[stage_index],
          &returned_count[stage_index]);
      returned_addresses[stage_index] = returned_buffer;
      bound_matches[stage_index] =
          returned_buffer == buffer.get() &&
          returned_first[stage_index] ==
              requested[stage_index].first_constant &&
          returned_count[stage_index] == requested[stage_index].num_constants;
      all_matches = all_matches && bound_matches[stage_index];
      if (returned_buffer)
        returned_buffer->Release();
    }

    std::array<const void *, kGraphicsShaderStageCount> unbound_addresses = {};
    std::array<bool, kGraphicsShaderStageCount> unbound_matches = {};
    for (UINT stage_index = 0; stage_index < kGraphicsShaderStageCount;
         ++stage_index) {
      ID3D11Buffer *null_buffer = nullptr;
      SetConstantBufferRange(
          context1_.get(), static_cast<GraphicsShaderStage>(stage_index),
          slots[stage_index], &null_buffer, nullptr, nullptr);
      ID3D11Buffer *after_unbind = nullptr;
      GetConstantBufferRange(
          context1_.get(), static_cast<GraphicsShaderStage>(stage_index),
          slots[stage_index], &after_unbind, nullptr, nullptr);
      unbound_addresses[stage_index] = after_unbind;
      unbound_matches[stage_index] = after_unbind == nullptr;
      all_matches = all_matches && unbound_matches[stage_index];
      if (after_unbind)
        after_unbind->Release();
    }

    if (all_matches)
      continue;

    UINT failing_stage_index = 0;
    while (failing_stage_index + 1u < kGraphicsShaderStageCount &&
           bound_matches[failing_stage_index] &&
           unbound_matches[failing_stage_index])
      ++failing_stage_index;
    const auto failing_stage =
        static_cast<GraphicsShaderStage>(failing_stage_index);
    const auto case_id = dxmt::test::LogicalCaseId(
        kGraphicsConstantBufferRangeCases.family(), logical);
    ADD_FAILURE()
        << "LogicalCaseId: " << case_id << '\n'
        << "Class: "
        << dxmt::test::TestClassName(
               kGraphicsConstantBufferRangeCases.family().traits.test_class)
        << '\n'
        << "Requirements: feature_level=11_0 queue=Immediate "
           "capability=Context1,GraphicsSetConstantBuffers1,"
           "GraphicsGetConstantBuffers1,ComReferenceState\n"
        << "ExecutionPath: "
        << dxmt::test::ExecutionPathName(
               kGraphicsConstantBufferRangeCases.family().traits.execution_path)
        << '\n'
        << "Parameters: logical=" << logical
        << " stage=" << StageName(failing_stage)
        << " slot=" << slots[failing_stage_index]
        << " first_constant=" << requested[failing_stage_index].first_constant
        << " num_constants=" << requested[failing_stage_index].num_constants
        << " selected_cases=" << selected_cases.size() << '\n'
        << "Observed: returned_first=" << returned_first[failing_stage_index]
        << " returned_count=" << returned_count[failing_stage_index]
        << " expected_buffer=" << buffer.get()
        << " returned_buffer=" << returned_addresses[failing_stage_index]
        << " unbound_buffer=" << unbound_addresses[failing_stage_index] << '\n'
        << "Replay: --dxmt-case-id=" << case_id;
    break;
  }

  for (UINT stage_index = 0; stage_index < kGraphicsShaderStageCount;
       ++stage_index) {
    ID3D11Buffer *null_buffer = nullptr;
    SetConstantBufferRange(context1_.get(),
                           static_cast<GraphicsShaderStage>(stage_index), 0,
                           &null_buffer, nullptr, nullptr);
  }
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
