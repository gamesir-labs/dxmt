#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"

#include <cstring>
#include <limits>
#include <vector>

namespace {

using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

enum class ShaderContainerCorruption {
  TruncatedHeader,
  BadMagic,
  DeclaredSizePastEnd,
  ExcessivePartCount,
  PartOffsetBeforeTable,
  PartOffsetPastEnd,
  PartSizePastEnd,
  TruncatedPartPayload,
};

class ShaderContainerSpec
    : public ::testing::TestWithParam<ShaderContainerCorruption> {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    const auto shader =
        CompileShader("[numthreads(1, 1, 1)] void main() {}", "cs_5_0");
    ASSERT_EQ(shader.result, S_OK) << shader.diagnostic_text();
    const auto *begin =
        static_cast<const std::uint8_t *>(shader.bytecode->GetBufferPointer());
    bytes_.assign(begin, begin + shader.bytecode->GetBufferSize());
    ASSERT_GT(bytes_.size(), 36u);

    D3D12_ROOT_SIGNATURE_DESC root_desc = {};
    root_signature_ = context_.CreateRootSignature(root_desc);
    ASSERT_TRUE(root_signature_);
  }

  std::uint32_t Load32(std::size_t offset) const {
    std::uint32_t value = 0;
    EXPECT_LE(offset + sizeof(value), bytes_.size());
    std::memcpy(&value, bytes_.data() + offset, sizeof(value));
    return value;
  }

  void Store32(std::size_t offset, std::uint32_t value) {
    ASSERT_LE(offset + sizeof(value), bytes_.size());
    std::memcpy(bytes_.data() + offset, &value, sizeof(value));
  }

  void Corrupt(ShaderContainerCorruption corruption) {
    constexpr std::size_t kContainerSizeOffset = 24;
    constexpr std::size_t kPartCountOffset = 28;
    constexpr std::size_t kPartTableOffset = 32;
    switch (corruption) {
    case ShaderContainerCorruption::TruncatedHeader:
      bytes_.resize(kPartTableOffset - 1);
      break;
    case ShaderContainerCorruption::BadMagic:
      Store32(0, 0);
      break;
    case ShaderContainerCorruption::DeclaredSizePastEnd:
      Store32(kContainerSizeOffset,
              static_cast<std::uint32_t>(bytes_.size() + 1));
      break;
    case ShaderContainerCorruption::ExcessivePartCount:
      Store32(kPartCountOffset, std::numeric_limits<std::uint32_t>::max());
      break;
    case ShaderContainerCorruption::PartOffsetBeforeTable:
      Store32(kPartTableOffset, kPartTableOffset);
      break;
    case ShaderContainerCorruption::PartOffsetPastEnd:
      Store32(kPartTableOffset, static_cast<std::uint32_t>(bytes_.size()));
      break;
    case ShaderContainerCorruption::PartSizePastEnd:
      Store32(Load32(kPartTableOffset) + sizeof(std::uint32_t),
              std::numeric_limits<std::uint32_t>::max());
      break;
    case ShaderContainerCorruption::TruncatedPartPayload:
      bytes_.pop_back();
      Store32(kContainerSizeOffset, static_cast<std::uint32_t>(bytes_.size()));
      break;
    }
  }

public:
  static const char *
  Name(const ::testing::TestParamInfo<ShaderContainerCorruption> &info) {
    switch (info.param) {
    case ShaderContainerCorruption::TruncatedHeader:
      return "TruncatedHeader";
    case ShaderContainerCorruption::BadMagic:
      return "BadMagic";
    case ShaderContainerCorruption::DeclaredSizePastEnd:
      return "DeclaredSizePastEnd";
    case ShaderContainerCorruption::ExcessivePartCount:
      return "ExcessivePartCount";
    case ShaderContainerCorruption::PartOffsetBeforeTable:
      return "PartOffsetBeforeTable";
    case ShaderContainerCorruption::PartOffsetPastEnd:
      return "PartOffsetPastEnd";
    case ShaderContainerCorruption::PartSizePastEnd:
      return "PartSizePastEnd";
    case ShaderContainerCorruption::TruncatedPartPayload:
      return "TruncatedPartPayload";
    }
    return "Unknown";
  }

protected:
  D3D12TestContext context_;
  ComPtr<ID3D12RootSignature> root_signature_;
  std::vector<std::uint8_t> bytes_;
};

TEST_P(ShaderContainerSpec, CorruptionCorpusIsRejected) {
  Corrupt(GetParam());
  D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
  desc.pRootSignature = root_signature_.get();
  desc.CS = {bytes_.data(), bytes_.size()};
  ComPtr<ID3D12PipelineState> pipeline;

  EXPECT_EQ(context_.device()->CreateComputePipelineState(
                &desc, IID_PPV_ARGS(pipeline.put())),
            E_INVALIDARG);
  EXPECT_FALSE(pipeline);
}

INSTANTIATE_TEST_SUITE_P(
    InvalidContainers, ShaderContainerSpec,
    ::testing::Values(ShaderContainerCorruption::TruncatedHeader,
                      ShaderContainerCorruption::BadMagic,
                      ShaderContainerCorruption::DeclaredSizePastEnd,
                      ShaderContainerCorruption::ExcessivePartCount,
                      ShaderContainerCorruption::PartOffsetBeforeTable,
                      ShaderContainerCorruption::PartOffsetPastEnd,
                      ShaderContainerCorruption::PartSizePastEnd,
                      ShaderContainerCorruption::TruncatedPartPayload),
    ShaderContainerSpec::Name);

} // namespace
