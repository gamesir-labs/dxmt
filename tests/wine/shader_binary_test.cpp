#include <dxmt_test.hpp>

#include "DXBCParser/ShaderBinary.h"

#include <array>
#include <cstdint>
#include <cstring>

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

} // namespace
