#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <vector>

namespace {

using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

std::uint32_t NextShaderRandom(std::uint32_t &state) {
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return state;
}

class D3D12ShaderContainerFuzzSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    D3D12_ROOT_SIGNATURE_DESC root_desc = {};
    root_ = context_.CreateRootSignature(root_desc);
    ASSERT_TRUE(root_);
    const auto shader =
        CompileShader("[numthreads(1, 1, 1)] void main() {}", "cs_5_0");
    ASSERT_EQ(shader.result, S_OK) << shader.diagnostic_text();
    const auto *begin =
        static_cast<const std::uint8_t *>(shader.bytecode->GetBufferPointer());
    valid_.assign(begin, begin + shader.bytecode->GetBufferSize());
    ASSERT_GT(valid_.size(), 32u);
  }

  D3D12TestContext context_;
  ComPtr<ID3D12RootSignature> root_;
  std::vector<std::uint8_t> valid_;
};

TEST_F(D3D12ShaderContainerFuzzSpec,
       StructuralMutationCorpusFailsClosedAndRecovers) {
  std::uint32_t seed = 0x31415926u;
  std::size_t case_count = 96;
  if (const char *replay_seed = std::getenv("DXMT_D3D12_SHADER_FUZZ_SEED")) {
    char *end = nullptr;
    const auto parsed = std::strtoul(replay_seed, &end, 0);
    ASSERT_NE(end, replay_seed) << "invalid shader fuzz seed";
    ASSERT_EQ(*end, '\0') << "invalid shader fuzz seed";
    seed = static_cast<std::uint32_t>(parsed);
    case_count = 1;
  }

  for (std::size_t case_index = 0; case_index < case_count; ++case_index) {
    const std::uint32_t case_seed = seed;
    SCOPED_TRACE(::testing::Message()
                 << "case=" << case_index << " seed=0x" << std::hex
                 << case_seed);
    auto mutated = valid_;
    const auto store32 = [&](std::size_t offset, std::uint32_t value) {
      ASSERT_LE(offset + sizeof(value), mutated.size());
      std::memcpy(mutated.data() + offset, &value, sizeof(value));
    };
    switch (NextShaderRandom(seed) % 5) {
    case 0:
      mutated.resize(NextShaderRandom(seed) % 32);
      break;
    case 1:
      mutated[NextShaderRandom(seed) % 4] ^=
          static_cast<std::uint8_t>(1u << (NextShaderRandom(seed) % 8));
      break;
    case 2:
      store32(24, (NextShaderRandom(seed) & 1)
                      ? static_cast<std::uint32_t>(mutated.size() + 1 +
                                                   NextShaderRandom(seed))
                      : static_cast<std::uint32_t>(NextShaderRandom(seed) % 24));
      break;
    case 3:
      store32(28, (NextShaderRandom(seed) & 1)
                      ? 0
                      : std::numeric_limits<std::uint32_t>::max());
      break;
    case 4:
      store32(32, (NextShaderRandom(seed) & 1)
                      ? static_cast<std::uint32_t>(mutated.size() +
                                                   NextShaderRandom(seed))
                      : static_cast<std::uint32_t>(NextShaderRandom(seed) % 32));
      break;
    }

    D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
    desc.pRootSignature = root_.get();
    desc.CS = {mutated.data(), mutated.size()};
    ComPtr<ID3D12PipelineState> pipeline;
    EXPECT_EQ(context_.device()->CreateComputePipelineState(
                  &desc, IID_PPV_ARGS(pipeline.put())),
              E_INVALIDARG);
    EXPECT_FALSE(pipeline);
    ASSERT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
  }

  D3D12_COMPUTE_PIPELINE_STATE_DESC proof_desc = {};
  proof_desc.pRootSignature = root_.get();
  proof_desc.CS = {valid_.data(), valid_.size()};
  ComPtr<ID3D12PipelineState> proof;
  EXPECT_EQ(context_.device()->CreateComputePipelineState(
                &proof_desc, IID_PPV_ARGS(proof.put())),
            S_OK);
  EXPECT_TRUE(proof);
}

} // namespace
