#include <dxmt_test.hpp>

#include "DXBCParser/ShaderBinary.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <vector>

namespace {

using namespace microsoft;
using namespace microsoft::D3D10ShaderBinary;

constexpr CShaderToken kDestinationOperand =
    ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT) |
    ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(
        D3D10_SB_OPERAND_4_COMPONENT_MASK_MODE) |
    ENCODE_D3D10_SB_OPERAND_4_COMPONENT_MASK(
        D3D10_SB_OPERAND_4_COMPONENT_MASK_ALL) |
    ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_TEMP) |
    ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_1D) |
    ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(
        0, D3D10_SB_OPERAND_INDEX_IMMEDIATE32);

constexpr CShaderToken kImmediateOperand =
    ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT) |
    ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_IMMEDIATE32);

constexpr CShaderToken InstructionToken(D3D10_SB_OPCODE_TYPE opcode,
                                        UINT length,
                                        CShaderToken controls = 0) {
  return static_cast<CShaderToken>(ENCODE_D3D10_SB_OPCODE_TYPE(opcode) |
                                   ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(
                                       length) |
                                   controls);
}

constexpr CShaderToken IndexedOperandToken(
    D3D10_SB_OPERAND_TYPE type,
    D3D10_SB_OPERAND_INDEX_DIMENSION dimension = D3D10_SB_OPERAND_INDEX_1D,
    UINT mask = D3D10_SB_OPERAND_4_COMPONENT_MASK_ALL) {
  CShaderToken token =
      ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT) |
      ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(
          D3D10_SB_OPERAND_4_COMPONENT_MASK_MODE) |
      ENCODE_D3D10_SB_OPERAND_4_COMPONENT_MASK(mask) |
      ENCODE_D3D10_SB_OPERAND_TYPE(type) |
      ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(dimension);
  if (dimension > D3D10_SB_OPERAND_INDEX_0D)
    token |= ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(
        0, D3D10_SB_OPERAND_INDEX_IMMEDIATE32);
  if (dimension > D3D10_SB_OPERAND_INDEX_1D)
    token |= ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(
        1, D3D10_SB_OPERAND_INDEX_IMMEDIATE32);
  if (dimension > D3D10_SB_OPERAND_INDEX_2D)
    token |= ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(
        2, D3D10_SB_OPERAND_INDEX_IMMEDIATE32);
  return token;
}

void ParseSingleInstruction(
    std::initializer_list<CShaderToken> tokens, CInstruction *instruction,
    D3D10_SB_TOKENIZED_PROGRAM_TYPE type = D3D11_SB_COMPUTE_SHADER,
    UINT major = 5, UINT minor = 0) {
  std::vector<CShaderToken> shader = {
      ENCODE_D3D10_SB_TOKENIZED_PROGRAM_VERSION_TOKEN(type, major, minor),
      ENCODE_D3D10_SB_TOKENIZED_PROGRAM_LENGTH(
          static_cast<UINT>(tokens.size() + 2)),
  };
  shader.insert(shader.end(), tokens.begin(), tokens.end());
  CShaderCodeParser parser(shader.data());
  parser.ParseInstruction(instruction);
  EXPECT_TRUE(parser.EndOfShader());
}

TEST(ShaderBinary, InitializesInstructionMetadataAndRejectsUnknownOpcodes) {
  EXPECT_EQ(GetNumInstructionOperands(D3D10_SB_OPCODE_MOV), 2u);
  EXPECT_EQ(std::strcmp(g_InstructionInfo[D3D10_SB_OPCODE_MOV].m_Name, "mov"),
            0);
  EXPECT_EQ(g_InstructionInfo[D3D10_SB_OPCODE_MOV].m_OpClass,
            D3D10_SB_FLOAT_OP);
  EXPECT_ANY_THROW(GetNumInstructionOperands(D3D10_SB_NUM_OPCODES));
}

TEST(ShaderBinary, ParsesProgramHeaderRegisterAndImmediateOperands) {
  constexpr UINT kPreciseMask = 0b0101;
  constexpr std::array<CShaderToken, 11> shader = {
      ENCODE_D3D10_SB_TOKENIZED_PROGRAM_VERSION_TOKEN(D3D10_SB_PIXEL_SHADER, 5,
                                                      0),
      ENCODE_D3D10_SB_TOKENIZED_PROGRAM_LENGTH(11),
      ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(8) |
          ENCODE_D3D10_SB_INSTRUCTION_SATURATE(true) |
          ENCODE_D3D11_SB_INSTRUCTION_PRECISE_VALUES(kPreciseMask),
      kDestinationOperand,
      3,
      kImmediateOperand,
      0x3f800000,
      0x40000000,
      0x40400000,
      0x40800000,
      ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_RET) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1),
  };

  CShaderCodeParser parser(shader.data());
  EXPECT_EQ(parser.ShaderType(), D3D10_SB_PIXEL_SHADER);
  EXPECT_EQ(parser.ShaderMajorVersion(), 5u);
  EXPECT_EQ(parser.ShaderMinorVersion(), 0u);
  EXPECT_EQ(parser.ShaderLengthInTokens(), shader.size());
  EXPECT_EQ(parser.CurrentTokenOffset(), 2u);

  CInstruction instruction;
  parser.ParseInstruction(&instruction);
  EXPECT_EQ(instruction.OpCode(), D3D10_SB_OPCODE_MOV);
  EXPECT_EQ(instruction.NumOperands(), 2u);
  EXPECT_TRUE(instruction.m_bSaturate);
  EXPECT_EQ(instruction.GetPreciseMask(), kPreciseMask);
  EXPECT_EQ(instruction.Operand(0).OperandType(), D3D10_SB_OPERAND_TYPE_TEMP);
  EXPECT_EQ(instruction.Operand(0).RegIndex(), 3u);
  EXPECT_EQ(instruction.Operand(0).WriteMask(),
            D3D10_SB_OPERAND_4_COMPONENT_MASK_ALL);
  EXPECT_EQ(instruction.Operand(1).OperandType(),
            D3D10_SB_OPERAND_TYPE_IMMEDIATE32);
  EXPECT_EQ(instruction.Operand(1).m_Value[0], 0x3f800000u);
  EXPECT_EQ(instruction.Operand(1).m_Value[3], 0x40800000u);
  EXPECT_EQ(parser.CurrentTokenOffset(), 10u);
  EXPECT_FALSE(parser.EndOfShader());

  std::array<char, 16> name{};
  EXPECT_TRUE(instruction.Disassemble(name.data(), name.size()));
  EXPECT_STREQ(name.data(), "mov");
  std::array<char, 3> short_name = {'x', 'x', '\0'};
  EXPECT_FALSE(instruction.Disassemble(short_name.data(), short_name.size()));
  EXPECT_STREQ(short_name.data(), "");
  EXPECT_FALSE(instruction.Disassemble(nullptr, 0));

  parser.ParseInstruction(&instruction);
  EXPECT_EQ(instruction.OpCode(), D3D10_SB_OPCODE_RET);
  EXPECT_TRUE(parser.EndOfShader());
}

TEST(ShaderBinary, ParsesExtendedRegisterOperandWithoutChangingParserState) {
  constexpr std::array<CShaderToken, 3> tokens = {
      ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT) |
          ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(
              D3D10_SB_OPERAND_4_COMPONENT_SELECT_1_MODE) |
          ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECT_1(D3D10_SB_4_COMPONENT_Z) |
          ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_TEMP) |
          ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_1D) |
          ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(
              0, D3D10_SB_OPERAND_INDEX_IMMEDIATE32) |
          ENCODE_D3D10_SB_OPERAND_EXTENDED(true),
      ENCODE_D3D10_SB_EXTENDED_OPERAND_MODIFIER(D3D10_SB_OPERAND_MODIFIER_NEG) |
          ENCODE_D3D11_SB_OPERAND_MIN_PRECISION(
              D3D11_SB_OPERAND_MIN_PRECISION_FLOAT_16) |
          ENCODE_D3D12_SB_OPERAND_NON_UNIFORM(true),
      42,
  };

  CShaderCodeParser parser;
  COperand operand;
  const auto *end = parser.ParseOperandAt(&operand, tokens.data(),
                                          tokens.data() + tokens.size());

  EXPECT_EQ(end, tokens.data() + tokens.size());
  EXPECT_EQ(operand.OperandType(), D3D10_SB_OPERAND_TYPE_TEMP);
  EXPECT_EQ(operand.RegIndex(), 42u);
  EXPECT_EQ(operand.m_ComponentName, D3D10_SB_4_COMPONENT_Z);
  EXPECT_EQ(operand.Modifier(), D3D10_SB_OPERAND_MODIFIER_NEG);
  EXPECT_EQ(operand.m_MinPrecision, D3D11_SB_OPERAND_MIN_PRECISION_FLOAT_16);
  EXPECT_TRUE(operand.m_Nonuniform);
}

TEST(ShaderBinary, ParsesEveryOperandIndexRepresentation) {
  constexpr CShaderToken kNestedIndexableTemp =
      ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(
          D3D10_SB_OPERAND_4_COMPONENT) |
      ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(
          D3D10_SB_OPERAND_4_COMPONENT_SELECT_1_MODE) |
      ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECT_1(
          D3D10_SB_4_COMPONENT_W) |
      ENCODE_D3D10_SB_OPERAND_TYPE(
          D3D10_SB_OPERAND_TYPE_INDEXABLE_TEMP) |
      ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_2D) |
      ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(
          0, D3D10_SB_OPERAND_INDEX_IMMEDIATE32) |
      ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(
          1, D3D10_SB_OPERAND_INDEX_IMMEDIATE32);
  constexpr std::array<CShaderToken, 8> mixed_indices = {
      ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(
          D3D10_SB_OPERAND_0_COMPONENT) |
          ENCODE_D3D10_SB_OPERAND_TYPE(
              D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER) |
          ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(
              D3D10_SB_OPERAND_INDEX_3D) |
          ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(
              0, D3D10_SB_OPERAND_INDEX_IMMEDIATE32) |
          ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(
              1, D3D10_SB_OPERAND_INDEX_IMMEDIATE64) |
          ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(
              2, D3D10_SB_OPERAND_INDEX_IMMEDIATE32_PLUS_RELATIVE),
      11,
      0x89abcdef,
      0x01234567,
      22,
      kNestedIndexableTemp,
      3,
      4,
  };

  CShaderCodeParser parser;
  COperand operand;
  const auto *end =
      parser.ParseOperandAt(&operand, mixed_indices.data(),
                            mixed_indices.data() + mixed_indices.size());
  EXPECT_EQ(end, mixed_indices.data() + mixed_indices.size());
  EXPECT_EQ(operand.OperandType(), D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER);
  EXPECT_EQ(operand.OperandIndexDimension(), D3D10_SB_OPERAND_INDEX_3D);
  EXPECT_EQ(operand.OperandIndexType(0),
            D3D10_SB_OPERAND_INDEX_IMMEDIATE32);
  EXPECT_EQ(operand.OperandIndex(0)->m_RegIndex, 11u);
  EXPECT_EQ(operand.OperandIndexType(1),
            D3D10_SB_OPERAND_INDEX_IMMEDIATE64);
  EXPECT_EQ(operand.OperandIndex(1)->m_RegIndexA[0], 0x89abcdefu);
  EXPECT_EQ(operand.OperandIndex(1)->m_RegIndexA[1], 0x01234567u);
  EXPECT_EQ(operand.OperandIndexType(2),
            D3D10_SB_OPERAND_INDEX_IMMEDIATE32_PLUS_RELATIVE);
  EXPECT_EQ(operand.OperandIndex(2)->m_RegIndex, 22u);
  EXPECT_EQ(operand.OperandIndex(2)->m_RelRegType,
            D3D10_SB_OPERAND_TYPE_INDEXABLE_TEMP);
  EXPECT_EQ(operand.OperandIndex(2)->m_IndexDimension,
            D3D10_SB_OPERAND_INDEX_2D);
  EXPECT_EQ(operand.OperandIndex(2)->m_RelIndex, 3u);
  EXPECT_EQ(operand.OperandIndex(2)->m_RelIndex1, 4u);
  EXPECT_EQ(operand.OperandIndex(2)->m_ComponentName,
            D3D10_SB_4_COMPONENT_W);

  constexpr std::array<CShaderToken, 4> relative_index = {
      ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(
          D3D10_SB_OPERAND_0_COMPONENT) |
          ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_RESOURCE) |
          ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(
              D3D10_SB_OPERAND_INDEX_1D) |
          ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(
              0, D3D10_SB_OPERAND_INDEX_RELATIVE),
      ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(
          D3D10_SB_OPERAND_4_COMPONENT) |
          ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(
              D3D10_SB_OPERAND_4_COMPONENT_SELECT_1_MODE) |
          ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECT_1(
              D3D10_SB_4_COMPONENT_Y) |
          ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_TEMP) |
          ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(
              D3D10_SB_OPERAND_INDEX_1D) |
          ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(
              0, D3D10_SB_OPERAND_INDEX_IMMEDIATE32) |
          ENCODE_D3D10_SB_OPERAND_EXTENDED(true),
      ENCODE_D3D10_SB_EXTENDED_OPERAND_MODIFIER(
          D3D10_SB_OPERAND_MODIFIER_NONE) |
          ENCODE_D3D11_SB_OPERAND_MIN_PRECISION(
              D3D11_SB_OPERAND_MIN_PRECISION_UINT_16),
      7,
  };
  COperand relative_operand;
  end = parser.ParseOperandAt(&relative_operand, relative_index.data(),
                              relative_index.data() + relative_index.size());
  EXPECT_EQ(end, relative_index.data() + relative_index.size());
  EXPECT_EQ(relative_operand.OperandIndexType(0),
            D3D10_SB_OPERAND_INDEX_RELATIVE);
  EXPECT_EQ(relative_operand.OperandIndex(0)->m_RelRegType,
            D3D10_SB_OPERAND_TYPE_TEMP);
  EXPECT_EQ(relative_operand.OperandIndex(0)->m_RelIndex, 7u);
  EXPECT_EQ(relative_operand.OperandIndex(0)->m_ComponentName,
            D3D10_SB_4_COMPONENT_Y);
  EXPECT_EQ(relative_operand.OperandIndex(0)->m_MinPrecision,
            D3D11_SB_OPERAND_MIN_PRECISION_UINT_16);
}

TEST(ShaderBinary, ParsesScalarDoubleAndSwizzledOperands) {
  CShaderCodeParser parser;

  constexpr std::array<CShaderToken, 2> scalar = {
      ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(
          D3D10_SB_OPERAND_1_COMPONENT) |
          ENCODE_D3D10_SB_OPERAND_TYPE(
              D3D10_SB_OPERAND_TYPE_IMMEDIATE32),
      0xdeadbeef,
  };
  COperand scalar_operand;
  auto *end = parser.ParseOperandAt(&scalar_operand, scalar.data(),
                                    scalar.data() + scalar.size());
  EXPECT_EQ(end, scalar.data() + scalar.size());
  EXPECT_EQ(scalar_operand.NumComponents(), D3D10_SB_OPERAND_1_COMPONENT);
  EXPECT_EQ(scalar_operand.OperandType(), D3D10_SB_OPERAND_TYPE_IMMEDIATE32);
  EXPECT_EQ(scalar_operand.Imm32(), 0xdeadbeefu);

  constexpr std::array<CShaderToken, 5> doubles = {
      ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(
          D3D10_SB_OPERAND_4_COMPONENT) |
          ENCODE_D3D10_SB_OPERAND_TYPE(
              D3D10_SB_OPERAND_TYPE_IMMEDIATE64),
      0x89abcdef,
      0x01234567,
      0x76543210,
      0xfedcba98,
  };
  COperand double_operand;
  end = parser.ParseOperandAt(&double_operand, doubles.data(),
                              doubles.data() + doubles.size());
  EXPECT_EQ(end, doubles.data() + doubles.size());
  EXPECT_EQ(double_operand.OperandType(), D3D10_SB_OPERAND_TYPE_IMMEDIATE64);
  EXPECT_EQ(double_operand.m_Value[0], 0x89abcdefu);
  EXPECT_EQ(double_operand.m_Value[1], 0x01234567u);
  EXPECT_EQ(double_operand.m_Value[2], 0x76543210u);
  EXPECT_EQ(double_operand.m_Value[3], 0xfedcba98u);

  constexpr std::array<CShaderToken, 2> swizzled = {
      ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(
          D3D10_SB_OPERAND_4_COMPONENT) |
          ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(
              D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE_MODE) |
          ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE(
              D3D10_SB_4_COMPONENT_W, D3D10_SB_4_COMPONENT_Z,
              D3D10_SB_4_COMPONENT_Y, D3D10_SB_4_COMPONENT_X) |
          ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_TEMP) |
          ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(
              D3D10_SB_OPERAND_INDEX_1D) |
          ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(
              0, D3D10_SB_OPERAND_INDEX_IMMEDIATE32),
      9,
  };
  COperand swizzled_operand;
  end = parser.ParseOperandAt(&swizzled_operand, swizzled.data(),
                              swizzled.data() + swizzled.size());
  EXPECT_EQ(end, swizzled.data() + swizzled.size());
  EXPECT_EQ(swizzled_operand.RegIndex(), 9u);
  EXPECT_EQ(swizzled_operand.SwizzleComponent(0), D3D10_SB_4_COMPONENT_W);
  EXPECT_EQ(swizzled_operand.SwizzleComponent(1), D3D10_SB_4_COMPONENT_Z);
  EXPECT_EQ(swizzled_operand.SwizzleComponent(2), D3D10_SB_4_COMPONENT_Y);
  EXPECT_EQ(swizzled_operand.SwizzleComponent(3), D3D10_SB_4_COMPONENT_X);
}

TEST(ShaderBinary, ParsesSignedExtendedSampleOffsets) {
  constexpr std::array<CShaderToken, 4> shader = {
      ENCODE_D3D10_SB_TOKENIZED_PROGRAM_VERSION_TOKEN(D3D10_SB_PIXEL_SHADER, 5,
                                                      0),
      ENCODE_D3D10_SB_TOKENIZED_PROGRAM_LENGTH(4),
      ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_RET) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(2) |
          ENCODE_D3D10_SB_OPCODE_EXTENDED(true),
      ENCODE_D3D10_SB_EXTENDED_OPCODE_TYPE(
          D3D10_SB_EXTENDED_OPCODE_SAMPLE_CONTROLS) |
          ENCODE_IMMEDIATE_D3D10_SB_ADDRESS_OFFSET(
              D3D10_SB_IMMEDIATE_ADDRESS_OFFSET_U, 0xf) |
          ENCODE_IMMEDIATE_D3D10_SB_ADDRESS_OFFSET(
              D3D10_SB_IMMEDIATE_ADDRESS_OFFSET_V, 2) |
          ENCODE_IMMEDIATE_D3D10_SB_ADDRESS_OFFSET(
              D3D10_SB_IMMEDIATE_ADDRESS_OFFSET_W, 0x8),
  };

  CShaderCodeParser parser(shader.data());
  CInstruction instruction;
  parser.ParseInstruction(&instruction);

  EXPECT_EQ(instruction.m_ExtendedOpCodeCount, 1u);
  EXPECT_EQ(instruction.m_OpCodeEx[0],
            D3D10_SB_EXTENDED_OPCODE_SAMPLE_CONTROLS);
  EXPECT_EQ(instruction.m_TexelOffset[0], -1);
  EXPECT_EQ(instruction.m_TexelOffset[1], 2);
  EXPECT_EQ(instruction.m_TexelOffset[2], -8);
  EXPECT_TRUE(parser.EndOfShader());
}

TEST(ShaderBinary, CopiesAndTerminatesCommentCustomData) {
  constexpr std::array<CShaderToken, 7> shader = {
      ENCODE_D3D10_SB_TOKENIZED_PROGRAM_VERSION_TOKEN(D3D10_SB_PIXEL_SHADER, 5,
                                                      0),
      ENCODE_D3D10_SB_TOKENIZED_PROGRAM_LENGTH(7),
      ENCODE_D3D10_SB_CUSTOMDATA_CLASS(D3D10_SB_CUSTOMDATA_COMMENT),
      4,
      0x11223344,
      0xaabbccdd,
      ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_RET) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1),
  };

  CShaderCodeParser parser(shader.data());
  CInstruction instruction;
  parser.ParseInstruction(&instruction);
  ASSERT_NE(instruction.m_CustomData.pData, nullptr);
  EXPECT_EQ(instruction.m_CustomData.Type, D3D10_SB_CUSTOMDATA_COMMENT);
  EXPECT_EQ(instruction.m_CustomData.DataSizeInBytes, 8u);
  const auto *data = static_cast<const UINT *>(instruction.m_CustomData.pData);
  EXPECT_EQ(data[0], 0x11223344u);
  EXPECT_EQ(data[1], 0x00bbccddu);

  parser.ParseInstruction(&instruction);
  EXPECT_EQ(instruction.OpCode(), D3D10_SB_OPCODE_RET);
  EXPECT_TRUE(parser.EndOfShader());
}

TEST(ShaderBinary, DecodesEverySynchronizationScope) {
  constexpr std::array<CShaderToken, 3> shader = {
      ENCODE_D3D10_SB_TOKENIZED_PROGRAM_VERSION_TOKEN(D3D11_SB_COMPUTE_SHADER,
                                                      5, 0),
      ENCODE_D3D10_SB_TOKENIZED_PROGRAM_LENGTH(3),
      ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_SYNC) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1) |
          ENCODE_D3D11_SB_SYNC_FLAGS(
              D3D11_SB_SYNC_THREADS_IN_GROUP |
              D3D11_SB_SYNC_THREAD_GROUP_SHARED_MEMORY |
              D3D11_SB_SYNC_UNORDERED_ACCESS_VIEW_MEMORY_GLOBAL),
  };

  CShaderCodeParser parser(shader.data());
  CInstruction instruction;
  parser.ParseInstruction(&instruction);

  EXPECT_TRUE(instruction.m_SyncFlags.bThreadsInGroup);
  EXPECT_TRUE(instruction.m_SyncFlags.bThreadGroupSharedMemory);
  EXPECT_TRUE(instruction.m_SyncFlags.bUnorderedAccessViewMemoryGlobal);
  EXPECT_FALSE(instruction.m_SyncFlags.bUnorderedAccessViewMemoryGroup);
}

TEST(ShaderBinary, DecodesChainedResourceExtensionTokens) {
  constexpr std::array<CShaderToken, 5> shader = {
      ENCODE_D3D10_SB_TOKENIZED_PROGRAM_VERSION_TOKEN(D3D11_SB_COMPUTE_SHADER,
                                                      5, 0),
      ENCODE_D3D10_SB_TOKENIZED_PROGRAM_LENGTH(5),
      ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_RET) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3) |
          ENCODE_D3D10_SB_OPCODE_EXTENDED(true),
      ENCODE_D3D10_SB_EXTENDED_OPCODE_TYPE(
          D3D11_SB_EXTENDED_OPCODE_RESOURCE_DIM) |
          ENCODE_D3D11_SB_EXTENDED_RESOURCE_DIMENSION(
              D3D11_SB_RESOURCE_DIMENSION_STRUCTURED_BUFFER) |
          ENCODE_D3D11_SB_EXTENDED_RESOURCE_DIMENSION_STRUCTURE_STRIDE(48) |
          ENCODE_D3D10_SB_OPCODE_EXTENDED(true),
      ENCODE_D3D10_SB_EXTENDED_OPCODE_TYPE(
          D3D11_SB_EXTENDED_OPCODE_RESOURCE_RETURN_TYPE) |
          ENCODE_D3D11_SB_EXTENDED_RESOURCE_RETURN_TYPE(
              D3D10_SB_RETURN_TYPE_UNORM, 0) |
          ENCODE_D3D11_SB_EXTENDED_RESOURCE_RETURN_TYPE(
              D3D10_SB_RETURN_TYPE_SNORM, 1) |
          ENCODE_D3D11_SB_EXTENDED_RESOURCE_RETURN_TYPE(
              D3D10_SB_RETURN_TYPE_SINT, 2) |
          ENCODE_D3D11_SB_EXTENDED_RESOURCE_RETURN_TYPE(
              D3D11_SB_RETURN_TYPE_DOUBLE, 3),
  };

  CShaderCodeParser parser(shader.data());
  CInstruction instruction;
  parser.ParseInstruction(&instruction);

  EXPECT_EQ(instruction.m_ExtendedOpCodeCount, 2u);
  EXPECT_EQ(instruction.m_OpCodeEx[0], D3D11_SB_EXTENDED_OPCODE_RESOURCE_DIM);
  EXPECT_EQ(instruction.m_OpCodeEx[1],
            D3D11_SB_EXTENDED_OPCODE_RESOURCE_RETURN_TYPE);
  EXPECT_EQ(instruction.m_ResourceDimEx,
            D3D11_SB_RESOURCE_DIMENSION_STRUCTURED_BUFFER);
  EXPECT_EQ(instruction.m_ResourceDimStructureStrideEx, 48u);
  EXPECT_EQ(instruction.m_ResourceReturnTypeEx[0],
            D3D10_SB_RETURN_TYPE_UNORM);
  EXPECT_EQ(instruction.m_ResourceReturnTypeEx[1],
            D3D10_SB_RETURN_TYPE_SNORM);
  EXPECT_EQ(instruction.m_ResourceReturnTypeEx[2],
            D3D10_SB_RETURN_TYPE_SINT);
  EXPECT_EQ(instruction.m_ResourceReturnTypeEx[3],
            D3D11_SB_RETURN_TYPE_DOUBLE);
  EXPECT_TRUE(parser.EndOfShader());
}

TEST(ShaderBinary, DecodesInstructionTestsAndReturnTypes) {
  constexpr CShaderToken immediate =
      ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_1_COMPONENT) |
      ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_IMMEDIATE32);
  constexpr std::array<D3D10_SB_OPCODE_TYPE, 5> conditional_opcodes = {
      D3D10_SB_OPCODE_IF, D3D10_SB_OPCODE_BREAKC, D3D10_SB_OPCODE_CONTINUEC,
      D3D10_SB_OPCODE_RETC, D3D10_SB_OPCODE_DISCARD,
  };

  for (const auto opcode : conditional_opcodes) {
    const std::array<CShaderToken, 5> shader = {
        ENCODE_D3D10_SB_TOKENIZED_PROGRAM_VERSION_TOKEN(D3D10_SB_PIXEL_SHADER,
                                                        5, 0),
        ENCODE_D3D10_SB_TOKENIZED_PROGRAM_LENGTH(5),
        static_cast<CShaderToken>(
            ENCODE_D3D10_SB_OPCODE_TYPE(opcode) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3) |
            ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                D3D10_SB_INSTRUCTION_TEST_NONZERO)),
        immediate,
        17,
    };
    CShaderCodeParser parser(shader.data());
    CInstruction instruction;
    parser.ParseInstruction(&instruction);
    EXPECT_EQ(instruction.OpCode(), opcode);
    EXPECT_EQ(instruction.Test(), D3D10_SB_INSTRUCTION_TEST_NONZERO);
    EXPECT_EQ(instruction.Operand(0).Imm32(), 17u);
  }

  constexpr std::array<CShaderToken, 7> callc_shader = {
      ENCODE_D3D10_SB_TOKENIZED_PROGRAM_VERSION_TOKEN(D3D10_SB_PIXEL_SHADER, 5,
                                                      0),
      ENCODE_D3D10_SB_TOKENIZED_PROGRAM_LENGTH(7),
      ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_CALLC) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5) |
          ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
              D3D10_SB_INSTRUCTION_TEST_ZERO),
      immediate,
      1,
      immediate,
      2,
  };
  CShaderCodeParser callc_parser(callc_shader.data());
  CInstruction callc;
  callc_parser.ParseInstruction(&callc);
  EXPECT_EQ(callc.Test(), D3D10_SB_INSTRUCTION_TEST_ZERO);
  EXPECT_EQ(callc.Operand(0).Imm32(), 1u);
  EXPECT_EQ(callc.Operand(1).Imm32(), 2u);

  constexpr std::array<CShaderToken, 9> resinfo_shader = {
      ENCODE_D3D10_SB_TOKENIZED_PROGRAM_VERSION_TOKEN(D3D10_SB_PIXEL_SHADER, 5,
                                                      0),
      ENCODE_D3D10_SB_TOKENIZED_PROGRAM_LENGTH(9),
      ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_RESINFO) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7) |
          ENCODE_D3D10_SB_RESINFO_INSTRUCTION_RETURN_TYPE(
              D3D10_SB_RESINFO_INSTRUCTION_RETURN_UINT),
      immediate,
      3,
      immediate,
      4,
      immediate,
      5,
  };
  CShaderCodeParser resinfo_parser(resinfo_shader.data());
  CInstruction resinfo;
  resinfo_parser.ParseInstruction(&resinfo);
  EXPECT_EQ(resinfo.m_ResInfoReturnType,
            D3D10_SB_RESINFO_INSTRUCTION_RETURN_UINT);

  constexpr std::array<CShaderToken, 7> sample_info_shader = {
      ENCODE_D3D10_SB_TOKENIZED_PROGRAM_VERSION_TOKEN(D3D10_SB_PIXEL_SHADER, 5,
                                                      0),
      ENCODE_D3D10_SB_TOKENIZED_PROGRAM_LENGTH(7),
      ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_1_SB_OPCODE_SAMPLE_INFO) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5) |
          ENCODE_D3D10_SB_INSTRUCTION_RETURN_TYPE(
              D3D10_SB_INSTRUCTION_RETURN_UINT),
      immediate,
      6,
      immediate,
      7,
  };
  CShaderCodeParser sample_info_parser(sample_info_shader.data());
  CInstruction sample_info;
  sample_info_parser.ParseInstruction(&sample_info);
  EXPECT_EQ(sample_info.m_InstructionReturnType,
            D3D10_SB_INSTRUCTION_RETURN_UINT);
}

TEST(ShaderBinary, RepositionsTheInstructionCursor) {
  constexpr std::array<CShaderToken, 4> shader = {
      ENCODE_D3D10_SB_TOKENIZED_PROGRAM_VERSION_TOKEN(D3D10_SB_VERTEX_SHADER,
                                                      4, 1),
      ENCODE_D3D10_SB_TOKENIZED_PROGRAM_LENGTH(4),
      ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_NOP) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1),
      ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_RET) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1),
  };
  CShaderCodeParser parser;
  parser.SetShader(shader.data());
  parser.SetCurrentTokenOffset(3);

  EXPECT_EQ(parser.CurrentTokenOffset(), 3u);
  EXPECT_EQ(parser.ShaderType(), D3D10_SB_VERTEX_SHADER);
  EXPECT_EQ(parser.ShaderMajorVersion(), 4u);
  EXPECT_EQ(parser.ShaderMinorVersion(), 1u);
  CInstruction instruction;
  parser.ParseInstruction(&instruction);
  EXPECT_EQ(instruction.OpCode(), D3D10_SB_OPCODE_RET);
  EXPECT_TRUE(parser.EndOfShader());
}

TEST(ShaderBinary, DecodesFixedPayloadDeclarations) {
  CInstruction instruction;

  ParseSingleInstruction(
      {InstructionToken(D3D11_SB_OPCODE_DCL_FUNCTION_BODY, 2), 37},
      &instruction);
  EXPECT_EQ(instruction.m_FunctionBodyDecl.FunctionBodyNumber, 37u);

  ParseSingleInstruction(
      {InstructionToken(D3D10_SB_OPCODE_DCL_TEMPS, 2), 19}, &instruction);
  EXPECT_EQ(instruction.m_TempsDecl.NumTemps, 19u);

  struct IndexableTempCase {
    UINT component_count;
    UINT expected_mask;
  };
  constexpr std::array<IndexableTempCase, 4> indexable_temp_cases = {{
      {0, D3D10_SB_OPERAND_4_COMPONENT_MASK_X},
      {2, D3D10_SB_OPERAND_4_COMPONENT_MASK_X |
              D3D10_SB_OPERAND_4_COMPONENT_MASK_Y},
      {3, D3D10_SB_OPERAND_4_COMPONENT_MASK_X |
              D3D10_SB_OPERAND_4_COMPONENT_MASK_Y |
              D3D10_SB_OPERAND_4_COMPONENT_MASK_Z},
      {9, D3D10_SB_OPERAND_4_COMPONENT_MASK_ALL},
  }};
  for (const auto &test_case : indexable_temp_cases) {
    ParseSingleInstruction(
        {InstructionToken(D3D10_SB_OPCODE_DCL_INDEXABLE_TEMP, 4), 7, 23,
         test_case.component_count},
        &instruction);
    EXPECT_EQ(instruction.m_IndexableTempDecl.IndexableTempNumber, 7u);
    EXPECT_EQ(instruction.m_IndexableTempDecl.NumRegisters, 23u);
    EXPECT_EQ(instruction.m_IndexableTempDecl.Mask, test_case.expected_mask);
  }

  ParseSingleInstruction(
      {InstructionToken(
          D3D10_SB_OPCODE_DCL_GS_OUTPUT_PRIMITIVE_TOPOLOGY, 1,
          ENCODE_D3D10_SB_GS_OUTPUT_PRIMITIVE_TOPOLOGY(
              D3D10_SB_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP_ADJ))},
      &instruction, D3D10_SB_GEOMETRY_SHADER);
  EXPECT_EQ(instruction.m_OutputTopologyDecl.Topology,
            D3D10_SB_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP_ADJ);

  ParseSingleInstruction(
      {InstructionToken(D3D10_SB_OPCODE_DCL_GS_INPUT_PRIMITIVE, 1,
                        ENCODE_D3D10_SB_GS_INPUT_PRIMITIVE(
                            D3D10_SB_PRIMITIVE_TRIANGLE_ADJ))},
      &instruction, D3D10_SB_GEOMETRY_SHADER);
  EXPECT_EQ(instruction.m_InputPrimitiveDecl.Primitive,
            D3D10_SB_PRIMITIVE_TRIANGLE_ADJ);

  ParseSingleInstruction(
      {InstructionToken(D3D10_SB_OPCODE_DCL_MAX_OUTPUT_VERTEX_COUNT, 2), 64},
      &instruction, D3D10_SB_GEOMETRY_SHADER);
  EXPECT_EQ(instruction.m_GSMaxOutputVertexCountDecl.MaxOutputVertexCount,
            64u);

  ParseSingleInstruction(
      {InstructionToken(D3D11_SB_OPCODE_DCL_GS_INSTANCE_COUNT, 2), 5},
      &instruction, D3D10_SB_GEOMETRY_SHADER);
  EXPECT_EQ(instruction.m_GSInstanceCountDecl.InstanceCount, 5u);

  constexpr UINT global_flags =
      D3D10_SB_GLOBAL_FLAG_REFACTORING_ALLOWED |
      D3D11_SB_GLOBAL_FLAG_ENABLE_DOUBLE_PRECISION_FLOAT_OPS |
      D3D11_SB_GLOBAL_FLAG_FORCE_EARLY_DEPTH_STENCIL |
      D3D11_SB_GLOBAL_FLAG_ENABLE_RAW_AND_STRUCTURED_BUFFERS |
      D3D11_1_SB_GLOBAL_FLAG_SKIP_OPTIMIZATION |
      D3D11_1_SB_GLOBAL_FLAG_ENABLE_MINIMUM_PRECISION |
      D3D11_1_SB_GLOBAL_FLAG_ENABLE_DOUBLE_EXTENSIONS |
      D3D11_1_SB_GLOBAL_FLAG_ENABLE_SHADER_EXTENSIONS |
      D3D12_SB_GLOBAL_FLAG_ALL_RESOURCES_BOUND;
  ParseSingleInstruction(
      {InstructionToken(D3D10_SB_OPCODE_DCL_GLOBAL_FLAGS, 1,
                        ENCODE_D3D10_SB_GLOBAL_FLAGS(global_flags))},
      &instruction);
  EXPECT_EQ(instruction.m_GlobalFlagsDecl.Flags, global_flags);
}

TEST(ShaderBinary, DecodesTessellationAndThreadGroupDeclarations) {
  CInstruction instruction;

  ParseSingleInstruction(
      {InstructionToken(D3D11_SB_OPCODE_DCL_INPUT_CONTROL_POINT_COUNT, 1,
                        ENCODE_D3D11_SB_INPUT_CONTROL_POINT_COUNT(13))},
      &instruction, D3D11_SB_HULL_SHADER);
  EXPECT_EQ(
      instruction.m_InputControlPointCountDecl.InputControlPointCount, 13u);

  ParseSingleInstruction(
      {InstructionToken(D3D11_SB_OPCODE_DCL_OUTPUT_CONTROL_POINT_COUNT, 1,
                        ENCODE_D3D11_SB_OUTPUT_CONTROL_POINT_COUNT(29))},
      &instruction, D3D11_SB_HULL_SHADER);
  EXPECT_EQ(
      instruction.m_OutputControlPointCountDecl.OutputControlPointCount, 29u);

  ParseSingleInstruction(
      {InstructionToken(D3D11_SB_OPCODE_DCL_TESS_DOMAIN, 1,
                        ENCODE_D3D11_SB_TESS_DOMAIN(
                            D3D11_SB_TESSELLATOR_DOMAIN_QUAD))},
      &instruction, D3D11_SB_HULL_SHADER);
  EXPECT_EQ(instruction.m_TessellatorDomainDecl.TessellatorDomain,
            D3D11_SB_TESSELLATOR_DOMAIN_QUAD);

  ParseSingleInstruction(
      {InstructionToken(D3D11_SB_OPCODE_DCL_TESS_PARTITIONING, 1,
                        ENCODE_D3D11_SB_TESS_PARTITIONING(
                            D3D11_SB_TESSELLATOR_PARTITIONING_FRACTIONAL_EVEN))},
      &instruction, D3D11_SB_HULL_SHADER);
  EXPECT_EQ(instruction.m_TessellatorPartitioningDecl.TessellatorPartitioning,
            D3D11_SB_TESSELLATOR_PARTITIONING_FRACTIONAL_EVEN);

  ParseSingleInstruction(
      {InstructionToken(D3D11_SB_OPCODE_DCL_TESS_OUTPUT_PRIMITIVE, 1,
                        ENCODE_D3D11_SB_TESS_OUTPUT_PRIMITIVE(
                            D3D11_SB_TESSELLATOR_OUTPUT_TRIANGLE_CCW))},
      &instruction, D3D11_SB_HULL_SHADER);
  EXPECT_EQ(
      instruction.m_TessellatorOutputPrimitiveDecl.TessellatorOutputPrimitive,
      D3D11_SB_TESSELLATOR_OUTPUT_TRIANGLE_CCW);

  ParseSingleInstruction(
      {InstructionToken(D3D11_SB_OPCODE_DCL_HS_MAX_TESSFACTOR, 2),
       0x40b00000},
      &instruction, D3D11_SB_HULL_SHADER);
  EXPECT_EQ(instruction.m_HSMaxTessFactorDecl.MaxTessFactor, 5.5f);

  ParseSingleInstruction(
      {InstructionToken(D3D11_SB_OPCODE_DCL_HS_FORK_PHASE_INSTANCE_COUNT, 2),
       7},
      &instruction, D3D11_SB_HULL_SHADER);
  EXPECT_EQ(instruction.m_HSForkPhaseInstanceCountDecl.InstanceCount, 7u);

  ParseSingleInstruction(
      {InstructionToken(D3D11_SB_OPCODE_DCL_HS_JOIN_PHASE_INSTANCE_COUNT, 2),
       11},
      &instruction, D3D11_SB_HULL_SHADER);
  EXPECT_EQ(instruction.m_HSJoinPhaseInstanceCountDecl.InstanceCount, 11u);

  ParseSingleInstruction(
      {InstructionToken(D3D11_SB_OPCODE_DCL_THREAD_GROUP, 4), 8, 4, 2},
      &instruction);
  EXPECT_EQ(instruction.m_ThreadGroupDecl.x, 8u);
  EXPECT_EQ(instruction.m_ThreadGroupDecl.y, 4u);
  EXPECT_EQ(instruction.m_ThreadGroupDecl.z, 2u);
}

TEST(ShaderBinary, DecodesRegisterAndSystemValueDeclarations) {
  CInstruction instruction;

  ParseSingleInstruction(
      {InstructionToken(D3D10_SB_OPCODE_DCL_INPUT, 3),
       IndexedOperandToken(D3D10_SB_OPERAND_TYPE_INPUT,
                           D3D10_SB_OPERAND_INDEX_1D,
                           D3D10_SB_OPERAND_4_COMPONENT_MASK_X |
                               D3D10_SB_OPERAND_4_COMPONENT_MASK_Z),
       4},
      &instruction, D3D10_SB_VERTEX_SHADER);
  EXPECT_EQ(instruction.Operand(0).OperandType(),
            D3D10_SB_OPERAND_TYPE_INPUT);
  EXPECT_EQ(instruction.Operand(0).RegIndex(), 4u);
  EXPECT_EQ(instruction.Operand(0).WriteMask(),
            D3D10_SB_OPERAND_4_COMPONENT_MASK_X |
                D3D10_SB_OPERAND_4_COMPONENT_MASK_Z);

  ParseSingleInstruction(
      {InstructionToken(D3D10_SB_OPCODE_DCL_OUTPUT, 3),
       IndexedOperandToken(D3D10_SB_OPERAND_TYPE_OUTPUT), 6},
      &instruction, D3D10_SB_VERTEX_SHADER);
  EXPECT_EQ(instruction.Operand(0).OperandType(),
            D3D10_SB_OPERAND_TYPE_OUTPUT);
  EXPECT_EQ(instruction.Operand(0).RegIndex(), 6u);

  ParseSingleInstruction(
      {InstructionToken(D3D10_SB_OPCODE_DCL_INPUT_SIV, 4),
       IndexedOperandToken(D3D10_SB_OPERAND_TYPE_INPUT), 1,
       ENCODE_D3D10_SB_NAME(D3D10_SB_NAME_VERTEX_ID)},
      &instruction, D3D10_SB_VERTEX_SHADER);
  EXPECT_EQ(instruction.m_InputDeclSIV.Name, D3D10_SB_NAME_VERTEX_ID);

  ParseSingleInstruction(
      {InstructionToken(D3D10_SB_OPCODE_DCL_INPUT_SGV, 4),
       IndexedOperandToken(D3D10_SB_OPERAND_TYPE_INPUT), 2,
       ENCODE_D3D10_SB_NAME(D3D10_SB_NAME_INSTANCE_ID)},
      &instruction, D3D10_SB_VERTEX_SHADER);
  EXPECT_EQ(instruction.m_InputDeclSGV.Name, D3D10_SB_NAME_INSTANCE_ID);

  ParseSingleInstruction(
      {InstructionToken(
           D3D10_SB_OPCODE_DCL_INPUT_PS, 3,
           ENCODE_D3D10_SB_INPUT_INTERPOLATION_MODE(
               D3D10_SB_INTERPOLATION_LINEAR_NOPERSPECTIVE_SAMPLE)),
       IndexedOperandToken(D3D10_SB_OPERAND_TYPE_INPUT), 3},
      &instruction, D3D10_SB_PIXEL_SHADER);
  EXPECT_EQ(instruction.m_InputPSDecl.InterpolationMode,
            D3D10_SB_INTERPOLATION_LINEAR_NOPERSPECTIVE_SAMPLE);

  ParseSingleInstruction(
      {InstructionToken(D3D10_SB_OPCODE_DCL_INPUT_PS_SIV, 4,
                        ENCODE_D3D10_SB_INPUT_INTERPOLATION_MODE(
                            D3D10_SB_INTERPOLATION_LINEAR_CENTROID)),
       IndexedOperandToken(D3D10_SB_OPERAND_TYPE_INPUT), 4,
       ENCODE_D3D10_SB_NAME(D3D10_SB_NAME_SAMPLE_INDEX)},
      &instruction, D3D10_SB_PIXEL_SHADER);
  EXPECT_EQ(instruction.m_InputPSDeclSIV.InterpolationMode,
            D3D10_SB_INTERPOLATION_LINEAR_CENTROID);
  EXPECT_EQ(instruction.m_InputPSDeclSIV.Name, D3D10_SB_NAME_SAMPLE_INDEX);

  ParseSingleInstruction(
      {InstructionToken(D3D10_SB_OPCODE_DCL_INPUT_PS_SGV, 4,
                        ENCODE_D3D10_SB_INPUT_INTERPOLATION_MODE(
                            D3D10_SB_INTERPOLATION_CONSTANT)),
       IndexedOperandToken(D3D10_SB_OPERAND_TYPE_INPUT), 5,
       ENCODE_D3D10_SB_NAME(D3D10_SB_NAME_IS_FRONT_FACE)},
      &instruction, D3D10_SB_PIXEL_SHADER);
  EXPECT_EQ(instruction.m_InputPSDeclSGV.InterpolationMode,
            D3D10_SB_INTERPOLATION_CONSTANT);
  EXPECT_EQ(instruction.m_InputPSDeclSGV.Name, D3D10_SB_NAME_IS_FRONT_FACE);

  ParseSingleInstruction(
      {InstructionToken(D3D10_SB_OPCODE_DCL_OUTPUT_SIV, 4),
       IndexedOperandToken(D3D10_SB_OPERAND_TYPE_OUTPUT), 7,
       ENCODE_D3D10_SB_NAME(D3D10_SB_NAME_CLIP_DISTANCE)},
      &instruction, D3D10_SB_GEOMETRY_SHADER);
  EXPECT_EQ(instruction.m_OutputDeclSIV.Name, D3D10_SB_NAME_CLIP_DISTANCE);

  ParseSingleInstruction(
      {InstructionToken(D3D10_SB_OPCODE_DCL_OUTPUT_SGV, 4),
       IndexedOperandToken(D3D10_SB_OPERAND_TYPE_OUTPUT), 8,
       ENCODE_D3D10_SB_NAME(D3D10_SB_NAME_RENDER_TARGET_ARRAY_INDEX)},
      &instruction, D3D10_SB_GEOMETRY_SHADER);
  EXPECT_EQ(instruction.m_OutputDeclSGV.Name,
            D3D10_SB_NAME_RENDER_TARGET_ARRAY_INDEX);

  ParseSingleInstruction(
      {InstructionToken(D3D10_SB_OPCODE_DCL_INDEX_RANGE, 4),
       IndexedOperandToken(D3D10_SB_OPERAND_TYPE_INPUT), 9, 12},
      &instruction, D3D10_SB_VERTEX_SHADER);
  EXPECT_EQ(instruction.m_IndexRangeDecl.RegCount, 12u);

  ParseSingleInstruction(
      {InstructionToken(D3D11_SB_OPCODE_DCL_STREAM, 3),
       IndexedOperandToken(D3D11_SB_OPERAND_TYPE_STREAM), 2},
      &instruction, D3D10_SB_GEOMETRY_SHADER);
  EXPECT_EQ(instruction.Operand(0).OperandType(),
            D3D11_SB_OPERAND_TYPE_STREAM);
  EXPECT_EQ(instruction.Operand(0).RegIndex(), 2u);
}

TEST(ShaderBinary, DecodesShaderModelFiveResourceDeclarations) {
  CInstruction instruction;
  constexpr CShaderToken return_types =
      ENCODE_D3D10_SB_RESOURCE_RETURN_TYPE(D3D10_SB_RETURN_TYPE_UNORM, 0) |
      ENCODE_D3D10_SB_RESOURCE_RETURN_TYPE(D3D10_SB_RETURN_TYPE_SNORM, 1) |
      ENCODE_D3D10_SB_RESOURCE_RETURN_TYPE(D3D10_SB_RETURN_TYPE_SINT, 2) |
      ENCODE_D3D10_SB_RESOURCE_RETURN_TYPE(D3D10_SB_RETURN_TYPE_FLOAT, 3);

  ParseSingleInstruction(
      {InstructionToken(
           D3D10_SB_OPCODE_DCL_RESOURCE, 4,
           ENCODE_D3D10_SB_RESOURCE_DIMENSION(
               D3D10_SB_RESOURCE_DIMENSION_TEXTURE2DMSARRAY) |
               ENCODE_D3D10_SB_RESOURCE_SAMPLE_COUNT(8)),
       IndexedOperandToken(D3D10_SB_OPERAND_TYPE_RESOURCE), 5, return_types},
      &instruction, D3D10_SB_PIXEL_SHADER);
  EXPECT_EQ(instruction.m_ResourceDecl.Dimension,
            D3D10_SB_RESOURCE_DIMENSION_TEXTURE2DMSARRAY);
  EXPECT_EQ(instruction.m_ResourceDecl.SampleCount, 8u);
  EXPECT_EQ(instruction.m_ResourceDecl.ReturnType[0],
            D3D10_SB_RETURN_TYPE_UNORM);
  EXPECT_EQ(instruction.m_ResourceDecl.ReturnType[1],
            D3D10_SB_RETURN_TYPE_SNORM);
  EXPECT_EQ(instruction.m_ResourceDecl.ReturnType[2],
            D3D10_SB_RETURN_TYPE_SINT);
  EXPECT_EQ(instruction.m_ResourceDecl.ReturnType[3],
            D3D10_SB_RETURN_TYPE_FLOAT);
  EXPECT_EQ(instruction.m_ResourceDecl.Space, 0u);

  ParseSingleInstruction(
      {InstructionToken(D3D10_SB_OPCODE_DCL_SAMPLER, 3,
                        ENCODE_D3D10_SB_SAMPLER_MODE(
                            D3D10_SB_SAMPLER_MODE_COMPARISON)),
       IndexedOperandToken(D3D10_SB_OPERAND_TYPE_SAMPLER), 3},
      &instruction, D3D10_SB_PIXEL_SHADER);
  EXPECT_EQ(instruction.m_SamplerDecl.SamplerMode,
            D3D10_SB_SAMPLER_MODE_COMPARISON);
  EXPECT_EQ(instruction.m_SamplerDecl.Space, 0u);

  ParseSingleInstruction(
      {InstructionToken(
           D3D10_SB_OPCODE_DCL_CONSTANT_BUFFER, 4,
           ENCODE_D3D10_SB_D3D10_SB_CONSTANT_BUFFER_ACCESS_PATTERN(
               D3D10_SB_CONSTANT_BUFFER_DYNAMIC_INDEXED)),
       IndexedOperandToken(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
                           D3D10_SB_OPERAND_INDEX_2D),
       2, 96},
      &instruction, D3D10_SB_VERTEX_SHADER);
  EXPECT_EQ(instruction.m_ConstantBufferDecl.AccessPattern,
            D3D10_SB_CONSTANT_BUFFER_DYNAMIC_INDEXED);
  EXPECT_EQ(instruction.m_ConstantBufferDecl.Size, 96u);
  EXPECT_EQ(instruction.m_ConstantBufferDecl.Space, 0u);
}

} // namespace
