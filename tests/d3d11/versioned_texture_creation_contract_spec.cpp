#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <array>
#include <cstdint>
#include <vector>

// Public D3D11.3 versioned texture creation coverage. The focused matrix
// verifies Desc1 round-trips and legacy-interface compatibility for every
// D3D11 usage supported by Texture2D1 / Texture3D1 resources.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

struct VersionedTextureCase {
  bool is_3d;
  D3D11_USAGE usage;
  const char *name;
};

constexpr std::array kVersionedTextureDescriptions = {
    VersionedTextureCase{false, D3D11_USAGE_DEFAULT, "Texture2D1Default"},
    VersionedTextureCase{false, D3D11_USAGE_STAGING, "Texture2D1Staging"},
    VersionedTextureCase{true, D3D11_USAGE_DEFAULT, "Texture3D1Default"},
    VersionedTextureCase{true, D3D11_USAGE_STAGING, "Texture3D1Staging"},
    VersionedTextureCase{false, D3D11_USAGE_DYNAMIC, "Texture2D1Dynamic"},
    VersionedTextureCase{false, D3D11_USAGE_IMMUTABLE, "Texture2D1Immutable"},
    VersionedTextureCase{true, D3D11_USAGE_DYNAMIC, "Texture3D1Dynamic"},
    VersionedTextureCase{true, D3D11_USAGE_IMMUTABLE, "Texture3D1Immutable"},
};

constexpr std::uint32_t kVersionedTextureCaseCount =
    kVersionedTextureDescriptions.size();

const dxmt::test::LogicalCaseFamilyRegistration kVersionedTextureCases(
    "D3D11VersionedTextureCreationContractSpec."
    "RoundTripsAllUsageDescriptions",
    "D3D11.Resource.VersionedTexture.Description.", kVersionedTextureCaseCount,
    1,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "Device",
      "ID3D11Device3,CreateTexture2D1,CreateTexture3D1,"
      "ID3D11Texture2D1GetDesc1,ID3D11Texture3D1GetDesc1,"
      "ID3D11DeviceChildGetDevice"},
     dxmt::test::kResourceTestCost,
     "one test-local D3D11 device, one initialization payload when required, "
     "and one live versioned texture per selected logical case",
     "create default, staging, dynamic, and immutable Texture2D1 and "
     "Texture3D1 resources through ID3D11Device3, then query their complete "
     "public descriptions and legacy interfaces",
     "every resource preserves its Desc1 values, supports the corresponding "
     "legacy texture interface, and returns the creating device",
     "logical ID, selected-case count, dimension and usage, expected and "
     "returned descriptions, interface and owner addresses, HRESULT, failure "
     "phase, and exact replay argument"});

const dxmt::test::TestCostRegistration
    kVersionedTextureCost("D3D11VersionedTextureCreationContractSpec."
                          "RoundTripsAllUsageDescriptions",
                          dxmt::test::kResourceTestCost);

D3D11_TEXTURE2D_DESC1 Texture2dDesc(D3D11_USAGE usage) {
  const bool staging = usage == D3D11_USAGE_STAGING;
  const bool dynamic = usage == D3D11_USAGE_DYNAMIC;
  const bool single_subresource =
      staging || dynamic || usage == D3D11_USAGE_IMMUTABLE;
  D3D11_TEXTURE2D_DESC1 desc = {};
  desc.Width = 16;
  desc.Height = 8;
  desc.MipLevels = single_subresource ? 1u : 4u;
  desc.ArraySize = single_subresource ? 1u : 2u;
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.Usage = usage;
  desc.BindFlags = staging ? 0u : static_cast<UINT>(D3D11_BIND_SHADER_RESOURCE);
  desc.CPUAccessFlags = staging   ? static_cast<UINT>(D3D11_CPU_ACCESS_READ)
                        : dynamic ? static_cast<UINT>(D3D11_CPU_ACCESS_WRITE)
                                  : 0u;
  desc.TextureLayout = D3D11_TEXTURE_LAYOUT_UNDEFINED;
  return desc;
}

D3D11_TEXTURE3D_DESC1 Texture3dDesc(D3D11_USAGE usage) {
  const bool staging = usage == D3D11_USAGE_STAGING;
  const bool dynamic = usage == D3D11_USAGE_DYNAMIC;
  const bool single_subresource =
      staging || dynamic || usage == D3D11_USAGE_IMMUTABLE;
  D3D11_TEXTURE3D_DESC1 desc = {};
  desc.Width = 16;
  desc.Height = 8;
  desc.Depth = 4;
  desc.MipLevels = single_subresource ? 1u : 3u;
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.Usage = usage;
  desc.BindFlags = staging ? 0u : static_cast<UINT>(D3D11_BIND_SHADER_RESOURCE);
  desc.CPUAccessFlags = staging   ? static_cast<UINT>(D3D11_CPU_ACCESS_READ)
                        : dynamic ? static_cast<UINT>(D3D11_CPU_ACCESS_WRITE)
                                  : 0u;
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
       RoundTripsAllUsageDescriptions) {
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
    const VersionedTextureCase &test_case =
        kVersionedTextureDescriptions[logical];
    const bool immutable = test_case.usage == D3D11_USAGE_IMMUTABLE;
    std::vector<std::uint32_t> initial_values(
        16u * 8u * (test_case.is_3d ? 4u : 1u), 0x7f3a91c5u);
    D3D11_SUBRESOURCE_DATA initial_data = {};
    initial_data.pSysMem = initial_values.data();
    initial_data.SysMemPitch = 16u * sizeof(std::uint32_t);
    initial_data.SysMemSlicePitch = 16u * 8u * sizeof(std::uint32_t);
    const D3D11_SUBRESOURCE_DATA *initial_data_ptr =
        immutable ? &initial_data : nullptr;
    HRESULT create_result = E_FAIL;
    bool desc_matches = false;
    bool legacy_interface = false;
    ComPtr<ID3D11Device> owner;
    IUnknown *resource_address = nullptr;

    if (!test_case.is_3d) {
      const D3D11_TEXTURE2D_DESC1 expected = Texture2dDesc(test_case.usage);
      ComPtr<ID3D11Texture2D1> texture;
      create_result = device3_->CreateTexture2D1(&expected, initial_data_ptr,
                                                 texture.put());
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
      const D3D11_TEXTURE3D_DESC1 expected = Texture3dDesc(test_case.usage);
      ComPtr<ID3D11Texture3D1> texture;
      create_result = device3_->CreateTexture3D1(&expected, initial_data_ptr,
                                                 texture.put());
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
                  << " case=" << test_case.name
                  << " dimension=" << (test_case.is_3d ? "3D" : "2D")
                  << " usage=" << static_cast<UINT>(test_case.usage)
                  << " has_initial_data=" << immutable
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
       ValidatesNullImmutableAndTiledContracts) {
  ID3D11Texture2D1 *texture2d =
      reinterpret_cast<ID3D11Texture2D1 *>(static_cast<std::uintptr_t>(1));
  EXPECT_EQ(device3_->CreateTexture2D1(nullptr, nullptr, &texture2d),
            E_INVALIDARG);
  EXPECT_EQ(texture2d, nullptr);

  D3D11_TEXTURE2D_DESC1 desc2d = Texture2dDesc(D3D11_USAGE_DEFAULT);
  EXPECT_EQ(device3_->CreateTexture2D1(&desc2d, nullptr, nullptr), S_FALSE);
  desc2d.Usage = D3D11_USAGE_IMMUTABLE;
  texture2d =
      reinterpret_cast<ID3D11Texture2D1 *>(static_cast<std::uintptr_t>(1));
  EXPECT_EQ(device3_->CreateTexture2D1(&desc2d, nullptr, &texture2d),
            E_INVALIDARG);
  EXPECT_EQ(texture2d, nullptr);
  desc2d = Texture2dDesc(D3D11_USAGE_DEFAULT);
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

  D3D11_TEXTURE3D_DESC1 desc3d = Texture3dDesc(D3D11_USAGE_DEFAULT);
  EXPECT_EQ(device3_->CreateTexture3D1(&desc3d, nullptr, nullptr), S_FALSE);
  desc3d.Usage = D3D11_USAGE_IMMUTABLE;
  texture3d =
      reinterpret_cast<ID3D11Texture3D1 *>(static_cast<std::uintptr_t>(1));
  EXPECT_EQ(device3_->CreateTexture3D1(&desc3d, nullptr, &texture3d),
            E_INVALIDARG);
  EXPECT_EQ(texture3d, nullptr);
  desc3d = Texture3dDesc(D3D11_USAGE_DEFAULT);
  desc3d.MiscFlags = D3D11_RESOURCE_MISC_TILED;
  texture3d =
      reinterpret_cast<ID3D11Texture3D1 *>(static_cast<std::uintptr_t>(1));
  EXPECT_EQ(device3_->CreateTexture3D1(&desc3d, nullptr, &texture3d), S_OK);
  EXPECT_NE(texture3d, nullptr);
  if (texture3d)
    texture3d->Release();
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
