#include <dxmt_test.hpp>

#include "dxmt_pipeline_diag.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <thread>

namespace {

TEST(PipelineDiag, IgnoresNullAndReturnsEmptyUnknownEntries) {
  dxmt::RegisterComputePipelineDiagInfo(0, "ignored");
  EXPECT_EQ(dxmt::LookupComputePipelineDiagInfo(0).id, 0u);
  EXPECT_EQ(dxmt::LookupComputePipelineDiagInfo(0x1001).id, 0u);
}

TEST(PipelineDiag, KeepsStableIdsWhileUpdatingCacheKeys) {
  constexpr obj_handle_t first_handle = 0x2001;
  constexpr obj_handle_t second_handle = 0x2002;

  dxmt::RegisterComputePipelineDiagInfo(first_handle, "first-key");
  const auto first = dxmt::LookupComputePipelineDiagInfo(first_handle);
  ASSERT_NE(first.id, 0u);
  EXPECT_EQ(first.shader_cache_key, "first-key");

  dxmt::RegisterComputePipelineDiagInfo(first_handle, "updated-key");
  const auto updated = dxmt::LookupComputePipelineDiagInfo(first_handle);
  EXPECT_EQ(updated.id, first.id);
  EXPECT_EQ(updated.shader_cache_key, "updated-key");

  dxmt::RegisterComputePipelineDiagInfo(second_handle, "second-key");
  const auto second = dxmt::LookupComputePipelineDiagInfo(second_handle);
  EXPECT_NE(second.id, first.id);
  EXPECT_EQ(second.shader_cache_key, "second-key");
}

TEST(PipelineDiag, SerializesConcurrentRegistration) {
  constexpr obj_handle_t handle = 0x3001;
  const std::array<std::string, 4> keys = {"alpha", "beta", "gamma", "delta"};
  std::array<std::thread, keys.size()> workers;
  for (size_t i = 0; i < workers.size(); ++i) {
    workers[i] = std::thread(
        [&, i] { dxmt::RegisterComputePipelineDiagInfo(handle, keys[i]); });
  }
  for (auto &worker : workers)
    worker.join();

  const auto result = dxmt::LookupComputePipelineDiagInfo(handle);
  EXPECT_NE(result.id, 0u);
  EXPECT_NE(std::find(keys.begin(), keys.end(), result.shader_cache_key),
            keys.end());
}

TEST(PipelineDiag, BuildsStableMetalPsoLabel) {
  const std::string key = "0123456789abcdef0123456789abcdef01234567";
  EXPECT_EQ(dxmt::BuildMetalPsoDebugLabel("graphics", key, 64),
            "graphics:" + key);
}

TEST(PipelineDiag, BoundsAdversarialMetalPsoLabelInputs) {
  const auto label = dxmt::BuildMetalPsoDebugLabel(
      std::string(256, 'k'), std::string(256, 'f'), 16);
  EXPECT_EQ(label, "kkkk:ffffffffff");
  EXPECT_EQ(label.size(), 15u);
  EXPECT_TRUE(dxmt::BuildMetalPsoDebugLabel("x", "y", 1).empty());
}

} // namespace
