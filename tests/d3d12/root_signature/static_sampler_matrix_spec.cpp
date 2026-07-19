#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

#include <string>
#include <vector>

// Public D3D12 root signature static sampler matrix.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

struct StaticSamplerCase {
  D3D12_FILTER filter;
  D3D12_TEXTURE_ADDRESS_MODE address;
  D3D12_SHADER_VISIBILITY visibility;
  UINT shader_register;
};

std::vector<StaticSamplerCase> BuildStaticSamplerCases() {
  std::vector<StaticSamplerCase> cases;
  const D3D12_FILTER filters[] = {
      D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_FILTER_MIN_MAG_MIP_LINEAR,
      D3D12_FILTER_ANISOTROPIC, D3D12_FILTER_COMPARISON_MIN_MAG_MIP_POINT,
  };
  const D3D12_TEXTURE_ADDRESS_MODE addresses[] = {
      D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_MIRROR,
      D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_BORDER,
      D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE,
  };
  const D3D12_SHADER_VISIBILITY vis[] = {
      D3D12_SHADER_VISIBILITY_ALL, D3D12_SHADER_VISIBILITY_PIXEL,
      D3D12_SHADER_VISIBILITY_VERTEX, D3D12_SHADER_VISIBILITY_GEOMETRY,
      D3D12_SHADER_VISIBILITY_HULL, D3D12_SHADER_VISIBILITY_DOMAIN,
  };
  for (const auto filter : filters) {
    for (const auto address : addresses) {
      for (const auto visibility : vis) {
        for (UINT reg = 0; reg < 4; ++reg)
          cases.push_back({filter, address, visibility, reg});
      }
    }
  }
  return cases;
}

class StaticSamplerMatrixSpec
    : public ::testing::TestWithParam<StaticSamplerCase> {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  D3D12TestContext context_;
};

TEST_P(StaticSamplerMatrixSpec, CreatesRootSignatureWithStaticSampler) {
  const auto &test = GetParam();
  D3D12_STATIC_SAMPLER_DESC sampler = {};
  sampler.Filter = test.filter;
  sampler.AddressU = test.address;
  sampler.AddressV = test.address;
  sampler.AddressW = test.address;
  sampler.MaxAnisotropy =
      (test.filter == D3D12_FILTER_ANISOTROPIC) ? 16u : 1u;
  sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
  sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
  sampler.MinLOD = 0.f;
  sampler.MaxLOD = D3D12_FLOAT32_MAX;
  sampler.ShaderRegister = test.shader_register;
  sampler.RegisterSpace = 0;
  sampler.ShaderVisibility = test.visibility;
  D3D12_ROOT_SIGNATURE_DESC desc = {};
  desc.NumStaticSamplers = 1;
  desc.pStaticSamplers = &sampler;
  auto root = context_.CreateRootSignature(desc);
  ASSERT_TRUE(root) << "filter=" << static_cast<UINT>(test.filter)
                    << " address=" << static_cast<UINT>(test.address)
                    << " vis=" << static_cast<UINT>(test.visibility)
                    << " reg=" << test.shader_register;
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

std::string StaticSamplerName(
    const ::testing::TestParamInfo<StaticSamplerCase> &info) {
  return "F" + std::to_string(static_cast<UINT>(info.param.filter)) + "A" +
         std::to_string(static_cast<UINT>(info.param.address)) + "V" +
         std::to_string(static_cast<UINT>(info.param.visibility)) + "R" +
         std::to_string(info.param.shader_register) + "N" +
         std::to_string(info.index);
}

INSTANTIATE_TEST_SUITE_P(StaticSamplerMatrix, StaticSamplerMatrixSpec,
                         ::testing::ValuesIn(BuildStaticSamplerCases()),
                         StaticSamplerName);

} // namespace
