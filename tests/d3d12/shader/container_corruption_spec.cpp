#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace {

using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

constexpr std::uint32_t FourCC(char a, char b, char c, char d) {
  return std::uint32_t(std::uint8_t(a)) |
         (std::uint32_t(std::uint8_t(b)) << 8) |
         (std::uint32_t(std::uint8_t(c)) << 16) |
         (std::uint32_t(std::uint8_t(d)) << 24);
}

std::uint32_t ReadU32(const std::vector<std::uint8_t> &bytes,
                      std::size_t offset) {
  std::uint32_t value = 0;
  std::memcpy(&value, bytes.data() + offset, sizeof(value));
  return value;
}

void WriteU32(std::vector<std::uint8_t> &bytes, std::size_t offset,
              std::uint32_t value) {
  std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

struct ShaderContainerLayout {
  std::uint32_t chunk_count = 0;
  std::uint32_t first_chunk_offset = 0;
  std::uint32_t first_chunk_size = 0;
  std::uint32_t shader_chunk_offset = 0;
  std::uint32_t shader_chunk_size = 0;
};

bool InspectShaderContainer(const std::vector<std::uint8_t> &bytes,
                            ShaderContainerLayout *layout) {
  if (!layout || bytes.size() < 36 ||
      ReadU32(bytes, 0) != FourCC('D', 'X', 'B', 'C'))
    return false;

  const std::uint32_t chunk_count = ReadU32(bytes, 28);
  if (!chunk_count || chunk_count > (bytes.size() - 32) / sizeof(std::uint32_t))
    return false;

  layout->chunk_count = chunk_count;
  for (std::uint32_t index = 0; index < chunk_count; ++index) {
    const std::uint32_t offset = ReadU32(bytes, 32 + index * 4);
    if (offset > bytes.size() || bytes.size() - offset < 8)
      return false;
    const std::uint32_t size = ReadU32(bytes, offset + 4);
    if (size > bytes.size() - offset - 8)
      return false;

    if (!index) {
      layout->first_chunk_offset = offset;
      layout->first_chunk_size = size;
    }
    const std::uint32_t tag = ReadU32(bytes, offset);
    if (tag == FourCC('S', 'H', 'D', 'R') ||
        tag == FourCC('S', 'H', 'E', 'X')) {
      layout->shader_chunk_offset = offset;
      layout->shader_chunk_size = size;
    }
  }
  return layout->first_chunk_size && layout->shader_chunk_size >= 12;
}

enum class ShaderContainerCorruption {
  TruncatedMagic,
  TruncatedChecksum,
  TruncatedVersion,
  TruncatedContainerSize,
  TruncatedChunkCount,
  TruncatedChunkTable,
  TruncatedFirstChunkHeader,
  TruncatedFirstChunkPayload,
  TruncatedShaderChunkHeader,
  TruncatedShaderChunkPayload,
  TruncatedContainerTail,
  InvalidMagic,
  InvalidChecksum,
  UnsupportedContainerVersion,
  ContainerSizeBelowHeader,
  ContainerSizePastInput,
  ZeroChunkCount,
  ExcessiveChunkCount,
  ChunkOffsetBeforeTable,
  ChunkOffsetPastEnd,
  ChunkSizeOverflow,
  MissingShaderChunk,
  UnalignedShaderChunk,
  ZeroShaderTokenCount,
  ShaderTokenCountPastChunk,
  MismatchedShaderStage,
  UnsupportedShaderModel,
  ZeroInstructionLength,
  InstructionLengthPastEnd,
};

struct ShaderContainerCase {
  const char *name;
  ShaderContainerCorruption corruption;
};

struct CorruptedShader {
  std::vector<std::uint8_t> bytes;
  std::size_t reported_size = 0;
};

CorruptedShader CorruptShader(const std::vector<std::uint8_t> &valid,
                              const ShaderContainerLayout &layout,
                              ShaderContainerCorruption corruption) {
  CorruptedShader result = {valid, valid.size()};
  const std::size_t table_end =
      32 + std::size_t(layout.chunk_count) * sizeof(std::uint32_t);
  const std::size_t shader_data = layout.shader_chunk_offset + 8;

  switch (corruption) {
  case ShaderContainerCorruption::TruncatedMagic:
    result.reported_size = 3;
    break;
  case ShaderContainerCorruption::TruncatedChecksum:
    result.reported_size = 19;
    break;
  case ShaderContainerCorruption::TruncatedVersion:
    result.reported_size = 23;
    break;
  case ShaderContainerCorruption::TruncatedContainerSize:
    result.reported_size = 27;
    break;
  case ShaderContainerCorruption::TruncatedChunkCount:
    result.reported_size = 31;
    break;
  case ShaderContainerCorruption::TruncatedChunkTable:
    result.reported_size = table_end - 1;
    break;
  case ShaderContainerCorruption::TruncatedFirstChunkHeader:
    result.reported_size = layout.first_chunk_offset + 7;
    break;
  case ShaderContainerCorruption::TruncatedFirstChunkPayload:
    result.reported_size =
        layout.first_chunk_offset + 8 + layout.first_chunk_size - 1;
    break;
  case ShaderContainerCorruption::TruncatedShaderChunkHeader:
    result.reported_size = layout.shader_chunk_offset + 7;
    break;
  case ShaderContainerCorruption::TruncatedShaderChunkPayload:
    result.reported_size =
        layout.shader_chunk_offset + 8 + layout.shader_chunk_size - 1;
    break;
  case ShaderContainerCorruption::TruncatedContainerTail:
    result.reported_size = valid.size() - 1;
    break;
  case ShaderContainerCorruption::InvalidMagic:
    WriteU32(result.bytes, 0, FourCC('N', 'O', 'P', 'E'));
    break;
  case ShaderContainerCorruption::InvalidChecksum:
    result.bytes[4] ^= 1;
    break;
  case ShaderContainerCorruption::UnsupportedContainerVersion:
    WriteU32(result.bytes, 20, 2);
    break;
  case ShaderContainerCorruption::ContainerSizeBelowHeader:
    WriteU32(result.bytes, 24, 31);
    break;
  case ShaderContainerCorruption::ContainerSizePastInput:
    WriteU32(result.bytes, 24, static_cast<std::uint32_t>(valid.size() + 4));
    break;
  case ShaderContainerCorruption::ZeroChunkCount:
    WriteU32(result.bytes, 28, 0);
    break;
  case ShaderContainerCorruption::ExcessiveChunkCount:
    WriteU32(result.bytes, 28, std::numeric_limits<std::uint32_t>::max());
    break;
  case ShaderContainerCorruption::ChunkOffsetBeforeTable:
    WriteU32(result.bytes, 32, 0);
    break;
  case ShaderContainerCorruption::ChunkOffsetPastEnd:
    WriteU32(result.bytes, 32, static_cast<std::uint32_t>(valid.size()));
    break;
  case ShaderContainerCorruption::ChunkSizeOverflow:
    WriteU32(result.bytes, layout.first_chunk_offset + 4,
             std::numeric_limits<std::uint32_t>::max());
    break;
  case ShaderContainerCorruption::MissingShaderChunk:
    WriteU32(result.bytes, layout.shader_chunk_offset,
             FourCC('N', 'O', 'N', 'E'));
    break;
  case ShaderContainerCorruption::UnalignedShaderChunk:
    WriteU32(result.bytes, layout.shader_chunk_offset + 4,
             layout.shader_chunk_size - 1);
    break;
  case ShaderContainerCorruption::ZeroShaderTokenCount:
    WriteU32(result.bytes, shader_data + 4, 0);
    break;
  case ShaderContainerCorruption::ShaderTokenCountPastChunk:
    WriteU32(result.bytes, shader_data + 4,
             layout.shader_chunk_size / sizeof(std::uint32_t) + 1);
    break;
  case ShaderContainerCorruption::MismatchedShaderStage: {
    const std::uint32_t version = ReadU32(result.bytes, shader_data);
    WriteU32(result.bytes, shader_data, version & 0x0000ffffu);
    break;
  }
  case ShaderContainerCorruption::UnsupportedShaderModel: {
    const std::uint32_t version = ReadU32(result.bytes, shader_data);
    WriteU32(result.bytes, shader_data, (version & 0xffff0000u) | 0xf0u);
    break;
  }
  case ShaderContainerCorruption::ZeroInstructionLength: {
    constexpr std::uint32_t kInstructionLengthMask = 0x7f000000u;
    const std::uint32_t instruction = ReadU32(result.bytes, shader_data + 8);
    WriteU32(result.bytes, shader_data + 8,
             instruction & ~kInstructionLengthMask);
    break;
  }
  case ShaderContainerCorruption::InstructionLengthPastEnd: {
    constexpr std::uint32_t kInstructionLengthMask = 0x7f000000u;
    const std::uint32_t instruction = ReadU32(result.bytes, shader_data + 8);
    WriteU32(result.bytes, shader_data + 8,
             (instruction & ~kInstructionLengthMask) | kInstructionLengthMask);
    break;
  }
  }
  return result;
}

class ShaderContainerSpec
    : public ::testing::TestWithParam<ShaderContainerCase> {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);

    D3D12_ROOT_SIGNATURE_DESC root_desc = {};
    root_signature_ = context_.CreateRootSignature(root_desc);
    ASSERT_TRUE(root_signature_);

    const auto shader =
        CompileShader("[numthreads(1, 1, 1)] void main() {}", "cs_5_0");
    ASSERT_EQ(shader.result, S_OK) << shader.diagnostic_text();
    const auto *data =
        static_cast<const std::uint8_t *>(shader.bytecode->GetBufferPointer());
    shader_.assign(data, data + shader.bytecode->GetBufferSize());
    ASSERT_TRUE(InspectShaderContainer(shader_, &layout_));
  }

  D3D12TestContext context_;
  ComPtr<ID3D12RootSignature> root_signature_;
  std::vector<std::uint8_t> shader_;
  ShaderContainerLayout layout_ = {};
};

TEST_P(ShaderContainerSpec, CorruptionCorpus) {
  D3D12_COMPUTE_PIPELINE_STATE_DESC valid_desc = {};
  valid_desc.pRootSignature = root_signature_.get();
  valid_desc.CS = {shader_.data(), shader_.size()};
  ComPtr<ID3D12PipelineState> valid_pipeline;
  ASSERT_EQ(context_.device()->CreateComputePipelineState(
                &valid_desc, __uuidof(ID3D12PipelineState),
                reinterpret_cast<void **>(valid_pipeline.put())),
            S_OK);
  ASSERT_TRUE(valid_pipeline);

  const auto corrupted = CorruptShader(shader_, layout_, GetParam().corruption);
  ASSERT_LE(corrupted.reported_size, corrupted.bytes.size());

  D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
  desc.pRootSignature = root_signature_.get();
  desc.CS = {corrupted.bytes.data(), corrupted.reported_size};
  ComPtr<ID3D12PipelineState> pipeline;
  const HRESULT hr = context_.device()->CreateComputePipelineState(
      &desc, __uuidof(ID3D12PipelineState),
      reinterpret_cast<void **>(pipeline.put()));

  EXPECT_TRUE(FAILED(hr)) << "HRESULT 0x" << std::hex
                          << static_cast<unsigned long>(hr);
  EXPECT_FALSE(pipeline);
}

std::string ShaderContainerCaseName(
    const ::testing::TestParamInfo<ShaderContainerCase> &info) {
  return info.param.name;
}

INSTANTIATE_TEST_SUITE_P(
    EveryStructuralCorruption, ShaderContainerSpec,
    ::testing::Values(
        ShaderContainerCase{"TruncatedMagic",
                            ShaderContainerCorruption::TruncatedMagic},
        ShaderContainerCase{"TruncatedChecksum",
                            ShaderContainerCorruption::TruncatedChecksum},
        ShaderContainerCase{"TruncatedVersion",
                            ShaderContainerCorruption::TruncatedVersion},
        ShaderContainerCase{"TruncatedContainerSize",
                            ShaderContainerCorruption::TruncatedContainerSize},
        ShaderContainerCase{"TruncatedChunkCount",
                            ShaderContainerCorruption::TruncatedChunkCount},
        ShaderContainerCase{"TruncatedChunkTable",
                            ShaderContainerCorruption::TruncatedChunkTable},
        ShaderContainerCase{
            "TruncatedFirstChunkHeader",
            ShaderContainerCorruption::TruncatedFirstChunkHeader},
        ShaderContainerCase{
            "TruncatedFirstChunkPayload",
            ShaderContainerCorruption::TruncatedFirstChunkPayload},
        ShaderContainerCase{
            "TruncatedShaderChunkHeader",
            ShaderContainerCorruption::TruncatedShaderChunkHeader},
        ShaderContainerCase{
            "TruncatedShaderChunkPayload",
            ShaderContainerCorruption::TruncatedShaderChunkPayload},
        ShaderContainerCase{"TruncatedContainerTail",
                            ShaderContainerCorruption::TruncatedContainerTail},
        ShaderContainerCase{"InvalidMagic",
                            ShaderContainerCorruption::InvalidMagic},
        ShaderContainerCase{"InvalidChecksum",
                            ShaderContainerCorruption::InvalidChecksum},
        ShaderContainerCase{
            "UnsupportedContainerVersion",
            ShaderContainerCorruption::UnsupportedContainerVersion},
        ShaderContainerCase{
            "ContainerSizeBelowHeader",
            ShaderContainerCorruption::ContainerSizeBelowHeader},
        ShaderContainerCase{"ContainerSizePastInput",
                            ShaderContainerCorruption::ContainerSizePastInput},
        ShaderContainerCase{"ZeroChunkCount",
                            ShaderContainerCorruption::ZeroChunkCount},
        ShaderContainerCase{"ExcessiveChunkCount",
                            ShaderContainerCorruption::ExcessiveChunkCount},
        ShaderContainerCase{"ChunkOffsetBeforeTable",
                            ShaderContainerCorruption::ChunkOffsetBeforeTable},
        ShaderContainerCase{"ChunkOffsetPastEnd",
                            ShaderContainerCorruption::ChunkOffsetPastEnd},
        ShaderContainerCase{"ChunkSizeOverflow",
                            ShaderContainerCorruption::ChunkSizeOverflow},
        ShaderContainerCase{"MissingShaderChunk",
                            ShaderContainerCorruption::MissingShaderChunk},
        ShaderContainerCase{"UnalignedShaderChunk",
                            ShaderContainerCorruption::UnalignedShaderChunk},
        ShaderContainerCase{"ZeroShaderTokenCount",
                            ShaderContainerCorruption::ZeroShaderTokenCount},
        ShaderContainerCase{
            "ShaderTokenCountPastChunk",
            ShaderContainerCorruption::ShaderTokenCountPastChunk},
        ShaderContainerCase{"MismatchedShaderStage",
                            ShaderContainerCorruption::MismatchedShaderStage},
        ShaderContainerCase{"UnsupportedShaderModel",
                            ShaderContainerCorruption::UnsupportedShaderModel},
        ShaderContainerCase{"ZeroInstructionLength",
                            ShaderContainerCorruption::ZeroInstructionLength},
        ShaderContainerCase{
            "InstructionLengthPastEnd",
            ShaderContainerCorruption::InstructionLengthPastEnd}),
    ShaderContainerCaseName);

} // namespace
