#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

#include <string>
#include <vector>

// Public D3D12 CreateSampler filter/address matrix (creation + GetDesc via
// CPU handle is not available; validates CreateSampler does not remove device).

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

struct SamplerFilterCase {
  D3D12_FILTER filter;
  D3D12_TEXTURE_ADDRESS_MODE address_u;
  D3D12_TEXTURE_ADDRESS_MODE address_v;
  D3D12_TEXTURE_ADDRESS_MODE address_w;
  UINT max_anisotropy;
};

std::vector<SamplerFilterCase> BuildSamplerFilterCases() {
  std::vector<SamplerFilterCase> cases;
  const D3D12_FILTER filters[] = {
      D3D12_FILTER_MIN_MAG_MIP_POINT,
      D3D12_FILTER_MIN_MAG_POINT_MIP_LINEAR,
      D3D12_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT,
      D3D12_FILTER_MIN_POINT_MAG_MIP_LINEAR,
      D3D12_FILTER_MIN_LINEAR_MAG_MIP_POINT,
      D3D12_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR,
      D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT,
      D3D12_FILTER_MIN_MAG_MIP_LINEAR,
      D3D12_FILTER_ANISOTROPIC,
      D3D12_FILTER_COMPARISON_MIN_MAG_MIP_POINT,
      D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR,
      D3D12_FILTER_COMPARISON_ANISOTROPIC,
      D3D12_FILTER_MINIMUM_MIN_MAG_MIP_POINT,
      D3D12_FILTER_MAXIMUM_MIN_MAG_MIP_POINT,
  };
  const D3D12_TEXTURE_ADDRESS_MODE addresses[] = {
      D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_MIRROR,
      D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_BORDER,
      D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE,
  };
  for (const auto filter : filters) {
    for (const auto address : addresses) {
      const UINT anisotropy =
          filter == D3D12_FILTER_ANISOTROPIC ||
                  filter == D3D12_FILTER_COMPARISON_ANISOTROPIC
              ? 16u
              : 1u;
      cases.push_back({filter, address, address, address, anisotropy});
      // Mixed address modes on U/V/W for non-aniso filters.
      if (filter != D3D12_FILTER_ANISOTROPIC &&
          filter != D3D12_FILTER_COMPARISON_ANISOTROPIC) {
        cases.push_back({filter, addresses[0], addresses[1], addresses[2], 1});
        cases.push_back({filter, addresses[2], addresses[3], addresses[4], 1});
      }
    }
  }
  // Anisotropy ladder.
  for (UINT a : {1u, 2u, 4u, 8u, 16u})
    cases.push_back({D3D12_FILTER_ANISOTROPIC,
                     D3D12_TEXTURE_ADDRESS_MODE_WRAP,
                     D3D12_TEXTURE_ADDRESS_MODE_WRAP,
                     D3D12_TEXTURE_ADDRESS_MODE_WRAP, a});
  return cases;
}

class SamplerFilterMatrixSpec
    : public ::testing::TestWithParam<SamplerFilterCase> {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  D3D12TestContext context_;
};

TEST_P(SamplerFilterMatrixSpec, CreateSamplerSucceedsWithoutDeviceRemoval) {
  const auto &test = GetParam();
  auto heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 1, true);
  ASSERT_TRUE(heap);
  D3D12_SAMPLER_DESC desc = {};
  desc.Filter = test.filter;
  desc.AddressU = test.address_u;
  desc.AddressV = test.address_v;
  desc.AddressW = test.address_w;
  desc.MaxAnisotropy = test.max_anisotropy;
  desc.ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
  desc.MinLOD = 0.f;
  desc.MaxLOD = D3D12_FLOAT32_MAX;
  desc.BorderColor[0] = 1.f;
  desc.BorderColor[1] = 0.f;
  desc.BorderColor[2] = 0.f;
  desc.BorderColor[3] = 1.f;
  context_.device()->CreateSampler(
      &desc, heap->GetCPUDescriptorHandleForHeapStart());
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

std::string SamplerFilterName(
    const ::testing::TestParamInfo<SamplerFilterCase> &info) {
  return "F" + std::to_string(static_cast<UINT>(info.param.filter)) + "U" +
         std::to_string(static_cast<UINT>(info.param.address_u)) + "V" +
         std::to_string(static_cast<UINT>(info.param.address_v)) + "W" +
         std::to_string(static_cast<UINT>(info.param.address_w)) + "A" +
         std::to_string(info.param.max_anisotropy) + "N" +
         std::to_string(info.index);
}

INSTANTIATE_TEST_SUITE_P(FilterMatrix, SamplerFilterMatrixSpec,
                         ::testing::ValuesIn(BuildSamplerFilterCases()),
                         SamplerFilterName);

} // namespace
