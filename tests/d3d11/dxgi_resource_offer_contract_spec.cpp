#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <dxgi1_2.h>

#include <array>
#include <cstdint>
#include <limits>
#include <vector>

// Public IDXGIDevice2 resource-offer coverage. Each case creates and reclaims
// only a test-local resource, so no cross-worker locking is required.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

struct PriorityCase {
  DXGI_OFFER_RESOURCE_PRIORITY priority;
  const char *name;
};

constexpr std::array kPriorityCases = {
    PriorityCase{DXGI_OFFER_RESOURCE_PRIORITY_LOW, "Low"},
    PriorityCase{DXGI_OFFER_RESOURCE_PRIORITY_NORMAL, "Normal"},
    PriorityCase{DXGI_OFFER_RESOURCE_PRIORITY_HIGH, "High"},
};

const dxmt::test::LogicalCaseFamilyRegistration kOfferRegistration(
    "D3D11DxgiResourceOfferContractSpec.OffersAndReclaimsDocumentedPriorities",
    "D3D11.DXGI.Device.ResourceOffer.Priority.", kPriorityCases.size(), 1,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "Device",
      "CreateBuffer,IDXGIResource,IDXGIDevice2OfferResources,"
      "IDXGIDevice2ReclaimResources"},
     dxmt::test::kResourceTestCost,
     "one test-local D3D11/DXGI device and one small live buffer per selected "
     "logical case",
     "offer and immediately reclaim a resource at low, normal, and high "
     "documented priorities",
     "both operations succeed and reclaim initializes its discarded-state "
     "output to a valid BOOL",
     "logical ID, selected-case count, priority name and value, HRESULTs, "
     "discarded output, and exact replay argument"});

const dxmt::test::TestCostRegistration kOfferCost(
    "D3D11DxgiResourceOfferContractSpec.OffersAndReclaimsDocumentedPriorities",
    dxmt::test::kResourceTestCost);

class D3D11DxgiResourceOfferContractSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_EQ(context_.device()->QueryInterface(
                  __uuidof(IDXGIDevice2),
                  reinterpret_cast<void **>(dxgi_device2_.put())),
              S_OK);
    ASSERT_NE(dxgi_device2_.get(), nullptr);
  }

  ComPtr<IDXGIResource> CreateResource() {
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = 64;
    desc.Usage = D3D11_USAGE_DEFAULT;
    ComPtr<ID3D11Buffer> buffer;
    EXPECT_EQ(context_.device()->CreateBuffer(&desc, nullptr, buffer.put()),
              S_OK);

    ComPtr<IDXGIResource> resource;
    if (buffer) {
      EXPECT_EQ(
          buffer->QueryInterface(__uuidof(IDXGIResource),
                                 reinterpret_cast<void **>(resource.put())),
          S_OK);
    }
    return resource;
  }

  D3D11TestContext context_;
  ComPtr<IDXGIDevice2> dxgi_device2_;
};

TEST_F(D3D11DxgiResourceOfferContractSpec,
       OffersAndReclaimsDocumentedPriorities) {
  std::vector<std::uint32_t> selected_cases;
  for (std::uint32_t logical = 0; logical < kPriorityCases.size(); ++logical) {
    if (dxmt::test::LogicalCaseSelected(kOfferRegistration.family(), logical))
      selected_cases.push_back(logical);
  }
  ASSERT_FALSE(selected_cases.empty());

  RecordProperty("logical_cases_executed", selected_cases.size());
  RecordProperty("logical_case_prefix",
                 kOfferRegistration.family().case_id_prefix);
  for (const std::uint32_t logical : selected_cases) {
    const PriorityCase &test_case = kPriorityCases[logical];
    ComPtr<IDXGIResource> resource = CreateResource();
    ASSERT_NE(resource.get(), nullptr);
    IDXGIResource *resources[] = {resource.get()};

    const HRESULT offer_result =
        dxgi_device2_->OfferResources(1, resources, test_case.priority);
    BOOL discarded = std::numeric_limits<BOOL>::max();
    const HRESULT reclaim_result =
        dxgi_device2_->ReclaimResources(1, resources, &discarded);
    const bool valid = offer_result == S_OK && reclaim_result == S_OK &&
                       (discarded == FALSE || discarded == TRUE);
    if (valid)
      continue;

    const auto case_id =
        dxmt::test::LogicalCaseId(kOfferRegistration.family(), logical);
    ADD_FAILURE() << "LogicalCaseId: " << case_id << '\n'
                  << "Parameters: logical=" << logical
                  << " priority_name=" << test_case.name
                  << " priority=" << static_cast<UINT>(test_case.priority)
                  << " selected_cases=" << selected_cases.size() << '\n'
                  << "Expected: offer_hresult=" << S_OK
                  << " reclaim_hresult=" << S_OK << " discarded=BOOL" << '\n'
                  << "Observed: offer_hresult=" << offer_result
                  << " reclaim_hresult=" << reclaim_result
                  << " discarded=" << discarded << '\n'
                  << "Replay: --dxmt-case-id=" << case_id;
    break;
  }

  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11DxgiResourceOfferContractSpec,
       ValidatesCountsPrioritiesAndResourceArrays) {
  EXPECT_EQ(dxgi_device2_->OfferResources(0, nullptr,
                                          DXGI_OFFER_RESOURCE_PRIORITY_LOW),
            S_OK);
  EXPECT_EQ(dxgi_device2_->ReclaimResources(0, nullptr, nullptr), S_OK);

  EXPECT_EQ(dxgi_device2_->OfferResources(
                0, nullptr, static_cast<DXGI_OFFER_RESOURCE_PRIORITY>(0)),
            E_INVALIDARG);
  EXPECT_EQ(dxgi_device2_->OfferResources(
                0, nullptr, static_cast<DXGI_OFFER_RESOURCE_PRIORITY>(4)),
            E_INVALIDARG);
  EXPECT_EQ(dxgi_device2_->OfferResources(1, nullptr,
                                          DXGI_OFFER_RESOURCE_PRIORITY_LOW),
            E_INVALIDARG);
  EXPECT_EQ(dxgi_device2_->ReclaimResources(1, nullptr, nullptr), E_INVALIDARG);

  IDXGIResource *null_resource[] = {nullptr};
  EXPECT_EQ(dxgi_device2_->OfferResources(1, null_resource,
                                          DXGI_OFFER_RESOURCE_PRIORITY_LOW),
            E_INVALIDARG);
  EXPECT_EQ(dxgi_device2_->ReclaimResources(1, null_resource, nullptr),
            E_INVALIDARG);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
