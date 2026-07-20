#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <string>

// Public D3D11 query-object contracts derived from native behavior: creation
// validation, descriptor/data-size identity, predicate COM identity, and
// lifecycle/error-path behavior. Every case owns its device and query objects,
// so the default parallel scheduler can run these without serialization.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

struct QueryContractCase {
  D3D11_QUERY type;
  UINT data_size;
  const char *name;
  bool predicate;
};

constexpr std::array kSupportedQueryCases = {
    QueryContractCase{D3D11_QUERY_EVENT, sizeof(BOOL), "Event", false},
    QueryContractCase{D3D11_QUERY_OCCLUSION, sizeof(UINT64), "Occlusion",
                      false},
    QueryContractCase{D3D11_QUERY_TIMESTAMP, sizeof(UINT64), "Timestamp",
                      false},
    QueryContractCase{D3D11_QUERY_TIMESTAMP_DISJOINT,
                      sizeof(D3D11_QUERY_DATA_TIMESTAMP_DISJOINT),
                      "TimestampDisjoint", false},
    QueryContractCase{D3D11_QUERY_PIPELINE_STATISTICS,
                      sizeof(D3D11_QUERY_DATA_PIPELINE_STATISTICS),
                      "PipelineStatistics", false},
    QueryContractCase{D3D11_QUERY_OCCLUSION_PREDICATE, sizeof(BOOL),
                      "OcclusionPredicate", true},
};

constexpr std::array kUnissuedQueryCases = {
    kSupportedQueryCases[0],
    kSupportedQueryCases[2],
    kSupportedQueryCases[3],
    kSupportedQueryCases[4],
};

std::string
QueryCaseName(const ::testing::TestParamInfo<QueryContractCase> &info) {
  return info.param.name;
}

template <typename T>
HRESULT WaitForQueryData(ID3D11DeviceContext *context,
                         ID3D11Asynchronous *query, T *data,
                         UINT attempts = 100) {
  HRESULT hr = S_FALSE;
  for (UINT attempt = 0; attempt < attempts && hr == S_FALSE; ++attempt) {
    hr = context->GetData(query, data, sizeof(*data), 0);
    if (hr == S_FALSE)
      Sleep(1);
  }
  return hr;
}

class D3D11QueryContractSpec
    : public ::testing::TestWithParam<QueryContractCase> {
protected:
  void SetUp() override {
    ASSERT_TRUE(SUCCEEDED(context_.Initialize()));
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_NE(context_.context(), nullptr);
  }

  D3D11TestContext context_;
};

TEST_P(D3D11QueryContractSpec,
       CreateQueryPreservesDescriptorSizeDeviceAndInterfaces) {
  const auto &test = GetParam();
  const D3D11_QUERY_DESC desc = {test.type, 0};

  EXPECT_EQ(context_.device()->CreateQuery(&desc, nullptr), S_FALSE);

  ComPtr<ID3D11Query> query;
  ASSERT_EQ(context_.device()->CreateQuery(&desc, query.put()), S_OK);
  ASSERT_NE(query.get(), nullptr);

  D3D11_QUERY_DESC actual_desc = {};
  query->GetDesc(&actual_desc);
  EXPECT_EQ(actual_desc.Query, desc.Query);
  EXPECT_EQ(actual_desc.MiscFlags, desc.MiscFlags);
  EXPECT_EQ(query->GetDataSize(), test.data_size);

  ComPtr<ID3D11Device> owner;
  query->GetDevice(owner.put());
  EXPECT_EQ(owner.get(), context_.device());

  ComPtr<ID3D11Asynchronous> asynchronous;
  EXPECT_EQ(
      query->QueryInterface(__uuidof(ID3D11Asynchronous),
                            reinterpret_cast<void **>(asynchronous.put())),
      S_OK);
  EXPECT_NE(asynchronous.get(), nullptr);

  ComPtr<ID3D11Query1> query1;
  ASSERT_EQ(query->QueryInterface(__uuidof(ID3D11Query1),
                                  reinterpret_cast<void **>(query1.put())),
            S_OK);
  ASSERT_NE(query1.get(), nullptr);
  D3D11_QUERY_DESC1 actual_desc1 = {};
  query1->GetDesc1(&actual_desc1);
  EXPECT_EQ(actual_desc1.Query, desc.Query);
  EXPECT_EQ(actual_desc1.MiscFlags, desc.MiscFlags);
  EXPECT_EQ(actual_desc1.ContextType, D3D11_CONTEXT_TYPE_ALL);

  ComPtr<ID3D11Predicate> predicate;
  const HRESULT predicate_hr = query->QueryInterface(
      __uuidof(ID3D11Predicate), reinterpret_cast<void **>(predicate.put()));
  if (test.predicate) {
    EXPECT_EQ(predicate_hr, S_OK);
    EXPECT_NE(predicate.get(), nullptr);
  } else {
    EXPECT_EQ(predicate_hr, E_NOINTERFACE);
    EXPECT_EQ(predicate.get(), nullptr);
  }
}

INSTANTIATE_TEST_SUITE_P(SupportedTypes, D3D11QueryContractSpec,
                         ::testing::ValuesIn(kSupportedQueryCases),
                         QueryCaseName);

class D3D11UnissuedQueryContractSpec
    : public ::testing::TestWithParam<QueryContractCase> {
protected:
  void SetUp() override {
    ASSERT_TRUE(SUCCEEDED(context_.Initialize()));
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_NE(context_.context(), nullptr);
  }

  D3D11TestContext context_;
};

TEST_P(D3D11UnissuedQueryContractSpec,
       GetDataRejectsUnissuedAndIncorrectSizesWithoutWriting) {
  const auto &test = GetParam();
  const D3D11_QUERY_DESC desc = {test.type, 0};
  ComPtr<ID3D11Query> query;
  ASSERT_EQ(context_.device()->CreateQuery(&desc, query.put()), S_OK);

  alignas(8) std::array<uint8_t, sizeof(D3D11_QUERY_DATA_PIPELINE_STATISTICS)>
      data;
  data.fill(0xa5);
  const auto poison = data;

  EXPECT_EQ(
      context_.context()->GetData(query.get(), data.data(), test.data_size, 0),
      DXGI_ERROR_INVALID_CALL);
  EXPECT_EQ(data, poison);

  ASSERT_GT(test.data_size, 1u);
  EXPECT_EQ(context_.context()->GetData(query.get(), data.data(),
                                        test.data_size - 1, 0),
            E_INVALIDARG);
  EXPECT_EQ(data, poison);
}

INSTANTIATE_TEST_SUITE_P(UnissuedTypes, D3D11UnissuedQueryContractSpec,
                         ::testing::ValuesIn(kUnissuedQueryCases),
                         QueryCaseName);

class D3D11QueryLifecycleSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_TRUE(SUCCEEDED(context_.Initialize()));
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_NE(context_.context(), nullptr);
  }

  D3D11TestContext context_;
};

TEST_F(D3D11QueryLifecycleSpec, CreationValidatesDescriptionAndPredicateKind) {
  ID3D11Query *query = nullptr;
  EXPECT_EQ(context_.device()->CreateQuery(nullptr, &query), E_INVALIDARG);
  EXPECT_EQ(query, nullptr);

  ID3D11Predicate *predicate = nullptr;
  EXPECT_EQ(context_.device()->CreatePredicate(nullptr, &predicate),
            E_INVALIDARG);
  EXPECT_EQ(predicate, nullptr);

  D3D11_QUERY_DESC desc = {D3D11_QUERY_EVENT, 0};
  EXPECT_EQ(context_.device()->CreatePredicate(&desc, nullptr), E_INVALIDARG);
  EXPECT_EQ(context_.device()->CreatePredicate(&desc, &predicate),
            E_INVALIDARG);
  EXPECT_EQ(predicate, nullptr);

  desc.Query = D3D11_QUERY_OCCLUSION_PREDICATE;
  EXPECT_EQ(context_.device()->CreatePredicate(&desc, nullptr), S_FALSE);
  ASSERT_EQ(context_.device()->CreatePredicate(&desc, &predicate), S_OK);
  ASSERT_NE(predicate, nullptr);
  predicate->Release();
}

TEST_F(D3D11QueryLifecycleSpec, EventCompletesWithTrueDataAndZeroSizeProbe) {
  const D3D11_QUERY_DESC desc = {D3D11_QUERY_EVENT, 0};
  ComPtr<ID3D11Query> query;
  ASSERT_EQ(context_.device()->CreateQuery(&desc, query.put()), S_OK);

  context_.context()->End(query.get());
  context_.context()->Flush();

  BOOL complete = FALSE;
  ASSERT_EQ(WaitForQueryData(context_.context(), query.get(), &complete), S_OK);
  EXPECT_TRUE(complete);
  EXPECT_EQ(context_.context()->GetData(query.get(), nullptr, 0,
                                        D3D11_ASYNC_GETDATA_DONOTFLUSH),
            S_OK);
}

TEST_F(D3D11QueryLifecycleSpec,
       EmptyPipelineStatisticsBracketReturnsZeroedCounters) {
  const D3D11_QUERY_DESC desc = {D3D11_QUERY_PIPELINE_STATISTICS, 0};
  ComPtr<ID3D11Query> query;
  ASSERT_EQ(context_.device()->CreateQuery(&desc, query.put()), S_OK);

  context_.context()->Begin(query.get());
  context_.context()->End(query.get());
  context_.context()->Flush();

  D3D11_QUERY_DATA_PIPELINE_STATISTICS statistics;
  std::memset(&statistics, 0xa5, sizeof(statistics));
  ASSERT_EQ(WaitForQueryData(context_.context(), query.get(), &statistics),
            S_OK);
  const D3D11_QUERY_DATA_PIPELINE_STATISTICS zero = {};
  EXPECT_EQ(std::memcmp(&statistics, &zero, sizeof(statistics)), 0);
}

} // namespace
