#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <cstdint>
#include <vector>

// Public D3D11 Texture1D creation and descriptor coverage. Widths 1 through
// 4096 exercise every automatic-mip transition in that range.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

constexpr std::uint32_t kTexture1DDescCaseCount = 4096;

const dxmt::test::LogicalCaseFamilyRegistration kTexture1DDescCases(
    "D3D11Texture1DDescMatrixSpec."
    "Expands4096WidthsToCompleteMipChains",
    "D3D11.Texture1D.Description.", kTexture1DDescCaseCount, 4,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "Device",
      "CreateTexture1D,ID3D11Texture1DGetDesc,ID3D11ResourceGetType,"
      "ID3D11DeviceChildGetDevice"},
     dxmt::test::kResourceTestCost,
     "one test-local D3D11 device and one live Texture1D per selected logical "
     "case",
     "create each width from 1 through 4096 with MipLevels zero, then query "
     "the complete public resource description, type, and owning device",
     "the runtime expands each request to floor(log2(width)) plus one mip "
     "levels while preserving every other field and resource relationship",
     "logical ID, selected-case count, requested width, expected and returned "
     "descriptions, resource type, owner and device addresses, HRESULT, "
     "failure phase, and exact replay argument"});

const dxmt::test::TestCostRegistration
    kTexture1DDescCost("D3D11Texture1DDescMatrixSpec."
                       "Expands4096WidthsToCompleteMipChains",
                       dxmt::test::kResourceTestCost);

UINT CompleteMipCount(UINT width) {
  UINT levels = 0;
  do {
    ++levels;
    width >>= 1u;
  } while (width != 0);
  return levels;
}

bool TextureDescsEqual(const D3D11_TEXTURE1D_DESC &actual,
                       const D3D11_TEXTURE1D_DESC &expected) {
  return actual.Width == expected.Width &&
         actual.MipLevels == expected.MipLevels &&
         actual.ArraySize == expected.ArraySize &&
         actual.Format == expected.Format && actual.Usage == expected.Usage &&
         actual.BindFlags == expected.BindFlags &&
         actual.CPUAccessFlags == expected.CPUAccessFlags &&
         actual.MiscFlags == expected.MiscFlags;
}

class D3D11Texture1DDescMatrixSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_NE(context_.context(), nullptr);
  }

  D3D11TestContext context_;
};

TEST_F(D3D11Texture1DDescMatrixSpec, Expands4096WidthsToCompleteMipChains) {
  std::vector<std::uint32_t> selected_cases;
  selected_cases.reserve(kTexture1DDescCaseCount);
  for (std::uint32_t logical = 0; logical < kTexture1DDescCaseCount;
       ++logical) {
    if (dxmt::test::LogicalCaseSelected(kTexture1DDescCases.family(), logical))
      selected_cases.push_back(logical);
  }
  ASSERT_FALSE(selected_cases.empty());

  RecordProperty("logical_cases_executed", selected_cases.size());
  RecordProperty("logical_case_prefix",
                 kTexture1DDescCases.family().case_id_prefix);
  for (const std::uint32_t logical : selected_cases) {
    D3D11_TEXTURE1D_DESC requested = {};
    requested.Width = logical + 1u;
    requested.MipLevels = 0;
    requested.ArraySize = 1;
    requested.Format = DXGI_FORMAT_R8_UNORM;
    requested.Usage = D3D11_USAGE_DEFAULT;
    requested.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_TEXTURE1D_DESC expected = requested;
    expected.MipLevels = CompleteMipCount(requested.Width);

    ComPtr<ID3D11Texture1D> texture;
    const HRESULT create_result =
        context_.device()->CreateTexture1D(&requested, nullptr, texture.put());
    D3D11_TEXTURE1D_DESC actual = {};
    D3D11_RESOURCE_DIMENSION dimension = D3D11_RESOURCE_DIMENSION_UNKNOWN;
    ComPtr<ID3D11Device> owner;
    if (create_result == S_OK && texture) {
      texture->GetDesc(&actual);
      texture->GetType(&dimension);
      texture->GetDevice(owner.put());
    }

    const bool desc_matches = TextureDescsEqual(actual, expected);
    const bool type_matches = dimension == D3D11_RESOURCE_DIMENSION_TEXTURE1D;
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
        dxmt::test::LogicalCaseId(kTexture1DDescCases.family(), logical);
    ADD_FAILURE()
        << "LogicalCaseId: " << case_id << '\n'
        << "Class: "
        << dxmt::test::TestClassName(
               kTexture1DDescCases.family().traits.test_class)
        << '\n'
        << "Requirements: feature_level=11_0 shader_model=None queue=Device "
           "capability=CreateTexture1D,Texture1DGetDesc,ResourceGetType,"
           "DeviceChildGetDevice\n"
        << "ExecutionPath: "
        << dxmt::test::ExecutionPathName(
               kTexture1DDescCases.family().traits.execution_path)
        << '\n'
        << "Parameters: logical=" << logical
        << " selected_cases=" << selected_cases.size() << '\n'
        << "Expected: width=" << expected.Width
        << " mip_levels=" << expected.MipLevels
        << " array_size=" << expected.ArraySize
        << " format=" << static_cast<UINT>(expected.Format)
        << " usage=" << static_cast<UINT>(expected.Usage)
        << " bind_flags=" << expected.BindFlags
        << " cpu_access=" << expected.CPUAccessFlags
        << " misc_flags=" << expected.MiscFlags
        << " dimension=" << D3D11_RESOURCE_DIMENSION_TEXTURE1D
        << " owner=" << context_.device() << '\n'
        << "Observed: create_hresult=" << create_result
        << " failure_phase=" << failure_phase << " width=" << actual.Width
        << " mip_levels=" << actual.MipLevels
        << " array_size=" << actual.ArraySize
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

TEST_F(D3D11Texture1DDescMatrixSpec,
       ValidatesNullZeroWidthImmutableAndOutputContracts) {
  ID3D11Texture1D *texture =
      reinterpret_cast<ID3D11Texture1D *>(static_cast<std::uintptr_t>(1));
  EXPECT_EQ(context_.device()->CreateTexture1D(nullptr, nullptr, &texture),
            E_INVALIDARG);
  EXPECT_EQ(texture, nullptr);

  D3D11_TEXTURE1D_DESC desc = {};
  desc.Width = 1;
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_R8_UNORM;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  EXPECT_EQ(context_.device()->CreateTexture1D(&desc, nullptr, nullptr),
            S_FALSE);

  desc.Width = 0;
  texture = reinterpret_cast<ID3D11Texture1D *>(static_cast<std::uintptr_t>(1));
  EXPECT_EQ(context_.device()->CreateTexture1D(&desc, nullptr, &texture),
            E_INVALIDARG);
  EXPECT_EQ(texture, nullptr);

  desc.Width = 1;
  desc.Usage = D3D11_USAGE_IMMUTABLE;
  texture = reinterpret_cast<ID3D11Texture1D *>(static_cast<std::uintptr_t>(1));
  EXPECT_EQ(context_.device()->CreateTexture1D(&desc, nullptr, &texture),
            E_INVALIDARG);
  EXPECT_EQ(texture, nullptr);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
