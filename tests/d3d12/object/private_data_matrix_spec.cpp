#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// Public D3D12 SetPrivateData / GetPrivateData size/content matrix on device.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

struct PrivateDataCase {
  UINT size;
  UINT8 fill;
};

std::vector<PrivateDataCase> BuildPrivateDataCases() {
  std::vector<PrivateDataCase> cases;
  for (UINT size = 1; size <= 256; ++size)
    cases.push_back({size, static_cast<UINT8>(size & 0xff)});
  for (UINT size : {512u, 1024u, 2048u, 4096u})
    cases.push_back({size, 0xa5});
  return cases;
}

// Stable GUID for this matrix (not a Microsoft reserved GUID).
static const GUID kMatrixPrivateDataGuid = {
    0x6b2f9e10,
    0x4c11,
    0x4a8d,
    {0x9e, 0x22, 0x11, 0x33, 0x55, 0x77, 0x99, 0xbb}};

class PrivateDataMatrixSpec
    : public ::testing::TestWithParam<PrivateDataCase> {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  D3D12TestContext context_;
};

TEST_P(PrivateDataMatrixSpec, DeviceRoundTripsExactBytePayload) {
  const auto &test = GetParam();
  std::vector<UINT8> payload(test.size, test.fill);
  ASSERT_EQ(context_.device()->SetPrivateData(kMatrixPrivateDataGuid, test.size,
                                             payload.data()),
            S_OK)
      << "size=" << test.size;

  UINT size = test.size;
  std::vector<UINT8> actual(test.size, 0);
  ASSERT_EQ(context_.device()->GetPrivateData(kMatrixPrivateDataGuid, &size,
                                             actual.data()),
            S_OK);
  EXPECT_EQ(size, test.size);
  EXPECT_EQ(actual, payload);

  // Query size only.
  UINT query_size = 0;
  EXPECT_EQ(context_.device()->GetPrivateData(kMatrixPrivateDataGuid,
                                             &query_size, nullptr),
            S_OK);
  EXPECT_EQ(query_size, test.size);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

std::string PrivateDataName(
    const ::testing::TestParamInfo<PrivateDataCase> &info) {
  return "Size" + std::to_string(info.param.size) + "Fill" +
         std::to_string(info.param.fill);
}

INSTANTIATE_TEST_SUITE_P(SizeMatrix, PrivateDataMatrixSpec,
                         ::testing::ValuesIn(BuildPrivateDataCases()),
                         PrivateDataName);

} // namespace
