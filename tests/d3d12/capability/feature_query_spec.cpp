#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

using dxmt::test::D3D12TestContext;

struct FeatureQueryCase {
  D3D12_FEATURE feature;
  UINT size;
  const char *name;
  void (*initialize)(void *);
};

template <typename T>
void InitializeZero(void *storage) {
  *static_cast<T *>(storage) = {};
}

void InitializeFeatureLevels(void *storage) {
  static constexpr std::array<D3D_FEATURE_LEVEL, 3> levels = {
      D3D_FEATURE_LEVEL_12_0,
      D3D_FEATURE_LEVEL_11_1,
      D3D_FEATURE_LEVEL_11_0,
  };
  auto &data = *static_cast<D3D12_FEATURE_DATA_FEATURE_LEVELS *>(storage);
  data = {};
  data.NumFeatureLevels = static_cast<UINT>(levels.size());
  data.pFeatureLevelsRequested = levels.data();
}

void InitializeArchitecture(void *storage) {
  auto &data = *static_cast<D3D12_FEATURE_DATA_ARCHITECTURE *>(storage);
  data = {};
  data.NodeIndex = 0;
}

void InitializeFormatSupport(void *storage) {
  auto &data = *static_cast<D3D12_FEATURE_DATA_FORMAT_SUPPORT *>(storage);
  data = {};
  data.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
}

void InitializeMultisample(void *storage) {
  auto &data =
      *static_cast<D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS *>(storage);
  data = {};
  data.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  data.SampleCount = 1;
  data.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
}

void InitializeFormatInfo(void *storage) {
  auto &data = *static_cast<D3D12_FEATURE_DATA_FORMAT_INFO *>(storage);
  data = {};
  data.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
}

void InitializeShaderModel(void *storage) {
  auto &data = *static_cast<D3D12_FEATURE_DATA_SHADER_MODEL *>(storage);
  data = {};
  data.HighestShaderModel = D3D_SHADER_MODEL_6_0;
}

void InitializeRootSignature(void *storage) {
  auto &data = *static_cast<D3D12_FEATURE_DATA_ROOT_SIGNATURE *>(storage);
  data = {};
  data.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
}

void InitializeArchitecture1(void *storage) {
  auto &data = *static_cast<D3D12_FEATURE_DATA_ARCHITECTURE1 *>(storage);
  data = {};
  data.NodeIndex = 0;
}

void InitializeQueuePriority(void *storage) {
  auto &data =
      *static_cast<D3D12_FEATURE_DATA_COMMAND_QUEUE_PRIORITY *>(storage);
  data = {};
  data.CommandListType = D3D12_COMMAND_LIST_TYPE_DIRECT;
  data.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
}

void InitializeSerialization(void *storage) {
  auto &data = *static_cast<D3D12_FEATURE_DATA_SERIALIZATION *>(storage);
  data = {};
  data.NodeIndex = 0;
}

std::vector<FeatureQueryCase> BuildFeatureQueryCases() {
#define FEATURE_CASE(feature_name, structure, initializer)                       \
  FeatureQueryCase {                                                            \
    D3D12_FEATURE_##feature_name, sizeof(structure), #feature_name, initializer  \
  }

  std::vector<FeatureQueryCase> cases = {
      FEATURE_CASE(FEATURE_LEVELS, D3D12_FEATURE_DATA_FEATURE_LEVELS,
                   InitializeFeatureLevels),
      FEATURE_CASE(ARCHITECTURE, D3D12_FEATURE_DATA_ARCHITECTURE,
                   InitializeArchitecture),
      FEATURE_CASE(D3D12_OPTIONS, D3D12_FEATURE_DATA_D3D12_OPTIONS,
                   InitializeZero<D3D12_FEATURE_DATA_D3D12_OPTIONS>),
      FEATURE_CASE(FORMAT_SUPPORT, D3D12_FEATURE_DATA_FORMAT_SUPPORT,
                   InitializeFormatSupport),
      FEATURE_CASE(MULTISAMPLE_QUALITY_LEVELS,
                   D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS,
                   InitializeMultisample),
      FEATURE_CASE(FORMAT_INFO, D3D12_FEATURE_DATA_FORMAT_INFO,
                   InitializeFormatInfo),
      FEATURE_CASE(GPU_VIRTUAL_ADDRESS_SUPPORT,
                   D3D12_FEATURE_DATA_GPU_VIRTUAL_ADDRESS_SUPPORT,
                   InitializeZero<
                       D3D12_FEATURE_DATA_GPU_VIRTUAL_ADDRESS_SUPPORT>),
      FEATURE_CASE(SHADER_MODEL, D3D12_FEATURE_DATA_SHADER_MODEL,
                   InitializeShaderModel),
      FEATURE_CASE(D3D12_OPTIONS1, D3D12_FEATURE_DATA_D3D12_OPTIONS1,
                   InitializeZero<D3D12_FEATURE_DATA_D3D12_OPTIONS1>),
      FEATURE_CASE(PROTECTED_RESOURCE_SESSION_SUPPORT,
                   D3D12_FEATURE_DATA_PROTECTED_RESOURCE_SESSION_SUPPORT,
                   InitializeZero<
                       D3D12_FEATURE_DATA_PROTECTED_RESOURCE_SESSION_SUPPORT>),
      FEATURE_CASE(ROOT_SIGNATURE, D3D12_FEATURE_DATA_ROOT_SIGNATURE,
                   InitializeRootSignature),
      FEATURE_CASE(ARCHITECTURE1, D3D12_FEATURE_DATA_ARCHITECTURE1,
                   InitializeArchitecture1),
      FEATURE_CASE(D3D12_OPTIONS2, D3D12_FEATURE_DATA_D3D12_OPTIONS2,
                   InitializeZero<D3D12_FEATURE_DATA_D3D12_OPTIONS2>),
      FEATURE_CASE(SHADER_CACHE, D3D12_FEATURE_DATA_SHADER_CACHE,
                   InitializeZero<D3D12_FEATURE_DATA_SHADER_CACHE>),
      FEATURE_CASE(COMMAND_QUEUE_PRIORITY,
                   D3D12_FEATURE_DATA_COMMAND_QUEUE_PRIORITY,
                   InitializeQueuePriority),
      FEATURE_CASE(D3D12_OPTIONS3, D3D12_FEATURE_DATA_D3D12_OPTIONS3,
                   InitializeZero<D3D12_FEATURE_DATA_D3D12_OPTIONS3>),
      FEATURE_CASE(EXISTING_HEAPS, D3D12_FEATURE_DATA_EXISTING_HEAPS,
                   InitializeZero<D3D12_FEATURE_DATA_EXISTING_HEAPS>),
      FEATURE_CASE(D3D12_OPTIONS4, D3D12_FEATURE_DATA_D3D12_OPTIONS4,
                   InitializeZero<D3D12_FEATURE_DATA_D3D12_OPTIONS4>),
      FEATURE_CASE(SERIALIZATION, D3D12_FEATURE_DATA_SERIALIZATION,
                   InitializeSerialization),
      FEATURE_CASE(CROSS_NODE, D3D12_FEATURE_DATA_CROSS_NODE,
                   InitializeZero<D3D12_FEATURE_DATA_CROSS_NODE>),
      FEATURE_CASE(D3D12_OPTIONS5, D3D12_FEATURE_DATA_D3D12_OPTIONS5,
                   InitializeZero<D3D12_FEATURE_DATA_D3D12_OPTIONS5>),
      FEATURE_CASE(DISPLAYABLE, D3D12_FEATURE_DATA_DISPLAYABLE,
                   InitializeZero<D3D12_FEATURE_DATA_DISPLAYABLE>),
      FEATURE_CASE(PROTECTED_RESOURCE_SESSION_TYPE_COUNT,
                   D3D12_FEATURE_DATA_PROTECTED_RESOURCE_SESSION_TYPE_COUNT,
                   InitializeZero<
                       D3D12_FEATURE_DATA_PROTECTED_RESOURCE_SESSION_TYPE_COUNT>),
      FEATURE_CASE(PROTECTED_RESOURCE_SESSION_TYPES,
                   D3D12_FEATURE_DATA_PROTECTED_RESOURCE_SESSION_TYPES,
                   InitializeZero<
                       D3D12_FEATURE_DATA_PROTECTED_RESOURCE_SESSION_TYPES>),
#ifdef __ID3D12GraphicsCommandList4_INTERFACE_DEFINED__
      FEATURE_CASE(D3D12_OPTIONS6, D3D12_FEATURE_DATA_D3D12_OPTIONS6,
                   InitializeZero<D3D12_FEATURE_DATA_D3D12_OPTIONS6>),
      FEATURE_CASE(D3D12_OPTIONS7, D3D12_FEATURE_DATA_D3D12_OPTIONS7,
                   InitializeZero<D3D12_FEATURE_DATA_D3D12_OPTIONS7>),
      FEATURE_CASE(D3D12_OPTIONS8, D3D12_FEATURE_DATA_D3D12_OPTIONS8,
                   InitializeZero<D3D12_FEATURE_DATA_D3D12_OPTIONS8>),
      FEATURE_CASE(D3D12_OPTIONS9, D3D12_FEATURE_DATA_D3D12_OPTIONS9,
                   InitializeZero<D3D12_FEATURE_DATA_D3D12_OPTIONS9>),
      FEATURE_CASE(D3D12_OPTIONS10, D3D12_FEATURE_DATA_D3D12_OPTIONS10,
                   InitializeZero<D3D12_FEATURE_DATA_D3D12_OPTIONS10>),
      FEATURE_CASE(D3D12_OPTIONS11, D3D12_FEATURE_DATA_D3D12_OPTIONS11,
                   InitializeZero<D3D12_FEATURE_DATA_D3D12_OPTIONS11>),
      FEATURE_CASE(D3D12_OPTIONS12, D3D12_FEATURE_DATA_D3D12_OPTIONS12,
                   InitializeZero<D3D12_FEATURE_DATA_D3D12_OPTIONS12>),
      FEATURE_CASE(D3D12_OPTIONS13, D3D12_FEATURE_DATA_D3D12_OPTIONS13,
                   InitializeZero<D3D12_FEATURE_DATA_D3D12_OPTIONS13>),
      FEATURE_CASE(D3D12_OPTIONS14, D3D12_FEATURE_DATA_D3D12_OPTIONS14,
                   InitializeZero<D3D12_FEATURE_DATA_D3D12_OPTIONS14>),
      FEATURE_CASE(D3D12_OPTIONS15, D3D12_FEATURE_DATA_D3D12_OPTIONS15,
                   InitializeZero<D3D12_FEATURE_DATA_D3D12_OPTIONS15>),
      FEATURE_CASE(D3D12_OPTIONS16, D3D12_FEATURE_DATA_D3D12_OPTIONS16,
                   InitializeZero<D3D12_FEATURE_DATA_D3D12_OPTIONS16>),
      FEATURE_CASE(D3D12_OPTIONS17, D3D12_FEATURE_DATA_D3D12_OPTIONS17,
                   InitializeZero<D3D12_FEATURE_DATA_D3D12_OPTIONS17>),
      FEATURE_CASE(D3D12_OPTIONS18, D3D12_FEATURE_DATA_D3D12_OPTIONS18,
                   InitializeZero<D3D12_FEATURE_DATA_D3D12_OPTIONS18>),
#endif
  };
#undef FEATURE_CASE
  return cases;
}

class D3D12FeatureQueryContractSpec
    : public ::testing::Test,
      public ::testing::WithParamInterface<FeatureQueryCase> {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.InitializeSharedDevice("feature-query-contract"), S_OK);
  }

  static constexpr std::uint8_t kSentinel = 0xa5;
  static constexpr UINT kGuardSize = 16;
  D3D12TestContext context_;
};

TEST_P(D3D12FeatureQueryContractSpec,
       ExactStructureSizeSucceedsWithoutOverwritingGuard) {
  const auto &query = GetParam();
  std::vector<std::uint8_t> storage(query.size + kGuardSize, kSentinel);
  query.initialize(storage.data());

  ASSERT_EQ(context_.device()->CheckFeatureSupport(
                query.feature, storage.data(), query.size),
            S_OK);
  EXPECT_TRUE(std::all_of(storage.begin() + query.size, storage.end(),
                          [](std::uint8_t byte) { return byte == kSentinel; }));
}

TEST_P(D3D12FeatureQueryContractSpec,
       StructureSizeMinusOneFailsWithoutMutation) {
  const auto &query = GetParam();
  std::vector<std::uint8_t> storage(query.size + kGuardSize, kSentinel);
  query.initialize(storage.data());
  const auto before = storage;

  EXPECT_EQ(context_.device()->CheckFeatureSupport(
                query.feature, storage.data(), query.size - 1),
            E_INVALIDARG);
  EXPECT_EQ(storage, before);
}

TEST_P(D3D12FeatureQueryContractSpec,
       StructureSizePlusOneFailsWithoutMutation) {
  const auto &query = GetParam();
  std::vector<std::uint8_t> storage(query.size + kGuardSize, kSentinel);
  query.initialize(storage.data());
  const auto before = storage;

  EXPECT_EQ(context_.device()->CheckFeatureSupport(
                query.feature, storage.data(), query.size + 1),
            E_INVALIDARG);
  EXPECT_EQ(storage, before);
}

INSTANTIATE_TEST_SUITE_P(
    SupportedFeatureMatrix, D3D12FeatureQueryContractSpec,
    ::testing::ValuesIn(BuildFeatureQueryCases()),
    [](const ::testing::TestParamInfo<FeatureQueryCase> &info) {
      return std::string(info.param.name);
    });

class D3D12FeatureQueryInvalidInputSpec : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }
  D3D12TestContext context_;
};

TEST_F(D3D12FeatureQueryInvalidInputSpec,
       UnknownFeatureFailsWithoutMutatingData) {
  std::array<std::uint8_t, 32> data = {};
  data.fill(0x5a);
  const auto before = data;
  EXPECT_EQ(context_.device()->CheckFeatureSupport(
                static_cast<D3D12_FEATURE>(0x7fffffff), data.data(),
                static_cast<UINT>(data.size())),
            DXGI_ERROR_UNSUPPORTED);
  EXPECT_EQ(data, before);
}

TEST_F(D3D12FeatureQueryInvalidInputSpec,
       FeatureLevelsRejectsNullRequestedArrayWithoutMutation) {
  D3D12_FEATURE_DATA_FEATURE_LEVELS data = {};
  data.NumFeatureLevels = 1;
  data.pFeatureLevelsRequested = nullptr;
  data.MaxSupportedFeatureLevel = D3D_FEATURE_LEVEL_9_3;
  const auto before = data;
  EXPECT_EQ(context_.device()->CheckFeatureSupport(
                D3D12_FEATURE_FEATURE_LEVELS, &data, sizeof(data)),
            E_INVALIDARG);
  EXPECT_EQ(std::memcmp(&data, &before, sizeof(data)), 0);
}

TEST_F(D3D12FeatureQueryInvalidInputSpec,
       FeatureLevelsRejectsZeroCountWithoutMutation) {
  constexpr D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_12_0;
  D3D12_FEATURE_DATA_FEATURE_LEVELS data = {};
  data.NumFeatureLevels = 0;
  data.pFeatureLevelsRequested = &level;
  data.MaxSupportedFeatureLevel = D3D_FEATURE_LEVEL_9_3;
  const auto before = data;
  EXPECT_EQ(context_.device()->CheckFeatureSupport(
                D3D12_FEATURE_FEATURE_LEVELS, &data, sizeof(data)),
            E_INVALIDARG);
  EXPECT_EQ(std::memcmp(&data, &before, sizeof(data)), 0);
}

TEST_F(D3D12FeatureQueryInvalidInputSpec,
       FeatureLevelsIgnoresUnknownAndDuplicateEntries) {
  const std::array<D3D_FEATURE_LEVEL, 4> levels = {
      static_cast<D3D_FEATURE_LEVEL>(0x7fffffff),
      D3D_FEATURE_LEVEL_11_0,
      D3D_FEATURE_LEVEL_12_0,
      D3D_FEATURE_LEVEL_12_0,
  };
  D3D12_FEATURE_DATA_FEATURE_LEVELS data = {};
  data.NumFeatureLevels = static_cast<UINT>(levels.size());
  data.pFeatureLevelsRequested = levels.data();
  ASSERT_EQ(context_.device()->CheckFeatureSupport(
                D3D12_FEATURE_FEATURE_LEVELS, &data, sizeof(data)),
            S_OK);
  EXPECT_EQ(data.MaxSupportedFeatureLevel, D3D_FEATURE_LEVEL_12_0);
}

TEST_F(D3D12FeatureQueryInvalidInputSpec,
       RootSignatureRejectsUnknownVersionWithoutMutation) {
  D3D12_FEATURE_DATA_ROOT_SIGNATURE data = {};
  data.HighestVersion = static_cast<D3D_ROOT_SIGNATURE_VERSION>(0x7fffffff);
  const auto before = data;
  EXPECT_EQ(context_.device()->CheckFeatureSupport(
                D3D12_FEATURE_ROOT_SIGNATURE, &data, sizeof(data)),
            E_INVALIDARG);
  EXPECT_EQ(std::memcmp(&data, &before, sizeof(data)), 0);
}

TEST_F(D3D12FeatureQueryInvalidInputSpec,
       MultisampleZeroSampleCountFailsAndClearsOutput) {
  D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS data = {};
  data.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  data.SampleCount = 0;
  data.NumQualityLevels = 0xfeedu;
  EXPECT_EQ(context_.device()->CheckFeatureSupport(
                D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &data,
                sizeof(data)),
            E_FAIL);
  EXPECT_EQ(data.NumQualityLevels, 0u);
}

TEST_F(D3D12FeatureQueryInvalidInputSpec,
       MultisampleQualityQueryReturnsAStablePublicResult) {
  D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS data = {};
  data.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  data.SampleCount = 1;
  data.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
  EXPECT_EQ(context_.device()->CheckFeatureSupport(
                D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &data,
                sizeof(data)),
            S_OK);
  EXPECT_GE(data.NumQualityLevels, 1u);
}

TEST_F(D3D12FeatureQueryInvalidInputSpec,
       MetaCommandQueryReportsNotFoundWithoutMutation) {
  D3D12_FEATURE_DATA_QUERY_META_COMMAND data = {};
  data.NodeMask = 0;
  const auto before = data;
  EXPECT_EQ(context_.device()->CheckFeatureSupport(
                D3D12_FEATURE_QUERY_META_COMMAND, &data, sizeof(data)),
            DXGI_ERROR_NOT_FOUND);
  EXPECT_EQ(std::memcmp(&data, &before, sizeof(data)), 0);
}

TEST_F(D3D12FeatureQueryInvalidInputSpec,
       MetaCommandQueryValidatesSizeAndNodeMask) {
  D3D12_FEATURE_DATA_QUERY_META_COMMAND data = {};
  const auto before = data;
  EXPECT_EQ(context_.device()->CheckFeatureSupport(
                D3D12_FEATURE_QUERY_META_COMMAND, &data, sizeof(data) - 1),
            E_INVALIDARG);
  EXPECT_EQ(std::memcmp(&data, &before, sizeof(data)), 0);

  data.NodeMask = 2;
  const auto invalid_node = data;
  EXPECT_EQ(context_.device()->CheckFeatureSupport(
                D3D12_FEATURE_QUERY_META_COMMAND, &data, sizeof(data)),
            E_INVALIDARG);
  EXPECT_EQ(std::memcmp(&data, &invalid_node, sizeof(data)), 0);
}

TEST_F(D3D12FeatureQueryInvalidInputSpec, NodeQueriesRejectNonZeroNode) {
  D3D12_FEATURE_DATA_ARCHITECTURE architecture = {};
  architecture.NodeIndex = 1;
  EXPECT_EQ(context_.device()->CheckFeatureSupport(
                D3D12_FEATURE_ARCHITECTURE, &architecture,
                sizeof(architecture)),
            E_INVALIDARG);

  D3D12_FEATURE_DATA_ARCHITECTURE1 architecture1 = {};
  architecture1.NodeIndex = 1;
  EXPECT_EQ(context_.device()->CheckFeatureSupport(
                D3D12_FEATURE_ARCHITECTURE1, &architecture1,
                sizeof(architecture1)),
            E_INVALIDARG);

  D3D12_FEATURE_DATA_SERIALIZATION serialization = {};
  serialization.NodeIndex = 1;
  EXPECT_EQ(context_.device()->CheckFeatureSupport(
                D3D12_FEATURE_SERIALIZATION, &serialization,
                sizeof(serialization)),
            E_INVALIDARG);

  D3D12_FEATURE_DATA_PROTECTED_RESOURCE_SESSION_SUPPORT protected_support = {};
  protected_support.NodeIndex = 1;
  EXPECT_EQ(context_.device()->CheckFeatureSupport(
                D3D12_FEATURE_PROTECTED_RESOURCE_SESSION_SUPPORT,
                &protected_support, sizeof(protected_support)),
            E_INVALIDARG);

  D3D12_FEATURE_DATA_PROTECTED_RESOURCE_SESSION_TYPE_COUNT protected_count = {};
  protected_count.NodeIndex = 1;
  EXPECT_EQ(context_.device()->CheckFeatureSupport(
                D3D12_FEATURE_PROTECTED_RESOURCE_SESSION_TYPE_COUNT,
                &protected_count, sizeof(protected_count)),
            E_INVALIDARG);

  D3D12_FEATURE_DATA_PROTECTED_RESOURCE_SESSION_TYPES protected_types = {};
  protected_types.NodeIndex = 1;
  EXPECT_EQ(context_.device()->CheckFeatureSupport(
                D3D12_FEATURE_PROTECTED_RESOURCE_SESSION_TYPES,
                &protected_types, sizeof(protected_types)),
            E_INVALIDARG);
}

TEST_F(D3D12FeatureQueryInvalidInputSpec,
       ProtectedSessionTypesRejectsMissingOutputArray) {
  D3D12_FEATURE_DATA_PROTECTED_RESOURCE_SESSION_TYPES data = {};
  data.NodeIndex = 0;
  data.Count = 1;
  data.pTypes = nullptr;
  const auto before = data;
  EXPECT_EQ(context_.device()->CheckFeatureSupport(
                D3D12_FEATURE_PROTECTED_RESOURCE_SESSION_TYPES, &data,
                sizeof(data)),
            E_INVALIDARG);
  EXPECT_EQ(std::memcmp(&data, &before, sizeof(data)), 0);
}

} // namespace
