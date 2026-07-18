#include <dxmt_test.hpp>

#include "DXBCParser/ShaderBinary.h"

#include <algorithm>
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
  EXPECT_ANY_THROW(GetNumInstructionOperands(
      static_cast<D3D10_SB_OPCODE_TYPE>(-1)));
}

TEST(ShaderBinary, InitializesEveryNonReservedOpcodeEntry) {
  constexpr std::array reserved = {
      D3D10_SB_OPCODE_RESERVED0,
      D3D10_1_SB_OPCODE_RESERVED1,
      D3D11_SB_OPCODE_RESERVED0,
      D3D11_1_SB_OPCODE_RESERVED0,
      D3DWDDM1_3_SB_OPCODE_RESERVED0,
  };
  const auto is_reserved = [&](D3D10_SB_OPCODE_TYPE opcode) {
    return std::find(reserved.begin(), reserved.end(), opcode) !=
           reserved.end();
  };

  for (UINT value = 0; value < D3D10_SB_NUM_OPCODES; ++value) {
    const auto opcode = static_cast<D3D10_SB_OPCODE_TYPE>(value);
    SCOPED_TRACE(value);
    if (is_reserved(opcode)) {
      EXPECT_EQ(g_InstructionInfo[value].m_Name[0], '\0');
      continue;
    }
    EXPECT_NE(g_InstructionInfo[value].m_Name[0], '\0');
    EXPECT_NE(std::memchr(g_InstructionInfo[value].m_Name, '\0',
                          sizeof(g_InstructionInfo[value].m_Name)),
              nullptr);
    EXPECT_LE(g_InstructionInfo[value].m_NumOperands,
              D3D10_SB_MAX_INSTRUCTION_OPERANDS);
    EXPECT_EQ(GetNumInstructionOperands(opcode),
              g_InstructionInfo[value].m_NumOperands);
    EXPECT_GE(g_InstructionInfo[value].m_OpClass, D3D10_SB_FLOAT_OP);
    EXPECT_LE(g_InstructionInfo[value].m_OpClass, D3D11_SB_DEBUG_OP);
  }
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

TEST(ShaderBinary, RejectsInvalidOperandEncodings) {
  CShaderCodeParser parser;
  COperandIndex index;
  EXPECT_ANY_THROW(parser.ParseIndex(
      &index, static_cast<D3D10_SB_OPERAND_INDEX_REPRESENTATION>(3)));

  constexpr std::array<CShaderToken, 1> invalid_component_selection = {
      ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(
          D3D10_SB_OPERAND_4_COMPONENT) |
      ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(
          static_cast<D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE>(3)) |
      ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_TEMP) |
      ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(
          D3D10_SB_OPERAND_INDEX_0D),
  };
  COperand operand;
  EXPECT_ANY_THROW(parser.ParseOperandAt(
      &operand, invalid_component_selection.data(),
      invalid_component_selection.data() + invalid_component_selection.size()));
}

TEST(ShaderBinary, IgnoresUnknownOperandAndInstructionExtensions) {
  constexpr std::array<CShaderToken, 2> operand_tokens = {
      ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(
          D3D10_SB_OPERAND_0_COMPONENT) |
          ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_TEMP) |
          ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(
              D3D10_SB_OPERAND_INDEX_0D) |
          ENCODE_D3D10_SB_OPERAND_EXTENDED(true),
      static_cast<CShaderToken>(3),
  };
  CShaderCodeParser parser;
  COperand operand;
  const auto *end = parser.ParseOperandAt(
      &operand, operand_tokens.data(),
      operand_tokens.data() + operand_tokens.size());
  EXPECT_EQ(end, operand_tokens.data() + operand_tokens.size());
  EXPECT_EQ(operand.m_ExtendedOperandType,
            static_cast<D3D10_SB_EXTENDED_OPERAND_TYPE>(3));
  EXPECT_EQ(operand.Modifier(), D3D10_SB_OPERAND_MODIFIER_NONE);
  EXPECT_EQ(operand.m_MinPrecision,
            D3D11_SB_OPERAND_MIN_PRECISION_DEFAULT);

  constexpr std::array<CShaderToken, 4> shader = {
      ENCODE_D3D10_SB_TOKENIZED_PROGRAM_VERSION_TOKEN(D3D10_SB_PIXEL_SHADER, 5,
                                                      0),
      ENCODE_D3D10_SB_TOKENIZED_PROGRAM_LENGTH(4),
      ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_RET) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(2) |
          ENCODE_D3D10_SB_OPCODE_EXTENDED(true),
      ENCODE_D3D10_SB_EXTENDED_OPCODE_TYPE(
          static_cast<D3D10_SB_EXTENDED_OPCODE_TYPE>(63)),
  };
  CShaderCodeParser instruction_parser(shader.data());
  CInstruction instruction;
  instruction_parser.ParseInstruction(&instruction);
  EXPECT_EQ(instruction.m_ExtendedOpCodeCount, 1u);
  EXPECT_EQ(instruction.m_OpCodeEx[0],
            static_cast<D3D10_SB_EXTENDED_OPCODE_TYPE>(63));
  EXPECT_TRUE(instruction_parser.EndOfShader());
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

TEST(ShaderBinary, DecodesShaderModelFiveOneRegisterSpaces) {
  CInstruction instruction;
  constexpr CShaderToken return_types =
      ENCODE_D3D10_SB_RESOURCE_RETURN_TYPE(D3D10_SB_RETURN_TYPE_UINT, 0) |
      ENCODE_D3D10_SB_RESOURCE_RETURN_TYPE(D3D10_SB_RETURN_TYPE_SINT, 1) |
      ENCODE_D3D10_SB_RESOURCE_RETURN_TYPE(D3D10_SB_RETURN_TYPE_FLOAT, 2) |
      ENCODE_D3D10_SB_RESOURCE_RETURN_TYPE(D3D11_SB_RETURN_TYPE_DOUBLE, 3);

  ParseSingleInstruction(
      {InstructionToken(D3D10_SB_OPCODE_DCL_RESOURCE, 7,
                        ENCODE_D3D10_SB_RESOURCE_DIMENSION(
                            D3D10_SB_RESOURCE_DIMENSION_TEXTURECUBEARRAY)),
       IndexedOperandToken(D3D10_SB_OPERAND_TYPE_RESOURCE,
                           D3D10_SB_OPERAND_INDEX_3D),
       101, 4, 15, return_types, 8},
      &instruction, D3D10_SB_PIXEL_SHADER, 5, 1);
  EXPECT_EQ(instruction.Operand(0).OperandIndexDimension(),
            D3D10_SB_OPERAND_INDEX_3D);
  EXPECT_EQ(instruction.Operand(0).OperandIndex(0)->m_RegIndex, 101u);
  EXPECT_EQ(instruction.Operand(0).OperandIndex(1)->m_RegIndex, 4u);
  EXPECT_EQ(instruction.Operand(0).OperandIndex(2)->m_RegIndex, 15u);
  EXPECT_EQ(instruction.m_ResourceDecl.Space, 8u);

  ParseSingleInstruction(
      {InstructionToken(D3D10_SB_OPCODE_DCL_SAMPLER, 6,
                        ENCODE_D3D10_SB_SAMPLER_MODE(
                            D3D10_SB_SAMPLER_MODE_MONO)),
       IndexedOperandToken(D3D10_SB_OPERAND_TYPE_SAMPLER,
                           D3D10_SB_OPERAND_INDEX_3D),
       102, 1, 3, 9},
      &instruction, D3D10_SB_PIXEL_SHADER, 5, 1);
  EXPECT_EQ(instruction.m_SamplerDecl.SamplerMode,
            D3D10_SB_SAMPLER_MODE_MONO);
  EXPECT_EQ(instruction.m_SamplerDecl.Space, 9u);

  ParseSingleInstruction(
      {InstructionToken(
           D3D10_SB_OPCODE_DCL_CONSTANT_BUFFER, 7,
           ENCODE_D3D10_SB_D3D10_SB_CONSTANT_BUFFER_ACCESS_PATTERN(
               D3D10_SB_CONSTANT_BUFFER_IMMEDIATE_INDEXED)),
       IndexedOperandToken(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER,
                           D3D10_SB_OPERAND_INDEX_3D),
       103, 2, 6, 256, 10},
      &instruction, D3D10_SB_VERTEX_SHADER, 5, 1);
  EXPECT_EQ(instruction.m_ConstantBufferDecl.Size, 256u);
  EXPECT_EQ(instruction.m_ConstantBufferDecl.Space, 10u);
}

TEST(ShaderBinary, DecodesUavSrvAndSharedMemoryDeclarations) {
  CInstruction instruction;
  constexpr UINT resource_flags =
      D3D11_SB_GLOBALLY_COHERENT_ACCESS |
      D3D11_SB_RASTERIZER_ORDERED_ACCESS |
      D3D11_SB_UAV_HAS_ORDER_PRESERVING_COUNTER;
  constexpr CShaderToken return_types =
      ENCODE_D3D10_SB_RESOURCE_RETURN_TYPE(D3D10_SB_RETURN_TYPE_UNORM, 0) |
      ENCODE_D3D10_SB_RESOURCE_RETURN_TYPE(D3D10_SB_RETURN_TYPE_UINT, 1) |
      ENCODE_D3D10_SB_RESOURCE_RETURN_TYPE(D3D10_SB_RETURN_TYPE_SINT, 2) |
      ENCODE_D3D10_SB_RESOURCE_RETURN_TYPE(D3D10_SB_RETURN_TYPE_FLOAT, 3);

  ParseSingleInstruction(
      {InstructionToken(
           D3D11_SB_OPCODE_DCL_UNORDERED_ACCESS_VIEW_TYPED, 7,
           ENCODE_D3D10_SB_RESOURCE_DIMENSION(
               D3D10_SB_RESOURCE_DIMENSION_TEXTURE3D) |
               ENCODE_D3D11_SB_RESOURCE_FLAGS(resource_flags)),
       IndexedOperandToken(D3D11_SB_OPERAND_TYPE_UNORDERED_ACCESS_VIEW,
                           D3D10_SB_OPERAND_INDEX_3D),
       201, 0, 7, return_types, 11},
      &instruction, D3D11_SB_COMPUTE_SHADER, 5, 1);
  EXPECT_EQ(instruction.m_TypedUAVDecl.Dimension,
            D3D10_SB_RESOURCE_DIMENSION_TEXTURE3D);
  EXPECT_EQ(instruction.m_TypedUAVDecl.Flags, resource_flags);
  EXPECT_EQ(instruction.m_TypedUAVDecl.ReturnType[0],
            D3D10_SB_RETURN_TYPE_UNORM);
  EXPECT_EQ(instruction.m_TypedUAVDecl.ReturnType[3],
            D3D10_SB_RETURN_TYPE_FLOAT);
  EXPECT_EQ(instruction.m_TypedUAVDecl.Space, 11u);

  ParseSingleInstruction(
      {InstructionToken(D3D11_SB_OPCODE_DCL_UNORDERED_ACCESS_VIEW_RAW, 6,
                        ENCODE_D3D11_SB_RESOURCE_FLAGS(resource_flags)),
       IndexedOperandToken(D3D11_SB_OPERAND_TYPE_UNORDERED_ACCESS_VIEW,
                           D3D10_SB_OPERAND_INDEX_3D),
       202, 8, 9, 12},
      &instruction, D3D11_SB_COMPUTE_SHADER, 5, 1);
  EXPECT_EQ(instruction.m_RawUAVDecl.Flags, resource_flags);
  EXPECT_EQ(instruction.m_RawUAVDecl.Space, 12u);

  ParseSingleInstruction(
      {InstructionToken(
           D3D11_SB_OPCODE_DCL_UNORDERED_ACCESS_VIEW_STRUCTURED, 7,
           ENCODE_D3D11_SB_RESOURCE_FLAGS(resource_flags)),
       IndexedOperandToken(D3D11_SB_OPERAND_TYPE_UNORDERED_ACCESS_VIEW,
                           D3D10_SB_OPERAND_INDEX_3D),
       203, 10, 20, 64, 13},
      &instruction, D3D11_SB_COMPUTE_SHADER, 5, 1);
  EXPECT_EQ(instruction.m_StructuredUAVDecl.Flags, resource_flags);
  EXPECT_EQ(instruction.m_StructuredUAVDecl.ByteStride, 64u);
  EXPECT_EQ(instruction.m_StructuredUAVDecl.Space, 13u);

  ParseSingleInstruction(
      {InstructionToken(D3D11_SB_OPCODE_DCL_RESOURCE_RAW, 6),
       IndexedOperandToken(D3D10_SB_OPERAND_TYPE_RESOURCE,
                           D3D10_SB_OPERAND_INDEX_3D),
       204, 21, 30, 14},
      &instruction, D3D11_SB_COMPUTE_SHADER, 5, 1);
  EXPECT_EQ(instruction.m_RawSRVDecl.Space, 14u);

  ParseSingleInstruction(
      {InstructionToken(D3D11_SB_OPCODE_DCL_RESOURCE_STRUCTURED, 7),
       IndexedOperandToken(D3D10_SB_OPERAND_TYPE_RESOURCE,
                           D3D10_SB_OPERAND_INDEX_3D),
       205, 31, 40, 80, 15},
      &instruction, D3D11_SB_COMPUTE_SHADER, 5, 1);
  EXPECT_EQ(instruction.m_StructuredSRVDecl.ByteStride, 80u);
  EXPECT_EQ(instruction.m_StructuredSRVDecl.Space, 15u);

  ParseSingleInstruction(
      {InstructionToken(
           D3D11_SB_OPCODE_DCL_THREAD_GROUP_SHARED_MEMORY_RAW, 4),
       IndexedOperandToken(
           D3D11_SB_OPERAND_TYPE_THREAD_GROUP_SHARED_MEMORY),
       3, 4096},
      &instruction);
  EXPECT_EQ(instruction.m_RawTGSMDecl.ByteCount, 4096u);

  ParseSingleInstruction(
      {InstructionToken(
           D3D11_SB_OPCODE_DCL_THREAD_GROUP_SHARED_MEMORY_STRUCTURED, 5),
       IndexedOperandToken(
           D3D11_SB_OPERAND_TYPE_THREAD_GROUP_SHARED_MEMORY),
       4, 32, 128},
      &instruction);
  EXPECT_EQ(instruction.m_StructuredTGSMDecl.StructByteStride, 32u);
  EXPECT_EQ(instruction.m_StructuredTGSMDecl.StructCount, 128u);
}

TEST(ShaderBinary, DecodesFunctionTablesInterfacesAndCalls) {
  CInstruction instruction;

  ParseSingleInstruction(
      {InstructionToken(D3D11_SB_OPCODE_DCL_FUNCTION_TABLE, 5), 7, 2, 31, 32},
      &instruction);
  EXPECT_EQ(instruction.m_FunctionTableDecl.FunctionTableNumber, 7u);
  ASSERT_EQ(instruction.m_FunctionTableDecl.TableLength, 2u);
  ASSERT_NE(instruction.m_FunctionTableDecl.pFunctionIdentifiers, nullptr);
  EXPECT_EQ(instruction.m_FunctionTableDecl.pFunctionIdentifiers[0], 31u);
  EXPECT_EQ(instruction.m_FunctionTableDecl.pFunctionIdentifiers[1], 32u);

  ParseSingleInstruction(
      {InstructionToken(D3D11_SB_OPCODE_DCL_FUNCTION_TABLE, 0,
                        ENCODE_D3D10_SB_OPCODE_EXTENDED(true)),
       6, 8, 2, 41, 42},
      &instruction);
  EXPECT_EQ(instruction.m_ExtendedOpCodeCount, 1u);
  EXPECT_EQ(instruction.m_FunctionTableDecl.FunctionTableNumber, 8u);
  ASSERT_EQ(instruction.m_FunctionTableDecl.TableLength, 2u);
  EXPECT_EQ(instruction.m_FunctionTableDecl.pFunctionIdentifiers[0], 41u);
  EXPECT_EQ(instruction.m_FunctionTableDecl.pFunctionIdentifiers[1], 42u);

  ParseSingleInstruction(
      {InstructionToken(D3D11_SB_OPCODE_DCL_INTERFACE, 6,
                        ENCODE_D3D11_SB_INTERFACE_INDEXED_BIT(true)),
       12, 4,
       ENCODE_D3D11_SB_INTERFACE_TABLE_LENGTH(2) |
           ENCODE_D3D11_SB_INTERFACE_ARRAY_LENGTH(3),
       51, 52},
      &instruction);
  EXPECT_TRUE(instruction.m_InterfaceDecl.bDynamicallyIndexed);
  EXPECT_EQ(instruction.m_InterfaceDecl.InterfaceNumber, 12u);
  EXPECT_EQ(instruction.m_InterfaceDecl.ExpectedTableSize, 4u);
  EXPECT_EQ(instruction.m_InterfaceDecl.TableLength, 2u);
  EXPECT_EQ(instruction.m_InterfaceDecl.ArrayLength, 3u);
  ASSERT_NE(instruction.m_InterfaceDecl.pFunctionTableIdentifiers, nullptr);
  EXPECT_EQ(instruction.m_InterfaceDecl.pFunctionTableIdentifiers[0], 51u);
  EXPECT_EQ(instruction.m_InterfaceDecl.pFunctionTableIdentifiers[1], 52u);

  ParseSingleInstruction(
      {InstructionToken(D3D11_SB_OPCODE_DCL_INTERFACE, 0,
                        ENCODE_D3D10_SB_OPCODE_EXTENDED(true)),
       7, 13, 5,
       ENCODE_D3D11_SB_INTERFACE_TABLE_LENGTH(2) |
           ENCODE_D3D11_SB_INTERFACE_ARRAY_LENGTH(6),
       61, 62},
      &instruction);
  EXPECT_FALSE(instruction.m_InterfaceDecl.bDynamicallyIndexed);
  EXPECT_EQ(instruction.m_InterfaceDecl.InterfaceNumber, 13u);
  EXPECT_EQ(instruction.m_InterfaceDecl.ExpectedTableSize, 5u);
  EXPECT_EQ(instruction.m_InterfaceDecl.TableLength, 2u);
  EXPECT_EQ(instruction.m_InterfaceDecl.ArrayLength, 6u);

  ParseSingleInstruction(
      {InstructionToken(D3D11_SB_OPCODE_INTERFACE_CALL, 4), 9,
       IndexedOperandToken(D3D11_SB_OPERAND_TYPE_INTERFACE,
                           D3D10_SB_OPERAND_INDEX_1D),
       14},
      &instruction);
  EXPECT_EQ(instruction.m_InterfaceCall.FunctionIndex, 9u);
  ASSERT_EQ(instruction.m_InterfaceCall.pInterfaceOperand,
            &instruction.m_Operands[0]);
  EXPECT_EQ(instruction.m_InterfaceCall.pInterfaceOperand->OperandType(),
            D3D11_SB_OPERAND_TYPE_INTERFACE);
  EXPECT_EQ(instruction.m_InterfaceCall.pInterfaceOperand->RegIndex(), 14u);
}

TEST(ShaderBinary, DecodesValidShaderMessages) {
  CInstruction instruction;
  constexpr CShaderToken immediate =
      ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_1_COMPONENT) |
      ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_IMMEDIATE32);

  ParseSingleInstruction(
      {ENCODE_D3D10_SB_CUSTOMDATA_CLASS(D3D11_SB_CUSTOMDATA_SHADER_MESSAGE),
       11, D3D11_SB_SHADER_MESSAGE_ID_MESSAGE,
       D3D11_SB_SHADER_MESSAGE_FORMAT_ANSI_PRINTF, 4, 1, 2, immediate, 99,
       0x25753d76, 0},
      &instruction, D3D10_SB_PIXEL_SHADER);

  EXPECT_EQ(instruction.m_CustomData.Type,
            D3D11_SB_CUSTOMDATA_SHADER_MESSAGE);
  EXPECT_EQ(instruction.m_CustomData.DataSizeInBytes, 36u);
  const auto &message = instruction.m_CustomData.ShaderMessage;
  EXPECT_EQ(message.MessageID, D3D11_SB_SHADER_MESSAGE_ID_MESSAGE);
  EXPECT_EQ(message.FormatStyle, D3D11_SB_SHADER_MESSAGE_FORMAT_ANSI_PRINTF);
  EXPECT_STREQ(message.pFormatString, "v=%u");
  ASSERT_EQ(message.NumOperands, 1u);
  ASSERT_NE(message.pOperands, nullptr);
  EXPECT_EQ(message.pOperands[0].OperandType(),
            D3D10_SB_OPERAND_TYPE_IMMEDIATE32);
  EXPECT_EQ(message.pOperands[0].Imm32(), 99u);

  ParseSingleInstruction({InstructionToken(D3D10_SB_OPCODE_RET, 1)},
                         &instruction, D3D10_SB_PIXEL_SHADER);
  EXPECT_EQ(instruction.OpCode(), D3D10_SB_OPCODE_RET);
}

TEST(ShaderBinary, RejectsMalformedShaderMessageHeadersAndOperandCounts) {
  CInstruction instruction;
  constexpr CShaderToken immediate =
      ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_1_COMPONENT) |
      ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_IMMEDIATE32);

  ParseSingleInstruction(
      {ENCODE_D3D10_SB_CUSTOMDATA_CLASS(D3D11_SB_CUSTOMDATA_SHADER_MESSAGE),
       7, 1, 2, 3, 4, 5},
      &instruction);
  EXPECT_EQ(instruction.m_CustomData.ShaderMessage.NumOperands, 0u);
  EXPECT_EQ(instruction.m_CustomData.ShaderMessage.pOperands, nullptr);

  ParseSingleInstruction(
      {ENCODE_D3D10_SB_CUSTOMDATA_CLASS(D3D11_SB_CUSTOMDATA_SHADER_MESSAGE),
       8, D3D11_SB_SHADER_MESSAGE_ID_ERROR,
       D3D11_SB_SHADER_MESSAGE_FORMAT_ANSI_TEXT, 0, 1, 0x10000, 0},
      &instruction);
  EXPECT_EQ(instruction.m_CustomData.ShaderMessage.NumOperands, 0u);
  EXPECT_EQ(instruction.m_CustomData.ShaderMessage.pOperands, nullptr);

  ParseSingleInstruction(
      {ENCODE_D3D10_SB_CUSTOMDATA_CLASS(D3D11_SB_CUSTOMDATA_SHADER_MESSAGE),
       11, D3D11_SB_SHADER_MESSAGE_ID_ERROR,
       D3D11_SB_SHADER_MESSAGE_FORMAT_ANSI_PRINTF, 4, 2, 2, immediate, 7,
       0x25753d76, 0},
      &instruction);
  EXPECT_EQ(instruction.m_CustomData.ShaderMessage.NumOperands, 0u);
  EXPECT_EQ(instruction.m_CustomData.ShaderMessage.pOperands, nullptr);

  ParseSingleInstruction(
      {ENCODE_D3D10_SB_CUSTOMDATA_CLASS(D3D10_SB_CUSTOMDATA_OPAQUE), 1},
      &instruction);
  EXPECT_EQ(instruction.m_CustomData.DataSizeInBytes, 0u);
  EXPECT_EQ(instruction.m_CustomData.pData, nullptr);
}

TEST(ShaderBinary, PreservesOpaqueCustomDataBytes) {
  CInstruction instruction;
  ParseSingleInstruction(
      {ENCODE_D3D10_SB_CUSTOMDATA_CLASS(
           D3D10_SB_CUSTOMDATA_DCL_IMMEDIATE_CONSTANT_BUFFER),
       5, 0x11223344, 0x55667788, 0x99aabbcc},
      &instruction);
  EXPECT_EQ(instruction.m_CustomData.Type,
            D3D10_SB_CUSTOMDATA_DCL_IMMEDIATE_CONSTANT_BUFFER);
  ASSERT_EQ(instruction.m_CustomData.DataSizeInBytes, 12u);
  ASSERT_NE(instruction.m_CustomData.pData, nullptr);
  const auto *data = static_cast<const UINT *>(instruction.m_CustomData.pData);
  EXPECT_EQ(data[0], 0x11223344u);
  EXPECT_EQ(data[1], 0x55667788u);
  EXPECT_EQ(data[2], 0x99aabbccu);
}

TEST(ShaderBinary, AppliesOperandMutatorsAndCopiesState) {
  COperand operand(D3D10_SB_OPERAND_TYPE_TEMP, 7u);
  operand.SetModifier(D3D10_SB_OPERAND_MODIFIER_ABSNEG);
  operand.SetMinPrecision(D3D11_SB_OPERAND_MIN_PRECISION_SINT_16);
  operand.SetNonuniform(true);
  operand.SetSwizzle(D3D10_SB_4_COMPONENT_W, D3D10_SB_4_COMPONENT_Z,
                     D3D10_SB_4_COMPONENT_Y, D3D10_SB_4_COMPONENT_X);
  operand.SetIndex(0, 5, D3D10_SB_OPERAND_TYPE_INDEXABLE_TEMP, 11, 13,
                   D3D10_SB_4_COMPONENT_Z,
                   D3D11_SB_OPERAND_MIN_PRECISION_UINT_16);

  EXPECT_EQ(operand.Modifier(), D3D10_SB_OPERAND_MODIFIER_ABSNEG);
  EXPECT_TRUE(operand.m_bExtendedOperand);
  EXPECT_EQ(operand.m_ExtendedOperandType,
            D3D10_SB_EXTENDED_OPERAND_MODIFIER);
  EXPECT_EQ(operand.m_MinPrecision,
            D3D11_SB_OPERAND_MIN_PRECISION_SINT_16);
  EXPECT_TRUE(operand.m_Nonuniform);
  EXPECT_EQ(operand.SwizzleComponent(0), D3D10_SB_4_COMPONENT_W);
  EXPECT_EQ(operand.SwizzleComponent(3), D3D10_SB_4_COMPONENT_X);
  EXPECT_EQ(operand.OperandIndexType(0),
            D3D10_SB_OPERAND_INDEX_IMMEDIATE32_PLUS_RELATIVE);
  EXPECT_EQ(operand.OperandIndex(0)->m_RegIndex, 5u);
  EXPECT_EQ(operand.OperandIndex(0)->m_RelRegType,
            D3D10_SB_OPERAND_TYPE_INDEXABLE_TEMP);
  EXPECT_EQ(operand.OperandIndex(0)->m_IndexDimension,
            D3D10_SB_OPERAND_INDEX_2D);
  EXPECT_EQ(operand.OperandIndex(0)->m_RelIndex, 11u);
  EXPECT_EQ(operand.OperandIndex(0)->m_RelIndex1, 13u);
  EXPECT_EQ(operand.OperandIndex(0)->m_ComponentName,
            D3D10_SB_4_COMPONENT_Z);
  EXPECT_EQ(operand.OperandIndex(0)->m_MinPrecision,
            D3D11_SB_OPERAND_MIN_PRECISION_UINT_16);

  COperandBase copied(operand);
  COperandBase assigned;
  assigned = copied;
  assigned = assigned;
  EXPECT_EQ(assigned.OperandType(), D3D10_SB_OPERAND_TYPE_TEMP);
  EXPECT_EQ(assigned.OperandIndex(0)->m_RelIndex1, 13u);
  EXPECT_EQ(assigned.Modifier(), D3D10_SB_OPERAND_MODIFIER_ABSNEG);

  assigned.SelectComponent(D3D10_SB_4_COMPONENT_Y);
  EXPECT_EQ(assigned.m_ComponentSelection,
            D3D10_SB_OPERAND_4_COMPONENT_SELECT_1_MODE);
  EXPECT_EQ(assigned.m_ComponentName, D3D10_SB_4_COMPONENT_Y);
  assigned.SetMask(D3D10_SB_OPERAND_4_COMPONENT_MASK_X |
                   D3D10_SB_OPERAND_4_COMPONENT_MASK_W);
  EXPECT_EQ(assigned.WriteMask(), D3D10_SB_OPERAND_4_COMPONENT_MASK_X |
                                      D3D10_SB_OPERAND_4_COMPONENT_MASK_W);

  COperandIndex index;
  index.SetMinPrecision(D3D11_SB_OPERAND_MIN_PRECISION_FLOAT_16);
  index.SetNonuniformIndex(true);
  EXPECT_TRUE(index.m_bExtendedOperand);
  EXPECT_EQ(index.m_ExtendedOperandType,
            D3D10_SB_EXTENDED_OPERAND_MODIFIER);
  EXPECT_EQ(index.m_MinPrecision,
            D3D11_SB_OPERAND_MIN_PRECISION_FLOAT_16);
  EXPECT_TRUE(index.m_Nonuniform);

  assigned.Clear();
  EXPECT_EQ(assigned.OperandType(), D3D10_SB_OPERAND_TYPE_TEMP);
  EXPECT_EQ(assigned.NumComponents(), D3D10_SB_OPERAND_0_COMPONENT);
  EXPECT_FALSE(assigned.m_bExtendedOperand);
}

TEST(ShaderBinary, ConstructsImmediateAndRegisterOperands) {
  COperand unsigned_value(0xfeedbeefu);
  EXPECT_EQ(unsigned_value.OperandType(), D3D10_SB_OPERAND_TYPE_IMMEDIATE32);
  EXPECT_EQ(unsigned_value.Imm32(), 0xfeedbeefu);

  COperand signed_value(-7);
  EXPECT_EQ(signed_value.Imm32(), static_cast<UINT>(-7));

  COperand float_value(1.25f);
  EXPECT_EQ(float_value.m_Valuef[0], 1.25f);

  COperand int64_value(static_cast<INT64>(-1234567890123ll));
  EXPECT_EQ(int64_value.OperandType(), D3D10_SB_OPERAND_TYPE_IMMEDIATE64);
  EXPECT_EQ(int64_value.m_Value64[0], -1234567890123ll);

  COperand float_vector(1.0f, 2.0f, 3.0f, 4.0f);
  EXPECT_EQ(float_vector.m_Valuef[0], 1.0f);
  EXPECT_EQ(float_vector.m_Valuef[3], 4.0f);

  COperand double_vector(2.5, -3.5);
  EXPECT_EQ(double_vector.m_Valued[0], 2.5);
  EXPECT_EQ(double_vector.m_Valued[1], -3.5);

  COperand float_swizzle(1.0f, 2.0f, 3.0f, 4.0f,
                         D3D10_SB_4_COMPONENT_Z, D3D10_SB_4_COMPONENT_Z,
                         D3D10_SB_4_COMPONENT_X, D3D10_SB_4_COMPONENT_Y);
  EXPECT_EQ(float_swizzle.SwizzleComponent(0), D3D10_SB_4_COMPONENT_Z);
  EXPECT_EQ(float_swizzle.SwizzleComponent(3), D3D10_SB_4_COMPONENT_Y);

  COperand int_vector(1, -2, 3, -4);
  EXPECT_EQ(int_vector.m_Value[1], static_cast<UINT>(-2));
  EXPECT_EQ(int_vector.m_Value[3], static_cast<UINT>(-4));

  COperand int_swizzle(1, 2, 3, 4, D3D10_SB_4_COMPONENT_Y,
                       D3D10_SB_4_COMPONENT_X, D3D10_SB_4_COMPONENT_W,
                       D3D10_SB_4_COMPONENT_Z);
  EXPECT_EQ(int_swizzle.SwizzleComponent(2), D3D10_SB_4_COMPONENT_W);

  COperand int64_vector(static_cast<INT64>(11), static_cast<INT64>(22));
  EXPECT_EQ(int64_vector.m_Value64[0], 11);
  EXPECT_EQ(int64_vector.m_Value64[1], 22);

  COperand register_swizzle(
      D3D10_SB_OPERAND_TYPE_TEMP, 9, D3D10_SB_4_COMPONENT_W,
      D3D10_SB_4_COMPONENT_W, D3D10_SB_4_COMPONENT_X,
      D3D10_SB_4_COMPONENT_Y, D3D11_SB_OPERAND_MIN_PRECISION_FLOAT_16);
  EXPECT_EQ(register_swizzle.RegIndex(), 9u);
  EXPECT_EQ(register_swizzle.SwizzleComponent(2), D3D10_SB_4_COMPONENT_X);
  EXPECT_EQ(register_swizzle.m_MinPrecision,
            D3D11_SB_OPERAND_MIN_PRECISION_FLOAT_16);

  COperand scalar_builtin(D3D10_SB_OPERAND_TYPE_INPUT_PRIMITIVEID);
  EXPECT_EQ(scalar_builtin.NumComponents(), D3D10_SB_OPERAND_1_COMPONENT);
  COperand vector_builtin(D3D11_SB_OPERAND_TYPE_INPUT_THREAD_ID);
  EXPECT_EQ(vector_builtin.NumComponents(), D3D10_SB_OPERAND_4_COMPONENT);
  COperand empty_builtin(D3D10_SB_OPERAND_TYPE_NULL);
  EXPECT_EQ(empty_builtin.NumComponents(), D3D10_SB_OPERAND_0_COMPONENT);

  COperand relative(D3D10_SB_OPERAND_TYPE_RESOURCE, 4,
                    D3D10_SB_OPERAND_TYPE_TEMP, 8,
                    D3D10_SB_4_COMPONENT_Y,
                    D3D11_SB_OPERAND_MIN_PRECISION_DEFAULT,
                    D3D11_SB_OPERAND_MIN_PRECISION_UINT_16);
  EXPECT_EQ(relative.OperandIndexType(0),
            D3D10_SB_OPERAND_INDEX_IMMEDIATE32_PLUS_RELATIVE);
  EXPECT_EQ(relative.OperandIndex(0)->m_RelIndex, 8u);
  EXPECT_EQ(relative.OperandIndex(0)->m_MinPrecision,
            D3D11_SB_OPERAND_MIN_PRECISION_UINT_16);
}

TEST(ShaderBinary, ConstructsInstructionsAndTracksExtensionState) {
  COperandDst destination(D3D10_SB_OPERAND_TYPE_TEMP, 1u);
  COperand first(1.0f);
  COperand second(2.0f);
  COperand third(3.0f);

  CInstruction empty(D3D10_SB_OPCODE_NOP);
  EXPECT_EQ(empty.OpCode(), D3D10_SB_OPCODE_NOP);
  EXPECT_EQ(empty.NumOperands(), 0u);

  CInstruction conditional(D3D10_SB_OPCODE_IF, first,
                           D3D10_SB_INSTRUCTION_TEST_NONZERO);
  EXPECT_EQ(conditional.NumOperands(), 1u);
  EXPECT_EQ(conditional.Test(), D3D10_SB_INSTRUCTION_TEST_NONZERO);

  CInstruction binary(D3D10_SB_OPCODE_MOV, destination, first);
  EXPECT_EQ(binary.NumOperands(), 2u);
  EXPECT_EQ(binary.Operand(0).RegIndex(), 1u);

  CInstruction ternary(D3D10_SB_OPCODE_ADD, destination, first, second);
  EXPECT_EQ(ternary.NumOperands(), 3u);

  CInstruction quaternary(D3D10_SB_OPCODE_MAD, destination, first, second,
                          third);
  EXPECT_EQ(quaternary.NumOperands(), 4u);
  EXPECT_EQ(quaternary.m_TexelOffset[0], 0);
  EXPECT_EQ(quaternary.m_TexelOffset[2], 0);

  quaternary.SetNumOperands(3);
  quaternary.SetTest(D3D10_SB_INSTRUCTION_TEST_ZERO);
  quaternary.SetPreciseMask(0b1010);
  quaternary.SetPrivateData(17, 0);
  quaternary.SetPrivateData(23, 1);
  quaternary.SetPrivateData(99, 2);
  EXPECT_EQ(quaternary.NumOperands(), 3u);
  EXPECT_EQ(quaternary.Test(), D3D10_SB_INSTRUCTION_TEST_ZERO);
  EXPECT_EQ(quaternary.GetPreciseMask(), 0b1010u);
  EXPECT_EQ(quaternary.PrivateData(0), 17u);
  EXPECT_EQ(quaternary.PrivateData(1), 23u);
  EXPECT_EQ(quaternary.PrivateData(2), 0xffffffffu);

  CInstruction offset(D3D10_SB_OPCODE_SAMPLE);
  offset.SetTexelOffset(-1, 2, -3);
  EXPECT_EQ(offset.m_ExtendedOpCodeCount, 1u);
  EXPECT_EQ(offset.m_OpCodeEx[0], D3D10_SB_EXTENDED_OPCODE_SAMPLE_CONTROLS);
  EXPECT_EQ(offset.m_TexelOffset[0], -1);
  EXPECT_EQ(offset.m_TexelOffset[2], -3);

  CInstruction array_offset(D3D10_SB_OPCODE_SAMPLE);
  constexpr INT8 offsets[3] = {4, -5, 6};
  array_offset.SetTexelOffset(offsets);
  EXPECT_EQ(array_offset.m_TexelOffset[0], 4);
  EXPECT_EQ(array_offset.m_TexelOffset[1], -5);
  EXPECT_EQ(array_offset.m_TexelOffset[2], 6);

  CInstruction resource(D3D10_SB_OPCODE_LD);
  D3D10_SB_RESOURCE_RETURN_TYPE return_types[4] = {
      D3D10_SB_RETURN_TYPE_UINT, D3D10_SB_RETURN_TYPE_SINT,
      D3D10_SB_RETURN_TYPE_FLOAT, D3D11_SB_RETURN_TYPE_DOUBLE};
  resource.SetResourceDim(D3D11_SB_RESOURCE_DIMENSION_STRUCTURED_BUFFER,
                          return_types, 24);
  EXPECT_EQ(resource.m_ExtendedOpCodeCount, 2u);
  EXPECT_EQ(resource.m_ResourceDimEx,
            D3D11_SB_RESOURCE_DIMENSION_STRUCTURED_BUFFER);
  EXPECT_EQ(resource.m_ResourceDimStructureStrideEx, 24u);
  EXPECT_EQ(resource.m_ResourceReturnTypeEx[3],
            D3D11_SB_RETURN_TYPE_DOUBLE);
}

TEST(ShaderBinary, ConstructsEveryFourComponentOperandForm) {
  COperand4 basic(D3D10_SB_OPERAND_TYPE_TEMP, 1,
                  D3D11_SB_OPERAND_MIN_PRECISION_FLOAT_16);
  EXPECT_EQ(basic.RegIndex(), 1u);
  EXPECT_EQ(basic.m_ComponentSelection,
            D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE_MODE);

  COperand4 selected(D3D10_SB_OPERAND_TYPE_INPUT, 2,
                     D3D10_SB_4_COMPONENT_Z);
  EXPECT_EQ(selected.m_ComponentSelection,
            D3D10_SB_OPERAND_4_COMPONENT_SELECT_1_MODE);
  EXPECT_EQ(selected.m_ComponentName, D3D10_SB_4_COMPONENT_Z);

  COperand4 selected_relative(
      D3D10_SB_OPERAND_TYPE_RESOURCE, 3, D3D10_SB_4_COMPONENT_W,
      D3D10_SB_OPERAND_TYPE_TEMP, 4, D3D10_SB_4_COMPONENT_Y,
      D3D11_SB_OPERAND_MIN_PRECISION_DEFAULT,
      D3D11_SB_OPERAND_MIN_PRECISION_UINT_16);
  EXPECT_EQ(selected_relative.OperandIndexType(0),
            D3D10_SB_OPERAND_INDEX_IMMEDIATE32_PLUS_RELATIVE);
  EXPECT_EQ(selected_relative.OperandIndex(0)->m_RelIndex, 4u);

  COperand4 relative(D3D10_SB_OPERAND_TYPE_RESOURCE, 5,
                     D3D10_SB_OPERAND_TYPE_TEMP, 6,
                     D3D10_SB_4_COMPONENT_X);
  EXPECT_EQ(relative.OperandIndex(0)->m_RelIndex, 6u);

  COperand4 indexable_relative(
      D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, 7,
      D3D10_SB_OPERAND_TYPE_INDEXABLE_TEMP, 8, 9,
      D3D10_SB_4_COMPONENT_Z);
  EXPECT_EQ(indexable_relative.OperandIndex(0)->m_RelIndex, 8u);
  EXPECT_EQ(indexable_relative.OperandIndex(0)->m_RelIndex1, 9u);
  EXPECT_EQ(indexable_relative.OperandIndex(0)->m_IndexDimension,
            D3D10_SB_OPERAND_INDEX_2D);

  COperand4 swizzled(D3D10_SB_OPERAND_TYPE_TEMP, 10,
                     D3D10_SB_4_COMPONENT_W, D3D10_SB_4_COMPONENT_Z,
                     D3D10_SB_4_COMPONENT_Y, D3D10_SB_4_COMPONENT_X);
  EXPECT_EQ(swizzled.SwizzleComponent(0), D3D10_SB_4_COMPONENT_W);
  EXPECT_EQ(swizzled.SwizzleComponent(3), D3D10_SB_4_COMPONENT_X);

  COperand4 swizzled_relative(
      D3D10_SB_OPERAND_TYPE_RESOURCE, 11, D3D10_SB_4_COMPONENT_Y,
      D3D10_SB_4_COMPONENT_X, D3D10_SB_4_COMPONENT_W,
      D3D10_SB_4_COMPONENT_Z, D3D10_SB_OPERAND_TYPE_INDEXABLE_TEMP, 12, 13,
      D3D10_SB_4_COMPONENT_W);
  EXPECT_EQ(swizzled_relative.SwizzleComponent(2), D3D10_SB_4_COMPONENT_W);
  EXPECT_EQ(swizzled_relative.OperandIndex(0)->m_RelIndex1, 13u);
}

TEST(ShaderBinary, ConstructsEveryDestinationOperandForm) {
  COperandDst basic(D3D10_SB_OPERAND_TYPE_TEMP, 1,
                    D3D11_SB_OPERAND_MIN_PRECISION_FLOAT_16);
  EXPECT_EQ(basic.WriteMask(), D3D10_SB_OPERAND_4_COMPONENT_MASK_ALL);

  constexpr UINT xz_mask = D3D10_SB_OPERAND_4_COMPONENT_MASK_X |
                           D3D10_SB_OPERAND_4_COMPONENT_MASK_Z;
  COperandDst masked(D3D10_SB_OPERAND_TYPE_OUTPUT, 2, xz_mask);
  EXPECT_EQ(masked.WriteMask(), xz_mask);

  COperandDst relative(
      D3D10_SB_OPERAND_TYPE_OUTPUT, 3, xz_mask,
      D3D10_SB_OPERAND_TYPE_INDEXABLE_TEMP, 4, 5,
      D3D10_SB_4_COMPONENT_Y, D3D11_SB_OPERAND_MIN_PRECISION_DEFAULT,
      D3D11_SB_OPERAND_MIN_PRECISION_SINT_16);
  EXPECT_EQ(relative.OperandIndex(0)->m_RelIndex, 4u);
  EXPECT_EQ(relative.OperandIndex(0)->m_RelIndex1, 5u);

  COperandDst relative_second_dimension(
      D3D10_SB_OPERAND_TYPE_OUTPUT, 6, xz_mask,
      D3D10_SB_OPERAND_TYPE_TEMP, 7, 8, D3D10_SB_4_COMPONENT_Z, 0,
      D3D11_SB_OPERAND_MIN_PRECISION_DEFAULT,
      D3D11_SB_OPERAND_MIN_PRECISION_UINT_16);
  EXPECT_EQ(relative_second_dimension.OperandIndexDimension(),
            D3D10_SB_OPERAND_INDEX_2D);
  EXPECT_EQ(relative_second_dimension.RegIndex(0), 6u);
  EXPECT_EQ(relative_second_dimension.OperandIndex(1)->m_RegIndex, 7u);
  EXPECT_EQ(relative_second_dimension.OperandIndex(1)->m_RelIndex, 8u);

  COperandDst two_dimensional(D3D10_SB_OPERAND_TYPE_OUTPUT, 9, 10, xz_mask);
  EXPECT_EQ(two_dimensional.RegIndexForMinorDimension(), 10u);

  COperandDst depth(D3D10_SB_OPERAND_TYPE_OUTPUT_DEPTH);
  EXPECT_EQ(depth.NumComponents(), D3D10_SB_OPERAND_1_COMPONENT);
  COperandDst no_indices(D3D10_SB_OPERAND_TYPE_NULL);
  EXPECT_EQ(no_indices.NumComponents(), D3D10_SB_OPERAND_0_COMPONENT);

  COperandDst mask_without_index(
      D3D10_SB_OPERAND_4_COMPONENT_MASK_Y,
      D3D11_SB_OPERAND_TYPE_OUTPUT_CONTROL_POINT_ID);
  EXPECT_EQ(mask_without_index.OperandIndexDimension(),
            D3D10_SB_OPERAND_INDEX_0D);
  EXPECT_EQ(mask_without_index.WriteMask(),
            D3D10_SB_OPERAND_4_COMPONENT_MASK_Y);
}

TEST(ShaderBinary, ConstructsEveryMultidimensionalOperandForm) {
  COperand2D basic(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, 1, 2);
  EXPECT_EQ(basic.RegIndex(0), 1u);
  EXPECT_EQ(basic.RegIndexForMinorDimension(), 2u);

  COperand2D selected(D3D11_SB_OPERAND_TYPE_INPUT_CONTROL_POINT, 3, 4,
                      D3D10_SB_4_COMPONENT_W);
  EXPECT_EQ(selected.m_ComponentName, D3D10_SB_4_COMPONENT_W);

  COperand2D indexable_relative(
      D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, 5, 6,
      D3D10_SB_OPERAND_TYPE_INDEXABLE_TEMP, 7, 8,
      D3D10_SB_4_COMPONENT_Z, D3D11_SB_OPERAND_MIN_PRECISION_DEFAULT,
      D3D11_SB_OPERAND_MIN_PRECISION_UINT_16);
  EXPECT_EQ(indexable_relative.OperandIndex(1)->m_RelIndex, 7u);
  EXPECT_EQ(indexable_relative.OperandIndex(1)->m_RelIndex1, 8u);

  COperand2D temp_relative(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, 9, 10,
                           D3D10_SB_OPERAND_TYPE_TEMP, 11,
                           D3D10_SB_4_COMPONENT_Y);
  EXPECT_EQ(temp_relative.OperandIndex(1)->m_RelIndex, 11u);
  EXPECT_EQ(temp_relative.OperandIndex(1)->m_RelIndex1, 0u);

  COperand2D both_relative(
      D3D10_SB_OPERAND_TYPE_RESOURCE, TRUE, TRUE, 12, 13,
      D3D10_SB_OPERAND_TYPE_TEMP, 14, 15, D3D10_SB_4_COMPONENT_X,
      D3D10_SB_OPERAND_TYPE_INDEXABLE_TEMP, 16, 17,
      D3D10_SB_4_COMPONENT_W);
  EXPECT_EQ(both_relative.OperandIndex(0)->m_RelIndex, 14u);
  EXPECT_EQ(both_relative.OperandIndex(1)->m_RelIndex, 16u);

  COperand2D mixed_relative(
      D3D10_SB_OPERAND_TYPE_RESOURCE, FALSE, TRUE, 18, 19,
      D3D10_SB_OPERAND_TYPE_TEMP, 20, 21, D3D10_SB_4_COMPONENT_X,
      D3D10_SB_OPERAND_TYPE_TEMP, 22, 23, D3D10_SB_4_COMPONENT_Y);
  EXPECT_EQ(mixed_relative.OperandIndexType(0),
            D3D10_SB_OPERAND_INDEX_IMMEDIATE32);
  EXPECT_EQ(mixed_relative.OperandIndexType(1),
            D3D10_SB_OPERAND_INDEX_IMMEDIATE32_PLUS_RELATIVE);

  COperand2D swizzled(D3D10_SB_OPERAND_TYPE_RESOURCE, 24, 25,
                      D3D10_SB_4_COMPONENT_Z, D3D10_SB_4_COMPONENT_W,
                      D3D10_SB_4_COMPONENT_X, D3D10_SB_4_COMPONENT_Y);
  EXPECT_EQ(swizzled.SwizzleComponent(0), D3D10_SB_4_COMPONENT_Z);

  COperand2D swizzled_relative(
      D3D10_SB_OPERAND_TYPE_RESOURCE, D3D10_SB_4_COMPONENT_W,
      D3D10_SB_4_COMPONENT_Z, D3D10_SB_4_COMPONENT_Y,
      D3D10_SB_4_COMPONENT_X, TRUE, FALSE, 26,
      D3D10_SB_OPERAND_TYPE_INDEXABLE_TEMP, 27, 28,
      D3D10_SB_4_COMPONENT_Z, 29, D3D10_SB_OPERAND_TYPE_TEMP, 30, 31,
      D3D10_SB_4_COMPONENT_W);
  EXPECT_EQ(swizzled_relative.OperandIndex(0)->m_RelIndex, 27u);
  EXPECT_EQ(swizzled_relative.OperandIndexType(1),
            D3D10_SB_OPERAND_INDEX_IMMEDIATE32);
  EXPECT_EQ(swizzled_relative.SwizzleComponent(3),
            D3D10_SB_4_COMPONENT_X);

  COperand3D three_dimensional(D3D10_SB_OPERAND_TYPE_RESOURCE, 32, 33, 34,
                               D3D11_SB_OPERAND_MIN_PRECISION_FLOAT_16);
  EXPECT_EQ(three_dimensional.OperandIndexDimension(),
            D3D10_SB_OPERAND_INDEX_3D);
  EXPECT_EQ(three_dimensional.RegIndexForMinorDimension(), 34u);
}

} // namespace
