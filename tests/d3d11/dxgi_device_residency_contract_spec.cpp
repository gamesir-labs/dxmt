#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <dxgi.h>

#include <array>
#include <cstdint>
#include <sstream>
#include <vector>

// Public IDXGIDevice residency coverage for resources created through D3D11.
// Every resource belongs to the test-local device, so queries are safe under
// the default parallel scheduler.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

constexpr std::array<const char *, 5> kResidencyCaseNames = {
    "Buffer", "Texture1D", "Texture2D", "Texture3D", "AllResources",
};

const dxmt::test::LogicalCaseFamilyRegistration kResidencyRegistration(
    "D3D11DxgiDeviceResidencyContractSpec.ReportsFreshResourcesFullyResident",
    "D3D11.DXGI.Device.Residency.FreshResource.", kResidencyCaseNames.size(), 1,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "Device",
      "CreateBuffer,CreateTexture1D,CreateTexture2D,CreateTexture3D,"
      "IDXGIDeviceQueryResourceResidency"},
     dxmt::test::kResourceTestCost,
     "one test-local D3D11/DXGI device and up to four small live resources per "
     "selected logical case",
     "query fresh buffers and one-, two-, and three-dimensional textures "
     "individually and as one heterogeneous resource array",
     "every fresh test-local resource is reported fully resident",
     "logical ID, selected-case count, resource kind and count, HRESULT, each "
     "residency result, and exact replay argument"});

const dxmt::test::TestCostRegistration kResidencyCost(
    "D3D11DxgiDeviceResidencyContractSpec.ReportsFreshResourcesFullyResident",
    dxmt::test::kResourceTestCost);

class D3D11DxgiDeviceResidencyContractSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_EQ(context_.device()->QueryInterface(
                  __uuidof(IDXGIDevice),
                  reinterpret_cast<void **>(dxgi_device_.put())),
              S_OK);
    ASSERT_NE(dxgi_device_.get(), nullptr);
  }

  std::array<ComPtr<ID3D11Resource>, 4> CreateResources() {
    std::array<ComPtr<ID3D11Resource>, 4> resources;

    D3D11_BUFFER_DESC buffer_desc = {};
    buffer_desc.ByteWidth = 64;
    buffer_desc.Usage = D3D11_USAGE_DEFAULT;
    ComPtr<ID3D11Buffer> buffer;
    EXPECT_EQ(
        context_.device()->CreateBuffer(&buffer_desc, nullptr, buffer.put()),
        S_OK);
    if (buffer) {
      EXPECT_EQ(
          buffer->QueryInterface(__uuidof(ID3D11Resource),
                                 reinterpret_cast<void **>(resources[0].put())),
          S_OK);
    }

    D3D11_TEXTURE1D_DESC texture1d_desc = {};
    texture1d_desc.Width = 8;
    texture1d_desc.MipLevels = 1;
    texture1d_desc.ArraySize = 1;
    texture1d_desc.Format = DXGI_FORMAT_R8_UNORM;
    texture1d_desc.Usage = D3D11_USAGE_DEFAULT;
    ComPtr<ID3D11Texture1D> texture1d;
    EXPECT_EQ(context_.device()->CreateTexture1D(&texture1d_desc, nullptr,
                                                 texture1d.put()),
              S_OK);
    if (texture1d) {
      EXPECT_EQ(texture1d->QueryInterface(
                    __uuidof(ID3D11Resource),
                    reinterpret_cast<void **>(resources[1].put())),
                S_OK);
    }

    D3D11_TEXTURE2D_DESC texture2d_desc = {};
    texture2d_desc.Width = 8;
    texture2d_desc.Height = 8;
    texture2d_desc.MipLevels = 1;
    texture2d_desc.ArraySize = 1;
    texture2d_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texture2d_desc.SampleDesc.Count = 1;
    texture2d_desc.Usage = D3D11_USAGE_DEFAULT;
    ComPtr<ID3D11Texture2D> texture2d;
    EXPECT_EQ(context_.device()->CreateTexture2D(&texture2d_desc, nullptr,
                                                 texture2d.put()),
              S_OK);
    if (texture2d) {
      EXPECT_EQ(texture2d->QueryInterface(
                    __uuidof(ID3D11Resource),
                    reinterpret_cast<void **>(resources[2].put())),
                S_OK);
    }

    D3D11_TEXTURE3D_DESC texture3d_desc = {};
    texture3d_desc.Width = 4;
    texture3d_desc.Height = 4;
    texture3d_desc.Depth = 4;
    texture3d_desc.MipLevels = 1;
    texture3d_desc.Format = DXGI_FORMAT_R8_UNORM;
    texture3d_desc.Usage = D3D11_USAGE_DEFAULT;
    ComPtr<ID3D11Texture3D> texture3d;
    EXPECT_EQ(context_.device()->CreateTexture3D(&texture3d_desc, nullptr,
                                                 texture3d.put()),
              S_OK);
    if (texture3d) {
      EXPECT_EQ(texture3d->QueryInterface(
                    __uuidof(ID3D11Resource),
                    reinterpret_cast<void **>(resources[3].put())),
                S_OK);
    }

    return resources;
  }

  D3D11TestContext context_;
  ComPtr<IDXGIDevice> dxgi_device_;
};

TEST_F(D3D11DxgiDeviceResidencyContractSpec,
       ReportsFreshResourcesFullyResident) {
  std::vector<std::uint32_t> selected_cases;
  for (std::uint32_t logical = 0; logical < kResidencyCaseNames.size();
       ++logical) {
    if (dxmt::test::LogicalCaseSelected(kResidencyRegistration.family(),
                                        logical))
      selected_cases.push_back(logical);
  }
  ASSERT_FALSE(selected_cases.empty());

  const auto resources = CreateResources();
  for (const auto &resource : resources)
    ASSERT_NE(resource.get(), nullptr);

  RecordProperty("logical_cases_executed", selected_cases.size());
  RecordProperty("logical_case_prefix",
                 kResidencyRegistration.family().case_id_prefix);
  for (const std::uint32_t logical : selected_cases) {
    std::vector<IUnknown *> queried_resources;
    if (logical < resources.size()) {
      queried_resources.push_back(resources[logical].get());
    } else {
      for (const auto &resource : resources)
        queried_resources.push_back(resource.get());
    }

    std::vector<DXGI_RESIDENCY> residencies(queried_resources.size(),
                                            DXGI_RESIDENCY_EVICTED_TO_DISK);
    const HRESULT query_result = dxgi_device_->QueryResourceResidency(
        queried_resources.data(), residencies.data(),
        static_cast<UINT>(queried_resources.size()));
    bool fully_resident = query_result == S_OK;
    for (const DXGI_RESIDENCY residency : residencies)
      fully_resident =
          fully_resident && residency == DXGI_RESIDENCY_FULLY_RESIDENT;
    if (fully_resident)
      continue;

    std::ostringstream observed_residencies;
    for (std::size_t index = 0; index < residencies.size(); ++index) {
      if (index)
        observed_residencies << ',';
      observed_residencies << static_cast<UINT>(residencies[index]);
    }

    const auto case_id =
        dxmt::test::LogicalCaseId(kResidencyRegistration.family(), logical);
    ADD_FAILURE() << "LogicalCaseId: " << case_id << '\n'
                  << "Parameters: logical=" << logical
                  << " resource_kind=" << kResidencyCaseNames[logical]
                  << " resource_count=" << queried_resources.size()
                  << " selected_cases=" << selected_cases.size() << '\n'
                  << "Expected: query_hresult=" << S_OK
                  << " residency=" << DXGI_RESIDENCY_FULLY_RESIDENT << '\n'
                  << "Observed: query_hresult=" << query_result
                  << " residencies=(" << observed_residencies.str() << ')'
                  << " residency_count=" << residencies.size() << '\n'
                  << "Replay: --dxmt-case-id=" << case_id;
    break;
  }

  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
