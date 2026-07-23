#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <array>
#include <cstdint>
#include <vector>

// Public D3D11.3 versioned texture creation coverage. The focused matrix
// verifies Desc1 round-trips and legacy-interface compatibility for default
// and staging Texture2D1 / Texture3D1 resources.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

constexpr std::uint32_t kVersionedTextureCaseCount = 4;
constexpr std::array<const char *, kVersionedTextureCaseCount> kCaseNames = {
    "Texture2D1Default",
    "Texture2D1Staging",
    "Texture3D1Default",
    "Texture3D1Staging",
};

const dxmt::test::LogicalCaseFamilyRegistration kVersionedTextureCases(
    "D3D11VersionedTextureCreationContractSpec."
    "RoundTripsDefaultAndStagingDescriptions",
    "D3D11.Resource.VersionedTexture.Description.", kVersionedTextureCaseCount,
    1,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "Device",
      "ID3D11Device3,CreateTexture2D1,CreateTexture3D1,"
      "ID3D11Texture2D1GetDesc1,ID3D11Texture3D1GetDesc1,"
      "ID3D11DeviceChildGetDevice"},
     dxmt::test::kResourceTestCost,
     "one test-local D3D11 device and one live versioned texture per selected "
     "logical case",
     "create default and staging Texture2D1 and Texture3D1 resources through "
     "ID3D11Device3, then query their complete public descriptions and legacy "
     "interfaces",
     "every resource preserves its Desc1 values, supports the corresponding "
     "legacy texture interface, and returns the creating device",
     "logical ID, selected-case count, dimension and usage, expected and "
     "returned descriptions, interface and owner addresses, HRESULT, failure "
     "phase, and exact replay argument"});

const dxmt::test::TestCostRegistration
    kVersionedTextureCost("D3D11VersionedTextureCreationContractSpec."
                          "RoundTripsDefaultAndStagingDescriptions",
                          dxmt::test::kResourceTestCost);

D3D11_TEXTURE2D_DESC1 Texture2dDesc(bool staging) {
  D3D11_TEXTURE2D_DESC1 desc = {};
  desc.Width = 16;
  desc.Height = 8;
  desc.MipLevels = staging ? 1u : 4u;
  desc.ArraySize = staging ? 1u : 2u;
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.Usage = staging ? D3D11_USAGE_STAGING : D3D11_USAGE_DEFAULT;
  desc.BindFlags = staging ? 0u : static_cast<UINT>(D3D11_BIND_SHADER_RESOURCE);
  desc.CPUAccessFlags = staging ? static_cast<UINT>(D3D11_CPU_ACCESS_READ) : 0u;
  desc.TextureLayout = D3D11_TEXTURE_LAYOUT_UNDEFINED;
  return desc;
}

D3D11_TEXTURE3D_DESC1 Texture3dDesc(bool staging) {
  D3D11_TEXTURE3D_DESC1 desc = {};
  desc.Width = 16;
  desc.Height = 8;
  desc.Depth = 4;
  desc.MipLevels = staging ? 1u : 3u;
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.Usage = staging ? D3D11_USAGE_STAGING : D3D11_USAGE_DEFAULT;
  desc.BindFlags = staging ? 0u : static_cast<UINT>(D3D11_BIND_SHADER_RESOURCE);
  desc.CPUAccessFlags = staging ? static_cast<UINT>(D3D11_CPU_ACCESS_READ) : 0u;
  desc.TextureLayout = D3D11_TEXTURE_LAYOUT_UNDEFINED;
  return desc;
}

bool Texture2dDescsEqual(const D3D11_TEXTURE2D_DESC1 &actual,
                         const D3D11_TEXTURE2D_DESC1 &expected) {
  return actual.Width == expected.Width && actual.Height == expected.Height &&
         actual.MipLevels == expected.MipLevels &&
         actual.ArraySize == expected.ArraySize &&
         actual.Format == expected.Format &&
         actual.SampleDesc.Count == expected.SampleDesc.Count &&
         actual.SampleDesc.Quality == expected.SampleDesc.Quality &&
         actual.Usage == expected.Usage &&
         actual.BindFlags == expected.BindFlags &&
         actual.CPUAccessFlags == expected.CPUAccessFlags &&
         actual.MiscFlags == expected.MiscFlags &&
         actual.TextureLayout == expected.TextureLayout;
}

bool Texture3dDescsEqual(const D3D11_TEXTURE3D_DESC1 &actual,
                         const D3D11_TEXTURE3D_DESC1 &expected) {
  return actual.Width == expected.Width && actual.Height == expected.Height &&
         actual.Depth == expected.Depth &&
         actual.MipLevels == expected.MipLevels &&
         actual.Format == expected.Format && actual.Usage == expected.Usage &&
         actual.BindFlags == expected.BindFlags &&
         actual.CPUAccessFlags == expected.CPUAccessFlags &&
         actual.MiscFlags == expected.MiscFlags &&
         actual.TextureLayout == expected.TextureLayout;
}

class D3D11VersionedTextureCreationContractSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_EQ(
        context_.device()->QueryInterface(
            __uuidof(ID3D11Device3), reinterpret_cast<void **>(device3_.put())),
        S_OK);
    ASSERT_NE(device3_.get(), nullptr);
  }

  D3D11TestContext context_;
  ComPtr<ID3D11Device3> device3_;
};

TEST_F(D3D11VersionedTextureCreationContractSpec,
       RoundTripsDefaultAndStagingDescriptions) {
  std::vector<std::uint32_t> selected_cases;
  for (std::uint32_t logical = 0; logical < kVersionedTextureCaseCount;
       ++logical) {
    if (dxmt::test::LogicalCaseSelected(kVersionedTextureCases.family(),
                                        logical))
      selected_cases.push_back(logical);
  }
  ASSERT_FALSE(selected_cases.empty());

  RecordProperty("logical_cases_executed", selected_cases.size());
  RecordProperty("logical_case_prefix",
                 kVersionedTextureCases.family().case_id_prefix);
  for (const std::uint32_t logical : selected_cases) {
    const bool is_3d = logical >= 2;
    const bool staging = (logical & 1u) != 0;
    HRESULT create_result = E_FAIL;
    bool desc_matches = false;
    bool legacy_interface = false;
    ComPtr<ID3D11Device> owner;
    IUnknown *resource_address = nullptr;

    if (!is_3d) {
      const D3D11_TEXTURE2D_DESC1 expected = Texture2dDesc(staging);
      ComPtr<ID3D11Texture2D1> texture;
      create_result =
          device3_->CreateTexture2D1(&expected, nullptr, texture.put());
      D3D11_TEXTURE2D_DESC1 actual = {};
      ComPtr<ID3D11Texture2D> legacy;
      if (create_result == S_OK && texture) {
        texture->GetDesc1(&actual);
        texture->GetDevice(owner.put());
        legacy_interface =
            texture->QueryInterface(__uuidof(ID3D11Texture2D),
                                    reinterpret_cast<void **>(legacy.put())) ==
                S_OK &&
            legacy;
      }
      resource_address = texture.get();
      desc_matches = Texture2dDescsEqual(actual, expected);
    } else {
      const D3D11_TEXTURE3D_DESC1 expected = Texture3dDesc(staging);
      ComPtr<ID3D11Texture3D1> texture;
      create_result =
          device3_->CreateTexture3D1(&expected, nullptr, texture.put());
      D3D11_TEXTURE3D_DESC1 actual = {};
      ComPtr<ID3D11Texture3D> legacy;
      if (create_result == S_OK && texture) {
        texture->GetDesc1(&actual);
        texture->GetDevice(owner.put());
        legacy_interface =
            texture->QueryInterface(__uuidof(ID3D11Texture3D),
                                    reinterpret_cast<void **>(legacy.put())) ==
                S_OK &&
            legacy;
      }
      resource_address = texture.get();
      desc_matches = Texture3dDescsEqual(actual, expected);
    }

    const bool valid = create_result == S_OK && resource_address &&
                       desc_matches && legacy_interface &&
                       owner.get() == context_.device();
    if (valid)
      continue;

    const auto case_id =
        dxmt::test::LogicalCaseId(kVersionedTextureCases.family(), logical);
    ADD_FAILURE() << "LogicalCaseId: " << case_id << '\n'
                  << "Class: "
                  << dxmt::test::TestClassName(
                         kVersionedTextureCases.family().traits.test_class)
                  << '\n'
                  << "Parameters: logical=" << logical
                  << " case=" << kCaseNames[logical]
                  << " dimension=" << (is_3d ? "3D" : "2D")
                  << " usage=" << (staging ? "staging" : "default")
                  << " selected_cases=" << selected_cases.size() << '\n'
                  << "Expected: create_hresult=" << S_OK
                  << " desc_match=true legacy_interface=true owner="
                  << context_.device() << '\n'
                  << "Observed: create_hresult=" << create_result
                  << " desc_match=" << desc_matches
                  << " legacy_interface=" << legacy_interface
                  << " owner=" << owner.get()
                  << " resource=" << resource_address << '\n'
                  << "Replay: --dxmt-case-id=" << case_id;
    break;
  }

  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11VersionedTextureCreationContractSpec,
       ValidatesNullImmutableTiledAndOutputContracts) {
  ID3D11Texture2D1 *texture2d =
      reinterpret_cast<ID3D11Texture2D1 *>(static_cast<std::uintptr_t>(1));
  EXPECT_EQ(device3_->CreateTexture2D1(nullptr, nullptr, &texture2d),
            E_INVALIDARG);
  EXPECT_EQ(texture2d, nullptr);

  D3D11_TEXTURE2D_DESC1 desc2d = Texture2dDesc(false);
  EXPECT_EQ(device3_->CreateTexture2D1(&desc2d, nullptr, nullptr), S_FALSE);
  desc2d.Usage = D3D11_USAGE_IMMUTABLE;
  texture2d =
      reinterpret_cast<ID3D11Texture2D1 *>(static_cast<std::uintptr_t>(1));
  EXPECT_EQ(device3_->CreateTexture2D1(&desc2d, nullptr, &texture2d),
            E_INVALIDARG);
  EXPECT_EQ(texture2d, nullptr);
  desc2d = Texture2dDesc(false);
  desc2d.MiscFlags = D3D11_RESOURCE_MISC_TILED;
  texture2d =
      reinterpret_cast<ID3D11Texture2D1 *>(static_cast<std::uintptr_t>(1));
  EXPECT_EQ(device3_->CreateTexture2D1(&desc2d, nullptr, &texture2d),
            E_INVALIDARG);
  EXPECT_EQ(texture2d, nullptr);

  ID3D11Texture3D1 *texture3d =
      reinterpret_cast<ID3D11Texture3D1 *>(static_cast<std::uintptr_t>(1));
  EXPECT_EQ(device3_->CreateTexture3D1(nullptr, nullptr, &texture3d),
            E_INVALIDARG);
  EXPECT_EQ(texture3d, nullptr);

  D3D11_TEXTURE3D_DESC1 desc3d = Texture3dDesc(false);
  EXPECT_EQ(device3_->CreateTexture3D1(&desc3d, nullptr, nullptr), S_FALSE);
  desc3d.Usage = D3D11_USAGE_IMMUTABLE;
  texture3d =
      reinterpret_cast<ID3D11Texture3D1 *>(static_cast<std::uintptr_t>(1));
  EXPECT_EQ(device3_->CreateTexture3D1(&desc3d, nullptr, &texture3d),
            E_INVALIDARG);
  EXPECT_EQ(texture3d, nullptr);
  desc3d = Texture3dDesc(false);
  desc3d.MiscFlags = D3D11_RESOURCE_MISC_TILED;
  texture3d =
      reinterpret_cast<ID3D11Texture3D1 *>(static_cast<std::uintptr_t>(1));
  EXPECT_EQ(device3_->CreateTexture3D1(&desc3d, nullptr, &texture3d),
            E_INVALIDARG);
  EXPECT_EQ(texture3d, nullptr);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
