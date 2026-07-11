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

} // namespace
