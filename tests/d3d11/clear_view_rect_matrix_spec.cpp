#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <vector>

// Batched public-D3D11.1 ClearView rectangle coverage. The full 64x64 target is
// partitioned into 4096 one-pixel logical cases so every rect origin and all
// four float components are checked for under-clear and range bleed.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

constexpr std::uint32_t kClearViewWidth = 64;
constexpr std::uint32_t kClearViewHeight = 64;
constexpr std::uint32_t kClearViewCaseCount =
    kClearViewWidth * kClearViewHeight;
constexpr std::uint32_t kComponentsPerPixel = 4;
constexpr UINT kPixelStride = kComponentsPerPixel * sizeof(std::uint32_t);

const dxmt::test::LogicalCaseFamilyRegistration kClearViewCases(
    "D3D11ClearViewRectMatrixSpec."
    "Clears4096SinglePixelFloatRectsWithoutBleed",
    "D3D11.ClearView.FloatPixel.", kClearViewCaseCount, 4,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "Immediate", "Context1",
      "ClearView,RenderTargetView,CopyResource,StagingMap"},
     dxmt::test::kGpuBatchTestCost,
     "a poison-initialized 64x64 R32G32B32A32_FLOAT render target and its RTV",
     "clear each selected logical pixel through a one-pixel D3D11_RECT with "
     "four case-specific exact binary FLOAT values",
     "all four selected components match the requested bit patterns and every "
     "unselected pixel remains poison",
     "logical ID, selection state, rect coordinates, component, staging row "
     "pitch, expected and actual FLOAT values and bits, and exact replay "
     "argument"});

const dxmt::test::TestCostRegistration
    kClearViewCost("D3D11ClearViewRectMatrixSpec."
                   "Clears4096SinglePixelFloatRectsWithoutBleed",
                   dxmt::test::kGpuBatchTestCost);

using PixelBits = std::array<std::uint32_t, kComponentsPerPixel>;
using PixelValues = std::array<float, kComponentsPerPixel>;

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

PixelValues ValuesForCase(std::uint32_t logical) {
  const float x = static_cast<float>(logical % kClearViewWidth);
  const float y = static_cast<float>(logical / kClearViewWidth);
  return {x + 0.25f, y + 0.5f, -(x + y) - 0.75f,
          static_cast<float>((logical * 13u) & 4095u) + 0.125f};
}

PixelBits ExpectedBits(std::uint32_t logical) {
  const auto values = ValuesForCase(logical);
  return {FloatBits(values[0]), FloatBits(values[1]), FloatBits(values[2]),
          FloatBits(values[3])};
}

PixelBits PoisonBits(std::uint32_t logical) {
  const auto expected = ExpectedBits(logical);
  return {expected[0] ^ 0x13579bdfu, expected[1] ^ 0x2468ace0u,
          expected[2] ^ 0xdeadbeefu, expected[3] ^ 0xc001d00du};
}

class D3D11ClearViewRectMatrixSpec : public ::testing::Test {
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

TEST_F(D3D11ClearViewRectMatrixSpec,
       Clears4096SinglePixelFloatRectsWithoutBleed) {
  std::vector<PixelBits> expected(kClearViewCaseCount);
  std::vector<PixelBits> initial(kClearViewCaseCount);
  std::vector<bool> selected(kClearViewCaseCount, false);
  std::vector<std::uint32_t> selected_cases;
  selected_cases.reserve(kClearViewCaseCount);
  for (std::uint32_t logical = 0; logical < kClearViewCaseCount; ++logical) {
    expected[logical] = ExpectedBits(logical);
    initial[logical] = PoisonBits(logical);
    if (dxmt::test::LogicalCaseSelected(kClearViewCases.family(), logical)) {
      selected[logical] = true;
      selected_cases.push_back(logical);
    }
  }
  ASSERT_FALSE(selected_cases.empty());

  D3D11_TEXTURE2D_DESC texture_desc = {};
  texture_desc.Width = kClearViewWidth;
  texture_desc.Height = kClearViewHeight;
  texture_desc.MipLevels = 1;
  texture_desc.ArraySize = 1;
  texture_desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
  texture_desc.SampleDesc.Count = 1;
  texture_desc.Usage = D3D11_USAGE_DEFAULT;
  texture_desc.BindFlags = D3D11_BIND_RENDER_TARGET;
  D3D11_SUBRESOURCE_DATA texture_data = {};
  texture_data.pSysMem = initial.data();
  texture_data.SysMemPitch = kClearViewWidth * kPixelStride;
  texture_data.SysMemSlicePitch =
      kClearViewWidth * kClearViewHeight * kPixelStride;
  ComPtr<ID3D11Texture2D> texture;
  ASSERT_EQ(context_.device()->CreateTexture2D(&texture_desc, &texture_data,
                                               texture.put()),
            S_OK);
  ComPtr<ID3D11RenderTargetView> render_target_view;
  ASSERT_EQ(context_.device()->CreateRenderTargetView(texture.get(), nullptr,
                                                      render_target_view.put()),
            S_OK);

  for (const std::uint32_t logical : selected_cases) {
    const LONG x = static_cast<LONG>(logical % kClearViewWidth);
    const LONG y = static_cast<LONG>(logical / kClearViewWidth);
    const D3D11_RECT rect = {x, y, x + 1, y + 1};
    const auto values = ValuesForCase(logical);
    context1_->ClearView(render_target_view.get(), values.data(), &rect, 1);
  }

  D3D11_TEXTURE2D_DESC staging_desc = texture_desc;
  staging_desc.Usage = D3D11_USAGE_STAGING;
  staging_desc.BindFlags = 0;
  staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Texture2D> staging;
  ASSERT_EQ(
      context_.device()->CreateTexture2D(&staging_desc, nullptr, staging.put()),
      S_OK);
  context_.context()->CopyResource(staging.get(), texture.get());
  context_.context()->Flush();

  D3D11_MAPPED_SUBRESOURCE mapped = {};
  ASSERT_EQ(
      context_.context()->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped),
      S_OK);
  RecordProperty("logical_cases_executed", selected_cases.size());
  RecordProperty("logical_case_prefix",
                 kClearViewCases.family().case_id_prefix);
  bool found_mismatch = false;
  for (std::uint32_t logical = 0; logical < kClearViewCaseCount; ++logical) {
    const std::uint32_t x = logical % kClearViewWidth;
    const std::uint32_t y = logical / kClearViewWidth;
    const auto *row = reinterpret_cast<const std::uint32_t *>(
        static_cast<const std::uint8_t *>(mapped.pData) + y * mapped.RowPitch);
    const auto &desired =
        selected[logical] ? expected[logical] : initial[logical];
    for (std::uint32_t component = 0; component < kComponentsPerPixel;
         ++component) {
      const std::uint32_t actual_bits =
          row[x * kComponentsPerPixel + component];
      if (actual_bits == desired[component])
        continue;

      const auto case_id =
          dxmt::test::LogicalCaseId(kClearViewCases.family(), logical);
      const auto replay_case_id =
          selected[logical]
              ? case_id
              : dxmt::test::LogicalCaseId(kClearViewCases.family(),
                                          selected_cases.front());
      ADD_FAILURE()
          << "LogicalCaseId: " << case_id << '\n'
          << "Class: "
          << dxmt::test::TestClassName(
                 kClearViewCases.family().traits.test_class)
          << '\n'
          << "Requirements: feature_level=11_0 queue=Immediate "
             "capability=Context1,ClearView,RenderTargetView,CopyResource,"
             "StagingMap\n"
          << "ExecutionPath: "
          << dxmt::test::ExecutionPathName(
                 kClearViewCases.family().traits.execution_path)
          << '\n'
          << "Parameters: logical=" << logical
          << " selected=" << (selected[logical] ? "true" : "false") << " rect=("
          << x << ',' << y << ',' << x + 1u << ',' << y + 1u
          << ") component=" << component << " row_pitch=" << mapped.RowPitch
          << " expected_float=" << FloatFromBits(desired[component])
          << " actual_float=" << FloatFromBits(actual_bits) << '\n'
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
