#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

// Public D3D11 Texture3D creation and descriptor coverage. Every dimension
// triple in [1, 16]^3 forms exactly 4096 automatic-mip cases.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

constexpr std::uint32_t kTexture3DDescCaseCount = 4096;

const dxmt::test::LogicalCaseFamilyRegistration kTexture3DDescCases(
    "D3D11Texture3DDescMatrixSpec."
    "Expands4096DimensionsToCompleteMipChains",
    "D3D11.Texture3D.Description.", kTexture3DDescCaseCount, 4,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "Device",
      "CreateTexture3D,ID3D11Texture3DGetDesc,ID3D11ResourceGetType,"
      "ID3D11DeviceChildGetDevice"},
     dxmt::test::kResourceTestCost,
     "one test-local D3D11 device and one live Texture3D per selected logical "
     "case",
     "create every width-height-depth triple from one through 16 with "
     "MipLevels zero, then query the public description, type, and owner",
     "the runtime expands each request to the complete mip chain determined "
     "by its largest dimension while preserving every other field",
     "logical ID, selected-case count, requested dimensions, expected and "
     "returned descriptions, resource type, owner and device addresses, "
     "HRESULT, failure phase, and exact replay argument"});

const dxmt::test::TestCostRegistration
    kTexture3DDescCost("D3D11Texture3DDescMatrixSpec."
                       "Expands4096DimensionsToCompleteMipChains",
                       dxmt::test::kResourceTestCost);

UINT CompleteMipCount(UINT largest_dimension) {
  UINT levels = 0;
  do {
    ++levels;
    largest_dimension >>= 1u;
  } while (largest_dimension != 0);
  return levels;
}

bool TextureDescsEqual(const D3D11_TEXTURE3D_DESC &actual,
                       const D3D11_TEXTURE3D_DESC &expected) {
  return actual.Width == expected.Width && actual.Height == expected.Height &&
         actual.Depth == expected.Depth &&
         actual.MipLevels == expected.MipLevels &&
         actual.Format == expected.Format && actual.Usage == expected.Usage &&
         actual.BindFlags == expected.BindFlags &&
         actual.CPUAccessFlags == expected.CPUAccessFlags &&
         actual.MiscFlags == expected.MiscFlags;
}

class D3D11Texture3DDescMatrixSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_NE(context_.context(), nullptr);
  }

  D3D11TestContext context_;
};

TEST_F(D3D11Texture3DDescMatrixSpec, Expands4096DimensionsToCompleteMipChains) {
  std::vector<std::uint32_t> selected_cases;
  selected_cases.reserve(kTexture3DDescCaseCount);
  for (std::uint32_t logical = 0; logical < kTexture3DDescCaseCount;
       ++logical) {
    if (dxmt::test::LogicalCaseSelected(kTexture3DDescCases.family(), logical))
      selected_cases.push_back(logical);
  }
  ASSERT_FALSE(selected_cases.empty());

  RecordProperty("logical_cases_executed", selected_cases.size());
  RecordProperty("logical_case_prefix",
                 kTexture3DDescCases.family().case_id_prefix);
  for (const std::uint32_t logical : selected_cases) {
    D3D11_TEXTURE3D_DESC requested = {};
    requested.Width = (logical & 15u) + 1u;
    requested.Height = ((logical >> 4u) & 15u) + 1u;
    requested.Depth = ((logical >> 8u) & 15u) + 1u;
    requested.MipLevels = 0;
    requested.Format = DXGI_FORMAT_R8_UNORM;
    requested.Usage = D3D11_USAGE_DEFAULT;
    requested.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_TEXTURE3D_DESC expected = requested;
    expected.MipLevels = CompleteMipCount(
        std::max({requested.Width, requested.Height, requested.Depth}));

    ComPtr<ID3D11Texture3D> texture;
    const HRESULT create_result =
        context_.device()->CreateTexture3D(&requested, nullptr, texture.put());
    D3D11_TEXTURE3D_DESC actual = {};
    D3D11_RESOURCE_DIMENSION dimension = D3D11_RESOURCE_DIMENSION_UNKNOWN;
    ComPtr<ID3D11Device> owner;
    if (create_result == S_OK && texture) {
      texture->GetDesc(&actual);
      texture->GetType(&dimension);
      texture->GetDevice(owner.put());
    }

    const bool desc_matches = TextureDescsEqual(actual, expected);
    const bool type_matches = dimension == D3D11_RESOURCE_DIMENSION_TEXTURE3D;
    const bool owner_matches = owner.get() == context_.device();
    if (create_result == S_OK && texture && desc_matches && type_matches &&
        owner_matches)
      continue;

    const char *failure_phase = "owner";
    if (create_result != S_OK || !texture)
      failure_phase = "create";
    else if (!desc_matches)
      failure_phase = "get_desc";
    else if (!type_matches)
      failure_phase = "get_type";

    const auto case_id =
        dxmt::test::LogicalCaseId(kTexture3DDescCases.family(), logical);
    ADD_FAILURE()
        << "LogicalCaseId: " << case_id << '\n'
        << "Class: "
        << dxmt::test::TestClassName(
               kTexture3DDescCases.family().traits.test_class)
        << '\n'
        << "Requirements: feature_level=11_0 shader_model=None queue=Device "
           "capability=CreateTexture3D,Texture3DGetDesc,ResourceGetType,"
           "DeviceChildGetDevice\n"
        << "ExecutionPath: "
        << dxmt::test::ExecutionPathName(
               kTexture3DDescCases.family().traits.execution_path)
        << '\n'
        << "Parameters: logical=" << logical
        << " selected_cases=" << selected_cases.size() << '\n'
        << "Expected: dimensions=" << expected.Width << 'x' << expected.Height
        << 'x' << expected.Depth << " mip_levels=" << expected.MipLevels
        << " format=" << static_cast<UINT>(expected.Format)
        << " usage=" << static_cast<UINT>(expected.Usage)
        << " bind_flags=" << expected.BindFlags
        << " cpu_access=" << expected.CPUAccessFlags
        << " misc_flags=" << expected.MiscFlags
        << " dimension=" << D3D11_RESOURCE_DIMENSION_TEXTURE3D
        << " owner=" << context_.device() << '\n'
        << "Observed: create_hresult=" << create_result
        << " failure_phase=" << failure_phase << " dimensions=" << actual.Width
        << 'x' << actual.Height << 'x' << actual.Depth
        << " mip_levels=" << actual.MipLevels
        << " format=" << static_cast<UINT>(actual.Format)
        << " usage=" << static_cast<UINT>(actual.Usage)
        << " bind_flags=" << actual.BindFlags
        << " cpu_access=" << actual.CPUAccessFlags
        << " misc_flags=" << actual.MiscFlags
        << " dimension=" << static_cast<UINT>(dimension)
        << " owner=" << owner.get() << " texture=" << texture.get() << '\n'
        << "Replay: --dxmt-case-id=" << case_id;
    break;
  }

  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11Texture3DDescMatrixSpec,
       ValidatesNullZeroWidthImmutableAndOutputContracts) {
  ID3D11Texture3D *texture =
      reinterpret_cast<ID3D11Texture3D *>(static_cast<std::uintptr_t>(1));
  EXPECT_EQ(context_.device()->CreateTexture3D(nullptr, nullptr, &texture),
            E_INVALIDARG);
  EXPECT_EQ(texture, nullptr);

  D3D11_TEXTURE3D_DESC desc = {};
  desc.Width = 1;
  desc.Height = 1;
  desc.Depth = 1;
  desc.MipLevels = 1;
  desc.Format = DXGI_FORMAT_R8_UNORM;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  EXPECT_EQ(context_.device()->CreateTexture3D(&desc, nullptr, nullptr),
            S_FALSE);

  desc.Width = 0;
  texture = reinterpret_cast<ID3D11Texture3D *>(static_cast<std::uintptr_t>(1));
  EXPECT_EQ(context_.device()->CreateTexture3D(&desc, nullptr, &texture),
            E_INVALIDARG);
  EXPECT_EQ(texture, nullptr);

  desc.Width = 1;
  desc.Usage = D3D11_USAGE_IMMUTABLE;
  texture = reinterpret_cast<ID3D11Texture3D *>(static_cast<std::uintptr_t>(1));
  EXPECT_EQ(context_.device()->CreateTexture3D(&desc, nullptr, &texture),
            E_INVALIDARG);
  EXPECT_EQ(texture, nullptr);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
