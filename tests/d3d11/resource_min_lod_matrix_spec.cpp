#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <cstdint>
#include <cstring>
#include <iomanip>
#include <vector>

// Public-D3D11 per-resource minimum-LOD state coverage. Values are binary
// fractions so the requested and returned FLOAT bit patterns can be compared
// exactly on both native D3D11 and DXMT.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

constexpr std::uint32_t kResourceMinLodCaseCount = 4096;
constexpr float kLodValueScale = 512.0f;
constexpr float kGuardLod = 7.5f;

const dxmt::test::LogicalCaseFamilyRegistration kResourceMinLodCases(
    "D3D11ResourceMinLodMatrixSpec."
    "RoundTrips4096ClampValuesAndIsolatesResources",
    "D3D11.ResourceMinLOD.Value.", kResourceMinLodCaseCount, 4,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "Immediate",
      "SetResourceMinLOD,GetResourceMinLOD,ResourceClamp"},
     dxmt::test::kResourceTestCost,
     "two independent 256x1 textures with nine mip levels and "
     "D3D11_RESOURCE_MISC_RESOURCE_CLAMP",
     "set each selected exact binary-fraction LOD on the primary texture and "
     "read both textures through ID3D11DeviceContext",
     "the primary getter returns the requested FLOAT bit pattern, the guard "
     "texture remains at 7.5, and both resources can be reset to zero",
     "logical ID, requested and returned LOD values and bit patterns, "
     "resource identity, selected-case count, and exact replay argument"});

const dxmt::test::TestCostRegistration
    kResourceMinLodCost("D3D11ResourceMinLodMatrixSpec."
                        "RoundTrips4096ClampValuesAndIsolatesResources",
                        dxmt::test::kResourceTestCost);

std::uint32_t FloatBits(float value) {
  std::uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

float RequestedLod(std::uint32_t logical) {
  return static_cast<float>(logical) / kLodValueScale;
}

class D3D11ResourceMinLodMatrixSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_NE(context_.context(), nullptr);
  }

  D3D11TestContext context_;
};

TEST_F(D3D11ResourceMinLodMatrixSpec,
       RoundTrips4096ClampValuesAndIsolatesResources) {
  std::vector<std::uint32_t> selected_cases;
  selected_cases.reserve(kResourceMinLodCaseCount);
  for (std::uint32_t logical = 0; logical < kResourceMinLodCaseCount;
       ++logical) {
    if (dxmt::test::LogicalCaseSelected(kResourceMinLodCases.family(), logical))
      selected_cases.push_back(logical);
  }
  ASSERT_FALSE(selected_cases.empty());

  D3D11_TEXTURE2D_DESC texture_desc = {};
  texture_desc.Width = 256;
  texture_desc.Height = 1;
  texture_desc.MipLevels = 9;
  texture_desc.ArraySize = 1;
  texture_desc.Format = DXGI_FORMAT_R8_UNORM;
  texture_desc.SampleDesc.Count = 1;
  texture_desc.Usage = D3D11_USAGE_DEFAULT;
  texture_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  texture_desc.MiscFlags = D3D11_RESOURCE_MISC_RESOURCE_CLAMP;

  ComPtr<ID3D11Texture2D> primary;
  ASSERT_EQ(
      context_.device()->CreateTexture2D(&texture_desc, nullptr, primary.put()),
      S_OK);
  ComPtr<ID3D11Texture2D> guard;
  ASSERT_EQ(
      context_.device()->CreateTexture2D(&texture_desc, nullptr, guard.put()),
      S_OK);

  ASSERT_EQ(FloatBits(context_.context()->GetResourceMinLOD(primary.get())),
            FloatBits(0.0f));
  ASSERT_EQ(FloatBits(context_.context()->GetResourceMinLOD(guard.get())),
            FloatBits(0.0f));
  context_.context()->SetResourceMinLOD(guard.get(), kGuardLod);
  ASSERT_EQ(FloatBits(context_.context()->GetResourceMinLOD(guard.get())),
            FloatBits(kGuardLod));

  RecordProperty("logical_cases_executed", selected_cases.size());
  RecordProperty("logical_case_prefix",
                 kResourceMinLodCases.family().case_id_prefix);
  for (const std::uint32_t logical : selected_cases) {
    const float requested = RequestedLod(logical);
    context_.context()->SetResourceMinLOD(primary.get(), requested);
    const float actual_primary =
        context_.context()->GetResourceMinLOD(primary.get());
    const float actual_guard =
        context_.context()->GetResourceMinLOD(guard.get());

    const bool primary_matches =
        FloatBits(actual_primary) == FloatBits(requested);
    const bool guard_matches = FloatBits(actual_guard) == FloatBits(kGuardLod);
    if (primary_matches && guard_matches)
      continue;

    const bool primary_failed = !primary_matches;
    const float expected_value = primary_failed ? requested : kGuardLod;
    const float actual_value = primary_failed ? actual_primary : actual_guard;
    const auto case_id =
        dxmt::test::LogicalCaseId(kResourceMinLodCases.family(), logical);
    ADD_FAILURE()
        << "LogicalCaseId: " << case_id << '\n'
        << "Class: "
        << dxmt::test::TestClassName(
               kResourceMinLodCases.family().traits.test_class)
        << '\n'
        << "Requirements: feature_level=11_0 queue=Immediate "
           "capability=SetResourceMinLOD,GetResourceMinLOD,ResourceClamp\n"
        << "ExecutionPath: "
        << dxmt::test::ExecutionPathName(
               kResourceMinLodCases.family().traits.execution_path)
        << '\n'
        << "Parameters: logical=" << logical << " requested_lod=" << requested
        << " requested_bits=0x" << std::hex << FloatBits(requested) << std::dec
        << " resource=" << (primary_failed ? "primary" : "guard")
        << " selected_cases=" << selected_cases.size() << '\n'
        << "GpuCaseResult: status=1 first_mismatch_index=" << logical
        << " expected_lod=" << expected_value << " expected_bits=0x" << std::hex
        << FloatBits(expected_value) << " actual_lod=" << std::dec
        << actual_value << " actual_bits=0x" << std::hex
        << FloatBits(actual_value) << std::dec << '\n'
        << "Replay: --dxmt-case-id=" << case_id;
    break;
  }

  context_.context()->SetResourceMinLOD(primary.get(), 0.0f);
  context_.context()->SetResourceMinLOD(guard.get(), 0.0f);
  EXPECT_EQ(FloatBits(context_.context()->GetResourceMinLOD(primary.get())),
            FloatBits(0.0f));
  EXPECT_EQ(FloatBits(context_.context()->GetResourceMinLOD(guard.get())),
            FloatBits(0.0f));
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
