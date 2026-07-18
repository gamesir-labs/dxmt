#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"

#include <DXBCParser/d3d12tokenizedprogramformat.hpp>

#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace {

using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

enum class ShaderContainerCorruption {
  EmptyContainer,
  TruncatedHeader,
  HeaderWithoutPartTable,
  BadMagic,
  DeclaredSizeBeforeHeader,
  DeclaredSizePastEnd,
  ZeroPartCount,
  ExcessivePartCount,
  PartOffsetBeforeTable,
  PartOffsetPastEnd,
  FirstPartTagZero,
  FirstPartSizeZero,
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
    case ShaderContainerCorruption::EmptyContainer:
      bytes_.clear();
      break;
    case ShaderContainerCorruption::TruncatedHeader:
      bytes_.resize(kPartTableOffset - 1);
      break;
    case ShaderContainerCorruption::HeaderWithoutPartTable:
      bytes_.resize(kPartTableOffset);
      Store32(kContainerSizeOffset, static_cast<std::uint32_t>(bytes_.size()));
      break;
    case ShaderContainerCorruption::BadMagic:
      Store32(0, 0);
      break;
    case ShaderContainerCorruption::DeclaredSizeBeforeHeader:
      Store32(kContainerSizeOffset, kPartTableOffset - 1);
      break;
    case ShaderContainerCorruption::DeclaredSizePastEnd:
      Store32(kContainerSizeOffset,
              static_cast<std::uint32_t>(bytes_.size() + 1));
      break;
    case ShaderContainerCorruption::ZeroPartCount:
      Store32(kPartCountOffset, 0);
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
    case ShaderContainerCorruption::FirstPartTagZero:
      Store32(Load32(kPartTableOffset), 0);
      break;
    case ShaderContainerCorruption::FirstPartSizeZero:
      Store32(Load32(kPartTableOffset) + sizeof(std::uint32_t), 0);
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
    case ShaderContainerCorruption::EmptyContainer:
      return "EmptyContainer";
    case ShaderContainerCorruption::TruncatedHeader:
      return "TruncatedHeader";
    case ShaderContainerCorruption::HeaderWithoutPartTable:
      return "HeaderWithoutPartTable";
    case ShaderContainerCorruption::BadMagic:
      return "BadMagic";
    case ShaderContainerCorruption::DeclaredSizeBeforeHeader:
      return "DeclaredSizeBeforeHeader";
    case ShaderContainerCorruption::DeclaredSizePastEnd:
      return "DeclaredSizePastEnd";
    case ShaderContainerCorruption::ZeroPartCount:
      return "ZeroPartCount";
    case ShaderContainerCorruption::ExcessivePartCount:
      return "ExcessivePartCount";
    case ShaderContainerCorruption::PartOffsetBeforeTable:
      return "PartOffsetBeforeTable";
    case ShaderContainerCorruption::PartOffsetPastEnd:
      return "PartOffsetPastEnd";
    case ShaderContainerCorruption::FirstPartTagZero:
      return "FirstPartTagZero";
    case ShaderContainerCorruption::FirstPartSizeZero:
      return "FirstPartSizeZero";
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
    ::testing::Values(ShaderContainerCorruption::EmptyContainer,
                      ShaderContainerCorruption::TruncatedHeader,
                      ShaderContainerCorruption::HeaderWithoutPartTable,
                      ShaderContainerCorruption::BadMagic,
                      ShaderContainerCorruption::DeclaredSizeBeforeHeader,
                      ShaderContainerCorruption::DeclaredSizePastEnd,
                      ShaderContainerCorruption::ZeroPartCount,
                      ShaderContainerCorruption::ExcessivePartCount,
                      ShaderContainerCorruption::PartOffsetBeforeTable,
                      ShaderContainerCorruption::PartOffsetPastEnd,
                      ShaderContainerCorruption::FirstPartTagZero,
                      ShaderContainerCorruption::FirstPartSizeZero,
                      ShaderContainerCorruption::PartSizePastEnd,
                      ShaderContainerCorruption::TruncatedPartPayload),
    ShaderContainerSpec::Name);

enum class ShaderInstructionCorruption {
  UnknownOpcode,
  TruncatedInstruction,
  InvalidOperandCount,
  UnsupportedShaderModel,
  StageIncompatibleInstruction,
  UnexpectedElse,
  UnbalancedLoop,
  InstructionLengthOverflow,
};

class ShaderInstructionSpec
    : public ::testing::TestWithParam<ShaderInstructionCorruption> {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    const auto shader =
        CompileShader("[numthreads(1, 1, 1)] void main() {}", "cs_5_0");
    ASSERT_EQ(shader.result, S_OK) << shader.diagnostic_text();
    const auto *begin =
        static_cast<const std::uint8_t *>(shader.bytecode->GetBufferPointer());
    valid_.assign(begin, begin + shader.bytecode->GetBufferSize());
    bytes_ = valid_;
    ASSERT_TRUE(FindProgram());

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

  bool FindProgram() {
    constexpr std::uint32_t kShex =
        std::uint32_t('S') | (std::uint32_t('H') << 8) |
        (std::uint32_t('E') << 16) | (std::uint32_t('X') << 24);
    constexpr std::uint32_t kShdr =
        std::uint32_t('S') | (std::uint32_t('H') << 8) |
        (std::uint32_t('D') << 16) | (std::uint32_t('R') << 24);
    if (bytes_.size() < 32)
      return false;
    const auto part_count = Load32(28);
    if (part_count > (bytes_.size() - 32) / sizeof(std::uint32_t))
      return false;
    for (std::uint32_t part = 0; part < part_count; ++part) {
      const auto part_offset = Load32(32 + part * sizeof(std::uint32_t));
      if (part_offset > bytes_.size() || bytes_.size() - part_offset < 8)
        return false;
      const auto tag = Load32(part_offset);
      if (tag != kShex && tag != kShdr)
        continue;
      const auto part_size = Load32(part_offset + sizeof(std::uint32_t));
      program_offset_ = part_offset + 2 * sizeof(std::uint32_t);
      if (part_size < 3 * sizeof(std::uint32_t) ||
          program_offset_ > bytes_.size() ||
          part_size > bytes_.size() - program_offset_)
        return false;
      token_count_ = Load32(program_offset_ + sizeof(std::uint32_t));
      return token_count_ == part_size / sizeof(std::uint32_t);
    }
    return false;
  }

  std::size_t FindInstruction(std::uint32_t wanted_opcode) const {
    for (std::uint32_t index = 2; index < token_count_;) {
      const auto offset = program_offset_ + index * sizeof(std::uint32_t);
      const auto token = Load32(offset);
      const auto opcode = token & D3D10_SB_OPCODE_TYPE_MASK;
      if (opcode == wanted_opcode)
        return offset;
      const auto length =
          (token & D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH_MASK) >>
          D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH_SHIFT;
      if (!length || length > token_count_ - index)
        return bytes_.size();
      index += length;
    }
    return bytes_.size();
  }

  void ReplaceOpcode(std::size_t offset, std::uint32_t opcode) {
    ASSERT_LT(offset, bytes_.size());
    Store32(offset, (Load32(offset) & ~D3D10_SB_OPCODE_TYPE_MASK) | opcode);
  }

  void ReplaceInstructionLength(std::size_t offset, std::uint32_t length) {
    ASSERT_LT(offset, bytes_.size());
    Store32(offset,
            (Load32(offset) &
             ~D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH_MASK) |
                (length << D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH_SHIFT));
  }

  void Corrupt(ShaderInstructionCorruption corruption) {
    using namespace microsoft;
    const auto thread_group = FindInstruction(D3D11_SB_OPCODE_DCL_THREAD_GROUP);
    const auto ret = FindInstruction(D3D10_SB_OPCODE_RET);
    ASSERT_LT(thread_group, bytes_.size());
    ASSERT_LT(ret, bytes_.size());
    switch (corruption) {
    case ShaderInstructionCorruption::UnknownOpcode:
      ReplaceOpcode(ret, D3D10_SB_OPCODE_TYPE_MASK);
      break;
    case ShaderInstructionCorruption::TruncatedInstruction:
      ReplaceInstructionLength(ret, 2);
      break;
    case ShaderInstructionCorruption::InvalidOperandCount:
      ReplaceInstructionLength(thread_group, 3);
      break;
    case ShaderInstructionCorruption::UnsupportedShaderModel:
      Store32(program_offset_,
              (Load32(program_offset_) & ~std::uint32_t(0xff)) | 0x60);
      break;
    case ShaderInstructionCorruption::StageIncompatibleInstruction:
      ReplaceOpcode(ret, D3D10_SB_OPCODE_EMIT);
      break;
    case ShaderInstructionCorruption::UnexpectedElse:
      ReplaceOpcode(ret, D3D10_SB_OPCODE_ELSE);
      break;
    case ShaderInstructionCorruption::UnbalancedLoop:
      ReplaceOpcode(ret, D3D10_SB_OPCODE_LOOP);
      break;
    case ShaderInstructionCorruption::InstructionLengthOverflow:
      ReplaceOpcode(thread_group, D3D10_SB_OPCODE_CUSTOMDATA);
      Store32(thread_group + sizeof(std::uint32_t),
              std::numeric_limits<std::uint32_t>::max());
      break;
    }
  }

  HRESULT CreatePipeline(const std::vector<std::uint8_t> &bytecode,
                         ComPtr<ID3D12PipelineState> *pipeline) {
    D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
    desc.pRootSignature = root_signature_.get();
    desc.CS = {bytecode.data(), bytecode.size()};
    return context_.device()->CreateComputePipelineState(
        &desc, IID_PPV_ARGS(pipeline->put()));
  }

public:
  static const char *
  Name(const ::testing::TestParamInfo<ShaderInstructionCorruption> &info) {
    switch (info.param) {
    case ShaderInstructionCorruption::UnknownOpcode:
      return "UnknownOpcode";
    case ShaderInstructionCorruption::TruncatedInstruction:
      return "TruncatedInstruction";
    case ShaderInstructionCorruption::InvalidOperandCount:
      return "InvalidOperandCount";
    case ShaderInstructionCorruption::UnsupportedShaderModel:
      return "UnsupportedShaderModel";
    case ShaderInstructionCorruption::StageIncompatibleInstruction:
      return "StageIncompatibleInstruction";
    case ShaderInstructionCorruption::UnexpectedElse:
      return "UnexpectedElse";
    case ShaderInstructionCorruption::UnbalancedLoop:
      return "UnbalancedLoop";
    case ShaderInstructionCorruption::InstructionLengthOverflow:
      return "InstructionLengthOverflow";
    }
    return "Unknown";
  }

protected:
  D3D12TestContext context_;
  ComPtr<ID3D12RootSignature> root_signature_;
  std::vector<std::uint8_t> valid_;
  std::vector<std::uint8_t> bytes_;
  std::size_t program_offset_ = 0;
  std::uint32_t token_count_ = 0;
};

TEST_P(ShaderInstructionSpec, InvalidInstructionCorpusFailsClosedAndRecovers) {
  Corrupt(GetParam());
  ComPtr<ID3D12PipelineState> pipeline;
  EXPECT_EQ(CreatePipeline(bytes_, &pipeline), E_INVALIDARG);
  EXPECT_FALSE(pipeline);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);

  ComPtr<ID3D12PipelineState> proof;
  EXPECT_EQ(CreatePipeline(valid_, &proof), S_OK);
  EXPECT_TRUE(proof);
}

INSTANTIATE_TEST_SUITE_P(
    InvalidInstructions, ShaderInstructionSpec,
    ::testing::Values(ShaderInstructionCorruption::UnknownOpcode,
                      ShaderInstructionCorruption::TruncatedInstruction,
                      ShaderInstructionCorruption::InvalidOperandCount,
                      ShaderInstructionCorruption::UnsupportedShaderModel,
                      ShaderInstructionCorruption::StageIncompatibleInstruction,
                      ShaderInstructionCorruption::UnexpectedElse,
                      ShaderInstructionCorruption::UnbalancedLoop,
                      ShaderInstructionCorruption::InstructionLengthOverflow),
    ShaderInstructionSpec::Name);

} // namespace
