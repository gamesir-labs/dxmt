#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <array>
#include <cstdint>
#include <vector>

// Public D3D11 and DXGI resource-usage coverage derived from the native
// runtime contract. Every resource is test-local, so this case is safe under
// the default parallel scheduler.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

struct DimensionCase {
  D3D11_RESOURCE_DIMENSION dimension;
  const char *name;
};

constexpr std::array kDimensionCases = {
    DimensionCase{D3D11_RESOURCE_DIMENSION_BUFFER, "Buffer"},
    DimensionCase{D3D11_RESOURCE_DIMENSION_TEXTURE1D, "Texture1D"},
    DimensionCase{D3D11_RESOURCE_DIMENSION_TEXTURE2D, "Texture2D"},
    DimensionCase{D3D11_RESOURCE_DIMENSION_TEXTURE3D, "Texture3D"},
};

constexpr std::array<UINT, 11> kBindFlagCases = {
    0,
    D3D11_BIND_SHADER_RESOURCE,
    D3D11_BIND_RENDER_TARGET,
    D3D11_BIND_DEPTH_STENCIL,
    D3D11_BIND_UNORDERED_ACCESS,
    D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET,
    D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
    D3D11_BIND_CONSTANT_BUFFER,
    D3D11_BIND_VERTEX_BUFFER,
    D3D11_BIND_INDEX_BUFFER,
    D3D11_BIND_STREAM_OUTPUT,
};

constexpr std::uint32_t kResourceUsageCaseCount = 30;

const dxmt::test::LogicalCaseFamilyRegistration kResourceUsageCases(
    "D3D11DxgiResourceUsageContractSpec.MapsD3D11BindFlagsAcrossResources",
    "D3D11.DXGI.Resource.Usage.", kResourceUsageCaseCount, 2,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "Device",
      "CreateBuffer,CreateTexture1D,CreateTexture2D,CreateTexture3D,"
      "IDXGIResourceGetUsage,IDXGIDeviceSubObjectGetDevice"},
     dxmt::test::kResourceTestCost,
     "one test-local D3D11/DXGI device and one small live resource per "
     "selected logical case",
     "create every valid native bind-flag case across Buffer, Texture1D, "
     "Texture2D, and Texture3D, then query its public DXGI usage and owner",
     "shader-resource, render-target, and unordered-access bindings map to "
     "their DXGI usage bits; other bindings map to zero; the resource "
     "returns its creating D3D11 device",
     "logical ID, dimension, bind flags, expected and observed DXGI usage, "
     "owner identity, HRESULTs, and exact replay argument"});

const dxmt::test::TestCostRegistration kResourceUsageCost(
    "D3D11DxgiResourceUsageContractSpec.MapsD3D11BindFlagsAcrossResources",
    dxmt::test::kResourceTestCost);

bool BindFlagsValidForDimension(D3D11_RESOURCE_DIMENSION dimension,
                                UINT bind_flags) {
  if (bind_flags & (D3D11_BIND_CONSTANT_BUFFER | D3D11_BIND_VERTEX_BUFFER |
                    D3D11_BIND_INDEX_BUFFER | D3D11_BIND_STREAM_OUTPUT))
    return dimension == D3D11_RESOURCE_DIMENSION_BUFFER;
  if (bind_flags & D3D11_BIND_DEPTH_STENCIL)
    return dimension == D3D11_RESOURCE_DIMENSION_TEXTURE1D ||
           dimension == D3D11_RESOURCE_DIMENSION_TEXTURE2D;
  return true;
}

DXGI_USAGE ExpectedUsage(UINT bind_flags) {
  DXGI_USAGE usage = 0;
  if (bind_flags & D3D11_BIND_SHADER_RESOURCE)
    usage |= DXGI_USAGE_SHADER_INPUT;
  if (bind_flags & D3D11_BIND_RENDER_TARGET)
    usage |= DXGI_USAGE_RENDER_TARGET_OUTPUT;
  if (bind_flags & D3D11_BIND_UNORDERED_ACCESS)
    usage |= DXGI_USAGE_UNORDERED_ACCESS;
  return usage;
}

HRESULT CreateResource(ID3D11Device *device, D3D11_RESOURCE_DIMENSION dimension,
                       UINT bind_flags, ID3D11Resource **resource) {
  *resource = nullptr;
  if (dimension == D3D11_RESOURCE_DIMENSION_BUFFER) {
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = 256;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = bind_flags;
    ComPtr<ID3D11Buffer> buffer;
    const HRESULT result = device->CreateBuffer(&desc, nullptr, buffer.put());
    if (SUCCEEDED(result))
      *resource = static_cast<ID3D11Resource *>(buffer.release());
    return result;
  }

  const DXGI_FORMAT format = bind_flags & D3D11_BIND_DEPTH_STENCIL
                                 ? DXGI_FORMAT_D24_UNORM_S8_UINT
                                 : DXGI_FORMAT_R8G8B8A8_UNORM;
  if (dimension == D3D11_RESOURCE_DIMENSION_TEXTURE1D) {
    D3D11_TEXTURE1D_DESC desc = {};
    desc.Width = 16;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = format;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = bind_flags;
    ComPtr<ID3D11Texture1D> texture;
    const HRESULT result =
        device->CreateTexture1D(&desc, nullptr, texture.put());
    if (SUCCEEDED(result))
      *resource = static_cast<ID3D11Resource *>(texture.release());
    return result;
  }

  if (dimension == D3D11_RESOURCE_DIMENSION_TEXTURE2D) {
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = 16;
    desc.Height = 8;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = bind_flags;
    ComPtr<ID3D11Texture2D> texture;
    const HRESULT result =
        device->CreateTexture2D(&desc, nullptr, texture.put());
    if (SUCCEEDED(result))
      *resource = static_cast<ID3D11Resource *>(texture.release());
    return result;
  }

  D3D11_TEXTURE3D_DESC desc = {};
  desc.Width = 16;
  desc.Height = 8;
  desc.Depth = 4;
  desc.MipLevels = 1;
  desc.Format = format;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = bind_flags;
  ComPtr<ID3D11Texture3D> texture;
  const HRESULT result = device->CreateTexture3D(&desc, nullptr, texture.put());
  if (SUCCEEDED(result))
    *resource = static_cast<ID3D11Resource *>(texture.release());
  return result;
}

class D3D11DxgiResourceUsageContractSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_NE(context_.device(), nullptr);
  }

  D3D11TestContext context_;
};

TEST_F(D3D11DxgiResourceUsageContractSpec, MapsD3D11BindFlagsAcrossResources) {
  std::vector<std::uint32_t> selected_cases;
  for (std::uint32_t logical = 0; logical < kResourceUsageCaseCount;
       ++logical) {
    if (dxmt::test::LogicalCaseSelected(kResourceUsageCases.family(), logical))
      selected_cases.push_back(logical);
  }
  ASSERT_FALSE(selected_cases.empty());

  RecordProperty("logical_cases_executed", selected_cases.size());
  RecordProperty("logical_case_prefix",
                 kResourceUsageCases.family().case_id_prefix);
  std::uint32_t logical = 0;
  for (const DimensionCase &dimension_case : kDimensionCases) {
    for (const UINT bind_flags : kBindFlagCases) {
      if (!BindFlagsValidForDimension(dimension_case.dimension, bind_flags))
        continue;

      const std::uint32_t current_logical = logical++;
      if (!dxmt::test::LogicalCaseSelected(kResourceUsageCases.family(),
                                           current_logical))
        continue;

      ComPtr<ID3D11Resource> d3d_resource;
      const HRESULT create_result =
          CreateResource(context_.device(), dimension_case.dimension,
                         bind_flags, d3d_resource.put());
      ComPtr<IDXGIResource> dxgi_resource;
      HRESULT query_result = E_FAIL;
      HRESULT usage_result = E_FAIL;
      HRESULT owner_result = E_FAIL;
      DXGI_USAGE actual_usage = ~DXGI_USAGE{0};
      ComPtr<ID3D11Device> owner;
      if (create_result == S_OK && d3d_resource) {
        query_result = d3d_resource->QueryInterface(
            __uuidof(IDXGIResource),
            reinterpret_cast<void **>(dxgi_resource.put()));
      }
      if (query_result == S_OK && dxgi_resource) {
        usage_result = dxgi_resource->GetUsage(&actual_usage);
        owner_result = dxgi_resource->GetDevice(
            __uuidof(ID3D11Device), reinterpret_cast<void **>(owner.put()));
      }

      const DXGI_USAGE expected_usage = ExpectedUsage(bind_flags);
      const bool valid =
          create_result == S_OK && d3d_resource && query_result == S_OK &&
          dxgi_resource && usage_result == S_OK &&
          actual_usage == expected_usage && owner_result == S_OK && owner &&
          owner.get() == context_.device();
      if (valid)
        continue;

      const auto case_id = dxmt::test::LogicalCaseId(
          kResourceUsageCases.family(), current_logical);
      ADD_FAILURE() << "LogicalCaseId: " << case_id << '\n'
                    << "Parameters: logical=" << current_logical
                    << " dimension=" << dimension_case.name
                    << " bind_flags=" << bind_flags
                    << " selected_cases=" << selected_cases.size() << '\n'
                    << "Expected: create_hresult=" << S_OK
                    << " query_hresult=" << S_OK << " usage_hresult=" << S_OK
                    << " usage=" << expected_usage << " owner_hresult=" << S_OK
                    << " owner=" << context_.device() << '\n'
                    << "Observed: create_hresult=" << create_result
                    << " resource=" << d3d_resource.get()
                    << " query_hresult=" << query_result
                    << " dxgi_resource=" << dxgi_resource.get()
                    << " usage_hresult=" << usage_result
                    << " usage=" << actual_usage
                    << " owner_hresult=" << owner_result
                    << " owner=" << owner.get() << '\n'
                    << "Replay: --dxmt-case-id=" << case_id;
      return;
    }
  }

  EXPECT_EQ(logical, kResourceUsageCaseCount);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
