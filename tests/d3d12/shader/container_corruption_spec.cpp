#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"

#include <array>
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

std::uint32_t LoadLe32(const std::uint8_t *data) {
  return std::uint32_t(data[0]) | (std::uint32_t(data[1]) << 8) |
         (std::uint32_t(data[2]) << 16) | (std::uint32_t(data[3]) << 24);
}

void StoreLe32(std::uint8_t *data, std::uint32_t value) {
  data[0] = static_cast<std::uint8_t>(value);
  data[1] = static_cast<std::uint8_t>(value >> 8);
  data[2] = static_cast<std::uint8_t>(value >> 16);
  data[3] = static_cast<std::uint8_t>(value >> 24);
}

std::uint32_t RotateLeft(std::uint32_t value, unsigned int shift) {
  return (value << shift) | (value >> (32 - shift));
}

void TransformChecksumBlock(std::array<std::uint32_t, 4> *state,
                            const std::uint8_t *block) {
  static constexpr std::array<std::uint32_t, 64> kConstants = {
      0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a,
      0xa8304613, 0xfd469501, 0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
      0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340,
      0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
      0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8,
      0x676f02d9, 0x8d2a4c8a, 0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
      0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70, 0x289b7ec6, 0xeaa127fa,
      0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
      0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92,
      0xffeff47d, 0x85845dd1, 0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
      0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
  static constexpr std::array<unsigned int, 64> kShifts = {
      7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
      5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
      4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
      6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};

  std::array<std::uint32_t, 16> words = {};
  for (std::size_t index = 0; index < words.size(); ++index)
    words[index] = LoadLe32(block + index * sizeof(std::uint32_t));

  std::uint32_t a = (*state)[0];
  std::uint32_t b = (*state)[1];
  std::uint32_t c = (*state)[2];
  std::uint32_t d = (*state)[3];
  for (std::uint32_t index = 0; index < 64; ++index) {
    std::uint32_t function = 0;
    std::uint32_t word_index = 0;
    if (index < 16) {
      function = (b & c) | (~b & d);
      word_index = index;
    } else if (index < 32) {
      function = (d & b) | (~d & c);
      word_index = (5 * index + 1) % 16;
    } else if (index < 48) {
      function = b ^ c ^ d;
      word_index = (3 * index + 5) % 16;
    } else {
      function = c ^ (b | ~d);
      word_index = (7 * index) % 16;
    }

    const std::uint32_t next_d = d;
    d = c;
    c = b;
    b += RotateLeft(a + function + kConstants[index] + words[word_index],
                    kShifts[index]);
    a = next_d;
  }

  (*state)[0] += a;
  (*state)[1] += b;
  (*state)[2] += c;
  (*state)[3] += d;
}

std::array<std::uint8_t, 16> ComputeDxbcChecksum(const std::uint8_t *bytes,
                                                 std::size_t size) {
  constexpr std::size_t kBlockSize = 64;
  constexpr std::size_t kHashedDataOffset = 20;
  std::array<std::uint32_t, 4> state = {0x67452301, 0xefcdab89, 0x98badcfe,
                                        0x10325476};

  const std::uint8_t *data = bytes + kHashedDataOffset;
  std::size_t remaining = size - kHashedDataOffset;
  const auto bit_count = static_cast<std::uint32_t>(remaining << 3);
  while (remaining >= kBlockSize) {
    TransformChecksumBlock(&state, data);
    data += kBlockSize;
    remaining -= kBlockSize;
  }

  std::array<std::uint8_t, kBlockSize> block = {};
  if (remaining <= 55) {
    StoreLe32(block.data(), bit_count);
    std::memcpy(block.data() + 4, data, remaining);
    block[4 + remaining] = 0x80;
  } else {
    std::memcpy(block.data(), data, remaining);
    block[remaining] = 0x80;
    TransformChecksumBlock(&state, block.data());
    block = {};
    StoreLe32(block.data(), bit_count);
  }
  StoreLe32(block.data() + 60, (bit_count >> 2) | 1);
  TransformChecksumBlock(&state, block.data());

  std::array<std::uint8_t, 16> checksum = {};
  for (std::size_t index = 0; index < state.size(); ++index)
    StoreLe32(checksum.data() + index * sizeof(std::uint32_t), state[index]);
  return checksum;
}

bool HasValidDxbcChecksum(const std::vector<std::uint8_t> &bytes,
                          std::size_t size) {
  if (size <= 20 || size > bytes.size())
    return false;
  const auto checksum = ComputeDxbcChecksum(bytes.data(), size);
  return !std::memcmp(bytes.data() + 4, checksum.data(), checksum.size());
}

void RecomputeDxbcChecksum(std::vector<std::uint8_t> *bytes, std::size_t size) {
  const auto checksum = ComputeDxbcChecksum(bytes->data(), size);
  std::memcpy(bytes->data() + 4, checksum.data(), checksum.size());
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
  UnknownOpcode,
  ReservedOpcode,
  GeometryOpcodeInCompute,
  UnbalancedEndIf,
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
    WriteU32(result.bytes, 32, 24);
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
  case ShaderContainerCorruption::UnknownOpcode: {
    constexpr std::uint32_t kOpcodeMask = 0x000007ffu;
    const std::uint32_t instruction = ReadU32(result.bytes, shader_data + 8);
    WriteU32(result.bytes, shader_data + 8, instruction | kOpcodeMask);
    break;
  }
  case ShaderContainerCorruption::ReservedOpcode: {
    constexpr std::uint32_t kOpcodeMask = 0x000007ffu;
    constexpr std::uint32_t kD3d10ReservedOpcode = 107u;
    const std::uint32_t instruction = ReadU32(result.bytes, shader_data + 8);
    WriteU32(result.bytes, shader_data + 8,
             (instruction & ~kOpcodeMask) | kD3d10ReservedOpcode);
    break;
  }
  case ShaderContainerCorruption::GeometryOpcodeInCompute: {
    constexpr std::uint32_t kOpcodeMask = 0x000007ffu;
    constexpr std::uint32_t kInstructionLengthMask = 0x7f000000u;
    constexpr std::uint32_t kInstructionLengthOne = 0x01000000u;
    constexpr std::uint32_t kD3d10EmitOpcode = 19u;
    const std::uint32_t instruction = ReadU32(result.bytes, shader_data + 8);
    WriteU32(result.bytes, shader_data + 8,
             (instruction & ~(kOpcodeMask | kInstructionLengthMask)) |
                 kInstructionLengthOne | kD3d10EmitOpcode);
    break;
  }
  case ShaderContainerCorruption::UnbalancedEndIf: {
    constexpr std::uint32_t kOpcodeMask = 0x000007ffu;
    constexpr std::uint32_t kInstructionLengthMask = 0x7f000000u;
    constexpr std::uint32_t kInstructionLengthOne = 0x01000000u;
    constexpr std::uint32_t kD3d10EndIfOpcode = 21u;
    const std::uint32_t instruction = ReadU32(result.bytes, shader_data + 8);
    WriteU32(result.bytes, shader_data + 8,
             (instruction & ~(kOpcodeMask | kInstructionLengthMask)) |
                 kInstructionLengthOne | kD3d10EndIfOpcode);
    break;
  }
  }
  if (corruption != ShaderContainerCorruption::InvalidChecksum &&
      result.reported_size > 20)
    RecomputeDxbcChecksum(&result.bytes, result.reported_size);
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
    ASSERT_TRUE(HasValidDxbcChecksum(shader_, shader_.size()));
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
  if (GetParam().corruption == ShaderContainerCorruption::InvalidChecksum) {
    ASSERT_FALSE(
        HasValidDxbcChecksum(corrupted.bytes, corrupted.reported_size));
  } else if (corrupted.reported_size > 20) {
    ASSERT_TRUE(HasValidDxbcChecksum(corrupted.bytes, corrupted.reported_size));
  }

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
            ShaderContainerCorruption::InstructionLengthPastEnd},
        ShaderContainerCase{"UnknownOpcode",
                            ShaderContainerCorruption::UnknownOpcode},
        ShaderContainerCase{"ReservedOpcode",
                            ShaderContainerCorruption::ReservedOpcode},
        ShaderContainerCase{"GeometryOpcodeInCompute",
                            ShaderContainerCorruption::GeometryOpcodeInCompute},
        ShaderContainerCase{"UnbalancedEndIf",
                            ShaderContainerCorruption::UnbalancedEndIf}),
    ShaderContainerCaseName);

} // namespace
