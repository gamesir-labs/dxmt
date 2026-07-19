#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

#include <string>
#include <vector>

// Plan §11 / query heaps: CreateQueryHeap count × type matrix.
// Public D3D12 API only.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;
using dxmt::test::IsSoftwareAdapter;

struct QueryHeapCase {
  D3D12_QUERY_HEAP_TYPE type;
  UINT count;
  bool expect_ok;
};

std::vector<QueryHeapCase> BuildQueryHeapCases() {
  std::vector<QueryHeapCase> cases;
  const D3D12_QUERY_HEAP_TYPE supported[] = {
      D3D12_QUERY_HEAP_TYPE_TIMESTAMP,
      D3D12_QUERY_HEAP_TYPE_OCCLUSION,
      D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS,
      D3D12_QUERY_HEAP_TYPE_SO_STATISTICS,
  };
  const UINT counts[] = {1, 32, 1024};
  for (const auto type : supported) {
    for (const UINT count : counts)
      cases.push_back({type, count, true});
  }
  return cases;
}

class QueryHeapSizeMatrixSpec
    : public ::testing::TestWithParam<QueryHeapCase> {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  D3D12TestContext context_;
};

TEST_P(QueryHeapSizeMatrixSpec, CreateMatchesExpectation) {
  const auto &test = GetParam();
  D3D12_QUERY_HEAP_DESC desc = {};
  desc.Type = test.type;
  desc.Count = test.count;
  desc.NodeMask = 0;
  ComPtr<ID3D12QueryHeap> heap;
  const HRESULT hr =
      context_.device()->CreateQueryHeap(&desc, IID_PPV_ARGS(heap.put()));
  if (test.expect_ok) {
    ASSERT_EQ(hr, S_OK) << "type=" << static_cast<UINT>(test.type)
                        << " count=" << test.count;
    EXPECT_TRUE(heap);
  } else {
    EXPECT_HRESULT_FAILED(hr) << "type=" << static_cast<UINT>(test.type)
                              << " count=" << test.count;
    // Unsupported statistics currently return E_NOTIMPL; invalids E_INVALIDARG.
    EXPECT_FALSE(heap);
  }
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_P(QueryHeapSizeMatrixSpec, CapabilityProbeWithoutObjectMatchesCreate) {
  const auto &test = GetParam();
  D3D12_QUERY_HEAP_DESC desc = {};
  desc.Type = test.type;
  desc.Count = test.count;
  const HRESULT probe = context_.device()->CreateQueryHeap(
      &desc, __uuidof(ID3D12QueryHeap), nullptr);
  ComPtr<ID3D12QueryHeap> heap;
  const HRESULT create =
      context_.device()->CreateQueryHeap(&desc, IID_PPV_ARGS(heap.put()));
  if (test.expect_ok) {
    EXPECT_EQ(probe,
              IsSoftwareAdapter(context_.device()) ? E_UNEXPECTED : S_FALSE);
    EXPECT_EQ(create, S_OK);
  } else {
    EXPECT_HRESULT_FAILED(probe);
    EXPECT_HRESULT_FAILED(create);
    // Probe and create should agree on failure class for the same desc.
    EXPECT_EQ(probe, create);
  }
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

std::string QueryHeapName(const ::testing::TestParamInfo<QueryHeapCase> &info) {
  return "T" + std::to_string(static_cast<UINT>(info.param.type)) + "C" +
         std::to_string(info.param.count) +
         (info.param.expect_ok ? "Ok" : "Bad");
}

INSTANTIATE_TEST_SUITE_P(SizeMatrix, QueryHeapSizeMatrixSpec,
                         ::testing::ValuesIn(BuildQueryHeapCases()),
                         QueryHeapName);

} // namespace
