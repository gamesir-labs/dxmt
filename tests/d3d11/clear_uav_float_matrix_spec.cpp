#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <vector>

// Batched public-D3D11 ClearUnorderedAccessViewFloat coverage. Every logical
// case clears a one-element typed-buffer UAV view so view-range isolation and
// all four FLOAT clear components are observable in one staging readback.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

constexpr std::uint32_t kClearUavFloatCaseCount = 4096;
constexpr std::uint32_t kComponentsPerElement = 4;
constexpr UINT kElementStride = kComponentsPerElement * sizeof(std::uint32_t);

const dxmt::test::LogicalCaseFamilyRegistration kClearUavFloatCases(
    "D3D11ClearUavFloatMatrixSpec."
    "Clears4096TypedBufferViewsWithoutRangeBleed",
    "D3D11.ClearUAVFloat.BufferElement.", kClearUavFloatCaseCount, 4,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "Immediate",
      "ClearUnorderedAccessViewFloat,TypedBufferUAV,CopyResource,StagingMap"},
     dxmt::test::kGpuBatchTestCost,
     "a poison-initialized R32G32B32A32_FLOAT buffer with one-element UAV "
     "views created for selected logical cases",
     "clear every selected UAV view with four logical-case-specific exact "
     "binary FLOAT values",
     "all four selected components match the requested FLOAT bit patterns "
     "and every unselected element remains poison",
     "logical ID, selection state, first element, component, expected and "
     "actual FLOAT values and bit patterns, edge observations, and exact "
     "replay argument"});

const dxmt::test::TestCostRegistration
    kClearUavFloatCost("D3D11ClearUavFloatMatrixSpec."
                       "Clears4096TypedBufferViewsWithoutRangeBleed",
                       dxmt::test::kGpuBatchTestCost);

using ClearBits = std::array<std::uint32_t, kComponentsPerElement>;
using ClearValues = std::array<float, kComponentsPerElement>;

std::uint32_t FloatBits(float value) {
  std::uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

float FloatFromBits(std::uint32_t bits) {
  float value = 0.0f;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

ClearValues ValuesForCase(std::uint32_t logical) {
  const float value = static_cast<float>(logical);
  return {value + 0.25f, -value - 0.5f,
          static_cast<float>(logical & 255u) / 16.0f,
          static_cast<float>((logical * 17u) & 4095u) + 0.75f};
}

ClearBits ExpectedBits(std::uint32_t logical) {
  const auto values = ValuesForCase(logical);
  return {FloatBits(values[0]), FloatBits(values[1]), FloatBits(values[2]),
          FloatBits(values[3])};
}

ClearBits PoisonBits(std::uint32_t logical) {
  const auto expected = ExpectedBits(logical);
  return {expected[0] ^ 0x13579bdfu, expected[1] ^ 0x2468ace0u,
          expected[2] ^ 0xdeadbeefu, expected[3] ^ 0xc001d00du};
}

class D3D11ClearUavFloatMatrixSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_NE(context_.context(), nullptr);
  }

  D3D11TestContext context_;
};

TEST_F(D3D11ClearUavFloatMatrixSpec,
       Clears4096TypedBufferViewsWithoutRangeBleed) {
  std::vector<ClearBits> expected(kClearUavFloatCaseCount);
  std::vector<ClearBits> initial(kClearUavFloatCaseCount);
  std::vector<bool> selected(kClearUavFloatCaseCount, false);
  std::vector<std::uint32_t> selected_cases;
  selected_cases.reserve(kClearUavFloatCaseCount);
  for (std::uint32_t logical = 0; logical < kClearUavFloatCaseCount;
       ++logical) {
    expected[logical] = ExpectedBits(logical);
    initial[logical] = PoisonBits(logical);
    if (dxmt::test::LogicalCaseSelected(kClearUavFloatCases.family(),
                                        logical)) {
      selected[logical] = true;
      selected_cases.push_back(logical);
    }
  }
  ASSERT_FALSE(selected_cases.empty());

  UINT format_support = 0;
  ASSERT_EQ(context_.device()->CheckFormatSupport(
                DXGI_FORMAT_R32G32B32A32_FLOAT, &format_support),
            S_OK);
  ASSERT_NE(format_support & D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW,
            0u);

  D3D11_BUFFER_DESC buffer_desc = {};
  buffer_desc.ByteWidth = kClearUavFloatCaseCount * kElementStride;
  buffer_desc.Usage = D3D11_USAGE_DEFAULT;
  buffer_desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
  D3D11_SUBRESOURCE_DATA buffer_data = {};
  buffer_data.pSysMem = initial.data();
  ComPtr<ID3D11Buffer> buffer;
  ASSERT_EQ(
      context_.device()->CreateBuffer(&buffer_desc, &buffer_data, buffer.put()),
      S_OK);

  for (const std::uint32_t logical : selected_cases) {
    D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
    uav_desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    uav_desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    uav_desc.Buffer.FirstElement = logical;
    uav_desc.Buffer.NumElements = 1;
    ComPtr<ID3D11UnorderedAccessView> uav;
    ASSERT_EQ(context_.device()->CreateUnorderedAccessView(
                  buffer.get(), &uav_desc, uav.put()),
              S_OK)
        << "logical=" << logical;

    const auto values = ValuesForCase(logical);
    context_.context()->ClearUnorderedAccessViewFloat(uav.get(), values.data());
  }

  D3D11_BUFFER_DESC staging_desc = buffer_desc;
  staging_desc.Usage = D3D11_USAGE_STAGING;
  staging_desc.BindFlags = 0;
  staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Buffer> staging;
  ASSERT_EQ(
      context_.device()->CreateBuffer(&staging_desc, nullptr, staging.put()),
      S_OK);
  context_.context()->CopyResource(staging.get(), buffer.get());
  context_.context()->Flush();

  D3D11_MAPPED_SUBRESOURCE mapped = {};
  ASSERT_EQ(
      context_.context()->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped),
      S_OK);
  const auto *actual = static_cast<const std::uint32_t *>(mapped.pData);
  RecordProperty("logical_cases_executed", selected_cases.size());
  RecordProperty("logical_case_prefix",
                 kClearUavFloatCases.family().case_id_prefix);
  bool found_mismatch = false;
  for (std::uint32_t logical = 0; logical < kClearUavFloatCaseCount;
       ++logical) {
    const auto &desired =
        selected[logical] ? expected[logical] : initial[logical];
    for (std::uint32_t component = 0; component < kComponentsPerElement;
         ++component) {
      const std::uint32_t actual_bits =
          actual[logical * kComponentsPerElement + component];
      if (actual_bits == desired[component])
        continue;

      const auto case_id =
          dxmt::test::LogicalCaseId(kClearUavFloatCases.family(), logical);
      const auto replay_case_id =
          selected[logical]
              ? case_id
              : dxmt::test::LogicalCaseId(kClearUavFloatCases.family(),
                                          selected_cases.front());
      ADD_FAILURE()
          << "LogicalCaseId: " << case_id << '\n'
          << "Class: "
          << dxmt::test::TestClassName(
                 kClearUavFloatCases.family().traits.test_class)
          << '\n'
          << "Requirements: feature_level=11_0 queue=Immediate "
             "capability=ClearUnorderedAccessViewFloat,TypedBufferUAV,"
             "CopyResource,StagingMap\n"
          << "ExecutionPath: "
          << dxmt::test::ExecutionPathName(
                 kClearUavFloatCases.family().traits.execution_path)
          << '\n'
          << "Parameters: logical=" << logical
          << " selected=" << (selected[logical] ? "true" : "false")
          << " first_element=" << logical
          << " num_elements=1 component=" << component
          << " expected_float=" << FloatFromBits(desired[component])
          << " actual_float=" << FloatFromBits(actual_bits)
          << " observed_edge_bits=(0x" << std::hex << actual[0] << ",0x"
          << actual[kComponentsPerElement] << ",0x"
          << actual[2u * kComponentsPerElement] << ",0x"
          << actual[(kClearUavFloatCaseCount - 1u) * kComponentsPerElement]
          << ")" << std::dec << '\n'
          << "GpuCaseResult: status=" << (selected[logical] ? 1u : 2u)
          << " first_mismatch_index=" << logical << " expected_bits=0x"
          << std::hex << desired[component] << " actual_bits=0x" << actual_bits
          << std::dec << '\n'
          << "Replay: --dxmt-case-id=" << replay_case_id;
      found_mismatch = true;
      break;
    }
    if (found_mismatch)
      break;
  }
  context_.context()->Unmap(staging.get(), 0);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
