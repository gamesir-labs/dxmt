#include <dxmt_test.hpp>

#include "DXILParser/DXILFormatConstants.hpp"
#include "DXILParser/DXILParser.hpp"
#include "dxil_llvm_coverage_fixture.hpp"
#include "dxil_metadata_translation_fixture.hpp"
#include "dxil_missing_metadata_fixture.hpp"
#include "dxil_typed_operations_fixture.hpp"
#include "dxil_validation_errors_fixture.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

template <typename T>
void Store(std::vector<uint8_t> &bytes, size_t offset, T value) {
  ASSERT_LE(offset + sizeof(value), bytes.size());
  std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

class DxilContainerBuilder {
public:
  DxilContainerBuilder &add(uint32_t fourcc, std::vector<uint8_t> data) {
    parts_.push_back({fourcc, std::move(data)});
    return *this;
  }

  std::vector<uint8_t> build() const {
    const size_t index_end =
        dxmt::dxil::kContainerHeaderSize + parts_.size() * sizeof(uint32_t);
    size_t total_size = index_end;
    for (const auto &part : parts_)
      total_size += dxmt::dxil::kPartHeaderSize + part.data.size();

    std::vector<uint8_t> bytes(total_size);
    Store(bytes, 0, dxmt::dxil::fourcc::Container);
    for (size_t i = 0; i < 16; ++i)
      bytes[4 + i] = static_cast<uint8_t>(i);
    Store<uint16_t>(bytes, 20, 1);
    Store<uint16_t>(bytes, 22, 0);
    Store<uint32_t>(bytes, 24, bytes.size());
    Store<uint32_t>(bytes, 28, parts_.size());

    size_t offset = index_end;
    for (size_t i = 0; i < parts_.size(); ++i) {
      Store<uint32_t>(bytes, dxmt::dxil::kContainerHeaderSize + i * 4, offset);
      Store(bytes, offset, parts_[i].fourcc);
      Store<uint32_t>(bytes, offset + 4, parts_[i].data.size());
      std::copy(parts_[i].data.begin(), parts_[i].data.end(),
                bytes.begin() + offset + dxmt::dxil::kPartHeaderSize);
      offset += dxmt::dxil::kPartHeaderSize + parts_[i].data.size();
    }
    return bytes;
  }

private:
  struct Part {
    uint32_t fourcc;
    std::vector<uint8_t> data;
  };

  std::vector<Part> parts_;
};

class DxilRuntimeDataBuilder {
public:
  DxilRuntimeDataBuilder &add(uint32_t type, std::vector<uint8_t> data) {
    parts_.push_back({type, std::move(data)});
    return *this;
  }

  std::vector<uint8_t> build() const {
    size_t total_size = dxmt::dxil::kRuntimeDataHeaderSize +
                        parts_.size() * sizeof(uint32_t);
    for (const auto &part : parts_)
      total_size += dxmt::dxil::kRuntimeDataPartHeaderSize +
                    ((part.data.size() + 3u) & ~size_t(3u));

    std::vector<uint8_t> bytes(total_size);
    Store<uint32_t>(bytes, 0, 1u);
    Store<uint32_t>(bytes, 4, parts_.size());

    size_t offset = dxmt::dxil::kRuntimeDataHeaderSize +
                    parts_.size() * sizeof(uint32_t);
    for (size_t i = 0; i < parts_.size(); ++i) {
      Store<uint32_t>(bytes,
                      dxmt::dxil::kRuntimeDataHeaderSize + i * sizeof(uint32_t),
                      offset);
      Store<uint32_t>(bytes, offset, parts_[i].type);
      Store<uint32_t>(bytes, offset + 4, parts_[i].data.size());
      std::copy(parts_[i].data.begin(), parts_[i].data.end(),
                bytes.begin() + offset + dxmt::dxil::kRuntimeDataPartHeaderSize);
      offset += dxmt::dxil::kRuntimeDataPartHeaderSize +
                ((parts_[i].data.size() + 3u) & ~size_t(3u));
    }
    return bytes;
  }

private:
  struct Part {
    uint32_t type;
    std::vector<uint8_t> data;
  };

  std::vector<Part> parts_;
};

class BitcodeBuilder {
public:
  BitcodeBuilder &bits(uint64_t value, uint32_t width) {
    for (uint32_t i = 0; i < width; ++i) {
      if ((bit_offset_ & 7u) == 0)
        bytes_.push_back(0);
      bytes_.back() |= uint8_t(((value >> i) & 1u) << (bit_offset_ & 7u));
      ++bit_offset_;
    }
    return *this;
  }

  BitcodeBuilder &vbr(uint64_t value, uint32_t width) {
    const auto payload_width = width - 1;
    const auto payload_mask = (uint64_t(1) << payload_width) - 1;
    do {
      auto piece = value & payload_mask;
      value >>= payload_width;
      if (value)
        piece |= uint64_t(1) << payload_width;
      bits(piece, width);
    } while (value);
    return *this;
  }

  BitcodeBuilder &align32() {
    while (bit_offset_ & 31u)
      bits(0, 1);
    return *this;
  }

  size_t byteSize() const { return bytes_.size(); }

  std::vector<uint8_t> build() const { return bytes_; }

private:
  std::vector<uint8_t> bytes_;
  uint64_t bit_offset_ = 0;
};

std::vector<uint8_t> BuildBitcodeWithAbbreviatedRecord() {
  using namespace dxmt::dxil;
  BitcodeBuilder builder;
  builder.bits(kBitcodeMagicValue, 32)
      .bits(bitc::EnterSubblock, 2)
      .vbr(8, 8)
      .vbr(3, 4)
      .align32()
      .bits(2, 32)
      .bits(bitc::DefineAbbrev, 3)
      .vbr(2, 5)
      .bits(1, 1)
      .vbr(7, 8)
      .bits(0, 1)
      .bits(1, 3)
      .vbr(5, 5)
      .bits(bitc::FirstApplicationAbbrev, 3)
      .bits(17, 5)
      .bits(bitc::EndBlock, 3)
      .align32();
  return builder.build();
}

std::vector<uint8_t> FinishSingleBitcodeBlock(BitcodeBuilder &builder,
                                               uint32_t abbrev_width) {
  builder.bits(dxmt::dxil::bitc::EndBlock, abbrev_width).align32();
  auto bytes = builder.build();
  Store<uint32_t>(bytes, 8, (bytes.size() - 12) / sizeof(uint32_t));
  return bytes;
}

dxmt::dxil::BlobPart Part(uint32_t fourcc, const std::vector<uint8_t> &bytes) {
  return {.fourcc = fourcc, .data = bytes};
}

std::vector<uint8_t> DecodeHex(std::string_view text) {
  const auto nibble = [](char value) -> uint8_t {
    return value >= 'a' ? uint8_t(value - 'a' + 10)
                        : uint8_t(value - '0');
  };
  std::vector<uint8_t> bytes;
  bytes.reserve(text.size() / 2);
  for (size_t i = 0; i + 1 < text.size(); i += 2)
    bytes.push_back(uint8_t((nibble(text[i]) << 4) | nibble(text[i + 1])));
  return bytes;
}

std::vector<uint8_t>
BuildDxilProgram(std::span<const uint8_t> bitcode, uint32_t shader_kind = 5u,
                 uint32_t shader_model_major = 6u,
                 uint32_t shader_model_minor = 0u,
                 uint32_t dxil_version = 0x00010000u,
                 size_t trailing_padding = 0u) {
  using namespace dxmt::dxil;
  const auto program_size =
      (kDxilProgramHeaderSize + bitcode.size() + trailing_padding + 3u) &
      ~size_t(3u);
  std::vector<uint8_t> program(program_size);
  Store<uint32_t>(program, 0, (shader_kind << 16) |
                                  (shader_model_major << 4) |
                                  shader_model_minor);
  Store<uint32_t>(program, 4, program.size() / sizeof(uint32_t));
  Store<uint32_t>(program, 8, kDxilMagicValue);
  Store<uint32_t>(program, 12, dxil_version);
  Store<uint32_t>(program, 16,
                  kDxilProgramHeaderSize - kDxilBitcodeHeaderOffset);
  Store<uint32_t>(program, 20, bitcode.size());
  std::copy(bitcode.begin(), bitcode.end(),
            program.begin() + kDxilProgramHeaderSize);
  return program;
}

} // namespace

TEST(DxilNames, PreservesAllFourBytesAndMapsKnownEnums) {
  using namespace dxmt::dxil;
  EXPECT_EQ(FourCCString(fourcc::Dxil), "DXIL");
  EXPECT_EQ(FourCCString(MakeFourCC('A', '\0', 'B', 'C')),
            std::string("A\0BC", 4));

  constexpr std::array status_names = {
      "ok",
      "invalid argument",
      "truncated",
      "bad container magic",
      "invalid container size",
      "invalid part offset",
      "invalid part size",
      "missing DXIL part",
      "invalid DXIL program",
      "invalid DXIL magic",
      "invalid DXIL bitcode range",
      "invalid signature",
      "invalid feature info",
      "invalid runtime data",
      "invalid pipeline state validation",
      "invalid shader hash",
      "invalid compiler version",
      "invalid shader debug name",
      "invalid source info",
      "invalid shader PDB info",
      "invalid shader statistics",
      "invalid resource definition",
      "invalid bitcode",
      "invalid LLVM module",
  };
  for (size_t i = 0; i < status_names.size(); ++i) {
    SCOPED_TRACE(i);
    EXPECT_STREQ(StatusName(static_cast<ParseStatus>(i)), status_names[i]);
  }
  EXPECT_STREQ(StatusName(static_cast<ParseStatus>(999)), "unknown");

  constexpr std::array runtime_data_part_names = {
      "Invalid",
      "StringBuffer",
      "IndexArrays",
      "ResourceTable",
      "FunctionTable",
      "RawBytes",
      "SubobjectTable",
      "NodeIDTable",
      "NodeShaderIOAttribTable",
      "NodeShaderFuncAttribTable",
      "IONodeTable",
      "NodeShaderInfoTable",
      "ReservedMeshNodesPreviewInfoTable",
      "SignatureElementTable",
      "VSInfoTable",
      "PSInfoTable",
      "HSInfoTable",
      "DSInfoTable",
      "GSInfoTable",
      "CSInfoTable",
      "MSInfoTable",
      "ASInfoTable",
  };
  for (size_t i = 0; i < runtime_data_part_names.size(); ++i) {
    SCOPED_TRACE(i);
    EXPECT_STREQ(RuntimeDataPartTypeName(i), runtime_data_part_names[i]);
  }
  EXPECT_STREQ(RuntimeDataPartTypeName(rdat::DxilPdbInfoTable),
               "DxilPdbInfoTable");
  EXPECT_STREQ(RuntimeDataPartTypeName(rdat::DxilPdbInfoSourceTable),
               "DxilPdbInfoSourceTable");
  EXPECT_STREQ(RuntimeDataPartTypeName(rdat::DxilPdbInfoLibraryTable),
               "DxilPdbInfoLibraryTable");
  EXPECT_STREQ(RuntimeDataPartTypeName(0xffff), "Unknown");

  constexpr std::array shader_kind_names = {
      "Pixel",         "Vertex",       "Geometry",     "Hull",
      "Domain",        "Compute",      "Library",      "RayGeneration",
      "Intersection",  "AnyHit",       "ClosestHit",   "Miss",
      "Callable",      "Mesh",         "Amplification", "Node",
  };
  for (size_t i = 0; i < shader_kind_names.size(); ++i) {
    SCOPED_TRACE(i);
    EXPECT_STREQ(PsvShaderKindName(i), shader_kind_names[i]);
  }
  EXPECT_STREQ(PsvShaderKindName(0xff), "Invalid");

  constexpr std::array validation_severity_names = {"info", "warning",
                                                     "error"};
  for (size_t i = 0; i < validation_severity_names.size(); ++i)
    EXPECT_STREQ(DxilValidationSeverityName(
                     static_cast<DxilValidationSeverity>(i)),
                 validation_severity_names[i]);
  EXPECT_STREQ(DxilValidationSeverityName(
                   static_cast<DxilValidationSeverity>(999)),
               "unknown");

  constexpr std::array validation_category_names = {
      "container", "program", "metadata", "runtime-data",
      "pipeline-state-validation", "instruction", "reflection",
  };
  for (size_t i = 0; i < validation_category_names.size(); ++i)
    EXPECT_STREQ(DxilValidationCategoryName(
                     static_cast<DxilValidationCategory>(i)),
                 validation_category_names[i]);
  EXPECT_STREQ(DxilValidationCategoryName(
                   static_cast<DxilValidationCategory>(999)),
               "unknown");
}

TEST(DxilValueObjects, ReportsHashAndNamedMetadataState) {
  using namespace dxmt::dxil;
  ShaderHashInfo hash;
  EXPECT_FALSE(hash.includes_source());
  EXPECT_FALSE(hash.is_populated());
  hash.flags = 1u;
  hash.digest.back() = 0x80;
  EXPECT_TRUE(hash.includes_source());
  EXPECT_TRUE(hash.is_populated());

  LlvmModuleInfo module;
  EXPECT_FALSE(module.hasNamedMetadata("dx.entryPoints"));
  module.named_metadata.push_back({"dx.entryPoints", 2u});
  module.named_metadata.push_back({"dx.resources", 0u});
  EXPECT_TRUE(module.hasNamedMetadata("dx.entryPoints"));
  EXPECT_TRUE(module.hasNamedMetadata("dx.resources"));
  EXPECT_FALSE(module.hasNamedMetadata("DX.ENTRYPOINTS"));
  EXPECT_FALSE(module.hasNamedMetadata({}));
}

TEST(DxilOpcode, ExposesStableMetadataAndRejectsUnknownOpcodes) {
  using namespace dxmt::dxil;
  const auto *sample = FindDxilOpcodeInfo(60);
  ASSERT_NE(sample, nullptr);
  EXPECT_EQ(sample->name, "Sample");
  EXPECT_EQ(sample->opcode_class, "Sample");
  EXPECT_NE(sample->semantic_flags & DxilOpcodeSemanticGradient, 0u);
  EXPECT_STREQ(DxilOpcodeName(60), "Sample");
  EXPECT_EQ(FindDxilOpcodeInfo(10000), nullptr);
  EXPECT_STREQ(DxilOpcodeName(10000), "Unknown");
}

TEST(DxilContainer, ParsesFindsAndDescribesParts) {
  using namespace dxmt::dxil;
  const auto bytes = DxilContainerBuilder()
                         .add(fourcc::FeatureInfo, std::vector<uint8_t>(8, 1))
                         .add(fourcc::PrivateData, {2, 3})
                         .add(fourcc::PrivateData, {4})
                         .build();

  ContainerInfo info;
  ASSERT_EQ(ParseContainer(bytes.data(), bytes.size(), info), ParseStatus::Ok);
  EXPECT_EQ(info.major_version, 1u);
  EXPECT_EQ(info.minor_version, 0u);
  EXPECT_EQ(info.container_size, bytes.size());
  ASSERT_EQ(info.parts.size(), 3u);
  EXPECT_EQ(info.hash[0], 0u);
  EXPECT_EQ(info.hash[15], 15u);
  EXPECT_TRUE(info.hasPart(fourcc::FeatureInfo));
  EXPECT_EQ(info.findPart(fourcc::PrivateData), &info.parts[1]);
  EXPECT_EQ(info.findPart(fourcc::PrivateData, 2), &info.parts[2]);
  EXPECT_EQ(info.findPart(fourcc::Dxil), nullptr);
  EXPECT_EQ(DescribeContainerParts(info),
            "DXContainer v1.0 size=79 parts=3 SFI0@44+8 PRIV@60+2 PRIV@70+1");
}

TEST(DxilContainer, ReportsPreciseHeaderAndPartFailures) {
  using namespace dxmt::dxil;
  ContainerInfo info;
  EXPECT_EQ(ParseContainer(nullptr, 1, info), ParseStatus::InvalidArgument);
  EXPECT_EQ(ParseContainer(nullptr, 0, info), ParseStatus::Truncated);

  auto bytes = DxilContainerBuilder().add(fourcc::PrivateData, {1, 2}).build();
  auto malformed = bytes;
  Store<uint32_t>(malformed, 0, 0u);
  EXPECT_EQ(ParseContainer(malformed.data(), malformed.size(), info),
            ParseStatus::BadContainerMagic);

  malformed = bytes;
  Store<uint32_t>(malformed, 24, malformed.size() + 1);
  EXPECT_EQ(ParseContainer(malformed.data(), malformed.size(), info),
            ParseStatus::InvalidContainerSize);

  malformed = bytes;
  Store<uint32_t>(malformed, kContainerHeaderSize, kContainerHeaderSize);
  EXPECT_EQ(ParseContainer(malformed.data(), malformed.size(), info),
            ParseStatus::InvalidPartOffset);

  malformed = bytes;
  Store<uint32_t>(malformed, kContainerHeaderSize + sizeof(uint32_t) + 4,
                  malformed.size());
  EXPECT_EQ(ParseContainer(malformed.data(), malformed.size(), info),
            ParseStatus::InvalidPartSize);
}

TEST(DxilContainer, RejectsAliasedPartRanges) {
  using namespace dxmt::dxil;
  auto bytes = DxilContainerBuilder()
                   .add(fourcc::PrivateData, {1, 2, 3, 4})
                   .add(fourcc::FeatureInfo, std::vector<uint8_t>(8))
                   .build();
  uint32_t first_offset = 0;
  std::memcpy(&first_offset, bytes.data() + kContainerHeaderSize,
              sizeof(first_offset));
  Store<uint32_t>(bytes, kContainerHeaderSize + 4, first_offset);

  ContainerInfo info;
  EXPECT_EQ(ParseContainer(bytes.data(), bytes.size(), info),
            ParseStatus::InvalidPartOffset);
}

TEST(DxilContainer, ResetsStateBeforeRejectingMalformedInput) {
  using namespace dxmt::dxil;
  const auto valid =
      DxilContainerBuilder().add(fourcc::PrivateData, {1, 2, 3}).build();
  Parser parser;
  ASSERT_EQ(parser.parseContainerOnly(valid), ParseStatus::Ok);
  ASSERT_EQ(parser.container().parts.size(), 1u);

  auto malformed = valid;
  Store<uint32_t>(malformed, 0, 0u);
  EXPECT_EQ(parser.parseContainerOnly(malformed),
            ParseStatus::BadContainerMagic);
  EXPECT_EQ(parser.container().container_size, 0u);
  EXPECT_TRUE(parser.container().parts.empty());
}

TEST(DxilContainer, RejectsEveryTruncatedHeaderAndIndexOverflow) {
  using namespace dxmt::dxil;
  const auto valid = DxilContainerBuilder().build();
  for (size_t size = 0; size < kContainerHeaderSize; ++size) {
    SCOPED_TRACE(size);
    Parser parser;
    EXPECT_EQ(parser.parseContainerOnly(valid.data(), size),
              ParseStatus::Truncated);
  }

  auto malformed = valid;
  Store<uint32_t>(malformed, 24, kContainerHeaderSize - 1);
  Parser parser;
  EXPECT_EQ(parser.parseContainerOnly(malformed),
            ParseStatus::InvalidContainerSize);

  malformed = valid;
  Store<uint32_t>(malformed, 28,
                  std::numeric_limits<uint32_t>::max());
  EXPECT_EQ(parser.parseContainerOnly(malformed),
            ParseStatus::InvalidContainerSize);
}

TEST(DxilContainer, AcceptsReorderedPartsAndRejectsPartialOverlap) {
  using namespace dxmt::dxil;
  auto bytes = DxilContainerBuilder()
                   .add(fourcc::PrivateData, std::vector<uint8_t>(16))
                   .add(fourcc::FeatureInfo, std::vector<uint8_t>(8))
                   .build();
  uint32_t first_offset = 0;
  uint32_t second_offset = 0;
  std::memcpy(&first_offset, bytes.data() + kContainerHeaderSize,
              sizeof(first_offset));
  std::memcpy(&second_offset,
              bytes.data() + kContainerHeaderSize + sizeof(uint32_t),
              sizeof(second_offset));
  Store<uint32_t>(bytes, kContainerHeaderSize, second_offset);
  Store<uint32_t>(bytes, kContainerHeaderSize + sizeof(uint32_t),
                  first_offset);

  ContainerInfo info;
  ASSERT_EQ(ParseContainer(bytes.data(), bytes.size(), info), ParseStatus::Ok);
  ASSERT_EQ(info.parts.size(), 2u);
  EXPECT_EQ(info.parts[0].fourcc, fourcc::FeatureInfo);
  EXPECT_EQ(info.parts[1].fourcc, fourcc::PrivateData);

  Store<uint32_t>(bytes, kContainerHeaderSize, first_offset);
  Store<uint32_t>(bytes, kContainerHeaderSize + sizeof(uint32_t),
                  first_offset + kPartHeaderSize);
  EXPECT_EQ(ParseContainer(bytes.data(), bytes.size(), info),
            ParseStatus::InvalidPartOffset);
}

TEST(DxilProgram, ParsesShaderVersionAndBitcodeRange) {
  using namespace dxmt::dxil;
  std::vector<uint8_t> bytes(kDxilProgramHeaderSize + 4);
  Store<uint32_t>(bytes, 0, (5u << 16) | (6u << 4) | 2u);
  Store<uint32_t>(bytes, 4, bytes.size() / 4);
  Store<uint32_t>(bytes, 8, kDxilMagicValue);
  Store<uint32_t>(bytes, 12, 0x00010008);
  Store<uint32_t>(bytes, 16, kDxilProgramHeaderSize - kDxilBitcodeHeaderOffset);
  Store<uint32_t>(bytes, 20, 4);
  bytes[24] = 'B';
  bytes[25] = 'C';
  bytes[26] = 0xc0;
  bytes[27] = 0xde;

  DxilProgramInfo info;
  ASSERT_EQ(ParseDxilProgram(Part(fourcc::Dxil, bytes), info), ParseStatus::Ok);
  EXPECT_EQ(info.shader_kind(), 5u);
  EXPECT_EQ(info.major_version(), 6u);
  EXPECT_EQ(info.minor_version(), 2u);
  EXPECT_EQ(info.dxil_version, 0x00010008u);
  EXPECT_EQ(info.bitcode_offset, kDxilProgramHeaderSize);
  ASSERT_EQ(info.bitcode.size(), 4u);
  EXPECT_TRUE(std::equal(info.bitcode.begin(), info.bitcode.end(),
                         std::array<uint8_t, 4>{'B', 'C', 0xc0, 0xde}.begin()));
}

TEST(DxilProgram, RejectsMalformedProgramHeaderAndBitcodeBounds) {
  using namespace dxmt::dxil;
  DxilProgramInfo info;
  std::vector<uint8_t> bytes(kDxilProgramHeaderSize + 4);
  Store<uint32_t>(bytes, 4, bytes.size() / 4);
  Store<uint32_t>(bytes, 8, kDxilMagicValue);
  Store<uint32_t>(bytes, 16, kDxilProgramHeaderSize - kDxilBitcodeHeaderOffset);
  Store<uint32_t>(bytes, 20, 4);

  auto malformed = std::vector<uint8_t>(kDxilProgramHeaderSize - 1);
  EXPECT_EQ(ParseDxilProgram(Part(fourcc::Dxil, malformed), info),
            ParseStatus::InvalidDxilProgram);

  malformed = bytes;
  Store<uint32_t>(malformed, 4, 1u);
  EXPECT_EQ(ParseDxilProgram(Part(fourcc::Dxil, malformed), info),
            ParseStatus::InvalidDxilProgram);

  malformed = bytes;
  Store<uint32_t>(malformed, 8, 0u);
  EXPECT_EQ(ParseDxilProgram(Part(fourcc::Dxil, malformed), info),
            ParseStatus::InvalidDxilMagic);

  malformed = bytes;
  Store<uint32_t>(malformed, 20, 0u);
  EXPECT_EQ(ParseDxilProgram(Part(fourcc::Dxil, malformed), info),
            ParseStatus::InvalidDxilBitcodeRange);

  malformed = bytes;
  Store<uint32_t>(malformed, 16, std::numeric_limits<uint32_t>::max());
  EXPECT_EQ(ParseDxilProgram(Part(fourcc::Dxil, malformed), info),
            ParseStatus::InvalidDxilBitcodeRange);
}

TEST(DxilProgram, RejectsBitcodeRangesThatOverlapTheProgramHeader) {
  using namespace dxmt::dxil;
  std::vector<uint8_t> bytes(kDxilProgramHeaderSize + 4);
  Store<uint32_t>(bytes, 4, bytes.size() / sizeof(uint32_t));
  Store<uint32_t>(bytes, 8, kDxilMagicValue);
  Store<uint32_t>(bytes, 20, 4u);

  DxilProgramInfo info;
  for (uint32_t offset : {0u, 4u, 15u}) {
    SCOPED_TRACE(offset);
    Store<uint32_t>(bytes, 16, offset);
    EXPECT_EQ(ParseDxilProgram(Part(fourcc::Dxil, bytes), info),
              ParseStatus::InvalidDxilBitcodeRange);
  }

  Store<uint32_t>(bytes, 16,
                  kDxilProgramHeaderSize - kDxilBitcodeHeaderOffset);
  EXPECT_EQ(ParseDxilProgram(Part(fourcc::Dxil, bytes), info),
            ParseStatus::Ok);
}

TEST(DxilParser, DistinguishesContainerOnlyAndFullParsing) {
  using namespace dxmt::dxil;
  const auto bytes = DxilContainerBuilder().build();
  Parser parser;
  EXPECT_EQ(parser.parseContainerOnly(bytes), ParseStatus::Ok);
  EXPECT_EQ(parser.container().parts.size(), 0u);
  EXPECT_EQ(parser.parse(bytes), ParseStatus::MissingDxilPart);
  EXPECT_FALSE(parser.dxilProgram().has_value());
}

TEST(DxilParser, ResetClearsEveryPublishedParserView) {
  using namespace dxmt::dxil;
  const auto bytes =
      DxilContainerBuilder()
          .add(fourcc::FeatureInfo, std::vector<uint8_t>(kFeatureInfoSize))
          .add(fourcc::ShaderHash, std::vector<uint8_t>(kShaderHashSize))
          .build();
  Parser parser;
  ASSERT_EQ(parser.parseContainerOnly(bytes.data(), bytes.size()),
            ParseStatus::Ok);
  EXPECT_EQ(parser.container().parts.size(), 2u);
  EXPECT_NE(parser.findPart(fourcc::FeatureInfo), nullptr);

  parser.reset();
  EXPECT_TRUE(parser.container().parts.empty());
  EXPECT_EQ(parser.findPart(fourcc::FeatureInfo), nullptr);
  EXPECT_FALSE(parser.dxilProgram().has_value());
  EXPECT_FALSE(parser.bitcode().has_value());
  EXPECT_FALSE(parser.llvmModule().has_value());
  EXPECT_TRUE(parser.signatures().empty());
  EXPECT_FALSE(parser.featureInfo().has_value());
  EXPECT_FALSE(parser.shaderHash().has_value());
  EXPECT_FALSE(parser.compilerVersion().has_value());
  EXPECT_FALSE(parser.shaderDebugName().has_value());
  EXPECT_FALSE(parser.sourceInfo().has_value());
  EXPECT_FALSE(parser.shaderPdbInfo().has_value());
  EXPECT_FALSE(parser.shaderStatistics().has_value());
  EXPECT_FALSE(parser.resourceDef().has_value());
  EXPECT_FALSE(parser.runtimeData().has_value());
  EXPECT_FALSE(parser.pipelineStateValidation().has_value());
  EXPECT_FALSE(parser.shaderReflection().has_value());
  EXPECT_FALSE(parser.dxilValidation().has_value());
  EXPECT_FALSE(parser.dxilTranslation().has_value());
}

TEST(DxilDerivedInfo, HandlesContainerOnlyParserState) {
  using namespace dxmt::dxil;
  const std::vector<uint8_t> root_signature = {1, 2, 3, 4};
  const auto bytes =
      DxilContainerBuilder()
          .add(fourcc::RootSignature, root_signature)
          .add(fourcc::FeatureInfo, std::vector<uint8_t>(kFeatureInfoSize))
          .add(fourcc::FeatureInfo, std::vector<uint8_t>(kFeatureInfoSize))
          .build();
  Parser parser;
  ASSERT_EQ(parser.parseContainerOnly(bytes), ParseStatus::Ok);

  ShaderReflectionInfo reflection;
  ASSERT_EQ(BuildShaderReflection(parser, reflection), ParseStatus::Ok);
  EXPECT_FALSE(reflection.valid);
  EXPECT_FALSE(reflection.has_llvm_module);
  EXPECT_FALSE(reflection.has_runtime_data);
  EXPECT_FALSE(reflection.has_pipeline_state_validation);
  EXPECT_FALSE(reflection.has_resource_def);
  EXPECT_TRUE(reflection.has_root_signature);
  EXPECT_GT(reflection.root_signature_offset, 0u);
  EXPECT_TRUE(std::equal(reflection.root_signature.begin(),
                         reflection.root_signature.end(),
                         root_signature.begin()));
  EXPECT_TRUE(reflection.resources.empty());

  DxilTranslationInfo translation;
  translation.valid = true;
  ASSERT_EQ(BuildDxilTranslationInfo(parser, translation), ParseStatus::Ok);
  EXPECT_FALSE(translation.valid);
  EXPECT_TRUE(translation.resources.empty());
  EXPECT_TRUE(translation.signatures.empty());
  EXPECT_TRUE(translation.operations.empty());

  DxilValidationInfo validation;
  ASSERT_EQ(ValidateDxil(parser, validation), ParseStatus::Ok);
  EXPECT_FALSE(validation.valid);
  EXPECT_EQ(validation.error_count, 2u);
  EXPECT_EQ(validation.warning_count, 2u);
  EXPECT_EQ(validation.info_count, 0u);
  ASSERT_EQ(validation.diagnostics.size(), 4u);
  const auto has_code = [&](std::string_view code) {
    return std::any_of(validation.diagnostics.begin(),
                       validation.diagnostics.end(),
                       [&](const DxilValidationDiagnostic &diagnostic) {
                         return diagnostic.code == code;
                       });
  };
  EXPECT_TRUE(has_code("dxil-part-count"));
  EXPECT_TRUE(has_code("duplicate-singleton-part"));
  EXPECT_TRUE(has_code("missing-dxil-program"));
  EXPECT_TRUE(has_code("llvm-module-unavailable"));
}

TEST(DxilParser, ParsesMinimalLlvmModuleEndToEnd) {
  using namespace dxmt::dxil;
  constexpr std::string_view bitcode_hex =
      "4243c0de3514000005000000620c30244a59be669dfbb4bf0b51804c01000000210c00004d010000"
      "0b02210002000000160000000781239141c80449061032399201840c250508191e048b62800c4502"
      "42920b42641032143808184b0a3232884870c421234412878c1041920264c808b1142043468820c9"
      "01323284182a282a90317cb05c9120c3c8000000892000000b0000003222c80820624600212b2498"
      "0c212524980c19270c85a4906032645c20246382a0a81100136420608e000c00131472c087746087"
      "36688779680372c0078d102687076f4e27a7ddbe211540080000000000000000000000000420b141"
      "a06854000040160806000000321e980c19114c908c092647c6044362318c009440410000b1180000"
      "ac0000003308801cc4e11c6614013d88433884c38c4280077978077398710ce6000fed100ef4800e"
      "330c421ec2c11dcea11c6630053d88433884831bcc033dc8433d8c033dcc788c7470077b08077948"
      "877070077a700376788770208719cc110eec900ee1300f6e300fe3f00ef0500e3310c41dde211cd8"
      "211dc2611e6630893bbc833bd04339b4033cbc833c84033bccf0147660077b680737688772680737"
      "808770908770600776280776f8057678877780875f08877118877298877998812ceef00eeee00ef5"
      "c00eec300362c8a11ce4a11ccca11ce4a11cdc611cca211cc4811dca6106d6904339c84339984339"
      "c84339b8c33894433888033b94c32fbc833cfc823bd4033bb0c30cc7698770588772708374680778"
      "608774188774a08719ce530fee000ff2500ee4900ee3400fe1200eec500e3320281ddcc11ec2411e"
      "d2211cdc811edce01ce4e11dea011e66185138b0433a9c833bcc50247660077b6807376087777807"
      "7898514cf4900ff0500e331e6a1eca611ce8211ddec11d7e011ee4a11ccc211df0610654858338cc"
      "c33bb0433dd04339fcc23ce4433b88c33bb0c38cc50a877998877718877408077a28077298815ce3"
      "100eecc00ee5500ef33023c1d2411ee4e117d8e11dde011e6648193bb0833db4831b84c3388c4339"
      "ccc33cb8c139c8c33bd4033ccc48b471080776600771088771588719dbc60eec600fede006f0200f"
      "e5300fe5200ff6500e6e100ee3300ee5300ff3e006e9e00ee4500ef83023e2ec611cc2811dd8e117"
      "ec211de6211dc4211dd8211de8211f66209d3bbc433db80339948339cc58bc7070077778077a0807"
      "7a488777708719cbe70eef300fe1e00ee9400fe9a00fe530c3010373a8077718875f988770708774"
      "a08774d087729881844139e0c338b0433d904339cc40c4a01dcaa11de0411edec11c662463300ee1"
      "c00eec300fe9400fe50000007920000025000000721e482043880c19097232482023818c9191d144"
      "a01028643c3132428e9021a328100a000201000063736d61696e0000230844308240082308c43082"
      "401023080030c3100cc40c4241cc2014c60cc221c8486082d221c37399430b232b936b7a232b631b"
      "25385221c373b12b939b4b7b731b25403221c373b10b63b32b931b2548d221c3732973a393cb837a"
      "4b73a39b1b255000a9180000250000000b0a7228877780077a587098433db8c338b04339d0c382e6"
      "1cc6a10de8411ec2c11de6211de8211ddec11d1634e3600ee7500fe1200fe4400fe1200fe7500ef4"
      "b08081077928877060077678877108077a28077258709cc338b4013ba4833d94c3026b1cd8211cdc"
      "e11cdc201ce4611cdc201ce8811ec2611cd0a11cc8611cc2811dd861c1010ff4200fe1500ff4800e"
      "00000000d11000000600000007cc3ca4833b9c033b94033da0833c94433890c30100000061200000"
      "06000000130481860301000002000000075010cd14610000000000007120000003000000320e1022"
      "8400c90200000000000000005d0c0000090000001203941b6d61696e31352e302e376478696c2d6d"
      "732d64783c737464696e3e0000000000";
  ASSERT_EQ(bitcode_hex.size() % 2, 0u);
  const auto bitcode = DecodeHex(bitcode_hex);
  ASSERT_EQ(bitcode.size(), 1416u);

  std::vector<uint8_t> program(kDxilProgramHeaderSize + bitcode.size());
  Store<uint32_t>(program, 0, (5u << 16) | (6u << 4));
  Store<uint32_t>(program, 4, program.size() / sizeof(uint32_t));
  Store<uint32_t>(program, 8, kDxilMagicValue);
  Store<uint32_t>(program, 12, 0x00010000u);
  Store<uint32_t>(program, 16,
                  kDxilProgramHeaderSize - kDxilBitcodeHeaderOffset);
  Store<uint32_t>(program, 20, bitcode.size());
  std::copy(bitcode.begin(), bitcode.end(),
            program.begin() + kDxilProgramHeaderSize);
  const auto container =
      DxilContainerBuilder().add(fourcc::Dxil, program).build();

  Parser parser;
  ASSERT_EQ(parser.parse(container), ParseStatus::Ok);
  ASSERT_TRUE(parser.dxilProgram().has_value());
  EXPECT_EQ(parser.dxilProgram()->shader_kind(), 5u);
  ASSERT_TRUE(parser.bitcode().has_value());
  EXPECT_FALSE(parser.bitcode()->blocks.empty());
  ASSERT_TRUE(parser.llvmModule().has_value());
  EXPECT_EQ(parser.llvmModule()->target_triple, "dxil-ms-dx");
  ASSERT_TRUE(parser.llvmModule()->shader_model.has_value());
  EXPECT_EQ(parser.llvmModule()->shader_model->kind, "cs");
  ASSERT_EQ(parser.llvmModule()->entry_points.size(), 1u);
  EXPECT_EQ(parser.llvmModule()->entry_points[0].function_name, "main");
  EXPECT_EQ(parser.llvmModule()->entry_points[0].name, "main");

  ASSERT_TRUE(parser.shaderReflection().has_value());
  EXPECT_TRUE(parser.shaderReflection()->valid);
  EXPECT_EQ(parser.shaderReflection()->entry_point_name, "main");
  EXPECT_EQ(parser.shaderReflection()->function_name, "main");
  EXPECT_EQ(parser.shaderReflection()->shader_stage_name, "Compute");
  EXPECT_EQ(parser.shaderReflection()->shader_model_major, 6u);
  EXPECT_EQ(parser.shaderReflection()->shader_model_minor, 0u);

  ASSERT_TRUE(parser.dxilValidation().has_value());
  EXPECT_TRUE(parser.dxilValidation()->valid);
  EXPECT_EQ(parser.dxilValidation()->error_count, 0u);
  ASSERT_TRUE(parser.dxilTranslation().has_value());
  EXPECT_TRUE(parser.dxilTranslation()->valid);
  EXPECT_TRUE(parser.dxilTranslation()->has_metadata);
  EXPECT_EQ(parser.dxilTranslation()->entry_point_name, "main");
  EXPECT_EQ(parser.dxilTranslation()->function_name, "main");
  EXPECT_EQ(parser.dxilTranslation()->shader_stage_name, "Compute");

  constexpr size_t runtime_offset = sizeof(uint32_t);
  constexpr size_t resource_count_offset = runtime_offset + kPsvRuntimeInfo4Size;
  constexpr size_t resource_stride_offset =
      resource_count_offset + sizeof(uint32_t);
  constexpr size_t resource_offset =
      resource_stride_offset + sizeof(uint32_t);
  constexpr size_t string_size_offset =
      resource_offset + kPsvResourceBindInfo1Size;
  constexpr size_t string_offset = string_size_offset + sizeof(uint32_t);
  constexpr size_t semantic_count_offset = string_offset + 8;
  std::vector<uint8_t> psv(semantic_count_offset + sizeof(uint32_t));
  Store<uint32_t>(psv, 0, kPsvRuntimeInfo4Size);
  psv[runtime_offset + 24] = 5;
  Store<uint32_t>(psv, runtime_offset + 36, 8u);
  Store<uint32_t>(psv, runtime_offset + 40, 4u);
  Store<uint32_t>(psv, runtime_offset + 44, 2u);
  Store<uint32_t>(psv, runtime_offset + 48, 0u);
  Store<uint32_t>(psv, runtime_offset + 52, 1024u);
  Store<uint32_t>(psv, resource_count_offset, 1u);
  Store<uint32_t>(psv, resource_stride_offset, kPsvResourceBindInfo1Size);
  Store<uint32_t>(psv, resource_offset + 0, 3u);
  Store<uint32_t>(psv, resource_offset + 4, 2u);
  Store<uint32_t>(psv, resource_offset + 8, 4u);
  Store<uint32_t>(psv, resource_offset + 12, 7u);
  Store<uint32_t>(psv, resource_offset + 16, 6u);
  Store<uint32_t>(psv, resource_offset + 20, 9u);
  Store<uint32_t>(psv, string_size_offset, 8u);
  std::memcpy(psv.data() + string_offset, "main", 5);
  Store<uint32_t>(psv, semantic_count_offset, 0u);
  const std::vector<uint8_t> root_signature = {9, 8, 7, 6};
  const auto integrated_container =
      DxilContainerBuilder()
          .add(fourcc::Dxil, program)
          .add(fourcc::PipelineStateValidation, psv)
          .add(fourcc::RootSignature, root_signature)
          .build();

  Parser integrated_parser;
  ASSERT_EQ(integrated_parser.parse(integrated_container), ParseStatus::Ok);
  ASSERT_TRUE(integrated_parser.shaderReflection().has_value());
  const auto &integrated_reflection = *integrated_parser.shaderReflection();
  EXPECT_TRUE(integrated_reflection.has_pipeline_state_validation);
  EXPECT_TRUE(integrated_reflection.has_root_signature);
  EXPECT_EQ(integrated_reflection.entry_point_name, "main");
  EXPECT_EQ(integrated_reflection.num_threads_x, 8u);
  EXPECT_EQ(integrated_reflection.num_threads_y, 4u);
  EXPECT_EQ(integrated_reflection.num_threads_z, 2u);
  EXPECT_EQ(integrated_reflection.group_shared_bytes_used, 1024u);
  EXPECT_TRUE(std::equal(integrated_reflection.root_signature.begin(),
                         integrated_reflection.root_signature.end(),
                         root_signature.begin()));
  ASSERT_EQ(integrated_reflection.resources.size(), 1u);
  EXPECT_TRUE(integrated_reflection.resources[0].from_psv);
  EXPECT_EQ(integrated_reflection.resources[0].space, 2u);
  EXPECT_EQ(integrated_reflection.resources[0].lower_bound, 4u);
  EXPECT_EQ(integrated_reflection.resources[0].upper_bound, 7u);
  EXPECT_EQ(integrated_reflection.resources[0].bind_count, 4u);

  ASSERT_TRUE(integrated_parser.dxilValidation().has_value());
  EXPECT_TRUE(integrated_parser.dxilValidation()->valid);
  EXPECT_EQ(integrated_parser.dxilValidation()->error_count, 0u);
  ASSERT_TRUE(integrated_parser.dxilTranslation().has_value());
  const auto &integrated_translation = *integrated_parser.dxilTranslation();
  EXPECT_TRUE(integrated_translation.has_pipeline_state_validation);
  EXPECT_TRUE(integrated_translation.has_root_signature);
  EXPECT_EQ(integrated_translation.num_threads_x, 8u);
  EXPECT_EQ(integrated_translation.group_shared_bytes_used, 1024u);
  ASSERT_EQ(integrated_translation.resources.size(), 1u);
  EXPECT_EQ(integrated_translation.resources[0].source_mask,
            DxilTranslationSourcePipelineStateValidation);
  EXPECT_EQ(integrated_translation.resources[0].bind_count, 4u);

  std::vector<uint8_t> strings = {'m', 'a', 'i', 'n', 0,
                                  't', 'e', 'x', 0};
  std::vector<uint8_t> indices(6 * sizeof(uint32_t));
  Store<uint32_t>(indices, 0, 3u);
  Store<uint32_t>(indices, 4, 8u);
  Store<uint32_t>(indices, 8, 4u);
  Store<uint32_t>(indices, 12, 2u);
  Store<uint32_t>(indices, 16, 1u);
  Store<uint32_t>(indices, 20, 0u);

  std::vector<uint8_t> resources(kRuntimeDataTableHeaderSize +
                                 kRdatResourceRecordSize);
  Store<uint32_t>(resources, 0, 1u);
  Store<uint32_t>(resources, 4, kRdatResourceRecordSize);
  Store<uint32_t>(resources, kRuntimeDataTableHeaderSize + 0, 0u);
  Store<uint32_t>(resources, kRuntimeDataTableHeaderSize + 4, 6u);
  Store<uint32_t>(resources, kRuntimeDataTableHeaderSize + 8, 3u);
  Store<uint32_t>(resources, kRuntimeDataTableHeaderSize + 12, 2u);
  Store<uint32_t>(resources, kRuntimeDataTableHeaderSize + 16, 4u);
  Store<uint32_t>(resources, kRuntimeDataTableHeaderSize + 20, 7u);
  Store<uint32_t>(resources, kRuntimeDataTableHeaderSize + 24, 5u);
  Store<uint32_t>(resources, kRuntimeDataTableHeaderSize + 28, 9u);

  std::vector<uint8_t> compute(kRuntimeDataTableHeaderSize +
                               kRdatCSInfoRecordSize);
  Store<uint32_t>(compute, 0, 1u);
  Store<uint32_t>(compute, 4, kRdatCSInfoRecordSize);
  Store<uint32_t>(compute, kRuntimeDataTableHeaderSize + 0, 0u);
  Store<uint32_t>(compute, kRuntimeDataTableHeaderSize + 4, 512u);

  std::vector<uint8_t> functions(kRuntimeDataTableHeaderSize +
                                 kRdatFunctionRecord2Size);
  Store<uint32_t>(functions, 0, 1u);
  Store<uint32_t>(functions, 4, kRdatFunctionRecord2Size);
  Store<uint32_t>(functions, kRuntimeDataTableHeaderSize + 0, 0u);
  Store<uint32_t>(functions, kRuntimeDataTableHeaderSize + 4, 0u);
  Store<uint32_t>(functions, kRuntimeDataTableHeaderSize + 8, 16u);
  Store<uint32_t>(functions, kRuntimeDataTableHeaderSize + 12,
                  kRdatNullRef);
  Store<uint32_t>(functions, kRuntimeDataTableHeaderSize + 16, 5u);
  Store<uint32_t>(functions, kRuntimeDataTableHeaderSize + 28, 0x11u);
  Store<uint32_t>(functions, kRuntimeDataTableHeaderSize + 32, 0x22u);
  Store<uint32_t>(functions, kRuntimeDataTableHeaderSize + 40, 0x60u);
  functions[kRuntimeDataTableHeaderSize + 44] = 16;
  functions[kRuntimeDataTableHeaderSize + 45] = 32;
  Store<uint16_t>(functions, kRuntimeDataTableHeaderSize + 46, 0x1234u);
  Store<uint32_t>(functions, kRuntimeDataTableHeaderSize + 48, 0u);

  const auto runtime_data =
      DxilRuntimeDataBuilder()
          .add(rdat::StringBuffer, strings)
          .add(rdat::IndexArrays, indices)
          .add(rdat::ResourceTable, resources)
          .add(rdat::CSInfoTable, compute)
          .add(rdat::FunctionTable, functions)
          .build();
  const auto runtime_container =
      DxilContainerBuilder()
          .add(fourcc::Dxil, program)
          .add(fourcc::RuntimeData, runtime_data)
          .add(fourcc::PipelineStateValidation, psv)
          .add(fourcc::RootSignature, root_signature)
          .build();

  Parser runtime_parser;
  ASSERT_EQ(runtime_parser.parse(runtime_container), ParseStatus::Ok);
  ASSERT_TRUE(runtime_parser.shaderReflection().has_value());
  const auto &runtime_reflection = *runtime_parser.shaderReflection();
  EXPECT_TRUE(runtime_reflection.has_runtime_data);
  EXPECT_TRUE(runtime_reflection.has_pipeline_state_validation);
  EXPECT_EQ(runtime_reflection.entry_point_name, "main");
  EXPECT_EQ(runtime_reflection.function_name, "main");
  EXPECT_EQ(runtime_reflection.num_threads_x, 8u);
  EXPECT_EQ(runtime_reflection.num_threads_y, 4u);
  EXPECT_EQ(runtime_reflection.num_threads_z, 2u);
  EXPECT_EQ(runtime_reflection.group_shared_bytes_used, 1024u);
  EXPECT_EQ(runtime_reflection.feature_flags, 0x0000002200000011ull);
  EXPECT_EQ(runtime_reflection.min_shader_target, 0x60u);
  EXPECT_EQ(runtime_reflection.shader_flags, 0x1234u);
  ASSERT_EQ(runtime_reflection.resources.size(), 1u);
  EXPECT_EQ(runtime_reflection.resources[0].name, "tex");
  EXPECT_TRUE(runtime_reflection.resources[0].from_runtime_data);
  EXPECT_FALSE(runtime_reflection.resources[0].from_psv);
  EXPECT_EQ(runtime_reflection.resources[0].bind_count, 4u);

  ASSERT_TRUE(runtime_parser.dxilValidation().has_value());
  EXPECT_TRUE(runtime_parser.dxilValidation()->valid);
  EXPECT_EQ(runtime_parser.dxilValidation()->error_count, 0u);
  ASSERT_TRUE(runtime_parser.dxilTranslation().has_value());
  const auto &runtime_translation = *runtime_parser.dxilTranslation();
  EXPECT_TRUE(runtime_translation.has_runtime_data);
  ASSERT_EQ(runtime_translation.resources.size(), 1u);
  EXPECT_EQ(runtime_translation.resources[0].name, "tex");
  EXPECT_EQ(runtime_translation.resources[0].source_mask,
            DxilTranslationSourceRuntimeData);
  EXPECT_EQ(runtime_translation.feature_flags, 0x0000002200000011ull);
  EXPECT_EQ(runtime_translation.shader_flags, 0x1234u);

  auto invalid_functions = functions;
  invalid_functions[kRuntimeDataTableHeaderSize + 44] = 64;
  invalid_functions[kRuntimeDataTableHeaderSize + 45] = 32;
  auto invalid_resources = resources;
  Store<uint32_t>(invalid_resources, kRuntimeDataTableHeaderSize + 16, 8u);
  Store<uint32_t>(invalid_resources, kRuntimeDataTableHeaderSize + 20, 7u);
  const auto invalid_runtime_data =
      DxilRuntimeDataBuilder()
          .add(rdat::StringBuffer, strings)
          .add(rdat::IndexArrays, indices)
          .add(rdat::ResourceTable, invalid_resources)
          .add(rdat::CSInfoTable, compute)
          .add(rdat::FunctionTable, invalid_functions)
          .build();
  auto invalid_psv = psv;
  Store<uint32_t>(invalid_psv, runtime_offset + 36, 0u);
  Store<uint32_t>(invalid_psv, resource_offset + 8, 8u);
  Store<uint32_t>(invalid_psv, resource_offset + 12, 7u);
  const auto invalid_container =
      DxilContainerBuilder()
          .add(fourcc::Dxil, program)
          .add(fourcc::RuntimeData, invalid_runtime_data)
          .add(fourcc::PipelineStateValidation, invalid_psv)
          .build();

  Parser invalid_parser;
  ASSERT_EQ(invalid_parser.parse(invalid_container), ParseStatus::Ok);
  ASSERT_TRUE(invalid_parser.dxilValidation().has_value());
  const auto &validation = *invalid_parser.dxilValidation();
  EXPECT_FALSE(validation.valid);
  EXPECT_EQ(validation.error_count, 4u);
  const auto has_error = [&](std::string_view code) {
    return std::any_of(
        validation.diagnostics.begin(), validation.diagnostics.end(),
        [&](const DxilValidationDiagnostic &diagnostic) {
          return diagnostic.severity == DxilValidationSeverity::Error &&
                 diagnostic.code == code;
        });
  };
  EXPECT_TRUE(has_error("invalid-wave-lane-range"));
  EXPECT_TRUE(has_error("invalid-rdat-resource-range"));
  EXPECT_TRUE(has_error("invalid-thread-group-size"));
  EXPECT_TRUE(has_error("invalid-psv-resource-range"));

  auto stage_psv = psv;
  stage_psv[runtime_offset + 24] = 1;
  const auto stage_container =
      DxilContainerBuilder()
          .add(fourcc::Dxil, program)
          .add(fourcc::RuntimeData, runtime_data)
          .add(fourcc::PipelineStateValidation, stage_psv)
          .build();
  Parser stage_parser;
  ASSERT_EQ(stage_parser.parse(stage_container), ParseStatus::Ok);
  ASSERT_TRUE(stage_parser.dxilValidation().has_value());
  const auto &stage_validation = *stage_parser.dxilValidation();
  EXPECT_FALSE(stage_validation.valid);
  EXPECT_EQ(stage_validation.error_count, 2u);
  const auto has_stage_error = [&](std::string_view code) {
    return std::any_of(
        stage_validation.diagnostics.begin(),
        stage_validation.diagnostics.end(),
        [&](const DxilValidationDiagnostic &diagnostic) {
          return diagnostic.severity == DxilValidationSeverity::Error &&
                 diagnostic.code == code;
        });
  };
  EXPECT_TRUE(has_stage_error("psv-stage-mismatch"));
  EXPECT_TRUE(has_stage_error("reflection-stage-mismatch"));
  ASSERT_TRUE(stage_parser.dxilTranslation().has_value());
  EXPECT_EQ(stage_parser.dxilTranslation()->shader_kind, 1u);
  EXPECT_EQ(stage_parser.dxilTranslation()->shader_stage_name, "Vertex");

  constexpr size_t rdef_resource_offset = kResourceDefHeaderSize;
  constexpr size_t rdef_name_offset =
      rdef_resource_offset + kResourceDefResourceBindingExtendedSize;
  std::vector<uint8_t> resource_def(rdef_name_offset + 5);
  Store<uint32_t>(resource_def, 8, 1u);
  Store<uint32_t>(resource_def, 12, rdef_resource_offset);
  Store<uint32_t>(resource_def, 16, 0x51u);
  Store<uint32_t>(resource_def, rdef_resource_offset + 0,
                  rdef_name_offset);
  Store<uint32_t>(resource_def, rdef_resource_offset + 4, 3u);
  Store<uint32_t>(resource_def, rdef_resource_offset + 8, 1u);
  Store<uint32_t>(resource_def, rdef_resource_offset + 12, 6u);
  Store<uint32_t>(resource_def, rdef_resource_offset + 16, 1u);
  Store<uint32_t>(resource_def, rdef_resource_offset + 20, 4u);
  Store<uint32_t>(resource_def, rdef_resource_offset + 24, 4u);
  Store<uint32_t>(resource_def, rdef_resource_offset + 28, 9u);
  Store<uint32_t>(resource_def, rdef_resource_offset + 32, 2u);
  Store<uint32_t>(resource_def, rdef_resource_offset + 36, 3u);
  std::memcpy(resource_def.data() + rdef_name_offset, "rdef", 5);
  const auto rdef_container =
      DxilContainerBuilder()
          .add(fourcc::Dxil, program)
          .add(fourcc::ResourceDef, resource_def)
          .add(fourcc::PipelineStateValidation, psv)
          .build();

  Parser rdef_parser;
  ASSERT_EQ(rdef_parser.parse(rdef_container), ParseStatus::Ok);
  ASSERT_TRUE(rdef_parser.shaderReflection().has_value());
  const auto &rdef_reflection = *rdef_parser.shaderReflection();
  EXPECT_TRUE(rdef_reflection.has_resource_def);
  EXPECT_FALSE(rdef_reflection.has_runtime_data);
  ASSERT_EQ(rdef_reflection.resources.size(), 1u);
  EXPECT_EQ(rdef_reflection.resources[0].name, "rdef");
  EXPECT_TRUE(rdef_reflection.resources[0].from_resource_def);
  EXPECT_FALSE(rdef_reflection.resources[0].from_psv);
  EXPECT_EQ(rdef_reflection.resources[0].id, 3u);
  EXPECT_EQ(rdef_reflection.resources[0].space, 2u);
  EXPECT_EQ(rdef_reflection.resources[0].lower_bound, 4u);
  EXPECT_EQ(rdef_reflection.resources[0].upper_bound, 7u);
  EXPECT_EQ(rdef_reflection.resources[0].bind_count, 4u);
  ASSERT_TRUE(rdef_parser.dxilValidation().has_value());
  EXPECT_TRUE(rdef_parser.dxilValidation()->valid);
  ASSERT_TRUE(rdef_parser.dxilTranslation().has_value());
  ASSERT_EQ(rdef_parser.dxilTranslation()->resources.size(), 1u);
  EXPECT_EQ(rdef_parser.dxilTranslation()->resources[0].source_mask,
            DxilTranslationSourceResourceDef);

  const auto make_legacy_signature = [](std::string_view semantic,
                                        uint32_t register_index) {
    constexpr size_t element_offset = kDxilSignatureHeaderSize;
    constexpr size_t name_offset =
        element_offset + kDxilSignatureElementSize;
    std::vector<uint8_t> signature(name_offset + semantic.size() + 1);
    Store<uint32_t>(signature, 0, 1u);
    Store<uint32_t>(signature, 4, element_offset);
    Store<uint32_t>(signature, element_offset + 4, name_offset);
    Store<uint32_t>(signature, element_offset + 8, register_index);
    Store<uint32_t>(signature, element_offset + 16, 3u);
    Store<uint32_t>(signature, element_offset + 20, register_index);
    signature[element_offset + 24] = 0xf;
    std::copy(semantic.begin(), semantic.end(),
              signature.begin() + name_offset);
    return signature;
  };
  const auto input_signature = make_legacy_signature("INPUT", 0);
  const auto output_signature = make_legacy_signature("OUTPUT", 1);
  const auto patch_signature = make_legacy_signature("PATCH", 2);
  const auto legacy_container =
      DxilContainerBuilder()
          .add(fourcc::Dxil, program)
          .add(fourcc::InputSignature, input_signature)
          .add(fourcc::OutputSignature, output_signature)
          .add(fourcc::PatchConstantSignature, patch_signature)
          .build();

  Parser legacy_parser;
  ASSERT_EQ(legacy_parser.parse(legacy_container), ParseStatus::Ok);
  ASSERT_TRUE(legacy_parser.shaderReflection().has_value());
  EXPECT_EQ(legacy_parser.shaderReflection()->legacy_signatures.size(), 3u);
  ASSERT_TRUE(legacy_parser.dxilTranslation().has_value());
  const auto &legacy_translation = *legacy_parser.dxilTranslation();
  ASSERT_EQ(legacy_translation.signatures.size(), 3u);
  EXPECT_EQ(legacy_translation.signatures[0].kind,
            DxilTranslationSignatureKind::Input);
  EXPECT_EQ(legacy_translation.signatures[0].semantic_name, "INPUT");
  EXPECT_EQ(legacy_translation.signatures[0].source_mask,
            DxilTranslationSourceLegacySignature);
  EXPECT_EQ(legacy_translation.signatures[1].kind,
            DxilTranslationSignatureKind::Output);
  EXPECT_EQ(legacy_translation.signatures[1].semantic_name, "OUTPUT");
  EXPECT_EQ(legacy_translation.signatures[1].source_mask,
            DxilTranslationSourceLegacySignature);
  EXPECT_EQ(legacy_translation.signatures[2].kind,
            DxilTranslationSignatureKind::PatchConstant);
  EXPECT_EQ(legacy_translation.signatures[2].semantic_name, "PATCH");
  EXPECT_EQ(legacy_translation.signatures[2].source_mask,
            DxilTranslationSourceLegacySignature);
}

TEST(DxilLlvmModule, ParsesTypesControlFlowCallsAndGlobals) {
  using namespace dxmt::dxil;
  LlvmModuleInfo module;
  EXPECT_EQ(ParseLlvmModule({}, module), ParseStatus::InvalidLlvmModule);
  EXPECT_EQ(ParseLlvmModule(std::array<uint8_t, 4>{1, 2, 3, 4}, module),
            ParseStatus::InvalidLlvmModule);

  const auto bitcode =
      DecodeHex(dxmt::test::kDxilLlvmCoverageBitcodeHex);
  ASSERT_EQ(bitcode.size(), 3036u);
  ASSERT_EQ(ParseLlvmModule(bitcode, module), ParseStatus::Ok);
  EXPECT_EQ(module.source_file_name, "dxil-llvm-coverage.ll");
  EXPECT_EQ(module.target_triple, "dxil-ms-dx");
  EXPECT_EQ(module.data_layout, "e-p:32:32-i64:64-n8:16:32:64");
  EXPECT_TRUE(module.hasNamedMetadata("custom.metadata"));
  ASSERT_TRUE(module.shader_model.has_value());
  EXPECT_EQ(module.shader_model->kind, "cs");
  EXPECT_EQ(module.shader_model->major, 6u);
  EXPECT_EQ(module.shader_model->minor, 7u);
  EXPECT_EQ(module.dxil_version, (std::vector<uint32_t>{1u, 8u}));
  EXPECT_EQ(module.validator_version, (std::vector<uint32_t>{1u, 9u}));
  ASSERT_EQ(module.entry_points.size(), 1u);
  EXPECT_EQ(module.entry_points[0].function_name, "entry");
  EXPECT_EQ(module.entry_points[0].name, "main");
  ASSERT_EQ(module.module_flags.size(), 1u);
  EXPECT_EQ(module.module_flags[0].behavior, 2u);
  EXPECT_EQ(module.module_flags[0].key, "coverage-flag");
  EXPECT_EQ(module.module_flags[0].value, "i32 7");

  const auto find_global = [&](std::string_view name) {
    const auto global = std::find_if(
        module.globals.begin(), module.globals.end(),
        [name](const LlvmGlobalInfo &candidate) {
          return candidate.name == name;
        });
    return global == module.globals.end() ? nullptr : &*global;
  };
  const auto expect_global_kind = [&](std::string_view name,
                                      LlvmTypeKind kind) {
    const auto *global = find_global(name);
    ASSERT_NE(global, nullptr) << name;
    EXPECT_EQ(global->value_type_info.kind, kind) << name;
  };
  expect_global_kind("named_value", LlvmTypeKind::Struct);
  expect_global_kind("opaque_value", LlvmTypeKind::Struct);
  expect_global_kind("array_value", LlvmTypeKind::Array);
  expect_global_kind("vector_value", LlvmTypeKind::Vector);
  expect_global_kind("half_value", LlvmTypeKind::Half);
  expect_global_kind("bfloat_value", LlvmTypeKind::BFloat);
  expect_global_kind("double_value", LlvmTypeKind::Double);
  expect_global_kind("x86_fp80_value", LlvmTypeKind::X86Fp80);
  expect_global_kind("fp128_value", LlvmTypeKind::Fp128);
  expect_global_kind("ppc_fp128_value", LlvmTypeKind::PpcFp128);
  expect_global_kind("x86_mmx_value", LlvmTypeKind::X86Mmx);

  const auto *named = find_global("named_value");
  ASSERT_NE(named, nullptr);
  EXPECT_EQ(named->value_type_info.name, "named");
  EXPECT_FALSE(named->value_type_info.is_opaque);
  ASSERT_EQ(named->value_type_info.contained_types.size(), 2u);
  EXPECT_EQ(named->value_type_info.contained_types[0].kind,
            LlvmTypeKind::Integer);
  EXPECT_EQ(named->value_type_info.contained_types[0].bit_width, 32u);
  EXPECT_EQ(named->value_type_info.contained_types[1].kind,
            LlvmTypeKind::Array);
  EXPECT_EQ(named->value_type_info.contained_types[1].element_count, 2u);
  ASSERT_EQ(named->value_type_info.contained_types[1].contained_types.size(),
            1u);
  EXPECT_EQ(named->value_type_info.contained_types[1].contained_types[0].kind,
            LlvmTypeKind::Float);

  const auto *opaque = find_global("opaque_value");
  ASSERT_NE(opaque, nullptr);
  EXPECT_EQ(opaque->value_type_info.name, "opaque");
  EXPECT_TRUE(opaque->value_type_info.is_opaque);
  EXPECT_TRUE(opaque->is_declaration);
  const auto *array = find_global("array_value");
  ASSERT_NE(array, nullptr);
  EXPECT_TRUE(array->is_constant);
  EXPECT_EQ(array->value_type_info.element_count, 3u);
  const auto *vector = find_global("vector_value");
  ASSERT_NE(vector, nullptr);
  EXPECT_EQ(vector->value_type_info.element_count, 4u);
  EXPECT_FALSE(vector->value_type_info.is_scalable);

  const auto find_function = [&](std::string_view name) {
    const auto function = std::find_if(
        module.functions.begin(), module.functions.end(),
        [name](const LlvmFunctionInfo &candidate) {
          return candidate.name == name;
        });
    return function == module.functions.end() ? nullptr : &*function;
  };
  const auto *scalable = find_function("scalable_vector_user");
  ASSERT_NE(scalable, nullptr);
  ASSERT_EQ(scalable->argument_type_infos.size(), 1u);
  EXPECT_EQ(scalable->argument_type_infos[0].kind, LlvmTypeKind::Vector);
  EXPECT_EQ(scalable->argument_type_infos[0].element_count, 2u);
  EXPECT_TRUE(scalable->argument_type_infos[0].is_scalable);

  const auto *entry = find_function("entry");
  ASSERT_NE(entry, nullptr);
  EXPECT_FALSE(entry->is_declaration);
  EXPECT_TRUE(entry->is_entry_reachable);
  EXPECT_TRUE(entry->has_indirect_calls);
  ASSERT_EQ(entry->argument_type_infos.size(), 2u);
  EXPECT_EQ(entry->argument_type_infos[0].kind, LlvmTypeKind::Integer);
  EXPECT_EQ(entry->argument_type_infos[1].kind, LlvmTypeKind::Pointer);
  EXPECT_EQ(entry->argument_type_infos[1].address_space, 0u);
  EXPECT_TRUE(entry->argument_type_infos[1].is_opaque);
  EXPECT_EQ(entry->called_functions,
            (std::vector<std::string>{"helper", "recursive"}));
  ASSERT_EQ(entry->basic_blocks.size(), 5u);

  const auto find_block = [&](std::string_view name) {
    const auto block = std::find_if(
        entry->basic_blocks.begin(), entry->basic_blocks.end(),
        [name](const LlvmBasicBlockInfo &candidate) {
          return candidate.name == name;
        });
    return block == entry->basic_blocks.end() ? nullptr : &*block;
  };
  const auto *entry_block = find_block("entry");
  const auto *left_block = find_block("left");
  const auto *dead_block = find_block("dead");
  const auto *right_block = find_block("right");
  const auto *join_block = find_block("join");
  ASSERT_NE(entry_block, nullptr);
  ASSERT_NE(left_block, nullptr);
  ASSERT_NE(dead_block, nullptr);
  ASSERT_NE(right_block, nullptr);
  ASSERT_NE(join_block, nullptr);
  EXPECT_TRUE(entry_block->has_branch);
  EXPECT_EQ(entry_block->successors,
            (std::vector<std::string>{"left", "right"}));
  EXPECT_TRUE(left_block->has_switch);
  EXPECT_EQ(left_block->successors,
            (std::vector<std::string>{"join", "dead"}));
  EXPECT_TRUE(dead_block->has_unreachable);
  EXPECT_TRUE(dead_block->successors.empty());
  EXPECT_TRUE(right_block->has_branch);
  EXPECT_EQ(right_block->successors,
            (std::vector<std::string>{"join"}));
  EXPECT_TRUE(join_block->has_return);

  ASSERT_EQ(entry->dxil_operations.size(), 2u);
  const auto thread_id = std::find_if(
      entry->dxil_operations.begin(), entry->dxil_operations.end(),
      [](const LlvmDxilOperationInfo &operation) {
        return operation.opcode == 93u;
      });
  ASSERT_NE(thread_id, entry->dxil_operations.end());
  EXPECT_TRUE(thread_id->opcode_known);
  EXPECT_EQ(thread_id->opcode_name, "ThreadId");
  EXPECT_EQ(thread_id->typed.kind, DxilTypedOperationKind::SystemValue);
  EXPECT_EQ(thread_id->typed.system_value, DxilSystemValueKind::ThreadId);
  EXPECT_TRUE(thread_id->typed.has_component_index);
  EXPECT_EQ(thread_id->typed.component_index, 2u);

  const auto unknown = std::find_if(
      entry->dxil_operations.begin(), entry->dxil_operations.end(),
      [](const LlvmDxilOperationInfo &operation) {
        return operation.opcode == 999u;
      });
  ASSERT_NE(unknown, entry->dxil_operations.end());
  EXPECT_FALSE(unknown->opcode_known);
  EXPECT_EQ(unknown->opcode_name, "unknown");
  EXPECT_EQ(unknown->semantic_kind, DxilSemanticOperationKind::Unknown);
  EXPECT_EQ(unknown->typed.kind, DxilTypedOperationKind::Unknown);

  const auto *recursive = find_function("recursive");
  ASSERT_NE(recursive, nullptr);
  EXPECT_TRUE(recursive->is_recursive);
  EXPECT_TRUE(recursive->is_entry_reachable);
  EXPECT_EQ(recursive->called_functions,
            (std::vector<std::string>{"recursive"}));
  EXPECT_TRUE(module.call_graph.has_indirect_calls);
  EXPECT_TRUE(module.call_graph.has_recursion);
  EXPECT_NE(std::find(module.call_graph.entry_reachable_functions.begin(),
                      module.call_graph.entry_reachable_functions.end(),
                      "helper"),
            module.call_graph.entry_reachable_functions.end());
  EXPECT_EQ(module.call_graph.recursive_functions,
            (std::vector<std::string>{"recursive"}));
  EXPECT_EQ(module.call_graph.unused_dx_intrinsic_declarations,
            (std::vector<std::string>{
                "dx.op.flattenedThreadIdInGroup.i32"}));
}

TEST(DxilLlvmModule, ParsesEveryTypedOperationKindAndSystemValue) {
  using namespace dxmt::dxil;
  const auto bitcode =
      DecodeHex(dxmt::test::kDxilTypedOperationsBitcodeHex);
  ASSERT_EQ(bitcode.size(), 4804u);
  LlvmModuleInfo module;
  ASSERT_EQ(ParseLlvmModule(bitcode, module), ParseStatus::Ok);

  const auto function = std::find_if(
      module.functions.begin(), module.functions.end(),
      [](const LlvmFunctionInfo &candidate) { return candidate.name == "ops"; });
  ASSERT_NE(function, module.functions.end());
  ASSERT_EQ(function->dxil_operations.size(), 52u);
  const auto find_operation = [&](uint32_t opcode)
      -> const LlvmDxilOperationInfo * {
    const auto operation = std::find_if(
        function->dxil_operations.begin(), function->dxil_operations.end(),
        [opcode](const LlvmDxilOperationInfo &candidate) {
          return candidate.opcode == opcode;
        });
    return operation == function->dxil_operations.end() ? nullptr
                                                         : &*operation;
  };
  const auto expect_kind = [&](uint32_t opcode, DxilTypedOperationKind kind) {
    const auto *operation = find_operation(opcode);
    ASSERT_NE(operation, nullptr) << opcode;
    EXPECT_TRUE(operation->opcode_known) << opcode;
    EXPECT_EQ(operation->typed.kind, kind) << opcode;
  };

  expect_kind(57u, DxilTypedOperationKind::CreateHandle);
  expect_kind(217u, DxilTypedOperationKind::CreateHandleFromBinding);
  expect_kind(218u, DxilTypedOperationKind::CreateHandleFromHeap);
  expect_kind(216u, DxilTypedOperationKind::AnnotateHandle);
  expect_kind(58u, DxilTypedOperationKind::CBufferLoad);
  expect_kind(59u, DxilTypedOperationKind::CBufferLoad);
  expect_kind(66u, DxilTypedOperationKind::TextureLoad);
  expect_kind(67u, DxilTypedOperationKind::TextureStore);
  expect_kind(68u, DxilTypedOperationKind::BufferLoad);
  expect_kind(69u, DxilTypedOperationKind::BufferStore);
  expect_kind(139u, DxilTypedOperationKind::RawBufferLoad);
  expect_kind(140u, DxilTypedOperationKind::RawBufferStore);
  expect_kind(60u, DxilTypedOperationKind::Sample);
  expect_kind(73u, DxilTypedOperationKind::Gather);
  expect_kind(78u, DxilTypedOperationKind::Atomic);
  expect_kind(79u, DxilTypedOperationKind::Atomic);
  expect_kind(70u, DxilTypedOperationKind::BufferStore);
  expect_kind(4u, DxilTypedOperationKind::LoadInput);
  expect_kind(103u, DxilTypedOperationKind::LoadInput);
  expect_kind(104u, DxilTypedOperationKind::LoadInput);
  expect_kind(5u, DxilTypedOperationKind::StoreOutput);
  expect_kind(106u, DxilTypedOperationKind::StoreOutput);
  expect_kind(171u, DxilTypedOperationKind::StoreOutput);
  expect_kind(172u, DxilTypedOperationKind::StoreOutput);

  const auto *create = find_operation(57u);
  ASSERT_NE(create, nullptr);
  EXPECT_TRUE(create->typed.has_resource_class);
  EXPECT_EQ(create->typed.resource_class, 2u);
  EXPECT_TRUE(create->typed.has_resource_range_id);
  EXPECT_EQ(create->typed.resource_range_id, 3u);
  EXPECT_TRUE(create->typed.has_resource_index);
  EXPECT_EQ(create->typed.resource_index, 4u);
  EXPECT_TRUE(create->typed.has_non_uniform);
  EXPECT_TRUE(create->typed.non_uniform);
  ASSERT_EQ(create->typed.operands.size(), 4u);
  EXPECT_EQ(create->typed.operands[0].name, "resource_class");
  EXPECT_EQ(create->typed.operands[3].name, "non_uniform_index");

  const auto *binding = find_operation(217u);
  ASSERT_NE(binding, nullptr);
  EXPECT_TRUE(binding->typed.has_resource_binding);
  EXPECT_EQ(binding->typed.resource_lower_bound, 4u);
  EXPECT_EQ(binding->typed.resource_space, 2u);
  EXPECT_TRUE(binding->typed.has_resource_class);
  EXPECT_EQ(binding->typed.resource_class, 1u);
  EXPECT_TRUE(binding->typed.has_resource_index);
  EXPECT_EQ(binding->typed.resource_index, 9u);
  EXPECT_TRUE(binding->typed.non_uniform);
  ASSERT_FALSE(binding->typed.operands.empty());
  EXPECT_EQ(binding->typed.operands[0].aggregate_integer_values,
            (std::vector<uint64_t>{4u, 7u, 2u, 1u}));

  const auto *heap = find_operation(218u);
  ASSERT_NE(heap, nullptr);
  EXPECT_EQ(heap->typed.resource_class, 1u);
  EXPECT_EQ(heap->typed.resource_index, 5u);
  EXPECT_TRUE(heap->typed.has_non_uniform);
  EXPECT_FALSE(heap->typed.non_uniform);

  const auto *cbuffer = find_operation(58u);
  ASSERT_NE(cbuffer, nullptr);
  EXPECT_TRUE(cbuffer->typed.is_read);
  EXPECT_EQ(cbuffer->typed.resource_index, 11u);
  const auto *texture_load = find_operation(66u);
  ASSERT_NE(texture_load, nullptr);
  EXPECT_TRUE(texture_load->typed.is_read);
  EXPECT_EQ(texture_load->typed.operands.size(), 8u);
  const auto *texture_store = find_operation(67u);
  ASSERT_NE(texture_store, nullptr);
  EXPECT_TRUE(texture_store->typed.is_write);
  EXPECT_TRUE(texture_store->typed.has_mask);
  EXPECT_EQ(texture_store->typed.mask, 15u);

  const auto *buffer_load = find_operation(68u);
  ASSERT_NE(buffer_load, nullptr);
  EXPECT_TRUE(buffer_load->typed.is_read);
  EXPECT_EQ(buffer_load->typed.resource_index, 21u);
  const auto *buffer_store = find_operation(69u);
  ASSERT_NE(buffer_store, nullptr);
  EXPECT_TRUE(buffer_store->typed.is_write);
  EXPECT_EQ(buffer_store->typed.resource_index, 22u);
  EXPECT_EQ(buffer_store->typed.mask, 13u);

  const auto *raw_load = find_operation(139u);
  ASSERT_NE(raw_load, nullptr);
  EXPECT_EQ(raw_load->typed.resource_index, 23u);
  EXPECT_EQ(raw_load->typed.mask, 7u);
  EXPECT_EQ(raw_load->typed.alignment, 16u);
  const auto *raw_store = find_operation(140u);
  ASSERT_NE(raw_store, nullptr);
  EXPECT_EQ(raw_store->typed.resource_index, 24u);
  EXPECT_EQ(raw_store->typed.mask, 11u);
  EXPECT_EQ(raw_store->typed.alignment, 32u);

  const auto *sample = find_operation(60u);
  ASSERT_NE(sample, nullptr);
  EXPECT_TRUE(sample->typed.is_read);
  EXPECT_TRUE(sample->typed.is_sample);
  ASSERT_EQ(sample->typed.operands.size(), 4u);
  EXPECT_EQ(sample->typed.operands[2].name, "sample_operand0");
  const auto *gather = find_operation(73u);
  ASSERT_NE(gather, nullptr);
  EXPECT_TRUE(gather->typed.is_read);
  EXPECT_TRUE(gather->typed.is_gather);
  ASSERT_EQ(gather->typed.operands.size(), 4u);
  EXPECT_EQ(gather->typed.operands[2].name, "gather_operand0");

  const auto *atomic = find_operation(78u);
  ASSERT_NE(atomic, nullptr);
  EXPECT_TRUE(atomic->typed.is_write);
  EXPECT_TRUE(atomic->typed.is_atomic);
  EXPECT_TRUE(atomic->typed.has_atomic_operation);
  EXPECT_EQ(atomic->typed.atomic_operation, 6u);
  const auto *compare_exchange = find_operation(79u);
  ASSERT_NE(compare_exchange, nullptr);
  EXPECT_TRUE(compare_exchange->typed.is_atomic);
  EXPECT_FALSE(compare_exchange->typed.has_atomic_operation);
  const auto *counter = find_operation(70u);
  ASSERT_NE(counter, nullptr);
  EXPECT_TRUE(counter->typed.is_read);
  EXPECT_TRUE(counter->typed.is_write);

  for (uint32_t opcode : {4u, 103u, 104u}) {
    const auto *load = find_operation(opcode);
    ASSERT_NE(load, nullptr);
    EXPECT_TRUE(load->typed.is_read);
    EXPECT_TRUE(load->typed.has_signature_element_id);
    EXPECT_TRUE(load->typed.has_row_index);
    EXPECT_TRUE(load->typed.has_column_index);
  }
  EXPECT_EQ(find_operation(4u)->typed.signature_element_id, 31u);
  EXPECT_EQ(find_operation(4u)->typed.row_index, 2u);
  EXPECT_EQ(find_operation(4u)->typed.column_index, 3u);
  for (uint32_t opcode : {5u, 106u, 171u, 172u}) {
    const auto *store = find_operation(opcode);
    ASSERT_NE(store, nullptr);
    EXPECT_TRUE(store->typed.is_write);
    EXPECT_TRUE(store->typed.has_signature_element_id);
    EXPECT_TRUE(store->typed.has_row_index);
    EXPECT_TRUE(store->typed.has_column_index);
  }
  EXPECT_EQ(find_operation(5u)->typed.signature_element_id, 41u);
  EXPECT_EQ(find_operation(5u)->typed.row_index, 5u);
  EXPECT_EQ(find_operation(5u)->typed.column_index, 2u);

  constexpr std::array system_values = {
      std::pair{90u, DxilSystemValueKind::SampleIndex},
      std::pair{91u, DxilSystemValueKind::Coverage},
      std::pair{92u, DxilSystemValueKind::InnerCoverage},
      std::pair{93u, DxilSystemValueKind::ThreadId},
      std::pair{94u, DxilSystemValueKind::GroupId},
      std::pair{95u, DxilSystemValueKind::ThreadIdInGroup},
      std::pair{96u, DxilSystemValueKind::FlattenedThreadIdInGroup},
      std::pair{100u, DxilSystemValueKind::GSInstanceID},
      std::pair{105u, DxilSystemValueKind::DomainLocation},
      std::pair{107u, DxilSystemValueKind::OutputControlPointID},
      std::pair{108u, DxilSystemValueKind::PrimitiveID},
      std::pair{138u, DxilSystemValueKind::ViewID},
      std::pair{141u, DxilSystemValueKind::InstanceID},
      std::pair{142u, DxilSystemValueKind::InstanceIndex},
      std::pair{143u, DxilSystemValueKind::HitKind},
      std::pair{144u, DxilSystemValueKind::RayFlags},
      std::pair{145u, DxilSystemValueKind::DispatchRaysIndex},
      std::pair{146u, DxilSystemValueKind::DispatchRaysDimensions},
      std::pair{161u, DxilSystemValueKind::PrimitiveIndex},
      std::pair{213u, DxilSystemValueKind::GeometryIndex},
  };
  for (const auto &[opcode, system_value] : system_values) {
    SCOPED_TRACE(opcode);
    const auto *operation = find_operation(opcode);
    ASSERT_NE(operation, nullptr);
    EXPECT_EQ(operation->typed.kind, DxilTypedOperationKind::SystemValue);
    EXPECT_EQ(operation->typed.system_value, system_value);
    EXPECT_TRUE(operation->typed.has_component_index);
  }

  constexpr std::array semantic_kinds = {
      std::pair{4u, DxilSemanticOperationKind::SignatureInput},
      std::pair{5u, DxilSemanticOperationKind::SignatureOutput},
      std::pair{57u, DxilSemanticOperationKind::ResourceHandle},
      std::pair{60u, DxilSemanticOperationKind::ResourceSample},
      std::pair{67u, DxilSemanticOperationKind::ResourceWrite},
      std::pair{58u, DxilSemanticOperationKind::ResourceRead},
      std::pair{72u, DxilSemanticOperationKind::ResourceQuery},
      std::pair{80u, DxilSemanticOperationKind::Barrier},
      std::pair{110u, DxilSemanticOperationKind::Wave},
      std::pair{83u, DxilSemanticOperationKind::Derivative},
      std::pair{157u, DxilSemanticOperationKind::Raytracing},
      std::pair{168u, DxilSemanticOperationKind::Mesh},
      std::pair{238u, DxilSemanticOperationKind::Node},
      std::pair{6u, DxilSemanticOperationKind::Math},
  };
  for (const auto &[opcode, semantic_kind] : semantic_kinds) {
    SCOPED_TRACE(opcode);
    const auto *operation = find_operation(opcode);
    ASSERT_NE(operation, nullptr);
    EXPECT_EQ(operation->semantic_kind, semantic_kind);
  }
}

TEST(DxilLlvmModule, ParsesMetadataAndPropagatesTranslationResourceUses) {
  using namespace dxmt::dxil;
  const auto bitcode =
      DecodeHex(dxmt::test::kDxilMetadataTranslationBitcodeHex);
  ASSERT_EQ(bitcode.size(), 2832u);

  LlvmModuleInfo module;
  ASSERT_EQ(ParseLlvmModule(bitcode, module), ParseStatus::Ok);
  ASSERT_EQ(module.resources.size(), 5u);
  const auto find_module_resource = [&](uint32_t id) {
    const auto resource = std::find_if(
        module.resources.begin(), module.resources.end(),
        [id](const DxilMetadataResourceInfo &candidate) {
          return candidate.id == id;
        });
    return resource == module.resources.end() ? nullptr : &*resource;
  };

  const auto *srv_metadata = find_module_resource(3u);
  ASSERT_NE(srv_metadata, nullptr);
  EXPECT_EQ(srv_metadata->resource_class, DxilMetadataResourceClass::Srv);
  EXPECT_EQ(srv_metadata->global_name, "srv");
  EXPECT_EQ(srv_metadata->name, "srvTex");
  EXPECT_EQ(srv_metadata->space, 1u);
  EXPECT_EQ(srv_metadata->lower_bound, 2u);
  EXPECT_EQ(srv_metadata->range_size, 4u);
  EXPECT_EQ(srv_metadata->upper_bound, 5u);
  EXPECT_EQ(srv_metadata->kind, 2u);
  EXPECT_EQ(srv_metadata->element_type, 7u);
  EXPECT_EQ(srv_metadata->flags, 9u);
  EXPECT_EQ(srv_metadata->numeric_operands,
            (std::vector<uint32_t>{2u, 7u, 9u}));
  ASSERT_EQ(srv_metadata->tags.size(), 2u);
  EXPECT_EQ(srv_metadata->tags[0].tag, 1u);
  EXPECT_TRUE(srv_metadata->tags[0].has_uint_value);
  EXPECT_EQ(srv_metadata->tags[0].uint_value, 64u);
  EXPECT_EQ(srv_metadata->tags[1].tag, 7u);
  EXPECT_EQ(srv_metadata->tags[1].string_value, "coherent");

  const auto *uav_metadata = find_module_resource(4u);
  ASSERT_NE(uav_metadata, nullptr);
  EXPECT_EQ(uav_metadata->resource_class, DxilMetadataResourceClass::Uav);
  EXPECT_EQ(uav_metadata->upper_bound,
            std::numeric_limits<uint32_t>::max());
  ASSERT_EQ(uav_metadata->tags.size(), 1u);
  EXPECT_EQ(uav_metadata->tags[0].uint_value, 4294967297ull);
  EXPECT_EQ(find_module_resource(5u)->resource_class,
            DxilMetadataResourceClass::Cbv);
  EXPECT_EQ(find_module_resource(6u)->resource_class,
            DxilMetadataResourceClass::Sampler);
  EXPECT_EQ(find_module_resource(7u)->resource_class,
            DxilMetadataResourceClass::Unknown);

  ASSERT_EQ(module.entry_points.size(), 1u);
  const auto &entry = module.entry_points[0];
  EXPECT_TRUE(entry.has_signature);
  EXPECT_EQ(entry.signature_operand_count, 3u);
  EXPECT_TRUE(entry.has_resources);
  EXPECT_EQ(entry.resource_operand_count, 5u);
  EXPECT_TRUE(entry.has_properties);
  EXPECT_EQ(entry.property_operand_count, 2u);
  EXPECT_TRUE(entry.properties.empty());
  ASSERT_EQ(entry.property_tags.size(), 2u);
  EXPECT_EQ(entry.property_tags[0].tag, 4u);
  EXPECT_EQ(entry.property_tags[0].uint_value, 8u);
  EXPECT_EQ(entry.property_tags[1].tag, 9u);
  EXPECT_EQ(entry.property_tags[1].string_value, "property");

  ASSERT_EQ(entry.input_signature.size(), 1u);
  const auto &input = entry.input_signature[0];
  EXPECT_EQ(input.id, 10u);
  EXPECT_EQ(input.semantic_name, "POSITION");
  EXPECT_EQ(input.semantic_indices, (std::vector<uint32_t>{0u, 1u}));
  EXPECT_EQ(input.rows, 1u);
  EXPECT_EQ(input.cols, 4u);
  EXPECT_EQ(input.component_type, 3u);
  EXPECT_EQ(input.semantic_kind, 0u);
  EXPECT_EQ(input.interpolation_mode, 2u);
  ASSERT_EQ(input.tags.size(), 1u);
  EXPECT_EQ(input.tags[0].tag, 11u);

  ASSERT_EQ(entry.output_signature.size(), 1u);
  const auto &output = entry.output_signature[0];
  EXPECT_EQ(output.id, 20u);
  EXPECT_EQ(output.semantic_name, "SV_Target");
  EXPECT_EQ(output.semantic_indices, (std::vector<uint32_t>{0u}));
  EXPECT_EQ(output.rows, 1u);
  EXPECT_EQ(output.cols, 4u);
  EXPECT_EQ(output.start_row, 1u);
  EXPECT_EQ(output.start_col, 0u);
  EXPECT_EQ(output.semantic_kind, 9u);
  EXPECT_EQ(output.component_type, 3u);
  EXPECT_EQ(output.interpolation_mode, 4u);
  EXPECT_EQ(output.dynamic_index_mask, 5u);
  EXPECT_EQ(output.stream, 2u);
  ASSERT_EQ(output.tags.size(), 1u);
  EXPECT_EQ(output.tags[0].string_value, "output-tag");

  ASSERT_EQ(entry.patch_constant_signature.size(), 1u);
  const auto &patch = entry.patch_constant_signature[0];
  EXPECT_EQ(patch.id, 30u);
  EXPECT_EQ(patch.semantic_indices, (std::vector<uint32_t>{1u}));
  EXPECT_EQ(patch.rows, 2u);
  EXPECT_EQ(patch.cols, 2u);
  EXPECT_EQ(patch.start_col, 2u);
  EXPECT_EQ(module.signature_elements.size(), 3u);

  std::vector<uint8_t> program(kDxilProgramHeaderSize + bitcode.size());
  Store<uint32_t>(program, 0, (5u << 16) | (6u << 4) | 8u);
  Store<uint32_t>(program, 4, program.size() / sizeof(uint32_t));
  Store<uint32_t>(program, 8, kDxilMagicValue);
  Store<uint32_t>(program, 12, 0x00010008u);
  Store<uint32_t>(program, 16,
                  kDxilProgramHeaderSize - kDxilBitcodeHeaderOffset);
  Store<uint32_t>(program, 20, bitcode.size());
  std::copy(bitcode.begin(), bitcode.end(),
            program.begin() + kDxilProgramHeaderSize);
  const auto container =
      DxilContainerBuilder().add(fourcc::Dxil, program).build();

  Parser parser;
  ASSERT_EQ(parser.parse(container), ParseStatus::Ok);
  ASSERT_TRUE(parser.shaderReflection().has_value());
  const auto &reflection = *parser.shaderReflection();
  EXPECT_TRUE(reflection.valid);
  EXPECT_EQ(reflection.entry_point_name, "main");
  EXPECT_EQ(reflection.function_name, "main");
  EXPECT_EQ(reflection.shader_model_kind, "cs");
  ASSERT_EQ(reflection.resources.size(), 5u);
  EXPECT_TRUE(std::all_of(
      reflection.resources.begin(), reflection.resources.end(),
      [](const ShaderReflectionResourceInfo &resource) {
        return resource.from_metadata && !resource.from_runtime_data &&
               !resource.from_resource_def && !resource.from_psv;
      }));
  EXPECT_EQ(reflection.resources[0].element_stride, 64u);
  EXPECT_EQ(reflection.resources[1].element_stride,
            std::numeric_limits<uint32_t>::max());
  ASSERT_EQ(reflection.input_signature.size(), 1u);
  ASSERT_EQ(reflection.output_signature.size(), 1u);
  ASSERT_EQ(reflection.patch_constant_signature.size(), 1u);
  EXPECT_EQ(reflection.input_signature[0].semantic_name, "POSITION");
  EXPECT_EQ(reflection.output_signature[0].semantic_name, "SV_Target");
  EXPECT_EQ(reflection.patch_constant_signature[0].semantic_name, "PATCH");

  ASSERT_TRUE(parser.dxilTranslation().has_value());
  const auto &translation = *parser.dxilTranslation();
  EXPECT_TRUE(translation.valid);
  EXPECT_TRUE(translation.has_metadata);
  ASSERT_EQ(translation.resources.size(), 5u);
  const auto find_translation_resource = [&](uint32_t id) {
    const auto resource = std::find_if(
        translation.resources.begin(), translation.resources.end(),
        [id](const DxilTranslationResourceInfo &candidate) {
          return candidate.id == id;
        });
    return resource == translation.resources.end() ? nullptr : &*resource;
  };

  const auto *srv = find_translation_resource(3u);
  ASSERT_NE(srv, nullptr);
  EXPECT_EQ(srv->resource_class, DxilTranslationResourceClass::Srv);
  EXPECT_EQ(srv->source_mask, DxilTranslationSourceMetadata);
  EXPECT_TRUE(srv->referenced_by_handle);
  EXPECT_TRUE(srv->read);
  EXPECT_FALSE(srv->written);
  EXPECT_TRUE(srv->sampled);
  EXPECT_TRUE(srv->compared);
  EXPECT_TRUE(srv->queried);
  EXPECT_FALSE(srv->counter);
  EXPECT_EQ(srv->element_stride, 64u);

  const auto *uav = find_translation_resource(4u);
  ASSERT_NE(uav, nullptr);
  EXPECT_EQ(uav->resource_class, DxilTranslationResourceClass::Uav);
  EXPECT_TRUE(uav->unbounded);
  EXPECT_TRUE(uav->referenced_by_handle);
  EXPECT_TRUE(uav->read);
  EXPECT_TRUE(uav->written);
  EXPECT_FALSE(uav->sampled);
  EXPECT_TRUE(uav->counter);

  const auto *cbv = find_translation_resource(5u);
  ASSERT_NE(cbv, nullptr);
  EXPECT_EQ(cbv->resource_class, DxilTranslationResourceClass::Cbv);
  EXPECT_TRUE(cbv->referenced_by_handle);
  EXPECT_TRUE(cbv->read);
  EXPECT_FALSE(cbv->written);
  const auto *sampler = find_translation_resource(6u);
  ASSERT_NE(sampler, nullptr);
  EXPECT_EQ(sampler->resource_class, DxilTranslationResourceClass::Sampler);
  EXPECT_TRUE(sampler->referenced_by_handle);
  const auto *future = find_translation_resource(7u);
  ASSERT_NE(future, nullptr);
  EXPECT_EQ(future->resource_class, DxilTranslationResourceClass::Unknown);
  EXPECT_FALSE(future->referenced_by_handle);

  ASSERT_EQ(translation.operations.size(), 13u);
  EXPECT_TRUE(std::all_of(
      translation.operations.begin(), translation.operations.end(),
      [](const DxilTranslationOperationInfo &operation) {
        return operation.function_name == "main" &&
               operation.basic_block_name == "entry";
      }));
  const auto translated_sample = std::find_if(
      translation.operations.begin(), translation.operations.end(),
      [](const DxilTranslationOperationInfo &operation) {
        return operation.opcode == 64u;
      });
  ASSERT_NE(translated_sample, translation.operations.end());
  EXPECT_TRUE(translated_sample->has_resource_id);
  EXPECT_EQ(translated_sample->resource_id, 3u);
  EXPECT_TRUE(translated_sample->typed.has_resource_class);
  EXPECT_EQ(translated_sample->typed.resource_class, 0u);

  ASSERT_EQ(translation.signatures.size(), 3u);
  const auto find_signature = [&](DxilTranslationSignatureKind kind) {
    const auto signature = std::find_if(
        translation.signatures.begin(), translation.signatures.end(),
        [kind](const DxilTranslationSignatureElementInfo &candidate) {
          return candidate.kind == kind;
        });
    return signature == translation.signatures.end() ? nullptr : &*signature;
  };
  const auto *translated_input =
      find_signature(DxilTranslationSignatureKind::Input);
  const auto *translated_output =
      find_signature(DxilTranslationSignatureKind::Output);
  const auto *translated_patch =
      find_signature(DxilTranslationSignatureKind::PatchConstant);
  ASSERT_NE(translated_input, nullptr);
  ASSERT_NE(translated_output, nullptr);
  ASSERT_NE(translated_patch, nullptr);
  EXPECT_EQ(translated_input->source_mask, DxilTranslationSourceMetadata);
  EXPECT_EQ(translated_input->semantic_key, "POSITION0");
  EXPECT_EQ(translated_input->component_mask, 0xfu);
  EXPECT_TRUE(translated_input->has_element_id);
  EXPECT_EQ(translated_input->element_id, 10u);
  EXPECT_EQ(translated_output->semantic_key, "SV_TARGET0");
  EXPECT_EQ(translated_output->component_mask, 0xfu);
  EXPECT_EQ(translated_patch->semantic_key, "PATCH1");
  EXPECT_EQ(translated_patch->component_start, 2u);
  EXPECT_EQ(translated_patch->component_mask, 0xcu);

  ASSERT_TRUE(parser.dxilValidation().has_value());
  EXPECT_TRUE(parser.dxilValidation()->valid);
  EXPECT_EQ(parser.dxilValidation()->error_count, 0u);
}

TEST(DxilValidation, ReportsMissingMetadataAndReflectionParts) {
  using namespace dxmt::dxil;
  const auto bitcode = DecodeHex(dxmt::test::kDxilMissingMetadataBitcodeHex);
  ASSERT_EQ(bitcode.size(), 1264u);
  const auto container =
      DxilContainerBuilder()
          .add(fourcc::Dxil, BuildDxilProgram(bitcode))
          .build();
  Parser parser;
  ASSERT_EQ(parser.parse(container), ParseStatus::Ok);
  ASSERT_TRUE(parser.dxilValidation().has_value());
  const auto &validation = *parser.dxilValidation();
  EXPECT_FALSE(validation.valid);
  const auto find_diagnostic = [&](std::string_view code) {
    const auto diagnostic = std::find_if(
        validation.diagnostics.begin(), validation.diagnostics.end(),
        [code](const DxilValidationDiagnostic &candidate) {
          return candidate.code == code;
        });
    return diagnostic == validation.diagnostics.end() ? nullptr
                                                       : &*diagnostic;
  };
  for (std::string_view code : {"missing-shader-model",
                                "missing-dxil-version",
                                "missing-entry-points",
                                "missing-validator-version", "missing-rdat",
                                "missing-psv"}) {
    SCOPED_TRACE(code);
    EXPECT_NE(find_diagnostic(code), nullptr);
  }
  EXPECT_EQ(find_diagnostic("missing-shader-model")->severity,
            DxilValidationSeverity::Error);
  EXPECT_EQ(find_diagnostic("missing-validator-version")->severity,
            DxilValidationSeverity::Warning);
  EXPECT_EQ(find_diagnostic("missing-rdat")->category,
            DxilValidationCategory::Reflection);
}

TEST(DxilValidation, ReportsInstructionFlowAndMetadataFailures) {
  using namespace dxmt::dxil;
  const auto bitcode = DecodeHex(dxmt::test::kDxilValidationErrorsBitcodeHex);
  ASSERT_EQ(bitcode.size(), 2216u);
  const auto container =
      DxilContainerBuilder()
          .add(fourcc::Dxil, BuildDxilProgram(bitcode))
          .build();
  Parser parser;
  ASSERT_EQ(parser.parse(container), ParseStatus::Ok);
  ASSERT_TRUE(parser.dxilValidation().has_value());
  const auto &validation = *parser.dxilValidation();
  EXPECT_FALSE(validation.valid);
  const auto find_diagnostic = [&](std::string_view code) {
    const auto diagnostic = std::find_if(
        validation.diagnostics.begin(), validation.diagnostics.end(),
        [code](const DxilValidationDiagnostic &candidate) {
          return candidate.code == code;
        });
    return diagnostic == validation.diagnostics.end() ? nullptr
                                                       : &*diagnostic;
  };
  for (std::string_view code : {
           "unknown-shader-model-kind", "shader-model-mismatch",
           "dxil-version-mismatch", "missing-validator-version",
           "missing-entry-function", "indirect-call", "recursive-call",
           "unreachable-function", "defined-dx-intrinsic",
           "missing-constant-opcode", "unknown-opcode", "reserved-opcode",
           "opcode-function-mismatch", "opcode-shader-model",
           "invalid-signature-reference", "invalid-resource-reference",
           "missing-rdat", "missing-psv"}) {
    SCOPED_TRACE(code);
    EXPECT_NE(find_diagnostic(code), nullptr);
  }

  const auto *unknown = find_diagnostic("unknown-opcode");
  ASSERT_NE(unknown, nullptr);
  EXPECT_EQ(unknown->severity, DxilValidationSeverity::Error);
  EXPECT_EQ(unknown->category, DxilValidationCategory::Instruction);
  EXPECT_EQ(unknown->function_name, "main");
  EXPECT_TRUE(unknown->has_instruction);
  EXPECT_TRUE(unknown->has_opcode);
  EXPECT_EQ(unknown->opcode, 999u);
  EXPECT_FALSE(unknown->message.empty());

  const auto *dynamic = find_diagnostic("missing-constant-opcode");
  ASSERT_NE(dynamic, nullptr);
  EXPECT_EQ(dynamic->function_name, "main");
  EXPECT_TRUE(dynamic->has_instruction);
  EXPECT_FALSE(dynamic->has_opcode);
  const auto *indirect = find_diagnostic("indirect-call");
  ASSERT_NE(indirect, nullptr);
  EXPECT_EQ(indirect->severity, DxilValidationSeverity::Warning);
}

TEST(DxilValidation, ReportsEveryProgramHeaderMismatch) {
  using namespace dxmt::dxil;
  const auto bitcode =
      DecodeHex(dxmt::test::kDxilMetadataTranslationBitcodeHex);
  ASSERT_EQ(bitcode.size(), 2832u);

  const auto expect_diagnostic = [&](std::string_view case_name,
                                     std::vector<uint8_t> program,
                                     std::string_view code) {
    SCOPED_TRACE(case_name);
    const auto container =
        DxilContainerBuilder().add(fourcc::Dxil, std::move(program)).build();
    Parser parser;
    ASSERT_EQ(parser.parse(container), ParseStatus::Ok);
    ASSERT_TRUE(parser.dxilValidation().has_value());
    EXPECT_TRUE(std::any_of(
        parser.dxilValidation()->diagnostics.begin(),
        parser.dxilValidation()->diagnostics.end(),
        [code](const DxilValidationDiagnostic &diagnostic) {
          return diagnostic.code == code;
        }));
  };

  expect_diagnostic("invalid shader kind",
                    BuildDxilProgram(bitcode, 16u, 6u, 8u, 0x00010008u),
                    "invalid-shader-kind");
  expect_diagnostic("shader kind mismatch",
                    BuildDxilProgram(bitcode, 1u, 6u, 8u, 0x00010008u),
                    "shader-kind-mismatch");
  expect_diagnostic("invalid shader model",
                    BuildDxilProgram(bitcode, 5u, 5u, 0u, 0x00010008u),
                    "invalid-shader-model");
  expect_diagnostic("shader model mismatch",
                    BuildDxilProgram(bitcode, 5u, 6u, 7u, 0x00010008u),
                    "shader-model-mismatch");
  expect_diagnostic("invalid DXIL version",
                    BuildDxilProgram(bitcode, 5u, 6u, 8u, 0u),
                    "invalid-dxil-version");
  expect_diagnostic("DXIL version mismatch",
                    BuildDxilProgram(bitcode, 5u, 6u, 8u, 0x00010007u),
                    "dxil-version-mismatch");

  auto padded =
      BuildDxilProgram(bitcode, 5u, 6u, 8u, 0x00010008u, 4u);
  Store<uint32_t>(padded, 4,
                  (padded.size() - 4u) / sizeof(uint32_t));
  expect_diagnostic("part padding", std::move(padded),
                    "dxil-size-mismatch");
}

TEST(DxilValidation, ReportsReachableRdatPsvAndCrossSourceDiagnostics) {
  using namespace dxmt::dxil;
  const auto bitcode =
      DecodeHex(dxmt::test::kDxilMetadataTranslationBitcodeHex);
  const auto program =
      BuildDxilProgram(bitcode, 5u, 6u, 8u, 0x00010008u);

  const auto expect_diagnostics =
      [&](std::string_view case_name, std::vector<uint8_t> container,
          std::initializer_list<std::string_view> codes) {
        SCOPED_TRACE(case_name);
        Parser parser;
        ASSERT_EQ(parser.parse(container), ParseStatus::Ok);
        ASSERT_TRUE(parser.dxilValidation().has_value());
        for (const auto code : codes) {
          SCOPED_TRACE(code);
          EXPECT_TRUE(std::any_of(
              parser.dxilValidation()->diagnostics.begin(),
              parser.dxilValidation()->diagnostics.end(),
              [code](const DxilValidationDiagnostic &diagnostic) {
                return diagnostic.code == code;
              }));
        }
      };

  std::vector<uint8_t> empty_runtime_data(kRuntimeDataHeaderSize);
  Store<uint32_t>(empty_runtime_data, 0, 1u);
  expect_diagnostics(
      "empty RDAT",
      DxilContainerBuilder()
          .add(fourcc::Dxil, program)
          .add(fourcc::RuntimeData, empty_runtime_data)
          .build(),
      {"missing-rdat-functions"});

  const std::vector<uint8_t> invalid_kind_strings = {'b', 'a', 'd', 0};
  std::vector<uint8_t> invalid_kind_functions(
      kRuntimeDataTableHeaderSize + kRdatFunctionRecordSize);
  Store<uint32_t>(invalid_kind_functions, 0, 1u);
  Store<uint32_t>(invalid_kind_functions, 4, kRdatFunctionRecordSize);
  Store<uint32_t>(invalid_kind_functions, kRuntimeDataTableHeaderSize + 0,
                  0u);
  Store<uint32_t>(invalid_kind_functions, kRuntimeDataTableHeaderSize + 4,
                  kRdatNullRef);
  Store<uint32_t>(invalid_kind_functions, kRuntimeDataTableHeaderSize + 8,
                  kRdatNullRef);
  Store<uint32_t>(invalid_kind_functions, kRuntimeDataTableHeaderSize + 12,
                  kRdatNullRef);
  Store<uint32_t>(invalid_kind_functions, kRuntimeDataTableHeaderSize + 16,
                  16u);
  const auto invalid_kind_runtime_data =
      DxilRuntimeDataBuilder()
          .add(rdat::StringBuffer, invalid_kind_strings)
          .add(rdat::FunctionTable, invalid_kind_functions)
          .build();
  expect_diagnostics(
      "invalid RDAT shader kind",
      DxilContainerBuilder()
          .add(fourcc::Dxil, program)
          .add(fourcc::RuntimeData, invalid_kind_runtime_data)
          .build(),
      {"invalid-rdat-shader-kind"});

  const std::vector<uint8_t> resource_strings = {'a', 0, 'b', 0};
  std::vector<uint8_t> resources(
      kRuntimeDataTableHeaderSize + 2u * kRdatResourceRecordSize);
  Store<uint32_t>(resources, 0, 2u);
  Store<uint32_t>(resources, 4, kRdatResourceRecordSize);
  const auto store_resource = [&](size_t record, uint32_t name_offset,
                                  uint32_t id, uint32_t lower_bound,
                                  uint32_t upper_bound) {
    const auto offset = kRuntimeDataTableHeaderSize +
                        record * kRdatResourceRecordSize;
    Store<uint32_t>(resources, offset + 0, name_offset);
    Store<uint32_t>(resources, offset + 4, 0u);
    Store<uint32_t>(resources, offset + 8, 2u);
    Store<uint32_t>(resources, offset + 12, id);
    Store<uint32_t>(resources, offset + 16, 1u);
    Store<uint32_t>(resources, offset + 20, lower_bound);
    Store<uint32_t>(resources, offset + 24, upper_bound);
  };
  store_resource(0, 0u, 3u, 0u, 5u);
  store_resource(1, 2u, 99u, 4u, 8u);
  const auto mismatched_runtime_data =
      DxilRuntimeDataBuilder()
          .add(rdat::StringBuffer, resource_strings)
          .add(rdat::ResourceTable, resources)
          .build();
  expect_diagnostics(
      "overlapping and mismatched RDAT resources",
      DxilContainerBuilder()
          .add(fourcc::Dxil, program)
          .add(fourcc::RuntimeData, mismatched_runtime_data)
          .build(),
      {"missing-rdat-functions", "overlapping-rdat-resources",
       "metadata-rdat-resource-mismatch"});

  constexpr size_t rdef_resource_offset = kResourceDefHeaderSize;
  constexpr size_t rdef_name_offset =
      rdef_resource_offset + kResourceDefResourceBindingExtendedSize;
  std::vector<uint8_t> resource_def(rdef_name_offset + 5u);
  Store<uint32_t>(resource_def, 8, 1u);
  Store<uint32_t>(resource_def, 12, rdef_resource_offset);
  Store<uint32_t>(resource_def, 16, 0x68u);
  Store<uint32_t>(resource_def, rdef_resource_offset + 0,
                  rdef_name_offset);
  Store<uint32_t>(resource_def, rdef_resource_offset + 4, 1u);
  Store<uint32_t>(resource_def, rdef_resource_offset + 20, 2u);
  Store<uint32_t>(resource_def, rdef_resource_offset + 24, 2u);
  Store<uint32_t>(resource_def, rdef_resource_offset + 32, 1u);
  Store<uint32_t>(resource_def, rdef_resource_offset + 36, 3u);
  std::memcpy(resource_def.data() + rdef_name_offset, "rdef", 5u);
  expect_diagnostics(
      "metadata and RDEF range mismatch",
      DxilContainerBuilder()
          .add(fourcc::Dxil, program)
          .add(fourcc::ResourceDef, resource_def)
          .build(),
      {"metadata-rdef-resource-mismatch"});

  std::vector<uint8_t> old_psv(2u * sizeof(uint32_t));
  expect_diagnostics(
      "old PSV runtime info",
      DxilContainerBuilder()
          .add(fourcc::Dxil, program)
          .add(fourcc::PipelineStateValidation, old_psv)
          .build(),
      {"old-psv-runtime-info"});

  constexpr size_t invalid_psv_resource_count_offset =
      sizeof(uint32_t) + kPsvRuntimeInfo1Size;
  std::vector<uint8_t> invalid_stage_psv(
      invalid_psv_resource_count_offset + 3u * sizeof(uint32_t));
  Store<uint32_t>(invalid_stage_psv, 0, kPsvRuntimeInfo1Size);
  invalid_stage_psv[sizeof(uint32_t) + 24] = 0xff;
  expect_diagnostics(
      "invalid PSV shader stage",
      DxilContainerBuilder()
          .add(fourcc::Dxil, program)
          .add(fourcc::PipelineStateValidation, invalid_stage_psv)
          .build(),
      {"invalid-psv-shader-stage", "psv-stage-mismatch"});

  const std::vector<uint8_t> compute_strings = {'m', 'a', 'i', 'n', 0};
  std::vector<uint8_t> compute_indices(4u * sizeof(uint32_t));
  Store<uint32_t>(compute_indices, 0, 3u);
  Store<uint32_t>(compute_indices, 4, 8u);
  Store<uint32_t>(compute_indices, 8, 4u);
  Store<uint32_t>(compute_indices, 12, 2u);
  std::vector<uint8_t> compute_info(kRuntimeDataTableHeaderSize +
                                    kRdatCSInfoRecordSize);
  Store<uint32_t>(compute_info, 0, 1u);
  Store<uint32_t>(compute_info, 4, kRdatCSInfoRecordSize);
  Store<uint32_t>(compute_info, kRuntimeDataTableHeaderSize, 0u);
  std::vector<uint8_t> compute_function(kRuntimeDataTableHeaderSize +
                                        kRdatFunctionRecord2Size);
  Store<uint32_t>(compute_function, 0, 1u);
  Store<uint32_t>(compute_function, 4, kRdatFunctionRecord2Size);
  Store<uint32_t>(compute_function, kRuntimeDataTableHeaderSize + 0, 0u);
  Store<uint32_t>(compute_function, kRuntimeDataTableHeaderSize + 4, 0u);
  Store<uint32_t>(compute_function, kRuntimeDataTableHeaderSize + 8,
                  kRdatNullRef);
  Store<uint32_t>(compute_function, kRuntimeDataTableHeaderSize + 12,
                  kRdatNullRef);
  Store<uint32_t>(compute_function, kRuntimeDataTableHeaderSize + 16, 5u);
  Store<uint32_t>(compute_function, kRuntimeDataTableHeaderSize + 48, 0u);
  const auto compute_runtime_data =
      DxilRuntimeDataBuilder()
          .add(rdat::StringBuffer, compute_strings)
          .add(rdat::IndexArrays, compute_indices)
          .add(rdat::CSInfoTable, compute_info)
          .add(rdat::FunctionTable, compute_function)
          .build();
  constexpr size_t psv2_resource_count_offset =
      sizeof(uint32_t) + kPsvRuntimeInfo2Size;
  std::vector<uint8_t> mismatched_thread_psv(
      psv2_resource_count_offset + 3u * sizeof(uint32_t));
  Store<uint32_t>(mismatched_thread_psv, 0, kPsvRuntimeInfo2Size);
  mismatched_thread_psv[sizeof(uint32_t) + 24] = 5u;
  Store<uint32_t>(mismatched_thread_psv, sizeof(uint32_t) + 36, 4u);
  Store<uint32_t>(mismatched_thread_psv, sizeof(uint32_t) + 40, 4u);
  Store<uint32_t>(mismatched_thread_psv, sizeof(uint32_t) + 44, 2u);
  expect_diagnostics(
      "RDAT and PSV thread group mismatch",
      DxilContainerBuilder()
          .add(fourcc::Dxil, program)
          .add(fourcc::RuntimeData, compute_runtime_data)
          .add(fourcc::PipelineStateValidation, mismatched_thread_psv)
          .build(),
      {"thread-group-size-mismatch"});

  constexpr size_t psv_resource_count_offset =
      sizeof(uint32_t) + kPsvRuntimeInfo1Size;
  constexpr size_t psv_resource_stride_offset =
      psv_resource_count_offset + sizeof(uint32_t);
  constexpr size_t psv_resource_offset =
      psv_resource_stride_offset + sizeof(uint32_t);
  constexpr size_t psv_string_size_offset =
      psv_resource_offset + kPsvResourceBindInfo0Size;
  std::vector<uint8_t> unmatched_resource_psv(
      psv_string_size_offset + 2u * sizeof(uint32_t));
  Store<uint32_t>(unmatched_resource_psv, 0, kPsvRuntimeInfo1Size);
  unmatched_resource_psv[sizeof(uint32_t) + 24] = 5u;
  Store<uint32_t>(unmatched_resource_psv, psv_resource_count_offset, 1u);
  Store<uint32_t>(unmatched_resource_psv, psv_resource_stride_offset,
                  kPsvResourceBindInfo0Size);
  Store<uint32_t>(unmatched_resource_psv, psv_resource_offset + 0, 3u);
  Store<uint32_t>(unmatched_resource_psv, psv_resource_offset + 4, 9u);
  Store<uint32_t>(unmatched_resource_psv, psv_resource_offset + 8, 1u);
  Store<uint32_t>(unmatched_resource_psv, psv_resource_offset + 12, 2u);
  const auto metadata_free_bitcode =
      DecodeHex(dxmt::test::kDxilMissingMetadataBitcodeHex);
  expect_diagnostics(
      "unmatched PSV resource",
      DxilContainerBuilder()
          .add(fourcc::Dxil, BuildDxilProgram(metadata_free_bitcode))
          .add(fourcc::RuntimeData, empty_runtime_data)
          .add(fourcc::PipelineStateValidation, unmatched_resource_psv)
          .build(),
      {"unmatched-psv-resource"});
}

TEST(DxilBitcode, RejectsTruncatedStreamsAndWrappers) {
  using namespace dxmt::dxil;
  BitcodeInfo info;
  EXPECT_EQ(ParseBitcode({}, info), ParseStatus::InvalidBitcode);

  const std::array<uint8_t, 4> bad_magic = {1, 2, 3, 4};
  EXPECT_EQ(ParseBitcode(bad_magic, info), ParseStatus::InvalidBitcode);

  std::vector<uint8_t> wrapper(kBitcodeWrapperHeaderSize);
  Store<uint32_t>(wrapper, 0, kBitcodeWrapperMagicValue);
  Store<uint32_t>(wrapper, 8, wrapper.size());
  Store<uint32_t>(wrapper, 12, 4u);
  EXPECT_EQ(ParseBitcode(wrapper, info), ParseStatus::InvalidBitcode);
}

TEST(DxilBitcode, ParsesNestedAbbreviatedRecordsAndWrappers) {
  using namespace dxmt::dxil;
  const auto bitcode = BuildBitcodeWithAbbreviatedRecord();

  BitcodeInfo info;
  ASSERT_EQ(ParseBitcode(bitcode, info), ParseStatus::Ok);
  EXPECT_EQ(info.magic, kBitcodeMagicValue);
  EXPECT_FALSE(info.has_wrapper);
  ASSERT_EQ(info.blocks.size(), 2u);
  EXPECT_EQ(info.blocks[0].id, std::numeric_limits<uint32_t>::max());
  EXPECT_EQ(info.blocks[0].abbreviation_id_width, 2u);
  EXPECT_EQ(info.blocks[0].depth, 0u);
  EXPECT_EQ(info.blocks[0].start_bit, 32u);
  EXPECT_EQ(info.blocks[0].end_bit, bitcode.size() * 8u);
  EXPECT_EQ(info.blocks[0].record_count, 0u);
  EXPECT_EQ(info.blocks[1].id, 8u);
  EXPECT_EQ(info.blocks[1].abbreviation_id_width, 3u);
  EXPECT_EQ(info.blocks[1].depth, 1u);
  EXPECT_EQ(info.blocks[1].start_bit, 96u);
  EXPECT_EQ(info.blocks[1].end_bit, bitcode.size() * 8u);
  EXPECT_EQ(info.blocks[1].record_count, 1u);
  ASSERT_EQ(info.records.size(), 1u);
  EXPECT_EQ(info.records[0].block_id, 8u);
  EXPECT_EQ(info.records[0].code, 7u);
  EXPECT_EQ(info.records[0].operand_count, 1u);
  EXPECT_TRUE(info.records[0].abbreviated);

  std::vector<uint8_t> wrapper(kBitcodeWrapperHeaderSize + bitcode.size());
  Store<uint32_t>(wrapper, 0, kBitcodeWrapperMagicValue);
  Store<uint32_t>(wrapper, 4, 3u);
  Store<uint32_t>(wrapper, 8, kBitcodeWrapperHeaderSize);
  Store<uint32_t>(wrapper, 12, bitcode.size());
  Store<uint32_t>(wrapper, 16, 0x0100000cu);
  std::copy(bitcode.begin(), bitcode.end(),
            wrapper.begin() + kBitcodeWrapperHeaderSize);

  ASSERT_EQ(ParseBitcode(wrapper, info), ParseStatus::Ok);
  EXPECT_TRUE(info.has_wrapper);
  EXPECT_EQ(info.wrapper_version, 3u);
  EXPECT_EQ(info.wrapper_offset, kBitcodeWrapperHeaderSize);
  EXPECT_EQ(info.wrapper_size, bitcode.size());
  EXPECT_EQ(info.wrapper_cpu_type, 0x0100000cu);
  ASSERT_EQ(info.records.size(), 1u);
  EXPECT_EQ(info.records[0].code, 7u);
}

TEST(DxilBitcode, RejectsInvalidSubblockHeadersAndAbbreviations) {
  using namespace dxmt::dxil;
  BitcodeInfo info;

  auto malformed = BuildBitcodeWithAbbreviatedRecord();
  Store<uint32_t>(malformed, 8, 3u);
  EXPECT_EQ(ParseBitcode(malformed, info), ParseStatus::InvalidBitcode);

  malformed = BitcodeBuilder()
                  .bits(kBitcodeMagicValue, 32)
                  .bits(bitc::EnterSubblock, 2)
                  .vbr(8, 8)
                  .vbr(0, 4)
                  .align32()
                  .bits(0, 32)
                  .build();
  EXPECT_EQ(ParseBitcode(malformed, info), ParseStatus::InvalidBitcode);

  malformed = BitcodeBuilder()
                  .bits(kBitcodeMagicValue, 32)
                  .bits(bitc::EnterSubblock, 2)
                  .vbr(8, 8)
                  .vbr(3, 4)
                  .align32()
                  .bits(1, 32)
                  .bits(bitc::FirstApplicationAbbrev, 3)
                  .align32()
                  .build();
  EXPECT_EQ(ParseBitcode(malformed, info), ParseStatus::InvalidBitcode);
}

TEST(DxilBitcode, ParsesEveryAbbreviationEncoding) {
  using namespace dxmt::dxil;
  BitcodeBuilder builder;
  builder.bits(kBitcodeMagicValue, 32)
      .bits(bitc::EnterSubblock, 2)
      .vbr(8, 8)
      .vbr(3, 4)
      .align32()
      .bits(0, 32)
      .bits(bitc::DefineAbbrev, 3)
      .vbr(2, 5)
      .bits(1, 1)
      .vbr(10, 8)
      .bits(0, 1)
      .bits(2, 3)
      .vbr(6, 5)
      .bits(bitc::FirstApplicationAbbrev, 3)
      .vbr(300, 6)
      .bits(bitc::DefineAbbrev, 3)
      .vbr(3, 5)
      .bits(1, 1)
      .vbr(11, 8)
      .bits(0, 1)
      .bits(3, 3)
      .bits(0, 1)
      .bits(4, 3)
      .bits(bitc::FirstApplicationAbbrev + 1, 3)
      .vbr(3, 6)
      .bits(1, 6)
      .bits(2, 6)
      .bits(3, 6)
      .bits(bitc::DefineAbbrev, 3)
      .vbr(2, 5)
      .bits(1, 1)
      .vbr(12, 8)
      .bits(0, 1)
      .bits(5, 3)
      .bits(bitc::FirstApplicationAbbrev + 2, 3)
      .vbr(3, 6)
      .align32()
      .bits(0x00ccbbaau, 24)
      .align32();
  const auto bitcode = FinishSingleBitcodeBlock(builder, 3);

  BitcodeInfo info;
  ASSERT_EQ(ParseBitcode(bitcode, info), ParseStatus::Ok);
  ASSERT_EQ(info.records.size(), 3u);
  EXPECT_EQ(info.records[0].code, 10u);
  EXPECT_EQ(info.records[0].operand_count, 1u);
  EXPECT_TRUE(info.records[0].abbreviated);
  EXPECT_EQ(info.records[1].code, 11u);
  EXPECT_EQ(info.records[1].operand_count, 3u);
  EXPECT_TRUE(info.records[1].abbreviated);
  EXPECT_EQ(info.records[2].code, 12u);
  EXPECT_EQ(info.records[2].operand_count, 1u);
  EXPECT_TRUE(info.records[2].abbreviated);
}

TEST(DxilBitcode, RejectsMalformedAbbreviationEncodings) {
  using namespace dxmt::dxil;
  const auto begin_block = [] {
    BitcodeBuilder builder;
    builder.bits(kBitcodeMagicValue, 32)
        .bits(bitc::EnterSubblock, 2)
        .vbr(8, 8)
        .vbr(3, 4)
        .align32()
        .bits(0, 32);
    return builder;
  };
  BitcodeInfo info;

  auto builder = begin_block();
  builder.bits(bitc::DefineAbbrev, 3)
      .vbr(1, 5)
      .bits(0, 1)
      .bits(0, 3);
  auto malformed = FinishSingleBitcodeBlock(builder, 3);
  EXPECT_EQ(ParseBitcode(malformed, info), ParseStatus::InvalidBitcode);

  builder = begin_block();
  builder.bits(bitc::DefineAbbrev, 3)
      .vbr(2, 5)
      .bits(1, 1)
      .vbr(13, 8)
      .bits(0, 1)
      .bits(3, 3)
      .bits(bitc::FirstApplicationAbbrev, 3);
  malformed = FinishSingleBitcodeBlock(builder, 3);
  EXPECT_EQ(ParseBitcode(malformed, info), ParseStatus::InvalidBitcode);

  builder = begin_block();
  builder.bits(bitc::DefineAbbrev, 3)
      .vbr(2, 5)
      .bits(1, 1)
      .vbr(14, 8)
      .bits(0, 1)
      .bits(2, 3)
      .vbr(1, 5)
      .bits(bitc::FirstApplicationAbbrev, 3);
  malformed = FinishSingleBitcodeBlock(builder, 3);
  EXPECT_EQ(ParseBitcode(malformed, info), ParseStatus::InvalidBitcode);

  builder = begin_block();
  builder.bits(bitc::DefineAbbrev, 3)
      .vbr(2, 5)
      .bits(1, 1)
      .vbr(15, 8)
      .bits(0, 1)
      .bits(5, 3)
      .bits(bitc::FirstApplicationAbbrev, 3)
      .vbr(63, 6);
  malformed = FinishSingleBitcodeBlock(builder, 3);
  EXPECT_EQ(ParseBitcode(malformed, info), ParseStatus::InvalidBitcode);
}

TEST(DxilBitcode, InheritsMultipleBlockInfoAbbreviations) {
  using namespace dxmt::dxil;
  BitcodeBuilder builder;
  builder.bits(kBitcodeMagicValue, 32)
      .bits(bitc::EnterSubblock, 2)
      .vbr(bitc::BlockInfoBlockId, 8)
      .vbr(2, 4)
      .align32();
  const size_t block_info_word_offset = builder.byteSize();
  builder.bits(0, 32);
  const size_t block_info_start = builder.byteSize();
  builder.bits(bitc::UnabbrevRecord, 2)
      .vbr(bitc::BlockInfoSetBid, 6)
      .vbr(1, 6)
      .vbr(8, 6)
      .bits(bitc::DefineAbbrev, 2)
      .vbr(2, 5)
      .bits(1, 1)
      .vbr(21, 8)
      .bits(0, 1)
      .bits(1, 3)
      .vbr(5, 5)
      .bits(bitc::DefineAbbrev, 2)
      .vbr(2, 5)
      .bits(1, 1)
      .vbr(22, 8)
      .bits(0, 1)
      .bits(2, 3)
      .vbr(6, 5)
      .bits(bitc::EndBlock, 2)
      .align32();
  const size_t block_info_end = builder.byteSize();

  builder.bits(bitc::EnterSubblock, 2).vbr(8, 8).vbr(3, 4).align32();
  const size_t target_word_offset = builder.byteSize();
  builder.bits(0, 32);
  const size_t target_start = builder.byteSize();
  builder.bits(bitc::FirstApplicationAbbrev, 3)
      .bits(17, 5)
      .bits(bitc::FirstApplicationAbbrev + 1, 3)
      .vbr(300, 6)
      .bits(bitc::EndBlock, 3)
      .align32();
  const size_t target_end = builder.byteSize();

  auto bitcode = builder.build();
  Store<uint32_t>(bitcode, block_info_word_offset,
                  (block_info_end - block_info_start) / sizeof(uint32_t));
  Store<uint32_t>(bitcode, target_word_offset,
                  (target_end - target_start) / sizeof(uint32_t));

  BitcodeInfo info;
  ASSERT_EQ(ParseBitcode(bitcode, info), ParseStatus::Ok);
  ASSERT_EQ(info.blocks.size(), 3u);
  EXPECT_EQ(info.blocks[1].id, bitc::BlockInfoBlockId);
  EXPECT_EQ(info.blocks[1].record_count, 1u);
  EXPECT_EQ(info.blocks[2].id, 8u);
  EXPECT_EQ(info.blocks[2].record_count, 2u);
  ASSERT_EQ(info.records.size(), 3u);
  EXPECT_EQ(info.records[0].block_id, bitc::BlockInfoBlockId);
  EXPECT_EQ(info.records[0].code, bitc::BlockInfoSetBid);
  EXPECT_EQ(info.records[0].operand_count, 1u);
  EXPECT_FALSE(info.records[0].abbreviated);
  EXPECT_EQ(info.records[1].block_id, 8u);
  EXPECT_EQ(info.records[1].code, 21u);
  EXPECT_EQ(info.records[1].operand_count, 1u);
  EXPECT_TRUE(info.records[1].abbreviated);
  EXPECT_EQ(info.records[2].block_id, 8u);
  EXPECT_EQ(info.records[2].code, 22u);
  EXPECT_EQ(info.records[2].operand_count, 1u);
  EXPECT_TRUE(info.records[2].abbreviated);
}

TEST(DxilParts, ParsesSignatureElementsAndValidatesStringOffsets) {
  using namespace dxmt::dxil;
  constexpr size_t element_offset = kDxilSignatureHeaderSize;
  constexpr size_t name_offset = element_offset + kDxilSignatureElementSize;
  std::vector<uint8_t> bytes(name_offset + 9);
  Store<uint32_t>(bytes, 0, 1u);
  Store<uint32_t>(bytes, 4, element_offset);
  Store<uint32_t>(bytes, element_offset + 0, 2u);
  Store<uint32_t>(bytes, element_offset + 4, name_offset);
  Store<uint32_t>(bytes, element_offset + 8, 3u);
  Store<uint32_t>(bytes, element_offset + 12, 4u);
  Store<uint32_t>(bytes, element_offset + 16, 5u);
  Store<uint32_t>(bytes, element_offset + 20, 6u);
  bytes[element_offset + 24] = 0xb;
  bytes[element_offset + 25] = 0x3;
  Store<uint32_t>(bytes, element_offset + 28, 7u);
  std::memcpy(bytes.data() + name_offset, "POSITION", 9);

  SignatureInfo signature;
  ASSERT_EQ(ParseSignature(Part(fourcc::InputSignature, bytes), signature),
            ParseStatus::Ok);
  EXPECT_EQ(signature.part_fourcc, fourcc::InputSignature);
  ASSERT_EQ(signature.elements.size(), 1u);
  const auto &element = signature.elements.front();
  EXPECT_EQ(element.semantic_name, "POSITION");
  EXPECT_EQ(element.stream, 2u);
  EXPECT_EQ(element.semantic_index, 3u);
  EXPECT_EQ(element.system_value, 4u);
  EXPECT_EQ(element.component_type, 5u);
  EXPECT_EQ(element.register_index, 6u);
  EXPECT_EQ(element.mask, 0xbu);
  EXPECT_EQ(element.read_write_mask, 0x3u);
  EXPECT_EQ(element.min_precision, 7u);

  Store<uint32_t>(bytes, element_offset + 4, bytes.size());
  EXPECT_EQ(ParseSignature(Part(fourcc::InputSignature, bytes), signature),
            ParseStatus::InvalidSignature);
}

TEST(DxilParts, RejectsMalformedSignatureTableAndStringRanges) {
  using namespace dxmt::dxil;
  constexpr size_t element_offset = kDxilSignatureHeaderSize;
  constexpr size_t name_offset = element_offset + kDxilSignatureElementSize;
  std::vector<uint8_t> valid(name_offset + 2);
  Store<uint32_t>(valid, 0, 1u);
  Store<uint32_t>(valid, 4, element_offset);
  Store<uint32_t>(valid, element_offset + 4, name_offset);
  valid[name_offset] = 'X';

  SignatureInfo info;
  ASSERT_EQ(ParseSignature(Part(fourcc::InputSignature, valid), info),
            ParseStatus::Ok);

  const auto expect_invalid = [&](std::string_view case_name,
                                  std::vector<uint8_t> bytes) {
    SCOPED_TRACE(case_name);
    EXPECT_EQ(ParseSignature(Part(fourcc::InputSignature, bytes), info),
              ParseStatus::InvalidSignature);
  };

  auto malformed = valid;
  Store<uint32_t>(malformed, 4, kDxilSignatureHeaderSize - 4);
  expect_invalid("table overlaps header", malformed);

  malformed = valid;
  Store<uint32_t>(malformed, 4, kDxilSignatureHeaderSize + 1);
  expect_invalid("unaligned table", malformed);

  malformed = valid;
  Store<uint32_t>(malformed, 0, std::numeric_limits<uint32_t>::max());
  expect_invalid("element count overflow", malformed);

  malformed = valid;
  malformed.pop_back();
  expect_invalid("unterminated semantic name", malformed);
}

TEST(DxilParts, ParsesFeatureAndShaderHash) {
  using namespace dxmt::dxil;
  std::vector<uint8_t> feature_bytes(kFeatureInfoSize);
  Store<uint64_t>(feature_bytes, 0, 0x0123456789abcdefull);
  FeatureInfo feature;
  ASSERT_EQ(ParseFeatureInfo(Part(fourcc::FeatureInfo, feature_bytes), feature),
            ParseStatus::Ok);
  EXPECT_EQ(feature.feature_flags, 0x0123456789abcdefull);

  std::vector<uint8_t> hash_bytes(kShaderHashSize);
  Store<uint32_t>(hash_bytes, 0, 1u);
  for (size_t i = 0; i < 16; ++i)
    hash_bytes[4 + i] = static_cast<uint8_t>(i + 1);
  ShaderHashInfo hash;
  ASSERT_EQ(ParseShaderHash(Part(fourcc::ShaderHash, hash_bytes), hash),
            ParseStatus::Ok);
  EXPECT_TRUE(hash.includes_source());
  EXPECT_TRUE(hash.is_populated());
  EXPECT_EQ(hash.digest.front(), 1u);
  EXPECT_EQ(hash.digest.back(), 16u);
}

TEST(DxilParts, ParsesCompilerVersionAndDebugName) {
  using namespace dxmt::dxil;
  constexpr std::string_view strings("abc\0custom\0", 11);
  std::vector<uint8_t> version_bytes(kCompilerVersionHeaderSize + 12);
  Store<uint16_t>(version_bytes, 0, 1u);
  Store<uint16_t>(version_bytes, 2, 8u);
  Store<uint32_t>(version_bytes, 4, 3u);
  Store<uint32_t>(version_bytes, 8, 42u);
  Store<uint32_t>(version_bytes, 12, strings.size());
  std::copy(strings.begin(), strings.end(),
            version_bytes.begin() + kCompilerVersionHeaderSize);
  CompilerVersionInfo version;
  ASSERT_EQ(ParseCompilerVersion(Part(fourcc::CompilerVersion, version_bytes),
                                 version),
            ParseStatus::Ok);
  EXPECT_EQ(version.major, 1u);
  EXPECT_EQ(version.minor, 8u);
  EXPECT_EQ(version.commit_count, 42u);
  EXPECT_EQ(version.commit_sha, "abc");
  EXPECT_EQ(version.custom_version_string, "custom");

  constexpr std::string_view name = "shader.pdb";
  std::vector<uint8_t> name_bytes(kShaderDebugNameHeaderSize + name.size() + 1);
  Store<uint16_t>(name_bytes, 0, 7u);
  Store<uint16_t>(name_bytes, 2, name.size());
  std::copy(name.begin(), name.end(), name_bytes.begin() + 4);
  ShaderDebugNameInfo debug_name;
  ASSERT_EQ(ParseShaderDebugName(Part(fourcc::ShaderDebugName, name_bytes),
                                 debug_name),
            ParseStatus::Ok);
  EXPECT_EQ(debug_name.flags, 7u);
  EXPECT_EQ(debug_name.name, name);
}

TEST(DxilParts, RejectsMalformedCompilerVersionStringLists) {
  using namespace dxmt::dxil;
  CompilerVersionInfo info;
  const auto expect_invalid = [&](std::string_view case_name,
                                  std::vector<uint8_t> bytes) {
    SCOPED_TRACE(case_name);
    EXPECT_EQ(ParseCompilerVersion(Part(fourcc::CompilerVersion, bytes), info),
              ParseStatus::InvalidCompilerVersion);
  };

  std::vector<uint8_t> bytes(kCompilerVersionHeaderSize);
  Store<uint32_t>(bytes, 12, std::numeric_limits<uint32_t>::max());
  expect_invalid("string list size overflow", bytes);

  bytes.resize(kCompilerVersionHeaderSize + 1);
  Store<uint32_t>(bytes, 12, 1u);
  bytes[kCompilerVersionHeaderSize] = 0;
  expect_invalid("missing aligned padding", bytes);

  bytes.assign(kCompilerVersionHeaderSize + 4, 0);
  Store<uint32_t>(bytes, 12, 3u);
  bytes[kCompilerVersionHeaderSize + 0] = 'a';
  bytes[kCompilerVersionHeaderSize + 1] = 'b';
  bytes[kCompilerVersionHeaderSize + 2] = 'c';
  expect_invalid("unterminated string", bytes);

  Store<uint32_t>(bytes, 12, 4u);
  bytes[kCompilerVersionHeaderSize + 0] = 'a';
  bytes[kCompilerVersionHeaderSize + 1] = 0;
  bytes[kCompilerVersionHeaderSize + 2] = 'b';
  bytes[kCompilerVersionHeaderSize + 3] = 'c';
  expect_invalid("unterminated second string", bytes);
}

TEST(DxilParts, HandlesEmptyDebugNameAndRejectsMissingTerminator) {
  using namespace dxmt::dxil;
  std::vector<uint8_t> bytes(kShaderDebugNameHeaderSize + 1);
  Store<uint16_t>(bytes, 0, 9u);
  Store<uint16_t>(bytes, 2, 0u);

  ShaderDebugNameInfo info;
  ASSERT_EQ(ParseShaderDebugName(Part(fourcc::ShaderDebugName, bytes), info),
            ParseStatus::Ok);
  EXPECT_EQ(info.flags, 9u);
  EXPECT_TRUE(info.name.empty());

  bytes[kShaderDebugNameHeaderSize] = 'x';
  EXPECT_EQ(ParseShaderDebugName(Part(fourcc::ShaderDebugName, bytes), info),
            ParseStatus::InvalidShaderDebugName);

  Store<uint16_t>(bytes, 2, 1u);
  EXPECT_EQ(ParseShaderDebugName(Part(fourcc::ShaderDebugName, bytes), info),
            ParseStatus::InvalidShaderDebugName);

  bytes.push_back('y');
  EXPECT_EQ(ParseShaderDebugName(Part(fourcc::ShaderDebugName, bytes), info),
            ParseStatus::InvalidShaderDebugName);
}

TEST(DxilParts, RejectsTruncatedFixedLayoutParts) {
  using namespace dxmt::dxil;
  std::vector<uint8_t> bytes(7);
  FeatureInfo feature;
  EXPECT_EQ(ParseFeatureInfo(Part(fourcc::FeatureInfo, bytes), feature),
            ParseStatus::InvalidFeatureInfo);

  ShaderHashInfo hash;
  EXPECT_EQ(ParseShaderHash(Part(fourcc::ShaderHash, bytes), hash),
            ParseStatus::InvalidShaderHash);

  CompilerVersionInfo version;
  EXPECT_EQ(ParseCompilerVersion(Part(fourcc::CompilerVersion, bytes), version),
            ParseStatus::InvalidCompilerVersion);

  std::vector<uint8_t> name_bytes = {0, 0, 1, 0, 'x'};
  ShaderDebugNameInfo name;
  EXPECT_EQ(
      ParseShaderDebugName(Part(fourcc::ShaderDebugName, name_bytes), name),
      ParseStatus::InvalidShaderDebugName);
}

TEST(DxilRuntimeData, ParsesRawPartsAndTypedViews) {
  using namespace dxmt::dxil;
  std::vector<uint8_t> empty(kRuntimeDataHeaderSize);
  Store<uint32_t>(empty, 0, 1u);
  RuntimeDataInfo empty_info;
  ASSERT_EQ(ParseRuntimeData(Part(fourcc::RuntimeData, empty), empty_info),
            ParseStatus::Ok);
  EXPECT_EQ(empty_info.version, 1u);
  EXPECT_TRUE(empty_info.parts.empty());

  std::vector<uint8_t> indices(3 * sizeof(uint32_t));
  Store<uint32_t>(indices, 0, 2u);
  Store<uint32_t>(indices, 4, 4u);
  Store<uint32_t>(indices, 8, 9u);
  const auto bytes = DxilRuntimeDataBuilder()
                         .add(rdat::StringBuffer, {'m', 'a', 'i', 'n', 0})
                         .add(rdat::IndexArrays, indices)
                         .add(rdat::RawBytes, {10, 11, 12})
                         .add(rdat::ResourceTable,
                              std::vector<uint8_t>(kRuntimeDataTableHeaderSize))
                         .build();

  RuntimeDataInfo info;
  ASSERT_EQ(ParseRuntimeData(Part(fourcc::RuntimeData, bytes), info),
            ParseStatus::Ok);
  EXPECT_EQ(info.version, 1u);
  EXPECT_EQ(info.part_count, 4u);
  ASSERT_EQ(info.parts.size(), 4u);
  EXPECT_EQ(info.findPart(rdat::RawBytes), &info.parts[2]);
  EXPECT_TRUE(info.parts[3].is_table);
  EXPECT_EQ(info.parts[3].record_count, 0u);

  std::string name;
  EXPECT_TRUE(info.readString(0, name));
  EXPECT_EQ(name, "main");
  EXPECT_FALSE(info.readString(5, name));

  std::vector<uint32_t> values;
  EXPECT_TRUE(info.readIndexArray(0, values));
  EXPECT_EQ(values, (std::vector<uint32_t>{4, 9}));
  EXPECT_FALSE(info.readIndexArray(kRdatNullRef, values));

  std::span<const uint8_t> raw;
  ASSERT_TRUE(info.readBytes(1, 2, raw));
  EXPECT_TRUE(std::equal(raw.begin(), raw.end(),
                         std::array<uint8_t, 2>{11, 12}.begin()));
  EXPECT_FALSE(info.readBytes(2, 2, raw));
}

TEST(DxilRuntimeData, RejectsMalformedOffsetsSizesAndTables) {
  using namespace dxmt::dxil;
  RuntimeDataInfo info;
  const auto expect_invalid = [&](std::string_view case_name,
                                  const std::vector<uint8_t> &bytes) {
    SCOPED_TRACE(case_name);
    EXPECT_EQ(ParseRuntimeData(Part(fourcc::RuntimeData, bytes), info),
              ParseStatus::InvalidRuntimeData);
  };

  expect_invalid("truncated header",
                 std::vector<uint8_t>(kRuntimeDataHeaderSize - 1));

  std::vector<uint8_t> truncated_index(kRuntimeDataHeaderSize);
  Store<uint32_t>(truncated_index, 4, 1u);
  expect_invalid("truncated offset table", truncated_index);

  const auto valid = DxilRuntimeDataBuilder().add(rdat::RawBytes, {1}).build();
  auto malformed = valid;
  Store<uint32_t>(malformed, kRuntimeDataHeaderSize,
                  kRuntimeDataHeaderSize + sizeof(uint32_t) + 1);
  expect_invalid("misaligned part offset", malformed);

  malformed = valid;
  Store<uint32_t>(malformed, kRuntimeDataHeaderSize, kRuntimeDataHeaderSize);
  expect_invalid("part offset inside table", malformed);

  malformed = valid;
  Store<uint32_t>(malformed, kRuntimeDataHeaderSize, malformed.size());
  expect_invalid("truncated part header", malformed);

  malformed = valid;
  Store<uint32_t>(malformed,
                  kRuntimeDataHeaderSize + sizeof(uint32_t) + 4,
                  std::numeric_limits<uint32_t>::max());
  expect_invalid("part size overflow", malformed);

  expect_invalid("unaligned index array",
                 DxilRuntimeDataBuilder().add(rdat::IndexArrays, {1}).build());
  expect_invalid(
      "truncated table header",
      DxilRuntimeDataBuilder().add(rdat::ResourceTable,
                                   std::vector<uint8_t>(
                                       kRuntimeDataTableHeaderSize - 1))
          .build());

  std::vector<uint8_t> table(kRuntimeDataTableHeaderSize);
  Store<uint32_t>(table, 4, 2u);
  expect_invalid(
      "unaligned record stride",
      DxilRuntimeDataBuilder().add(rdat::ResourceTable, table).build());

  Store<uint32_t>(table, 0, 1u);
  Store<uint32_t>(table, 4, kRdatResourceRecordSize);
  expect_invalid(
      "truncated record array",
      DxilRuntimeDataBuilder().add(rdat::ResourceTable, table).build());
}

TEST(DxilRuntimeData, RejectsOverlappingPartRanges) {
  using namespace dxmt::dxil;
  auto bytes = DxilRuntimeDataBuilder()
                   .add(rdat::RawBytes, {1, 2, 3, 4})
                   .add(rdat::StringBuffer, {'x', 0})
                   .build();
  uint32_t first_offset = 0;
  std::memcpy(&first_offset, bytes.data() + kRuntimeDataHeaderSize,
              sizeof(first_offset));
  Store<uint32_t>(bytes, kRuntimeDataHeaderSize + sizeof(uint32_t),
                  first_offset);

  RuntimeDataInfo info;
  EXPECT_EQ(ParseRuntimeData(Part(fourcc::RuntimeData, bytes), info),
            ParseStatus::InvalidRuntimeData);
}

TEST(DxilRuntimeData, ParsesResourceAndExtendedFunctionRecords) {
  using namespace dxmt::dxil;
  std::vector<uint8_t> strings = {
      't', 'e', 'x', 't', 'u', 'r', 'e', 0,
      'm', 'a', 'i', 'n', 0,
      'h', 'e', 'l', 'p', 'e', 'r', 0,
  };
  std::vector<uint8_t> indices(4 * sizeof(uint32_t));
  Store<uint32_t>(indices, 0, 1u);
  Store<uint32_t>(indices, 4, 0u);
  Store<uint32_t>(indices, 8, 1u);
  Store<uint32_t>(indices, 12, 13u);

  std::vector<uint8_t> resources(kRuntimeDataTableHeaderSize +
                                 kRdatResourceRecordSize);
  Store<uint32_t>(resources, 0, 1u);
  Store<uint32_t>(resources, 4, kRdatResourceRecordSize);
  const size_t resource = kRuntimeDataTableHeaderSize;
  Store<uint32_t>(resources, resource + 0, 1u);
  Store<uint32_t>(resources, resource + 4, 2u);
  Store<uint32_t>(resources, resource + 8, 3u);
  Store<uint32_t>(resources, resource + 12, 4u);
  Store<uint32_t>(resources, resource + 16, 5u);
  Store<uint32_t>(resources, resource + 20, 6u);
  Store<uint32_t>(resources, resource + 24, 0u);
  Store<uint32_t>(resources, resource + 28, 7u);

  std::vector<uint8_t> functions(kRuntimeDataTableHeaderSize +
                                 kRdatFunctionRecord2Size);
  Store<uint32_t>(functions, 0, 1u);
  Store<uint32_t>(functions, 4, kRdatFunctionRecord2Size);
  const size_t function = kRuntimeDataTableHeaderSize;
  Store<uint32_t>(functions, function + 0, 8u);
  Store<uint32_t>(functions, function + 4, kRdatNullRef);
  Store<uint32_t>(functions, function + 8, 0u);
  Store<uint32_t>(functions, function + 12, 2u);
  Store<uint32_t>(functions, function + 16, 5u);
  Store<uint32_t>(functions, function + 20, 16u);
  Store<uint32_t>(functions, function + 24, 32u);
  Store<uint32_t>(functions, function + 28, 0x89abcdefu);
  Store<uint32_t>(functions, function + 32, 0x01234567u);
  Store<uint32_t>(functions, function + 36, 0x20u);
  Store<uint32_t>(functions, function + 40, 0x65u);
  functions[function + 44] = 4;
  functions[function + 45] = 32;
  Store<uint16_t>(functions, function + 46, 9u);
  Store<uint32_t>(functions, function + 48, kRdatNullRef);

  const auto bytes = DxilRuntimeDataBuilder()
                         .add(rdat::StringBuffer, strings)
                         .add(rdat::IndexArrays, indices)
                         .add(rdat::ResourceTable, resources)
                         .add(rdat::FunctionTable, functions)
                         .build();

  RuntimeDataInfo info;
  ASSERT_EQ(ParseRuntimeData(Part(fourcc::RuntimeData, bytes), info),
            ParseStatus::Ok);
  ASSERT_EQ(info.resources.size(), 1u);
  EXPECT_EQ(info.resources[0].name, "texture");
  EXPECT_EQ(info.resources[0].resource_class, 1u);
  EXPECT_EQ(info.resources[0].kind, 2u);
  EXPECT_EQ(info.resources[0].id, 3u);
  EXPECT_EQ(info.resources[0].space, 4u);
  EXPECT_EQ(info.resources[0].lower_bound, 5u);
  EXPECT_EQ(info.resources[0].upper_bound, 6u);
  EXPECT_EQ(info.resources[0].flags, 7u);

  ASSERT_EQ(info.functions.size(), 1u);
  EXPECT_EQ(info.functions[0].name, "main");
  EXPECT_TRUE(info.functions[0].unmangled_name.empty());
  EXPECT_EQ(info.functions[0].resource_indices,
            (std::vector<uint32_t>{0}));
  EXPECT_EQ(info.functions[0].function_dependencies,
            (std::vector<std::string>{"helper"}));
  EXPECT_EQ(info.functions[0].shader_kind, 5u);
  EXPECT_EQ(info.functions[0].feature_flags(), 0x0123456789abcdefull);
  EXPECT_EQ(info.functions[0].minimum_expected_wave_lane_count, 4u);
  EXPECT_EQ(info.functions[0].maximum_expected_wave_lane_count, 32u);
  EXPECT_EQ(info.functions[0].shader_flags, 9u);
  EXPECT_FALSE(info.functions[0].has_shader_info);
}

TEST(DxilRuntimeData, RejectsInvalidCoreTableReferencesAndStrides) {
  using namespace dxmt::dxil;
  const auto build = [](uint32_t resource_name_offset,
                        uint32_t resource_index,
                        uint32_t dependency_name_offset,
                        uint32_t shader_info_index) {
    std::vector<uint8_t> strings = {
        't', 'e', 'x', 't', 'u', 'r', 'e', 0,
        'm', 'a', 'i', 'n', 0,
        'h', 'e', 'l', 'p', 'e', 'r', 0,
    };
    std::vector<uint8_t> indices(4 * sizeof(uint32_t));
    Store<uint32_t>(indices, 0, 1u);
    Store<uint32_t>(indices, 4, resource_index);
    Store<uint32_t>(indices, 8, 1u);
    Store<uint32_t>(indices, 12, dependency_name_offset);

    std::vector<uint8_t> resources(kRuntimeDataTableHeaderSize +
                                   kRdatResourceRecordSize);
    Store<uint32_t>(resources, 0, 1u);
    Store<uint32_t>(resources, 4, kRdatResourceRecordSize);
    Store<uint32_t>(resources, kRuntimeDataTableHeaderSize + 24,
                    resource_name_offset);

    std::vector<uint8_t> functions(kRuntimeDataTableHeaderSize +
                                   kRdatFunctionRecord2Size);
    Store<uint32_t>(functions, 0, 1u);
    Store<uint32_t>(functions, 4, kRdatFunctionRecord2Size);
    Store<uint32_t>(functions, kRuntimeDataTableHeaderSize + 0, 8u);
    Store<uint32_t>(functions, kRuntimeDataTableHeaderSize + 4,
                    kRdatNullRef);
    Store<uint32_t>(functions, kRuntimeDataTableHeaderSize + 8, 0u);
    Store<uint32_t>(functions, kRuntimeDataTableHeaderSize + 12, 2u);
    Store<uint32_t>(functions, kRuntimeDataTableHeaderSize + 16, 5u);
    Store<uint32_t>(functions, kRuntimeDataTableHeaderSize + 48,
                    shader_info_index);

    return DxilRuntimeDataBuilder()
        .add(rdat::StringBuffer, strings)
        .add(rdat::IndexArrays, indices)
        .add(rdat::ResourceTable, resources)
        .add(rdat::FunctionTable, functions)
        .build();
  };

  RuntimeDataInfo info;
  const auto expect_invalid = [&](std::string_view case_name,
                                  const std::vector<uint8_t> &bytes) {
    SCOPED_TRACE(case_name);
    EXPECT_EQ(ParseRuntimeData(Part(fourcc::RuntimeData, bytes), info),
              ParseStatus::InvalidRuntimeData);
  };

  expect_invalid("resource name out of bounds",
                 build(100u, 0u, 13u, kRdatNullRef));
  expect_invalid("resource index out of bounds",
                 build(0u, 1u, 13u, kRdatNullRef));
  expect_invalid("dependency name out of bounds",
                 build(0u, 0u, 100u, kRdatNullRef));
  expect_invalid("missing shader info record", build(0u, 0u, 13u, 0u));

  std::vector<uint8_t> short_resources(kRuntimeDataTableHeaderSize + 28);
  Store<uint32_t>(short_resources, 0, 1u);
  Store<uint32_t>(short_resources, 4, 28u);
  expect_invalid(
      "short resource record",
      DxilRuntimeDataBuilder().add(rdat::ResourceTable, short_resources).build());

  std::vector<uint8_t> short_functions(kRuntimeDataTableHeaderSize + 40);
  Store<uint32_t>(short_functions, 0, 1u);
  Store<uint32_t>(short_functions, 4, 40u);
  expect_invalid(
      "short function record",
      DxilRuntimeDataBuilder().add(rdat::FunctionTable, short_functions).build());
}

TEST(DxilRuntimeData, ParsesSignatureComputeAndFunctionTableLinks) {
  using namespace dxmt::dxil;
  std::vector<uint8_t> strings = {
      'T', 'E', 'X', 'C', 'O', 'O', 'R', 'D', 0,
  };
  std::vector<uint8_t> indices(7 * sizeof(uint32_t));
  Store<uint32_t>(indices, 0, 2u);
  Store<uint32_t>(indices, 4, 0u);
  Store<uint32_t>(indices, 8, 1u);
  Store<uint32_t>(indices, 12, 3u);
  Store<uint32_t>(indices, 16, 8u);
  Store<uint32_t>(indices, 20, 4u);
  Store<uint32_t>(indices, 24, 2u);

  constexpr size_t signature_stride = 16;
  std::vector<uint8_t> signatures(kRuntimeDataTableHeaderSize +
                                  signature_stride);
  Store<uint32_t>(signatures, 0, 1u);
  Store<uint32_t>(signatures, 4, signature_stride);
  const size_t signature = kRuntimeDataTableHeaderSize;
  Store<uint32_t>(signatures, signature + 0, 0u);
  Store<uint32_t>(signatures, signature + 4, 0u);
  signatures[signature + 8] = 2;
  signatures[signature + 9] = 3;
  signatures[signature + 10] = 4;
  signatures[signature + 11] = 5;
  signatures[signature + 12] = 0x26;
  signatures[signature + 13] = 0xab;

  std::vector<uint8_t> compute(kRuntimeDataTableHeaderSize +
                               kRdatCSInfoRecordSize);
  Store<uint32_t>(compute, 0, 1u);
  Store<uint32_t>(compute, 4, kRdatCSInfoRecordSize);
  Store<uint32_t>(compute, kRuntimeDataTableHeaderSize + 0, 3u);
  Store<uint32_t>(compute, kRuntimeDataTableHeaderSize + 4, 256u);

  std::vector<uint8_t> functions(kRuntimeDataTableHeaderSize +
                                 kRdatFunctionRecord2Size);
  Store<uint32_t>(functions, 0, 1u);
  Store<uint32_t>(functions, 4, kRdatFunctionRecord2Size);
  for (size_t offset : {size_t(0), size_t(4), size_t(8), size_t(12)})
    Store<uint32_t>(functions, kRuntimeDataTableHeaderSize + offset,
                    kRdatNullRef);
  Store<uint32_t>(functions, kRuntimeDataTableHeaderSize + 16, 5u);
  Store<uint32_t>(functions, kRuntimeDataTableHeaderSize + 48, 0u);

  const auto bytes = DxilRuntimeDataBuilder()
                         .add(rdat::StringBuffer, strings)
                         .add(rdat::IndexArrays, indices)
                         .add(rdat::SignatureElementTable, signatures)
                         .add(rdat::CSInfoTable, compute)
                         .add(rdat::FunctionTable, functions)
                         .build();

  RuntimeDataInfo info;
  ASSERT_EQ(ParseRuntimeData(Part(fourcc::RuntimeData, bytes), info),
            ParseStatus::Ok);
  ASSERT_EQ(info.signature_elements.size(), 1u);
  const auto &element = info.signature_elements[0];
  EXPECT_EQ(element.semantic_name, "TEXCOORD");
  EXPECT_EQ(element.semantic_indices, (std::vector<uint32_t>{0, 1}));
  EXPECT_EQ(element.semantic_kind, 2u);
  EXPECT_EQ(element.component_type, 3u);
  EXPECT_EQ(element.interpolation_mode, 4u);
  EXPECT_EQ(element.start_row, 5u);
  EXPECT_EQ(element.cols, 3u);
  EXPECT_EQ(element.start_col, 1u);
  EXPECT_EQ(element.output_stream, 2u);
  EXPECT_EQ(element.usage_mask, 0xbu);
  EXPECT_EQ(element.dynamic_index_mask, 0xau);

  ASSERT_EQ(info.shader_infos.size(), 1u);
  EXPECT_EQ(info.shader_infos[0].table_type, rdat::CSInfoTable);
  EXPECT_EQ(info.shader_infos[0].num_threads_x, 8u);
  EXPECT_EQ(info.shader_infos[0].num_threads_y, 4u);
  EXPECT_EQ(info.shader_infos[0].num_threads_z, 2u);
  EXPECT_EQ(info.shader_infos[0].group_shared_bytes_used, 256u);
  ASSERT_EQ(info.functions.size(), 1u);
  EXPECT_TRUE(info.functions[0].has_shader_info);
  EXPECT_EQ(info.functions[0].shader_info_table_type, rdat::CSInfoTable);
  EXPECT_EQ(info.functions[0].shader_info_index, 0u);
}

TEST(DxilRuntimeData, RejectsInvalidSignatureAndShaderInfoLinks) {
  using namespace dxmt::dxil;
  std::vector<uint8_t> strings = {'S', 'E', 'M', 0};
  std::vector<uint8_t> indices(4 * sizeof(uint32_t));
  Store<uint32_t>(indices, 0, 1u);
  Store<uint32_t>(indices, 4, 0u);
  Store<uint32_t>(indices, 8, 1u);
  Store<uint32_t>(indices, 12, 1u);

  std::vector<uint8_t> signatures(kRuntimeDataTableHeaderSize + 16);
  Store<uint32_t>(signatures, 0, 1u);
  Store<uint32_t>(signatures, 4, 16u);
  Store<uint32_t>(signatures, kRuntimeDataTableHeaderSize + 0, 0u);
  Store<uint32_t>(signatures, kRuntimeDataTableHeaderSize + 4, 0u);

  std::vector<uint8_t> compute(kRuntimeDataTableHeaderSize +
                               kRdatCSInfoRecordSize);
  Store<uint32_t>(compute, 0, 1u);
  Store<uint32_t>(compute, 4, kRdatCSInfoRecordSize);
  Store<uint32_t>(compute, kRuntimeDataTableHeaderSize + 0, 2u);

  std::vector<uint8_t> functions(kRuntimeDataTableHeaderSize +
                                 kRdatFunctionRecord2Size);
  Store<uint32_t>(functions, 0, 1u);
  Store<uint32_t>(functions, 4, kRdatFunctionRecord2Size);
  for (size_t offset : {size_t(0), size_t(4), size_t(8), size_t(12)})
    Store<uint32_t>(functions, kRuntimeDataTableHeaderSize + offset,
                    kRdatNullRef);
  Store<uint32_t>(functions, kRuntimeDataTableHeaderSize + 16, 5u);
  Store<uint32_t>(functions, kRuntimeDataTableHeaderSize + 48, 0u);

  const auto build = [&](const std::vector<uint8_t> &signature_table,
                         const std::vector<uint8_t> &compute_table,
                         const std::vector<uint8_t> &function_table) {
    return DxilRuntimeDataBuilder()
        .add(rdat::StringBuffer, strings)
        .add(rdat::IndexArrays, indices)
        .add(rdat::SignatureElementTable, signature_table)
        .add(rdat::CSInfoTable, compute_table)
        .add(rdat::FunctionTable, function_table)
        .build();
  };

  RuntimeDataInfo info;
  auto malformed = signatures;
  Store<uint32_t>(malformed, kRuntimeDataTableHeaderSize + 0, 100u);
  EXPECT_EQ(ParseRuntimeData(
                Part(fourcc::RuntimeData, build(malformed, compute, functions)),
                info),
            ParseStatus::InvalidRuntimeData);

  malformed.assign(kRuntimeDataTableHeaderSize + 12, 0);
  Store<uint32_t>(malformed, 0, 1u);
  Store<uint32_t>(malformed, 4, 12u);
  EXPECT_EQ(ParseRuntimeData(
                Part(fourcc::RuntimeData, build(malformed, compute, functions)),
                info),
            ParseStatus::InvalidRuntimeData);

  auto malformed_compute = compute;
  Store<uint32_t>(malformed_compute, kRuntimeDataTableHeaderSize + 0, 100u);
  EXPECT_EQ(ParseRuntimeData(
                Part(fourcc::RuntimeData,
                     build(signatures, malformed_compute, functions)),
                info),
            ParseStatus::InvalidRuntimeData);

  auto malformed_functions = functions;
  Store<uint32_t>(malformed_functions, kRuntimeDataTableHeaderSize + 48, 1u);
  EXPECT_EQ(ParseRuntimeData(
                Part(fourcc::RuntimeData,
                     build(signatures, compute, malformed_functions)),
                info),
            ParseStatus::InvalidRuntimeData);
}

TEST(DxilRuntimeData, ParsesRaytracingSubobjectRecords) {
  using namespace dxmt::dxil;
  std::vector<uint8_t> strings = {
      's', 't', 'a', 't', 'e', 0,
      'r', 'o', 'o', 't', 0,
      'a', 's', 's', 'o', 'c', 0,
      't', 'a', 'r', 'g', 'e', 't', 0,
      'e', 'x', 'p', 'o', 'r', 't', 'A', 0,
      'h', 'i', 't', 0,
      'a', 'n', 'y', 0,
      'c', 'l', 'o', 's', 'e', 's', 't', 0,
      'i', 'n', 't', 'e', 'r', 's', 'e', 'c', 't', 0,
  };
  std::vector<uint8_t> indices(2 * sizeof(uint32_t));
  Store<uint32_t>(indices, 0, 1u);
  Store<uint32_t>(indices, 4, 24u);
  std::vector<uint8_t> raw = {1, 2, 3, 4};

  constexpr size_t record_count = 4;
  std::vector<uint8_t> subobjects(kRuntimeDataTableHeaderSize +
                                  record_count * kRdatSubobjectRecordSize);
  Store<uint32_t>(subobjects, 0, record_count);
  Store<uint32_t>(subobjects, 4, kRdatSubobjectRecordSize);
  size_t record = kRuntimeDataTableHeaderSize;
  Store<uint32_t>(subobjects, record + 0, 0u);
  Store<uint32_t>(subobjects, record + 4, 0u);
  Store<uint32_t>(subobjects, record + 8, 9u);

  record += kRdatSubobjectRecordSize;
  Store<uint32_t>(subobjects, record + 0, 1u);
  Store<uint32_t>(subobjects, record + 4, 6u);
  Store<uint32_t>(subobjects, record + 8, 0u);
  Store<uint32_t>(subobjects, record + 12, raw.size());

  record += kRdatSubobjectRecordSize;
  Store<uint32_t>(subobjects, record + 0, 8u);
  Store<uint32_t>(subobjects, record + 4, 11u);
  Store<uint32_t>(subobjects, record + 8, 17u);
  Store<uint32_t>(subobjects, record + 12, 0u);

  record += kRdatSubobjectRecordSize;
  Store<uint32_t>(subobjects, record + 0, 11u);
  Store<uint32_t>(subobjects, record + 4, 32u);
  Store<uint32_t>(subobjects, record + 8, 2u);
  Store<uint32_t>(subobjects, record + 12, 36u);
  Store<uint32_t>(subobjects, record + 16, 40u);
  Store<uint32_t>(subobjects, record + 20, 48u);

  const auto bytes = DxilRuntimeDataBuilder()
                         .add(rdat::StringBuffer, strings)
                         .add(rdat::IndexArrays, indices)
                         .add(rdat::RawBytes, raw)
                         .add(rdat::SubobjectTable, subobjects)
                         .build();
  RuntimeDataInfo info;
  ASSERT_EQ(ParseRuntimeData(Part(fourcc::RuntimeData, bytes), info),
            ParseStatus::Ok);
  ASSERT_EQ(info.subobjects.size(), record_count);
  EXPECT_EQ(info.subobjects[0].name, "state");
  EXPECT_EQ(info.subobjects[0].state_object_flags, 9u);
  EXPECT_EQ(info.subobjects[1].name, "root");
  EXPECT_TRUE(std::equal(info.subobjects[1].root_signature.begin(),
                         info.subobjects[1].root_signature.end(), raw.begin()));
  EXPECT_EQ(info.subobjects[2].associated_subobject, "target");
  EXPECT_EQ(info.subobjects[2].associated_exports,
            (std::vector<std::string>{"exportA"}));
  EXPECT_EQ(info.subobjects[3].hit_group_type, 2u);
  EXPECT_EQ(info.subobjects[3].any_hit, "any");
  EXPECT_EQ(info.subobjects[3].closest_hit, "closest");
  EXPECT_EQ(info.subobjects[3].intersection, "intersect");
}

TEST(DxilRuntimeData, ParsesRemainingRaytracingSubobjectKinds) {
  using namespace dxmt::dxil;
  constexpr std::array<uint32_t, 5> kinds = {2, 9, 10, 12, 99};
  std::vector<uint8_t> subobjects(
      kRuntimeDataTableHeaderSize + kinds.size() * kRdatSubobjectRecordSize);
  Store<uint32_t>(subobjects, 0, kinds.size());
  Store<uint32_t>(subobjects, 4, kRdatSubobjectRecordSize);
  for (size_t i = 0; i < kinds.size(); ++i) {
    const size_t record =
        kRuntimeDataTableHeaderSize + i * kRdatSubobjectRecordSize;
    Store<uint32_t>(subobjects, record + 0, kinds[i]);
    Store<uint32_t>(subobjects, record + 4, kRdatNullRef);
  }

  size_t record = kRuntimeDataTableHeaderSize;
  Store<uint32_t>(subobjects, record + 8, kRdatNullRef);
  Store<uint32_t>(subobjects, record + 12, 0u);
  record += kRdatSubobjectRecordSize;
  Store<uint32_t>(subobjects, record + 8, 64u);
  Store<uint32_t>(subobjects, record + 12, 32u);
  record += kRdatSubobjectRecordSize;
  Store<uint32_t>(subobjects, record + 8, 7u);
  record += kRdatSubobjectRecordSize;
  Store<uint32_t>(subobjects, record + 8, 9u);
  Store<uint32_t>(subobjects, record + 12, 0xa5u);
  record += kRdatSubobjectRecordSize;
  for (size_t offset : {size_t(8), size_t(12), size_t(16), size_t(20)})
    Store<uint32_t>(subobjects, record + offset, 0xffffffffu);

  const auto bytes = DxilRuntimeDataBuilder()
                         .add(rdat::SubobjectTable, subobjects)
                         .build();
  RuntimeDataInfo info;
  ASSERT_EQ(ParseRuntimeData(Part(fourcc::RuntimeData, bytes), info),
            ParseStatus::Ok);
  ASSERT_EQ(info.subobjects.size(), kinds.size());
  EXPECT_TRUE(info.subobjects[0].root_signature.empty());
  EXPECT_EQ(info.subobjects[1].max_payload_size_in_bytes, 64u);
  EXPECT_EQ(info.subobjects[1].max_attribute_size_in_bytes, 32u);
  EXPECT_EQ(info.subobjects[2].max_trace_recursion_depth, 7u);
  EXPECT_EQ(info.subobjects[3].max_trace_recursion_depth, 9u);
  EXPECT_EQ(info.subobjects[3].raytracing_pipeline_flags, 0xa5u);
  EXPECT_EQ(info.subobjects[4].kind, 99u);
  EXPECT_EQ(info.subobjects[4].state_object_flags, 0u);
  EXPECT_TRUE(info.subobjects[4].root_signature.empty());
}

TEST(DxilRuntimeData, RejectsInvalidSubobjectPayloadReferences) {
  using namespace dxmt::dxil;
  std::vector<uint8_t> strings = {
      'r', 'o', 'o', 't', 0,
      'a', 's', 's', 'o', 'c', 0,
      't', 'a', 'r', 'g', 'e', 't', 0,
      'e', 'x', 'p', 'o', 'r', 't', 0,
      'h', 'i', 't', 0,
  };
  std::vector<uint8_t> indices(2 * sizeof(uint32_t));
  Store<uint32_t>(indices, 0, 1u);
  Store<uint32_t>(indices, 4, 18u);
  std::vector<uint8_t> raw = {1, 2, 3, 4};

  std::vector<uint8_t> subobjects(kRuntimeDataTableHeaderSize +
                                  3 * kRdatSubobjectRecordSize);
  Store<uint32_t>(subobjects, 0, 3u);
  Store<uint32_t>(subobjects, 4, kRdatSubobjectRecordSize);
  size_t record = kRuntimeDataTableHeaderSize;
  Store<uint32_t>(subobjects, record + 0, 1u);
  Store<uint32_t>(subobjects, record + 4, 0u);
  Store<uint32_t>(subobjects, record + 8, 0u);
  Store<uint32_t>(subobjects, record + 12, raw.size());
  record += kRdatSubobjectRecordSize;
  Store<uint32_t>(subobjects, record + 0, 8u);
  Store<uint32_t>(subobjects, record + 4, 5u);
  Store<uint32_t>(subobjects, record + 8, 11u);
  Store<uint32_t>(subobjects, record + 12, 0u);
  record += kRdatSubobjectRecordSize;
  Store<uint32_t>(subobjects, record + 0, 11u);
  Store<uint32_t>(subobjects, record + 4, 25u);
  Store<uint32_t>(subobjects, record + 8, 0u);
  for (size_t offset : {size_t(12), size_t(16), size_t(20)})
    Store<uint32_t>(subobjects, record + offset, kRdatNullRef);

  const auto build = [&](const std::vector<uint8_t> &table) {
    return DxilRuntimeDataBuilder()
        .add(rdat::StringBuffer, strings)
        .add(rdat::IndexArrays, indices)
        .add(rdat::RawBytes, raw)
        .add(rdat::SubobjectTable, table)
        .build();
  };
  RuntimeDataInfo info;

  auto malformed = subobjects;
  Store<uint32_t>(malformed, kRuntimeDataTableHeaderSize + 12, raw.size() + 1);
  EXPECT_EQ(ParseRuntimeData(
                Part(fourcc::RuntimeData, build(malformed)), info),
            ParseStatus::InvalidRuntimeData);

  malformed = subobjects;
  Store<uint32_t>(malformed,
                  kRuntimeDataTableHeaderSize + kRdatSubobjectRecordSize + 12,
                  100u);
  EXPECT_EQ(ParseRuntimeData(
                Part(fourcc::RuntimeData, build(malformed)), info),
            ParseStatus::InvalidRuntimeData);

  malformed = subobjects;
  Store<uint32_t>(malformed,
                  kRuntimeDataTableHeaderSize +
                      2 * kRdatSubobjectRecordSize + 20,
                  100u);
  EXPECT_EQ(ParseRuntimeData(
                Part(fourcc::RuntimeData, build(malformed)), info),
            ParseStatus::InvalidRuntimeData);

  malformed.assign(kRuntimeDataTableHeaderSize + 20, 0);
  Store<uint32_t>(malformed, 0, 1u);
  Store<uint32_t>(malformed, 4, 20u);
  EXPECT_EQ(ParseRuntimeData(
                Part(fourcc::RuntimeData, build(malformed)), info),
            ParseStatus::InvalidRuntimeData);
}

TEST(DxilRuntimeData, ParsesWorkGraphNodeRecords) {
  using namespace dxmt::dxil;
  std::vector<uint8_t> strings = {'n', 'o', 'd', 'e', 0};
  std::vector<uint8_t> indices(12 * sizeof(uint32_t));
  Store<uint32_t>(indices, 0, 3u);
  Store<uint32_t>(indices, 4, 8u);
  Store<uint32_t>(indices, 8, 4u);
  Store<uint32_t>(indices, 12, 2u);
  for (size_t offset : {size_t(16), size_t(24), size_t(32), size_t(40)}) {
    Store<uint32_t>(indices, offset, 1u);
    Store<uint32_t>(indices, offset + 4, 0u);
  }

  std::vector<uint8_t> node_ids(kRuntimeDataTableHeaderSize +
                                kRdatNodeIdRecordSize);
  Store<uint32_t>(node_ids, 0, 1u);
  Store<uint32_t>(node_ids, 4, kRdatNodeIdRecordSize);
  Store<uint32_t>(node_ids, kRuntimeDataTableHeaderSize + 0, 0u);
  Store<uint32_t>(node_ids, kRuntimeDataTableHeaderSize + 4, 7u);

  std::vector<uint8_t> function_attributes(
      kRuntimeDataTableHeaderSize + 2 * kRdatNodeShaderFuncAttribRecordSize);
  Store<uint32_t>(function_attributes, 0, 2u);
  Store<uint32_t>(function_attributes, 4,
                  kRdatNodeShaderFuncAttribRecordSize);
  Store<uint32_t>(function_attributes, kRuntimeDataTableHeaderSize + 0, 1u);
  Store<uint32_t>(function_attributes, kRuntimeDataTableHeaderSize + 4, 0u);
  Store<uint32_t>(function_attributes,
                  kRuntimeDataTableHeaderSize +
                      kRdatNodeShaderFuncAttribRecordSize + 0,
                  2u);
  Store<uint32_t>(function_attributes,
                  kRuntimeDataTableHeaderSize +
                      kRdatNodeShaderFuncAttribRecordSize + 4,
                  0u);

  std::vector<uint8_t> io_attributes(
      kRuntimeDataTableHeaderSize + 2 * kRdatNodeShaderIoAttribRecordSize);
  Store<uint32_t>(io_attributes, 0, 2u);
  Store<uint32_t>(io_attributes, 4, kRdatNodeShaderIoAttribRecordSize);
  Store<uint32_t>(io_attributes, kRuntimeDataTableHeaderSize + 0, 1u);
  Store<uint32_t>(io_attributes, kRuntimeDataTableHeaderSize + 4, 0u);
  const size_t dispatch_attribute =
      kRuntimeDataTableHeaderSize + kRdatNodeShaderIoAttribRecordSize;
  Store<uint32_t>(io_attributes, dispatch_attribute + 0, 5u);
  Store<uint16_t>(io_attributes, dispatch_attribute + 4, 12u);
  Store<uint16_t>(io_attributes, dispatch_attribute + 6, (5u << 2) | 3u);

  std::vector<uint8_t> io_nodes(kRuntimeDataTableHeaderSize +
                                kRdatIoNodeRecordSize);
  Store<uint32_t>(io_nodes, 0, 1u);
  Store<uint32_t>(io_nodes, 4, kRdatIoNodeRecordSize);
  Store<uint32_t>(io_nodes, kRuntimeDataTableHeaderSize + 0, 9u);
  Store<uint32_t>(io_nodes, kRuntimeDataTableHeaderSize + 4, 4u);

  std::vector<uint8_t> shader_nodes(kRuntimeDataTableHeaderSize +
                                    kRdatNodeShaderInfoRecordSize);
  Store<uint32_t>(shader_nodes, 0, 1u);
  Store<uint32_t>(shader_nodes, 4, kRdatNodeShaderInfoRecordSize);
  Store<uint32_t>(shader_nodes, kRuntimeDataTableHeaderSize + 0, 2u);
  Store<uint32_t>(shader_nodes, kRuntimeDataTableHeaderSize + 4, 128u);
  Store<uint32_t>(shader_nodes, kRuntimeDataTableHeaderSize + 8, 6u);
  Store<uint32_t>(shader_nodes, kRuntimeDataTableHeaderSize + 12, 8u);
  Store<uint32_t>(shader_nodes, kRuntimeDataTableHeaderSize + 16, 10u);

  const auto bytes = DxilRuntimeDataBuilder()
                         .add(rdat::StringBuffer, strings)
                         .add(rdat::IndexArrays, indices)
                         .add(rdat::NodeIDTable, node_ids)
                         .add(rdat::NodeShaderFuncAttribTable,
                              function_attributes)
                         .add(rdat::NodeShaderIOAttribTable, io_attributes)
                         .add(rdat::IONodeTable, io_nodes)
                         .add(rdat::NodeShaderInfoTable, shader_nodes)
                         .build();

  RuntimeDataInfo info;
  ASSERT_EQ(ParseRuntimeData(Part(fourcc::RuntimeData, bytes), info),
            ParseStatus::Ok);
  ASSERT_EQ(info.node_ids.size(), 1u);
  EXPECT_EQ(info.node_ids[0].name, "node");
  EXPECT_EQ(info.node_ids[0].index, 7u);
  ASSERT_EQ(info.node_function_attributes.size(), 2u);
  EXPECT_EQ(info.node_function_attributes[0].node_id_index, 0u);
  EXPECT_EQ(info.node_function_attributes[1].values,
            (std::vector<uint32_t>{8, 4, 2}));
  ASSERT_EQ(info.node_io_attributes.size(), 2u);
  EXPECT_EQ(info.node_io_attributes[0].node_id_index, 0u);
  EXPECT_EQ(info.node_io_attributes[1].record_dispatch_grid.byte_offset, 12u);
  EXPECT_EQ(info.node_io_attributes[1].record_dispatch_grid.component_count,
            3u);
  EXPECT_EQ(info.node_io_attributes[1].record_dispatch_grid.component_type,
            5u);
  ASSERT_EQ(info.io_nodes.size(), 1u);
  EXPECT_EQ(info.io_nodes[0].io_flags_and_kind, 9u);
  EXPECT_EQ(info.io_nodes[0].attribute_indices,
            (std::vector<uint32_t>{0}));
  ASSERT_EQ(info.node_shader_infos.size(), 1u);
  EXPECT_EQ(info.node_shader_infos[0].launch_type, 2u);
  EXPECT_EQ(info.node_shader_infos[0].group_shared_bytes_used, 128u);
  EXPECT_EQ(info.node_shader_infos[0].attribute_indices,
            (std::vector<uint32_t>{0}));
  EXPECT_EQ(info.node_shader_infos[0].output_indices,
            (std::vector<uint32_t>{0}));
  EXPECT_EQ(info.node_shader_infos[0].input_indices,
            (std::vector<uint32_t>{0}));
}

TEST(DxilRuntimeData, ParsesEveryWorkGraphAttributeKind) {
  using namespace dxmt::dxil;
  std::vector<uint8_t> indices(4 * sizeof(uint32_t));
  Store<uint32_t>(indices, 0, 3u);
  Store<uint32_t>(indices, 4, 8u);
  Store<uint32_t>(indices, 8, 4u);
  Store<uint32_t>(indices, 12, 2u);

  std::vector<uint8_t> node_ids(kRuntimeDataTableHeaderSize +
                                kRdatNodeIdRecordSize);
  Store<uint32_t>(node_ids, 0, 1u);
  Store<uint32_t>(node_ids, 4, kRdatNodeIdRecordSize);
  Store<uint32_t>(node_ids, kRuntimeDataTableHeaderSize + 0, kRdatNullRef);
  Store<uint32_t>(node_ids, kRuntimeDataTableHeaderSize + 4, 11u);

  constexpr size_t function_attribute_count = 10;
  std::vector<uint8_t> function_attributes(
      kRuntimeDataTableHeaderSize +
      function_attribute_count * kRdatNodeShaderFuncAttribRecordSize);
  Store<uint32_t>(function_attributes, 0, function_attribute_count);
  Store<uint32_t>(function_attributes, 4,
                  kRdatNodeShaderFuncAttribRecordSize);
  for (size_t i = 0; i < function_attribute_count; ++i) {
    const uint32_t kind = i < 9 ? uint32_t(i + 1) : 99u;
    const size_t record = kRuntimeDataTableHeaderSize +
                          i * kRdatNodeShaderFuncAttribRecordSize;
    Store<uint32_t>(function_attributes, record + 0, kind);
    Store<uint32_t>(function_attributes, record + 4, 100u + kind);
  }
  for (size_t i : {size_t(0), size_t(1), size_t(2), size_t(3), size_t(6)}) {
    const size_t record = kRuntimeDataTableHeaderSize +
                          i * kRdatNodeShaderFuncAttribRecordSize;
    Store<uint32_t>(function_attributes, record + 4, 0u);
  }

  constexpr size_t io_attribute_count = 9;
  std::vector<uint8_t> io_attributes(
      kRuntimeDataTableHeaderSize +
      io_attribute_count * kRdatNodeShaderIoAttribRecordSize);
  Store<uint32_t>(io_attributes, 0, io_attribute_count);
  Store<uint32_t>(io_attributes, 4, kRdatNodeShaderIoAttribRecordSize);
  for (size_t i = 0; i < io_attribute_count; ++i) {
    const uint32_t kind = i < 8 ? uint32_t(i + 1) : 99u;
    const size_t record = kRuntimeDataTableHeaderSize +
                          i * kRdatNodeShaderIoAttribRecordSize;
    Store<uint32_t>(io_attributes, record + 0, kind);
    Store<uint32_t>(io_attributes, record + 4, 200u + kind);
  }
  Store<uint32_t>(io_attributes, kRuntimeDataTableHeaderSize + 4, 0u);
  const size_t dispatch_record =
      kRuntimeDataTableHeaderSize + 4 * kRdatNodeShaderIoAttribRecordSize;
  Store<uint16_t>(io_attributes, dispatch_record + 4, 12u);
  Store<uint16_t>(io_attributes, dispatch_record + 6, (5u << 2) | 3u);

  const auto bytes =
      DxilRuntimeDataBuilder()
          .add(rdat::IndexArrays, indices)
          .add(rdat::NodeIDTable, node_ids)
          .add(rdat::NodeShaderFuncAttribTable, function_attributes)
          .add(rdat::NodeShaderIOAttribTable, io_attributes)
          .build();
  RuntimeDataInfo info;
  ASSERT_EQ(ParseRuntimeData(Part(fourcc::RuntimeData, bytes), info),
            ParseStatus::Ok);

  ASSERT_EQ(info.node_function_attributes.size(), function_attribute_count);
  EXPECT_EQ(info.node_function_attributes[0].node_id_index, 0u);
  EXPECT_EQ(info.node_function_attributes[1].values,
            (std::vector<uint32_t>{8, 4, 2}));
  EXPECT_EQ(info.node_function_attributes[2].node_id_index, 0u);
  EXPECT_EQ(info.node_function_attributes[3].values,
            (std::vector<uint32_t>{8, 4, 2}));
  EXPECT_EQ(info.node_function_attributes[4].value, 105u);
  EXPECT_EQ(info.node_function_attributes[5].value, 106u);
  EXPECT_EQ(info.node_function_attributes[6].values,
            (std::vector<uint32_t>{8, 4, 2}));
  EXPECT_EQ(info.node_function_attributes[7].value, 108u);
  EXPECT_EQ(info.node_function_attributes[8].value, 109u);
  EXPECT_EQ(info.node_function_attributes[9].kind, 99u);
  EXPECT_EQ(info.node_function_attributes[9].value, 0u);

  ASSERT_EQ(info.node_io_attributes.size(), io_attribute_count);
  EXPECT_EQ(info.node_io_attributes[0].node_id_index, 0u);
  EXPECT_EQ(info.node_io_attributes[1].value, 202u);
  EXPECT_EQ(info.node_io_attributes[2].value, 203u);
  EXPECT_EQ(info.node_io_attributes[3].value, 204u);
  EXPECT_EQ(info.node_io_attributes[4].record_dispatch_grid.byte_offset, 12u);
  EXPECT_EQ(info.node_io_attributes[4].record_dispatch_grid.component_count,
            3u);
  EXPECT_EQ(info.node_io_attributes[4].record_dispatch_grid.component_type,
            5u);
  EXPECT_EQ(info.node_io_attributes[5].value, 206u);
  EXPECT_EQ(info.node_io_attributes[6].value, 207u);
  EXPECT_EQ(info.node_io_attributes[7].value, 208u);
  EXPECT_EQ(info.node_io_attributes[8].kind, 99u);
  EXPECT_EQ(info.node_io_attributes[8].value, 0u);
}

TEST(DxilRuntimeData, RejectsInvalidWorkGraphNodeLinks) {
  using namespace dxmt::dxil;
  std::vector<uint8_t> strings = {'n', 'o', 'd', 'e', 0};
  std::vector<uint8_t> indices(8 * sizeof(uint32_t));
  for (size_t offset : {size_t(0), size_t(8), size_t(16), size_t(24)}) {
    Store<uint32_t>(indices, offset, 1u);
    Store<uint32_t>(indices, offset + 4, 0u);
  }

  std::vector<uint8_t> node_ids(kRuntimeDataTableHeaderSize +
                                kRdatNodeIdRecordSize);
  Store<uint32_t>(node_ids, 0, 1u);
  Store<uint32_t>(node_ids, 4, kRdatNodeIdRecordSize);
  Store<uint32_t>(node_ids, kRuntimeDataTableHeaderSize + 0, 0u);

  std::vector<uint8_t> function_attributes(
      kRuntimeDataTableHeaderSize + kRdatNodeShaderFuncAttribRecordSize);
  Store<uint32_t>(function_attributes, 0, 1u);
  Store<uint32_t>(function_attributes, 4,
                  kRdatNodeShaderFuncAttribRecordSize);
  Store<uint32_t>(function_attributes, kRuntimeDataTableHeaderSize + 0, 1u);
  Store<uint32_t>(function_attributes, kRuntimeDataTableHeaderSize + 4, 0u);

  std::vector<uint8_t> io_attributes(
      kRuntimeDataTableHeaderSize + kRdatNodeShaderIoAttribRecordSize);
  Store<uint32_t>(io_attributes, 0, 1u);
  Store<uint32_t>(io_attributes, 4, kRdatNodeShaderIoAttribRecordSize);
  Store<uint32_t>(io_attributes, kRuntimeDataTableHeaderSize + 0, 1u);
  Store<uint32_t>(io_attributes, kRuntimeDataTableHeaderSize + 4, 0u);

  std::vector<uint8_t> io_nodes(kRuntimeDataTableHeaderSize +
                                kRdatIoNodeRecordSize);
  Store<uint32_t>(io_nodes, 0, 1u);
  Store<uint32_t>(io_nodes, 4, kRdatIoNodeRecordSize);
  Store<uint32_t>(io_nodes, kRuntimeDataTableHeaderSize + 4, 1u);

  std::vector<uint8_t> shader_nodes(kRuntimeDataTableHeaderSize +
                                    kRdatNodeShaderInfoRecordSize);
  Store<uint32_t>(shader_nodes, 0, 1u);
  Store<uint32_t>(shader_nodes, 4, kRdatNodeShaderInfoRecordSize);
  Store<uint32_t>(shader_nodes, kRuntimeDataTableHeaderSize + 8, 2u);
  Store<uint32_t>(shader_nodes, kRuntimeDataTableHeaderSize + 12, 3u);
  Store<uint32_t>(shader_nodes, kRuntimeDataTableHeaderSize + 16, 3u);

  const auto build = [&](const std::vector<uint8_t> &ids,
                         const std::vector<uint8_t> &function_attrs,
                         const std::vector<uint8_t> &io_attrs,
                         const std::vector<uint8_t> &nodes,
                         const std::vector<uint8_t> &shader_info) {
    return DxilRuntimeDataBuilder()
        .add(rdat::StringBuffer, strings)
        .add(rdat::IndexArrays, indices)
        .add(rdat::NodeIDTable, ids)
        .add(rdat::NodeShaderFuncAttribTable, function_attrs)
        .add(rdat::NodeShaderIOAttribTable, io_attrs)
        .add(rdat::IONodeTable, nodes)
        .add(rdat::NodeShaderInfoTable, shader_info)
        .build();
  };

  RuntimeDataInfo info;
  auto malformed_ids = node_ids;
  Store<uint32_t>(malformed_ids, kRuntimeDataTableHeaderSize, 100u);
  EXPECT_EQ(ParseRuntimeData(
                Part(fourcc::RuntimeData,
                     build(malformed_ids, function_attributes, io_attributes,
                           io_nodes, shader_nodes)),
                info),
            ParseStatus::InvalidRuntimeData);

  auto malformed_functions = function_attributes;
  Store<uint32_t>(malformed_functions, kRuntimeDataTableHeaderSize + 4, 1u);
  EXPECT_EQ(ParseRuntimeData(
                Part(fourcc::RuntimeData,
                     build(node_ids, malformed_functions, io_attributes,
                           io_nodes, shader_nodes)),
                info),
            ParseStatus::InvalidRuntimeData);

  auto malformed_io = io_attributes;
  Store<uint32_t>(malformed_io, kRuntimeDataTableHeaderSize + 4, 1u);
  EXPECT_EQ(ParseRuntimeData(
                Part(fourcc::RuntimeData,
                     build(node_ids, function_attributes, malformed_io,
                           io_nodes, shader_nodes)),
                info),
            ParseStatus::InvalidRuntimeData);

  auto malformed_indices = indices;
  Store<uint32_t>(malformed_indices, 12, 1u);
  const auto invalid_index_bytes = DxilRuntimeDataBuilder()
                                       .add(rdat::StringBuffer, strings)
                                       .add(rdat::IndexArrays,
                                            malformed_indices)
                                       .add(rdat::NodeIDTable, node_ids)
                                       .add(rdat::NodeShaderFuncAttribTable,
                                            function_attributes)
                                       .add(rdat::NodeShaderIOAttribTable,
                                            io_attributes)
                                       .add(rdat::IONodeTable, io_nodes)
                                       .add(rdat::NodeShaderInfoTable,
                                            shader_nodes)
                                       .build();
  EXPECT_EQ(ParseRuntimeData(
                Part(fourcc::RuntimeData, invalid_index_bytes), info),
            ParseStatus::InvalidRuntimeData);
}

TEST(DxilRuntimeData, ParsesPdbSourceLibraryAndInfoRecords) {
  using namespace dxmt::dxil;
  std::vector<uint8_t> strings = {
      's', 'r', 'c', '.', 'h', 0,
      'c', 'o', 'd', 'e', 0,
      'l', 'i', 'b', '.', 'd', 'x', 'i', 'l', 0,
      '-', 'O', '3', 0,
      '1', 0,
      's', 'h', 'a', 'd', 'e', 'r', '.', 'p', 'd', 'b', 0,
  };
  std::vector<uint8_t> indices(7 * sizeof(uint32_t));
  Store<uint32_t>(indices, 0, 1u);
  Store<uint32_t>(indices, 4, 0u);
  Store<uint32_t>(indices, 8, 1u);
  Store<uint32_t>(indices, 12, 0u);
  Store<uint32_t>(indices, 16, 2u);
  Store<uint32_t>(indices, 20, 20u);
  Store<uint32_t>(indices, 24, 24u);
  std::vector<uint8_t> raw = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};

  std::vector<uint8_t> sources(kRuntimeDataTableHeaderSize +
                               kRdatPdbInfoSourceRecordSize);
  Store<uint32_t>(sources, 0, 1u);
  Store<uint32_t>(sources, 4, kRdatPdbInfoSourceRecordSize);
  Store<uint32_t>(sources, kRuntimeDataTableHeaderSize + 0, 0u);
  Store<uint32_t>(sources, kRuntimeDataTableHeaderSize + 4, 6u);

  std::vector<uint8_t> libraries(kRuntimeDataTableHeaderSize +
                                 kRdatPdbInfoLibraryRecordSize);
  Store<uint32_t>(libraries, 0, 1u);
  Store<uint32_t>(libraries, 4, kRdatPdbInfoLibraryRecordSize);
  Store<uint32_t>(libraries, kRuntimeDataTableHeaderSize + 0, 11u);
  Store<uint32_t>(libraries, kRuntimeDataTableHeaderSize + 4, 0u);
  Store<uint32_t>(libraries, kRuntimeDataTableHeaderSize + 8, 3u);

  std::vector<uint8_t> pdb_infos(kRuntimeDataTableHeaderSize +
                                 kRdatPdbInfoRecordSize);
  Store<uint32_t>(pdb_infos, 0, 1u);
  Store<uint32_t>(pdb_infos, 4, kRdatPdbInfoRecordSize);
  const size_t pdb = kRuntimeDataTableHeaderSize;
  Store<uint32_t>(pdb_infos, pdb + 0, 0u);
  Store<uint32_t>(pdb_infos, pdb + 4, 2u);
  Store<uint32_t>(pdb_infos, pdb + 8, 4u);
  Store<uint32_t>(pdb_infos, pdb + 12, 3u);
  Store<uint32_t>(pdb_infos, pdb + 16, 4u);
  Store<uint32_t>(pdb_infos, pdb + 20, 26u);
  Store<uint32_t>(pdb_infos, pdb + 24, 42u);
  Store<uint32_t>(pdb_infos, pdb + 28, 7u);
  Store<uint32_t>(pdb_infos, pdb + 32, 2u);
  Store<uint32_t>(pdb_infos, pdb + 36, 9u);
  Store<uint32_t>(pdb_infos, pdb + 40, 3u);

  const auto bytes = DxilRuntimeDataBuilder()
                         .add(rdat::StringBuffer, strings)
                         .add(rdat::IndexArrays, indices)
                         .add(rdat::RawBytes, raw)
                         .add(rdat::DxilPdbInfoSourceTable, sources)
                         .add(rdat::DxilPdbInfoLibraryTable, libraries)
                         .add(rdat::DxilPdbInfoTable, pdb_infos)
                         .build();
  RuntimeDataInfo info;
  ASSERT_EQ(ParseRuntimeData(Part(fourcc::RuntimeData, bytes), info),
            ParseStatus::Ok);
  ASSERT_EQ(info.pdb_sources.size(), 1u);
  EXPECT_EQ(info.pdb_sources[0].name, "src.h");
  EXPECT_EQ(info.pdb_sources[0].content, "code");
  ASSERT_EQ(info.pdb_libraries.size(), 1u);
  EXPECT_EQ(info.pdb_libraries[0].name, "lib.dxil");
  EXPECT_TRUE(std::equal(info.pdb_libraries[0].data.begin(),
                         info.pdb_libraries[0].data.end(), raw.begin()));
  ASSERT_EQ(info.pdb_infos.size(), 1u);
  EXPECT_EQ(info.pdb_infos[0].source_indices,
            (std::vector<uint32_t>{0}));
  EXPECT_EQ(info.pdb_infos[0].library_indices,
            (std::vector<uint32_t>{0}));
  EXPECT_EQ(info.pdb_infos[0].arg_pairs,
            (std::vector<std::string>{"-O3", "1"}));
  EXPECT_EQ(info.pdb_infos[0].pdb_name, "shader.pdb");
  EXPECT_EQ(info.pdb_infos[0].custom_toolchain_id, 42u);
  EXPECT_TRUE(std::equal(info.pdb_infos[0].hash.begin(),
                         info.pdb_infos[0].hash.end(), raw.begin() + 3));
  EXPECT_TRUE(std::equal(info.pdb_infos[0].custom_toolchain_data.begin(),
                         info.pdb_infos[0].custom_toolchain_data.end(),
                         raw.begin() + 7));
  EXPECT_TRUE(std::equal(info.pdb_infos[0].whole_dxil.begin(),
                         info.pdb_infos[0].whole_dxil.end(), raw.begin() + 9));
}

TEST(DxilRuntimeData, RejectsInvalidPdbRecordReferences) {
  using namespace dxmt::dxil;
  std::vector<uint8_t> strings = {
      's', 0, 'c', 0, 'l', 0, 'a', 0, 'p', 0,
  };
  std::vector<uint8_t> indices(7 * sizeof(uint32_t));
  Store<uint32_t>(indices, 0, 1u);
  Store<uint32_t>(indices, 4, 0u);
  Store<uint32_t>(indices, 8, 1u);
  Store<uint32_t>(indices, 12, 0u);
  Store<uint32_t>(indices, 16, 2u);
  Store<uint32_t>(indices, 20, 6u);
  Store<uint32_t>(indices, 24, 8u);
  std::vector<uint8_t> raw = {1, 2, 3, 4};

  std::vector<uint8_t> sources(kRuntimeDataTableHeaderSize +
                               kRdatPdbInfoSourceRecordSize);
  Store<uint32_t>(sources, 0, 1u);
  Store<uint32_t>(sources, 4, kRdatPdbInfoSourceRecordSize);
  Store<uint32_t>(sources, kRuntimeDataTableHeaderSize + 0, 0u);
  Store<uint32_t>(sources, kRuntimeDataTableHeaderSize + 4, 2u);

  std::vector<uint8_t> libraries(kRuntimeDataTableHeaderSize +
                                 kRdatPdbInfoLibraryRecordSize);
  Store<uint32_t>(libraries, 0, 1u);
  Store<uint32_t>(libraries, 4, kRdatPdbInfoLibraryRecordSize);
  Store<uint32_t>(libraries, kRuntimeDataTableHeaderSize + 0, 4u);
  Store<uint32_t>(libraries, kRuntimeDataTableHeaderSize + 4, 0u);
  Store<uint32_t>(libraries, kRuntimeDataTableHeaderSize + 8, raw.size());

  std::vector<uint8_t> pdb_infos(kRuntimeDataTableHeaderSize +
                                 kRdatPdbInfoRecordSize);
  Store<uint32_t>(pdb_infos, 0, 1u);
  Store<uint32_t>(pdb_infos, 4, kRdatPdbInfoRecordSize);
  Store<uint32_t>(pdb_infos, kRuntimeDataTableHeaderSize + 0, 0u);
  Store<uint32_t>(pdb_infos, kRuntimeDataTableHeaderSize + 4, 2u);
  Store<uint32_t>(pdb_infos, kRuntimeDataTableHeaderSize + 8, 4u);
  Store<uint32_t>(pdb_infos, kRuntimeDataTableHeaderSize + 12, 0u);
  Store<uint32_t>(pdb_infos, kRuntimeDataTableHeaderSize + 16, raw.size());
  Store<uint32_t>(pdb_infos, kRuntimeDataTableHeaderSize + 20, 8u);

  const auto build = [&](const std::vector<uint8_t> &index_data,
                         const std::vector<uint8_t> &source_table,
                         const std::vector<uint8_t> &library_table,
                         const std::vector<uint8_t> &pdb_table) {
    return DxilRuntimeDataBuilder()
        .add(rdat::StringBuffer, strings)
        .add(rdat::IndexArrays, index_data)
        .add(rdat::RawBytes, raw)
        .add(rdat::DxilPdbInfoSourceTable, source_table)
        .add(rdat::DxilPdbInfoLibraryTable, library_table)
        .add(rdat::DxilPdbInfoTable, pdb_table)
        .build();
  };
  RuntimeDataInfo info;

  auto malformed_sources = sources;
  Store<uint32_t>(malformed_sources, kRuntimeDataTableHeaderSize + 4, 100u);
  EXPECT_EQ(ParseRuntimeData(
                Part(fourcc::RuntimeData,
                     build(indices, malformed_sources, libraries, pdb_infos)),
                info),
            ParseStatus::InvalidRuntimeData);

  auto malformed_libraries = libraries;
  Store<uint32_t>(malformed_libraries, kRuntimeDataTableHeaderSize + 8,
                  raw.size() + 1);
  EXPECT_EQ(ParseRuntimeData(
                Part(fourcc::RuntimeData,
                     build(indices, sources, malformed_libraries, pdb_infos)),
                info),
            ParseStatus::InvalidRuntimeData);

  auto malformed_indices = indices;
  Store<uint32_t>(malformed_indices, 4, 1u);
  EXPECT_EQ(ParseRuntimeData(
                Part(fourcc::RuntimeData,
                     build(malformed_indices, sources, libraries, pdb_infos)),
                info),
            ParseStatus::InvalidRuntimeData);

  malformed_indices = indices;
  Store<uint32_t>(malformed_indices, 20, 100u);
  EXPECT_EQ(ParseRuntimeData(
                Part(fourcc::RuntimeData,
                     build(malformed_indices, sources, libraries, pdb_infos)),
                info),
            ParseStatus::InvalidRuntimeData);

  auto malformed_pdb = pdb_infos;
  Store<uint32_t>(malformed_pdb, kRuntimeDataTableHeaderSize + 16,
                  raw.size() + 1);
  EXPECT_EQ(ParseRuntimeData(
                Part(fourcc::RuntimeData,
                     build(indices, sources, libraries, malformed_pdb)),
                info),
            ParseStatus::InvalidRuntimeData);
}

TEST(DxilRuntimeData, ParsesEveryGraphicsAndMeshShaderInfoTable) {
  using namespace dxmt::dxil;
  const auto make_table = [](size_t stride) {
    std::vector<uint8_t> table(kRuntimeDataTableHeaderSize + stride);
    Store<uint32_t>(table, 0, 1u);
    Store<uint32_t>(table, 4, stride);
    return table;
  };

  auto vertex = make_table(kRdatVSInfoRecordSize);
  for (size_t offset : {size_t(0), size_t(4), size_t(8)})
    Store<uint32_t>(vertex, kRuntimeDataTableHeaderSize + offset,
                    kRdatNullRef);

  auto pixel = make_table(kRdatPSInfoRecordSize);
  Store<uint32_t>(pixel, kRuntimeDataTableHeaderSize + 0, kRdatNullRef);
  Store<uint32_t>(pixel, kRuntimeDataTableHeaderSize + 4, kRdatNullRef);

  auto hull = make_table(kRdatHSInfoRecordSize);
  for (size_t offset : {size_t(0), size_t(4), size_t(8), size_t(12),
                        size_t(20), size_t(28), size_t(36)})
    Store<uint32_t>(hull, kRuntimeDataTableHeaderSize + offset, kRdatNullRef);
  hull[kRuntimeDataTableHeaderSize + 44] = 3;
  hull[kRuntimeDataTableHeaderSize + 45] = 4;
  hull[kRuntimeDataTableHeaderSize + 46] = 5;
  hull[kRuntimeDataTableHeaderSize + 47] = 6;

  constexpr size_t domain_stride = 40;
  auto domain = make_table(domain_stride);
  for (size_t offset : {size_t(0), size_t(4), size_t(8), size_t(12),
                        size_t(20), size_t(28)})
    Store<uint32_t>(domain, kRuntimeDataTableHeaderSize + offset,
                    kRdatNullRef);
  domain[kRuntimeDataTableHeaderSize + 36] = 7;
  domain[kRuntimeDataTableHeaderSize + 37] = 8;

  auto geometry = make_table(kRdatGSInfoRecordSize);
  for (size_t offset : {size_t(0), size_t(4), size_t(8), size_t(16)})
    Store<uint32_t>(geometry, kRuntimeDataTableHeaderSize + offset,
                    kRdatNullRef);
  geometry[kRuntimeDataTableHeaderSize + 24] = 9;
  geometry[kRuntimeDataTableHeaderSize + 25] = 10;
  geometry[kRuntimeDataTableHeaderSize + 26] = 11;
  geometry[kRuntimeDataTableHeaderSize + 27] = 12;

  constexpr size_t mesh_stride = 48;
  auto mesh = make_table(mesh_stride);
  for (size_t offset : {size_t(0), size_t(4), size_t(8), size_t(16),
                        size_t(24)})
    Store<uint32_t>(mesh, kRuntimeDataTableHeaderSize + offset,
                    kRdatNullRef);
  Store<uint32_t>(mesh, kRuntimeDataTableHeaderSize + 28, 128u);
  Store<uint32_t>(mesh, kRuntimeDataTableHeaderSize + 32, 64u);
  Store<uint32_t>(mesh, kRuntimeDataTableHeaderSize + 36, 32u);
  Store<uint16_t>(mesh, kRuntimeDataTableHeaderSize + 40, 16u);
  Store<uint16_t>(mesh, kRuntimeDataTableHeaderSize + 42, 8u);
  mesh[kRuntimeDataTableHeaderSize + 44] = 13;

  auto amplification = make_table(kRdatASInfoRecordSize);
  Store<uint32_t>(amplification, kRuntimeDataTableHeaderSize + 0,
                  kRdatNullRef);
  Store<uint32_t>(amplification, kRuntimeDataTableHeaderSize + 4, 256u);
  Store<uint32_t>(amplification, kRuntimeDataTableHeaderSize + 8, 512u);

  const auto bytes = DxilRuntimeDataBuilder()
                         .add(rdat::VSInfoTable, vertex)
                         .add(rdat::PSInfoTable, pixel)
                         .add(rdat::HSInfoTable, hull)
                         .add(rdat::DSInfoTable, domain)
                         .add(rdat::GSInfoTable, geometry)
                         .add(rdat::MSInfoTable, mesh)
                         .add(rdat::ASInfoTable, amplification)
                         .build();
  RuntimeDataInfo info;
  ASSERT_EQ(ParseRuntimeData(Part(fourcc::RuntimeData, bytes), info),
            ParseStatus::Ok);
  ASSERT_EQ(info.shader_infos.size(), 7u);

  const auto *vs = info.findShaderInfo(rdat::VSInfoTable, 0);
  ASSERT_NE(vs, nullptr);
  EXPECT_TRUE(vs->input_signature_indices.empty());
  EXPECT_TRUE(vs->output_signature_indices.empty());

  const auto *ps = info.findShaderInfo(rdat::PSInfoTable, 0);
  ASSERT_NE(ps, nullptr);
  EXPECT_TRUE(ps->input_signature_indices.empty());
  EXPECT_TRUE(ps->output_signature_indices.empty());

  const auto *hs = info.findShaderInfo(rdat::HSInfoTable, 0);
  ASSERT_NE(hs, nullptr);
  EXPECT_EQ(hs->input_control_point_count, 3u);
  EXPECT_EQ(hs->output_control_point_count, 4u);
  EXPECT_EQ(hs->tessellator_domain, 5u);
  EXPECT_EQ(hs->tessellator_output_primitive, 6u);

  const auto *ds = info.findShaderInfo(rdat::DSInfoTable, 0);
  ASSERT_NE(ds, nullptr);
  EXPECT_EQ(ds->input_control_point_count, 7u);
  EXPECT_EQ(ds->tessellator_domain, 8u);

  const auto *gs = info.findShaderInfo(rdat::GSInfoTable, 0);
  ASSERT_NE(gs, nullptr);
  EXPECT_EQ(gs->input_primitive, 9u);
  EXPECT_EQ(gs->output_topology, 10u);
  EXPECT_EQ(gs->max_vertex_count, 11u);
  EXPECT_EQ(gs->output_stream_mask, 12u);

  const auto *ms = info.findShaderInfo(rdat::MSInfoTable, 0);
  ASSERT_NE(ms, nullptr);
  EXPECT_EQ(ms->num_threads_x, 1u);
  EXPECT_EQ(ms->num_threads_y, 1u);
  EXPECT_EQ(ms->num_threads_z, 1u);
  EXPECT_EQ(ms->group_shared_bytes_used, 128u);
  EXPECT_EQ(ms->group_shared_bytes_dependent_on_view_id, 64u);
  EXPECT_EQ(ms->payload_size_in_bytes, 32u);
  EXPECT_EQ(ms->max_output_vertices, 16u);
  EXPECT_EQ(ms->max_output_primitives, 8u);
  EXPECT_EQ(ms->mesh_output_topology, 13u);

  const auto *as = info.findShaderInfo(rdat::ASInfoTable, 0);
  ASSERT_NE(as, nullptr);
  EXPECT_EQ(as->num_threads_x, 1u);
  EXPECT_EQ(as->group_shared_bytes_used, 256u);
  EXPECT_EQ(as->payload_size_in_bytes, 512u);
  EXPECT_EQ(info.findShaderInfo(rdat::CSInfoTable, 0), nullptr);
}

TEST(DxilRuntimeData, RejectsShortStageSpecificShaderRecords) {
  using namespace dxmt::dxil;
  const auto expect_invalid = [](uint32_t table_type, size_t stride) {
    std::vector<uint8_t> table(kRuntimeDataTableHeaderSize + stride);
    Store<uint32_t>(table, 0, 1u);
    Store<uint32_t>(table, 4, stride);
    const auto bytes =
        DxilRuntimeDataBuilder().add(table_type, table).build();
    RuntimeDataInfo info;
    EXPECT_EQ(ParseRuntimeData(Part(fourcc::RuntimeData, bytes), info),
              ParseStatus::InvalidRuntimeData);
  };

  expect_invalid(rdat::VSInfoTable, kRdatVSInfoRecordSize - 1);
  expect_invalid(rdat::PSInfoTable, kRdatPSInfoRecordSize - 1);
  expect_invalid(rdat::HSInfoTable, kRdatHSInfoRecordSize - 1);
  expect_invalid(rdat::DSInfoTable, kRdatDSInfoRecordSize - 1);
  expect_invalid(rdat::GSInfoTable, kRdatGSInfoRecordSize - 1);
  expect_invalid(rdat::CSInfoTable, kRdatCSInfoRecordSize - 1);
  expect_invalid(rdat::MSInfoTable, 44u);
  expect_invalid(rdat::ASInfoTable, kRdatASInfoRecordSize - 1);
}

TEST(DxilRuntimeData, LinksEveryFunctionShaderKindToItsInfoTable) {
  using namespace dxmt::dxil;
  const auto make_table = [](size_t stride) {
    std::vector<uint8_t> table(kRuntimeDataTableHeaderSize + stride);
    Store<uint32_t>(table, 0, 1u);
    Store<uint32_t>(table, 4, stride);
    return table;
  };
  const auto set_null_refs = [](std::vector<uint8_t> &table,
                                std::initializer_list<size_t> offsets) {
    for (const auto offset : offsets)
      Store<uint32_t>(table, kRuntimeDataTableHeaderSize + offset,
                      kRdatNullRef);
  };

  auto vertex = make_table(kRdatVSInfoRecordSize);
  set_null_refs(vertex, {0, 4, 8});
  auto pixel = make_table(kRdatPSInfoRecordSize);
  set_null_refs(pixel, {0, 4});
  auto hull = make_table(kRdatHSInfoRecordSize);
  set_null_refs(hull, {0, 4, 8, 12, 20, 28, 36});
  auto domain = make_table(40);
  set_null_refs(domain, {0, 4, 8, 12, 20, 28});
  auto geometry = make_table(kRdatGSInfoRecordSize);
  set_null_refs(geometry, {0, 4, 8, 16});
  auto compute = make_table(kRdatCSInfoRecordSize);
  set_null_refs(compute, {0});
  auto mesh = make_table(48);
  set_null_refs(mesh, {0, 4, 8, 16, 24});
  auto amplification = make_table(kRdatASInfoRecordSize);
  set_null_refs(amplification, {0});
  auto node = make_table(kRdatNodeShaderInfoRecordSize);
  set_null_refs(node, {8, 12, 16});

  constexpr std::array<uint32_t, 9> shader_kinds = {0, 1, 2, 3, 4,
                                                    5, 13, 14, 15};
  constexpr std::array<uint32_t, 9> table_types = {
      rdat::PSInfoTable, rdat::VSInfoTable, rdat::GSInfoTable,
      rdat::HSInfoTable, rdat::DSInfoTable, rdat::CSInfoTable,
      rdat::MSInfoTable, rdat::ASInfoTable, rdat::NodeShaderInfoTable,
  };
  constexpr size_t function_count = shader_kinds.size() + 2;
  std::vector<uint8_t> functions(
      kRuntimeDataTableHeaderSize + function_count * kRdatFunctionRecord2Size);
  Store<uint32_t>(functions, 0, function_count);
  Store<uint32_t>(functions, 4, kRdatFunctionRecord2Size);
  for (size_t i = 0; i < function_count; ++i) {
    const size_t record =
        kRuntimeDataTableHeaderSize + i * kRdatFunctionRecord2Size;
    for (size_t offset : {size_t(0), size_t(4), size_t(8), size_t(12)})
      Store<uint32_t>(functions, record + offset, kRdatNullRef);
    Store<uint32_t>(functions, record + 48, 0u);
  }
  for (size_t i = 0; i < shader_kinds.size(); ++i) {
    const size_t record =
        kRuntimeDataTableHeaderSize + i * kRdatFunctionRecord2Size;
    Store<uint32_t>(functions, record + 16, shader_kinds[i]);
  }
  const size_t library_record = kRuntimeDataTableHeaderSize +
                                shader_kinds.size() * kRdatFunctionRecord2Size;
  Store<uint32_t>(functions, library_record + 16, 6u);
  const size_t null_record = library_record + kRdatFunctionRecord2Size;
  Store<uint32_t>(functions, null_record + 16, 0u);
  Store<uint32_t>(functions, null_record + 48, kRdatNullRef);

  const auto build = [&](const std::vector<uint8_t> &function_table) {
    return DxilRuntimeDataBuilder()
        .add(rdat::VSInfoTable, vertex)
        .add(rdat::PSInfoTable, pixel)
        .add(rdat::HSInfoTable, hull)
        .add(rdat::DSInfoTable, domain)
        .add(rdat::GSInfoTable, geometry)
        .add(rdat::CSInfoTable, compute)
        .add(rdat::MSInfoTable, mesh)
        .add(rdat::ASInfoTable, amplification)
        .add(rdat::NodeShaderInfoTable, node)
        .add(rdat::FunctionTable, function_table)
        .build();
  };

  auto bytes = build(functions);
  RuntimeDataInfo info;
  ASSERT_EQ(ParseRuntimeData(Part(fourcc::RuntimeData, bytes), info),
            ParseStatus::Ok);
  ASSERT_EQ(info.functions.size(), function_count);
  for (size_t i = 0; i < shader_kinds.size(); ++i) {
    SCOPED_TRACE(i);
    EXPECT_TRUE(info.functions[i].has_shader_info);
    EXPECT_EQ(info.functions[i].shader_info_table_type, table_types[i]);
    EXPECT_EQ(info.functions[i].shader_info_index, 0u);
  }
  EXPECT_FALSE(info.functions[shader_kinds.size()].has_shader_info);
  EXPECT_FALSE(info.functions[shader_kinds.size() + 1].has_shader_info);

  for (size_t i = 0; i < shader_kinds.size(); ++i) {
    SCOPED_TRACE(i);
    auto malformed = functions;
    const size_t record =
        kRuntimeDataTableHeaderSize + i * kRdatFunctionRecord2Size;
    Store<uint32_t>(malformed, record + 48, 1u);
    bytes = build(malformed);
    EXPECT_EQ(ParseRuntimeData(Part(fourcc::RuntimeData, bytes), info),
              ParseStatus::InvalidRuntimeData);
  }
}

TEST(DxilPipelineStateValidation, ParsesMinimalRuntimeAndResourceRecords) {
  using namespace dxmt::dxil;
  std::vector<uint8_t> bytes(4 + kPsvRuntimeInfo1Size + 4 + 4 + 4);
  Store<uint32_t>(bytes, 0, kPsvRuntimeInfo1Size);
  Store<uint32_t>(bytes, 4 + kPsvRuntimeInfo1Size, 0u);
  Store<uint32_t>(bytes, 8 + kPsvRuntimeInfo1Size, 0u);
  Store<uint32_t>(bytes, 12 + kPsvRuntimeInfo1Size, 0u);

  PipelineStateValidationInfo info;
  ASSERT_EQ(ParsePipelineStateValidation(
                Part(fourcc::PipelineStateValidation, bytes), info),
            ParseStatus::Ok);
  EXPECT_TRUE(info.has_runtime_info_1);
  EXPECT_FALSE(info.has_runtime_info_2);
  EXPECT_TRUE(info.resources.empty());
  EXPECT_TRUE(info.dependency_payload.empty());

  bytes.assign(4 + 4 + 4 + kPsvResourceBindInfo1Size, 0);
  Store<uint32_t>(bytes, 0, 0u);
  Store<uint32_t>(bytes, 4, 1u);
  Store<uint32_t>(bytes, 8, kPsvResourceBindInfo1Size);
  Store<uint32_t>(bytes, 12, 3u);
  Store<uint32_t>(bytes, 16, 4u);
  Store<uint32_t>(bytes, 20, 5u);
  Store<uint32_t>(bytes, 24, 6u);
  Store<uint32_t>(bytes, 28, 7u);
  Store<uint32_t>(bytes, 32, 8u);

  ASSERT_EQ(ParsePipelineStateValidation(
                Part(fourcc::PipelineStateValidation, bytes), info),
            ParseStatus::Ok);
  ASSERT_EQ(info.resources.size(), 1u);
  EXPECT_EQ(info.resources[0].resource_type, 3u);
  EXPECT_EQ(info.resources[0].space, 4u);
  EXPECT_EQ(info.resources[0].lower_bound, 5u);
  EXPECT_EQ(info.resources[0].upper_bound, 6u);
  EXPECT_EQ(info.resources[0].resource_kind, 7u);
  EXPECT_EQ(info.resources[0].resource_flags, 8u);
}

TEST(DxilPipelineStateValidation, ParsesRuntimeInfoVersionsAndLegacyBindings) {
  using namespace dxmt::dxil;
  constexpr size_t runtime_offset = sizeof(uint32_t);
  constexpr size_t resource_count_offset = runtime_offset + kPsvRuntimeInfo4Size;
  constexpr size_t resource_stride_offset = resource_count_offset + sizeof(uint32_t);
  constexpr size_t resource_offset = resource_stride_offset + sizeof(uint32_t);
  constexpr size_t string_size_offset = resource_offset + kPsvResourceBindInfo0Size;
  constexpr size_t string_offset = string_size_offset + sizeof(uint32_t);
  constexpr size_t semantic_count_offset = string_offset + 8;
  std::vector<uint8_t> bytes(semantic_count_offset + sizeof(uint32_t));
  Store<uint32_t>(bytes, 0, kPsvRuntimeInfo4Size);
  bytes[runtime_offset + 24] = 5;
  Store<uint32_t>(bytes, runtime_offset + 36, 8u);
  Store<uint32_t>(bytes, runtime_offset + 40, 4u);
  Store<uint32_t>(bytes, runtime_offset + 44, 2u);
  Store<uint32_t>(bytes, runtime_offset + 48, 0u);
  Store<uint32_t>(bytes, runtime_offset + 52, 1024u);
  Store<uint32_t>(bytes, resource_count_offset, 1u);
  Store<uint32_t>(bytes, resource_stride_offset, kPsvResourceBindInfo0Size);
  Store<uint32_t>(bytes, resource_offset + 0, 3u);
  Store<uint32_t>(bytes, resource_offset + 4, 4u);
  Store<uint32_t>(bytes, resource_offset + 8, 5u);
  Store<uint32_t>(bytes, resource_offset + 12, 6u);
  Store<uint32_t>(bytes, string_size_offset, 8u);
  std::memcpy(bytes.data() + string_offset, "main\0\0\0", 8);
  Store<uint32_t>(bytes, semantic_count_offset, 0u);

  PipelineStateValidationInfo info;
  ASSERT_EQ(ParsePipelineStateValidation(
                Part(fourcc::PipelineStateValidation, bytes), info),
            ParseStatus::Ok);
  EXPECT_TRUE(info.has_runtime_info_1);
  EXPECT_TRUE(info.has_runtime_info_2);
  EXPECT_TRUE(info.has_runtime_info_3);
  EXPECT_TRUE(info.has_runtime_info_4);
  EXPECT_EQ(info.shader_stage, 5u);
  EXPECT_EQ(info.num_threads_x, 8u);
  EXPECT_EQ(info.num_threads_y, 4u);
  EXPECT_EQ(info.num_threads_z, 2u);
  EXPECT_EQ(info.entry_function_name, "main");
  EXPECT_EQ(info.num_bytes_group_shared_memory, 1024u);
  ASSERT_EQ(info.resources.size(), 1u);
  EXPECT_EQ(info.resources[0].resource_type, 3u);
  EXPECT_EQ(info.resources[0].space, 4u);
  EXPECT_EQ(info.resources[0].lower_bound, 5u);
  EXPECT_EQ(info.resources[0].upper_bound, 6u);
  EXPECT_EQ(info.resources[0].resource_kind, 0u);
  EXPECT_EQ(info.resources[0].resource_flags, 0u);

  Store<uint32_t>(bytes, runtime_offset + 48, 100u);
  EXPECT_EQ(ParsePipelineStateValidation(
                Part(fourcc::PipelineStateValidation, bytes), info),
            ParseStatus::InvalidPipelineStateValidation);
}

TEST(DxilPipelineStateValidation, RejectsMalformedVariableLengthSections) {
  using namespace dxmt::dxil;
  PipelineStateValidationInfo info;
  const auto expect_invalid = [&](std::string_view case_name,
                                  const std::vector<uint8_t> &bytes) {
    SCOPED_TRACE(case_name);
    EXPECT_EQ(ParsePipelineStateValidation(
                  Part(fourcc::PipelineStateValidation, bytes), info),
              ParseStatus::InvalidPipelineStateValidation);
  };

  expect_invalid("truncated size field",
                 std::vector<uint8_t>(sizeof(uint32_t) - 1));

  std::vector<uint8_t> bytes(sizeof(uint32_t));
  Store<uint32_t>(bytes, 0, 1u);
  expect_invalid("truncated runtime info", bytes);

  Store<uint32_t>(bytes, 0, 0u);
  expect_invalid("missing resource count", bytes);

  bytes.resize(2 * sizeof(uint32_t));
  Store<uint32_t>(bytes, 4, 1u);
  expect_invalid("missing resource stride", bytes);

  bytes.resize(3 * sizeof(uint32_t));
  Store<uint32_t>(bytes, 8, kPsvResourceBindInfo0Size - 4);
  expect_invalid("short resource stride", bytes);
  Store<uint32_t>(bytes, 8, kPsvResourceBindInfo0Size + 2);
  expect_invalid("unaligned resource stride", bytes);
  Store<uint32_t>(bytes, 8, kPsvResourceBindInfo0Size);
  expect_invalid("truncated resource array", bytes);

  bytes.assign(4 + kPsvRuntimeInfo1Size + 4, 0);
  Store<uint32_t>(bytes, 0, kPsvRuntimeInfo1Size);
  expect_invalid("missing string table size", bytes);

  bytes.resize(bytes.size() + sizeof(uint32_t) + 1);
  Store<uint32_t>(bytes, 4 + kPsvRuntimeInfo1Size + 4, 1u);
  expect_invalid("unaligned string table", bytes);

  bytes.assign(4 + kPsvRuntimeInfo1Size + 4 + 4 + 4, 0);
  Store<uint32_t>(bytes, 0, kPsvRuntimeInfo1Size);
  Store<uint32_t>(bytes, 4 + kPsvRuntimeInfo1Size + 4, 0u);
  Store<uint32_t>(bytes, 4 + kPsvRuntimeInfo1Size + 8,
                  std::numeric_limits<uint32_t>::max());
  expect_invalid("truncated semantic index table", bytes);

  bytes[4 + 28] = 1;
  Store<uint32_t>(bytes, 4 + kPsvRuntimeInfo1Size + 8, 0u);
  expect_invalid("missing signature stride", bytes);

  bytes.resize(bytes.size() + sizeof(uint32_t));
  Store<uint32_t>(bytes, 4 + kPsvRuntimeInfo1Size + 12,
                  kPsvSignatureElement0Size - 4);
  expect_invalid("short signature stride", bytes);
  Store<uint32_t>(bytes, 4 + kPsvRuntimeInfo1Size + 12,
                  kPsvSignatureElement0Size + 2);
  expect_invalid("unaligned signature stride", bytes);
  Store<uint32_t>(bytes, 4 + kPsvRuntimeInfo1Size + 12,
                  kPsvSignatureElement0Size);
  expect_invalid("truncated signature array", bytes);
}

TEST(DxilPipelineStateValidation, ParsesSignaturesAndDependencyTables) {
  using namespace dxmt::dxil;
  constexpr size_t runtime_offset = sizeof(uint32_t);
  constexpr size_t resource_count_offset = runtime_offset + kPsvRuntimeInfo1Size;
  constexpr size_t string_size_offset = resource_count_offset + sizeof(uint32_t);
  constexpr size_t string_offset = string_size_offset + sizeof(uint32_t);
  constexpr size_t semantic_count_offset = string_offset + 8;
  constexpr size_t semantic_table_offset = semantic_count_offset + sizeof(uint32_t);
  constexpr size_t signature_size_offset = semantic_table_offset + 8;
  constexpr size_t signature_offset = signature_size_offset + sizeof(uint32_t);
  constexpr size_t dependency_offset = signature_offset +
                                       2 * kPsvSignatureElement0Size;
  std::vector<uint8_t> bytes(dependency_offset + 5 * sizeof(uint32_t));
  Store<uint32_t>(bytes, 0, kPsvRuntimeInfo1Size);
  bytes[runtime_offset + 25] = 1;
  bytes[runtime_offset + 28] = 1;
  bytes[runtime_offset + 29] = 1;
  bytes[runtime_offset + 31] = 1;
  bytes[runtime_offset + 32] = 1;
  Store<uint32_t>(bytes, resource_count_offset, 0u);
  Store<uint32_t>(bytes, string_size_offset, 8u);
  std::memcpy(bytes.data() + string_offset, "POS\0COL", 8);
  Store<uint32_t>(bytes, semantic_count_offset, 2u);
  Store<uint32_t>(bytes, semantic_table_offset + 0, 3u);
  Store<uint32_t>(bytes, semantic_table_offset + 4, 7u);
  Store<uint32_t>(bytes, signature_size_offset, kPsvSignatureElement0Size);

  Store<uint32_t>(bytes, signature_offset + 0, 0u);
  Store<uint32_t>(bytes, signature_offset + 4, 0u);
  bytes[signature_offset + 8] = 1;
  bytes[signature_offset + 9] = 2;
  bytes[signature_offset + 10] = 0x52;
  bytes[signature_offset + 11] = 4;
  bytes[signature_offset + 12] = 5;
  bytes[signature_offset + 13] = 6;
  bytes[signature_offset + 14] = 0x21;

  const size_t output_signature = signature_offset + kPsvSignatureElement0Size;
  Store<uint32_t>(bytes, output_signature + 0, 4u);
  Store<uint32_t>(bytes, output_signature + 4, 1u);
  bytes[output_signature + 8] = 1;
  bytes[output_signature + 9] = 3;
  bytes[output_signature + 10] = 0x01;
  bytes[output_signature + 11] = 8;
  bytes[output_signature + 12] = 9;
  bytes[output_signature + 13] = 10;
  bytes[output_signature + 14] = 0x12;

  Store<uint32_t>(bytes, dependency_offset + 0, 0x5u);
  Store<uint32_t>(bytes, dependency_offset + 4, 0x11u);
  Store<uint32_t>(bytes, dependency_offset + 8, 0x22u);
  Store<uint32_t>(bytes, dependency_offset + 12, 0x33u);
  Store<uint32_t>(bytes, dependency_offset + 16, 0x44u);

  PipelineStateValidationInfo info;
  ASSERT_EQ(ParsePipelineStateValidation(
                Part(fourcc::PipelineStateValidation, bytes), info),
            ParseStatus::Ok);
  ASSERT_EQ(info.input_signature_elements.size(), 1u);
  EXPECT_EQ(info.input_signature_elements[0].semantic_name, "POS");
  EXPECT_EQ(info.input_signature_elements[0].semantic_indexes,
            (std::vector<uint32_t>{3}));
  EXPECT_EQ(info.input_signature_elements[0].rows, 1u);
  EXPECT_EQ(info.input_signature_elements[0].start_row, 2u);
  EXPECT_EQ(info.input_signature_elements[0].cols, 2u);
  EXPECT_EQ(info.input_signature_elements[0].start_col, 1u);
  EXPECT_TRUE(info.input_signature_elements[0].allocated);
  EXPECT_EQ(info.input_signature_elements[0].dynamic_index_mask, 1u);
  EXPECT_EQ(info.input_signature_elements[0].output_stream, 2u);

  ASSERT_EQ(info.output_signature_elements.size(), 1u);
  EXPECT_EQ(info.output_signature_elements[0].semantic_name, "COL");
  EXPECT_EQ(info.output_signature_elements[0].semantic_indexes,
            (std::vector<uint32_t>{7}));
  ASSERT_EQ(info.view_id_output_masks[0].mask_words.size(), 1u);
  EXPECT_EQ(info.view_id_output_masks[0].mask_words[0], 0x5u);
  EXPECT_EQ(info.input_to_output_tables[0].input_vectors, 1u);
  EXPECT_EQ(info.input_to_output_tables[0].output_vectors, 1u);
  EXPECT_EQ(info.input_to_output_tables[0].mask_words,
            (std::vector<uint32_t>{0x11u, 0x22u, 0x33u, 0x44u}));
  EXPECT_EQ(info.dependency_payload.size(), 5 * sizeof(uint32_t));
}

TEST(DxilPipelineStateValidation, RejectsInvalidSignatureReferencesAndDependencies) {
  using namespace dxmt::dxil;
  constexpr size_t runtime_offset = sizeof(uint32_t);
  constexpr size_t resource_count_offset = runtime_offset + kPsvRuntimeInfo1Size;
  constexpr size_t string_size_offset = resource_count_offset + sizeof(uint32_t);
  constexpr size_t string_offset = string_size_offset + sizeof(uint32_t);
  constexpr size_t semantic_count_offset = string_offset + 8;
  constexpr size_t semantic_table_offset = semantic_count_offset + sizeof(uint32_t);
  constexpr size_t signature_size_offset = semantic_table_offset + 8;
  constexpr size_t signature_offset = signature_size_offset + sizeof(uint32_t);
  constexpr size_t dependency_offset = signature_offset + kPsvSignatureElement0Size;
  std::vector<uint8_t> valid(dependency_offset + 5 * sizeof(uint32_t));
  Store<uint32_t>(valid, 0, kPsvRuntimeInfo1Size);
  valid[runtime_offset + 25] = 1;
  valid[runtime_offset + 28] = 1;
  valid[runtime_offset + 31] = 1;
  valid[runtime_offset + 32] = 1;
  Store<uint32_t>(valid, string_size_offset, 8u);
  std::memcpy(valid.data() + string_offset, "POSITION", 8);
  valid[string_offset + 7] = 0;
  Store<uint32_t>(valid, semantic_count_offset, 2u);
  Store<uint32_t>(valid, semantic_table_offset + 0, 3u);
  Store<uint32_t>(valid, semantic_table_offset + 4, 7u);
  Store<uint32_t>(valid, signature_size_offset, kPsvSignatureElement0Size);
  Store<uint32_t>(valid, signature_offset + 0, 0u);
  Store<uint32_t>(valid, signature_offset + 4, 0u);
  valid[signature_offset + 8] = 1;

  PipelineStateValidationInfo info;
  ASSERT_EQ(ParsePipelineStateValidation(
                Part(fourcc::PipelineStateValidation, valid), info),
            ParseStatus::Ok);

  const auto expect_invalid = [&](std::string_view case_name,
                                  std::vector<uint8_t> bytes) {
    SCOPED_TRACE(case_name);
    EXPECT_EQ(ParsePipelineStateValidation(
                  Part(fourcc::PipelineStateValidation, bytes), info),
              ParseStatus::InvalidPipelineStateValidation);
  };

  auto malformed = valid;
  malformed[string_offset + 7] = 'x';
  expect_invalid("unterminated string table", malformed);

  malformed = valid;
  Store<uint32_t>(malformed, signature_offset + 0, 100u);
  expect_invalid("semantic name out of bounds", malformed);

  malformed = valid;
  Store<uint32_t>(malformed, signature_offset + 4, 2u);
  expect_invalid("semantic index out of bounds", malformed);

  malformed = valid;
  malformed.pop_back();
  expect_invalid("truncated dependency table", malformed);
}

TEST(DxilPipelineStateValidation, ParsesStageSpecificDependencyTables) {
  using namespace dxmt::dxil;
  constexpr size_t runtime_offset = sizeof(uint32_t);
  constexpr size_t resource_count_offset =
      runtime_offset + kPsvRuntimeInfo1Size;
  constexpr size_t string_size_offset =
      resource_count_offset + sizeof(uint32_t);
  constexpr size_t semantic_count_offset =
      string_size_offset + sizeof(uint32_t);
  constexpr size_t dependency_offset =
      semantic_count_offset + sizeof(uint32_t);
  const auto make_psv = [](uint8_t shader_stage, bool uses_view_id,
                           uint8_t input_vectors, uint8_t output_vectors,
                           uint8_t patch_vectors,
                           std::initializer_list<uint32_t> dependency_words) {
    std::vector<uint8_t> bytes(dependency_offset +
                               dependency_words.size() * sizeof(uint32_t));
    Store<uint32_t>(bytes, 0, kPsvRuntimeInfo1Size);
    bytes[runtime_offset + 24] = shader_stage;
    bytes[runtime_offset + 25] = uses_view_id ? 1 : 0;
    bytes[runtime_offset + 26] = patch_vectors;
    bytes[runtime_offset + 31] = input_vectors;
    bytes[runtime_offset + 32] = output_vectors;
    size_t offset = dependency_offset;
    for (const auto word : dependency_words) {
      Store<uint32_t>(bytes, offset, word);
      offset += sizeof(uint32_t);
    }
    return bytes;
  };

  const auto hull = make_psv(
      3, true, 1, 1, 1,
      {0x10u, 0x20u, 0x31u, 0x32u, 0x33u, 0x34u, 0x41u, 0x42u,
       0x43u, 0x44u});
  PipelineStateValidationInfo info;
  ASSERT_EQ(ParsePipelineStateValidation(
                Part(fourcc::PipelineStateValidation, hull), info),
            ParseStatus::Ok);
  ASSERT_EQ(info.view_id_output_masks[0].mask_words.size(), 1u);
  EXPECT_EQ(info.view_id_output_masks[0].mask_words[0], 0x10u);
  ASSERT_EQ(
      info.view_id_patch_constant_or_primitive_output_mask.mask_words.size(),
      1u);
  EXPECT_EQ(info.view_id_patch_constant_or_primitive_output_mask.mask_words[0],
            0x20u);
  EXPECT_EQ(info.input_to_output_tables[0].mask_words,
            (std::vector<uint32_t>{0x31u, 0x32u, 0x33u, 0x34u}));
  EXPECT_EQ(info.input_to_patch_constant_output_table.mask_words,
            (std::vector<uint32_t>{0x41u, 0x42u, 0x43u, 0x44u}));
  EXPECT_TRUE(info.patch_constant_input_to_output_table.mask_words.empty());

  const auto domain =
      make_psv(4, false, 1, 1, 1,
               {0x51u, 0x52u, 0x53u, 0x54u, 0x61u, 0x62u, 0x63u,
                0x64u});
  ASSERT_EQ(ParsePipelineStateValidation(
                Part(fourcc::PipelineStateValidation, domain), info),
            ParseStatus::Ok);
  EXPECT_EQ(info.input_to_output_tables[0].mask_words,
            (std::vector<uint32_t>{0x51u, 0x52u, 0x53u, 0x54u}));
  EXPECT_EQ(info.patch_constant_input_to_output_table.mask_words,
            (std::vector<uint32_t>{0x61u, 0x62u, 0x63u, 0x64u}));
  EXPECT_TRUE(info.input_to_patch_constant_output_table.mask_words.empty());

  const auto mesh = make_psv(13, true, 1, 1, 1, {0x70u, 0x80u});
  ASSERT_EQ(ParsePipelineStateValidation(
                Part(fourcc::PipelineStateValidation, mesh), info),
            ParseStatus::Ok);
  EXPECT_EQ(info.view_id_output_masks[0].mask_words,
            (std::vector<uint32_t>{0x70u}));
  EXPECT_EQ(info.view_id_patch_constant_or_primitive_output_mask.mask_words,
            (std::vector<uint32_t>{0x80u}));
  EXPECT_TRUE(info.input_to_output_tables[0].mask_words.empty());
  EXPECT_TRUE(info.input_to_patch_constant_output_table.mask_words.empty());
  EXPECT_TRUE(info.patch_constant_input_to_output_table.mask_words.empty());

  const auto expect_truncated = [&](std::vector<uint8_t> bytes) {
    bytes.resize(bytes.size() - sizeof(uint32_t));
    EXPECT_EQ(ParsePipelineStateValidation(
                  Part(fourcc::PipelineStateValidation, bytes), info),
              ParseStatus::InvalidPipelineStateValidation);
  };
  expect_truncated(hull);
  expect_truncated(domain);
  expect_truncated(mesh);
}

TEST(DxilPipelineStateValidation, ParsesMultiStreamVectorMaskBoundaries) {
  using namespace dxmt::dxil;
  constexpr size_t runtime_offset = sizeof(uint32_t);
  constexpr size_t dependency_offset = runtime_offset +
                                       kPsvRuntimeInfo1Size +
                                       3 * sizeof(uint32_t);
  constexpr size_t dependency_word_count = 36;
  std::vector<uint8_t> bytes(dependency_offset +
                             dependency_word_count * sizeof(uint32_t));
  Store<uint32_t>(bytes, 0, kPsvRuntimeInfo1Size);
  bytes[runtime_offset + 24] = 2;
  bytes[runtime_offset + 25] = 1;
  bytes[runtime_offset + 31] = 2;
  bytes[runtime_offset + 32] = 1;
  bytes[runtime_offset + 33] = 9;
  bytes[runtime_offset + 34] = 0;
  bytes[runtime_offset + 35] = 2;
  for (size_t i = 0; i < dependency_word_count; ++i)
    Store<uint32_t>(bytes, dependency_offset + i * sizeof(uint32_t),
                    0x100u + uint32_t(i));

  PipelineStateValidationInfo info;
  ASSERT_EQ(ParsePipelineStateValidation(
                Part(fourcc::PipelineStateValidation, bytes), info),
            ParseStatus::Ok);
  EXPECT_EQ(info.view_id_output_masks[0].vector_count, 1u);
  EXPECT_EQ(info.view_id_output_masks[0].mask_words,
            (std::vector<uint32_t>{0x100u}));
  EXPECT_EQ(info.view_id_output_masks[1].vector_count, 9u);
  EXPECT_EQ(info.view_id_output_masks[1].mask_words,
            (std::vector<uint32_t>{0x101u, 0x102u}));
  EXPECT_TRUE(info.view_id_output_masks[2].mask_words.empty());
  EXPECT_EQ(info.view_id_output_masks[3].vector_count, 2u);
  EXPECT_EQ(info.view_id_output_masks[3].mask_words,
            (std::vector<uint32_t>{0x103u}));

  EXPECT_EQ(info.input_to_output_tables[0].input_vectors, 2u);
  EXPECT_EQ(info.input_to_output_tables[0].output_vectors, 1u);
  ASSERT_EQ(info.input_to_output_tables[0].mask_words.size(), 8u);
  EXPECT_EQ(info.input_to_output_tables[0].mask_words.front(), 0x104u);
  EXPECT_EQ(info.input_to_output_tables[0].mask_words.back(), 0x10bu);
  EXPECT_EQ(info.input_to_output_tables[1].input_vectors, 2u);
  EXPECT_EQ(info.input_to_output_tables[1].output_vectors, 9u);
  ASSERT_EQ(info.input_to_output_tables[1].mask_words.size(), 16u);
  EXPECT_EQ(info.input_to_output_tables[1].mask_words.front(), 0x10cu);
  EXPECT_EQ(info.input_to_output_tables[1].mask_words.back(), 0x11bu);
  EXPECT_TRUE(info.input_to_output_tables[2].mask_words.empty());
  ASSERT_EQ(info.input_to_output_tables[3].mask_words.size(), 8u);
  EXPECT_EQ(info.input_to_output_tables[3].mask_words.front(), 0x11cu);
  EXPECT_EQ(info.input_to_output_tables[3].mask_words.back(), 0x123u);
  EXPECT_EQ(info.dependency_payload.size(),
            dependency_word_count * sizeof(uint32_t));

  bytes.resize(bytes.size() - sizeof(uint32_t));
  EXPECT_EQ(ParsePipelineStateValidation(
                Part(fourcc::PipelineStateValidation, bytes), info),
            ParseStatus::InvalidPipelineStateValidation);
}

TEST(DxilSourceInfo, ParsesAlignedSections) {
  using namespace dxmt::dxil;
  std::vector<uint8_t> bytes(kSourceInfoHeaderSize + 12 + 8);
  Store<uint32_t>(bytes, 0, bytes.size());
  Store<uint16_t>(bytes, 4, 3u);
  Store<uint16_t>(bytes, 6, 2u);
  Store<uint32_t>(bytes, kSourceInfoHeaderSize, 12u);
  Store<uint16_t>(bytes, kSourceInfoHeaderSize + 4, 4u);
  Store<uint16_t>(bytes, kSourceInfoHeaderSize + 6, 5u);
  bytes[kSourceInfoHeaderSize + kSourceInfoSectionHeaderSize + 0] = 10;
  bytes[kSourceInfoHeaderSize + kSourceInfoSectionHeaderSize + 1] = 11;
  bytes[kSourceInfoHeaderSize + kSourceInfoSectionHeaderSize + 2] = 12;
  bytes[kSourceInfoHeaderSize + kSourceInfoSectionHeaderSize + 3] = 13;
  Store<uint32_t>(bytes, kSourceInfoHeaderSize + 12, 8u);
  Store<uint16_t>(bytes, kSourceInfoHeaderSize + 16, 6u);
  Store<uint16_t>(bytes, kSourceInfoHeaderSize + 18, 7u);

  SourceInfo info;
  ASSERT_EQ(ParseSourceInfo(Part(fourcc::ShaderSourceInfo, bytes), info),
            ParseStatus::Ok);
  EXPECT_EQ(info.aligned_size, bytes.size());
  EXPECT_EQ(info.flags, 3u);
  ASSERT_EQ(info.sections.size(), 2u);
  EXPECT_EQ(info.sections[0].flags, 4u);
  EXPECT_EQ(info.sections[0].type, 5u);
  EXPECT_TRUE(std::equal(info.sections[0].data.begin(),
                         info.sections[0].data.end(),
                         std::array<uint8_t, 4>{10, 11, 12, 13}.begin()));
  EXPECT_EQ(info.sections[1].flags, 6u);
  EXPECT_EQ(info.sections[1].type, 7u);
  EXPECT_TRUE(info.sections[1].data.empty());
}

TEST(DxilSourceInfo, RejectsMalformedSectionBounds) {
  using namespace dxmt::dxil;
  std::vector<uint8_t> valid(kSourceInfoHeaderSize +
                             kSourceInfoSectionHeaderSize);
  Store<uint32_t>(valid, 0, valid.size());
  Store<uint16_t>(valid, 6, 1u);
  Store<uint32_t>(valid, kSourceInfoHeaderSize,
                  kSourceInfoSectionHeaderSize);

  SourceInfo info;
  const auto expect_invalid = [&](std::string_view case_name,
                                  std::vector<uint8_t> bytes) {
    SCOPED_TRACE(case_name);
    EXPECT_EQ(ParseSourceInfo(Part(fourcc::ShaderSourceInfo, bytes), info),
              ParseStatus::InvalidSourceInfo);
  };

  auto malformed = valid;
  Store<uint32_t>(malformed, 0, kSourceInfoHeaderSize - 1);
  expect_invalid("outer size before header", malformed);

  malformed = valid;
  Store<uint32_t>(malformed, 0, valid.size() - 2);
  expect_invalid("unaligned outer size", malformed);

  malformed = valid;
  Store<uint32_t>(malformed, 0, valid.size() + 4);
  expect_invalid("outer size past payload", malformed);

  malformed = valid;
  Store<uint16_t>(malformed, 6, 2u);
  expect_invalid("truncated section table", malformed);

  malformed = valid;
  Store<uint32_t>(malformed, kSourceInfoHeaderSize,
                  kSourceInfoSectionHeaderSize - 4);
  expect_invalid("section size before header", malformed);

  malformed = valid;
  Store<uint32_t>(malformed, kSourceInfoHeaderSize,
                  kSourceInfoSectionHeaderSize + 2);
  expect_invalid("unaligned section size", malformed);

  malformed = valid;
  Store<uint32_t>(malformed, kSourceInfoHeaderSize,
                  kSourceInfoSectionHeaderSize + 4);
  expect_invalid("section past outer payload", malformed);
}

TEST(DxilAuxiliaryParts, ParsesPdbPayloadAndStatistics) {
  using namespace dxmt::dxil;
  std::vector<uint8_t> pdb_bytes(kShaderPdbInfoHeaderSize + 3);
  Store<uint16_t>(pdb_bytes, 0, 1u);
  Store<uint16_t>(pdb_bytes, 2, 2u);
  Store<uint32_t>(pdb_bytes, 4, 3u);
  Store<uint32_t>(pdb_bytes, 8, 10u);
  pdb_bytes[12] = 7;
  pdb_bytes[13] = 8;
  pdb_bytes[14] = 9;

  ShaderPdbInfo pdb;
  ASSERT_EQ(ParseShaderPdbInfo(Part(fourcc::ShaderPdbInfo, pdb_bytes), pdb),
            ParseStatus::Ok);
  EXPECT_EQ(pdb.version, 1u);
  EXPECT_EQ(pdb.compression_type, 2u);
  EXPECT_EQ(pdb.uncompressed_size_in_bytes, 10u);
  EXPECT_TRUE(std::equal(pdb.payload.begin(), pdb.payload.end(),
                         std::array<uint8_t, 3>{7, 8, 9}.begin()));

  Store<uint32_t>(pdb_bytes, 4, 4u);
  EXPECT_EQ(ParseShaderPdbInfo(Part(fourcc::ShaderPdbInfo, pdb_bytes), pdb),
            ParseStatus::InvalidShaderPdbInfo);

  std::vector<uint8_t> statistics(2 * sizeof(uint32_t));
  Store<uint32_t>(statistics, 0, 0x12345678u);
  Store<uint32_t>(statistics, 4, 0x90abcdefu);
  ShaderStatisticsInfo stats;
  ASSERT_EQ(
      ParseShaderStatistics(Part(fourcc::ShaderStatistics, statistics), stats),
      ParseStatus::Ok);
  EXPECT_EQ(stats.values,
            (std::vector<uint32_t>{0x12345678u, 0x90abcdefu}));

  statistics.push_back(0);
  EXPECT_EQ(
      ParseShaderStatistics(Part(fourcc::ShaderStatistics, statistics), stats),
      ParseStatus::InvalidShaderStatistics);
}

TEST(DxilResourceDef, ParsesLegacyAndExtendedResourceRecords) {
  using namespace dxmt::dxil;
  constexpr size_t cbuffer_offset = kResourceDefHeaderSize;
  constexpr size_t resource_offset =
      cbuffer_offset + kResourceDefConstantBufferSize;
  constexpr size_t creator_offset =
      resource_offset + kResourceDefResourceBindingExtendedSize;
  constexpr size_t cbuffer_name_offset = creator_offset + 4;
  constexpr size_t resource_name_offset = cbuffer_name_offset + 8;
  std::vector<uint8_t> bytes(resource_name_offset + 9);
  Store<uint32_t>(bytes, 0, 1u);
  Store<uint32_t>(bytes, 4, cbuffer_offset);
  Store<uint32_t>(bytes, 8, 1u);
  Store<uint32_t>(bytes, 12, resource_offset);
  Store<uint32_t>(bytes, 16, 0x51u);
  Store<uint32_t>(bytes, 20, 42u);
  Store<uint32_t>(bytes, 24, creator_offset);
  Store<uint32_t>(bytes, cbuffer_offset + 0, cbuffer_name_offset);
  Store<uint32_t>(bytes, cbuffer_offset + 4, 2u);
  Store<uint32_t>(bytes, cbuffer_offset + 8, 200u);
  Store<uint32_t>(bytes, cbuffer_offset + 12, 64u);
  Store<uint32_t>(bytes, cbuffer_offset + 16, 5u);
  Store<uint32_t>(bytes, cbuffer_offset + 20, 6u);
  Store<uint32_t>(bytes, resource_offset + 0, resource_name_offset);
  Store<uint32_t>(bytes, resource_offset + 4, 7u);
  Store<uint32_t>(bytes, resource_offset + 8, 8u);
  Store<uint32_t>(bytes, resource_offset + 12, 9u);
  Store<uint32_t>(bytes, resource_offset + 16, 10u);
  Store<uint32_t>(bytes, resource_offset + 20, 11u);
  Store<uint32_t>(bytes, resource_offset + 24, 12u);
  Store<uint32_t>(bytes, resource_offset + 28, 13u);
  Store<uint32_t>(bytes, resource_offset + 32, 14u);
  Store<uint32_t>(bytes, resource_offset + 36, 15u);
  std::memcpy(bytes.data() + creator_offset, "dxc", 4);
  std::memcpy(bytes.data() + cbuffer_name_offset, "Globals", 8);
  std::memcpy(bytes.data() + resource_name_offset, "texture0", 9);

  ResourceDefInfo info;
  ASSERT_EQ(ParseResourceDef(Part(fourcc::ResourceDef, bytes), info),
            ParseStatus::Ok);
  EXPECT_EQ(info.creator, "dxc");
  ASSERT_EQ(info.constant_buffers.size(), 1u);
  EXPECT_EQ(info.constant_buffers[0].name, "Globals");
  EXPECT_EQ(info.constant_buffers[0].variable_count, 2u);
  EXPECT_EQ(info.constant_buffers[0].variable_offset, 200u);
  EXPECT_EQ(info.constant_buffers[0].size, 64u);
  EXPECT_EQ(info.constant_buffers[0].flags, 5u);
  EXPECT_EQ(info.constant_buffers[0].type, 6u);
  ASSERT_EQ(info.resources.size(), 1u);
  EXPECT_EQ(info.resources[0].name, "texture0");
  EXPECT_EQ(info.resources[0].type, 7u);
  EXPECT_EQ(info.resources[0].return_type, 8u);
  EXPECT_EQ(info.resources[0].dimension, 9u);
  EXPECT_EQ(info.resources[0].num_samples, 10u);
  EXPECT_EQ(info.resources[0].bind_point, 11u);
  EXPECT_EQ(info.resources[0].bind_count, 12u);
  EXPECT_EQ(info.resources[0].flags, 13u);
  EXPECT_EQ(info.resources[0].space, 14u);
  EXPECT_EQ(info.resources[0].id, 15u);

  Store<uint32_t>(bytes, 16, 0x50u);
  ASSERT_EQ(ParseResourceDef(Part(fourcc::ResourceDef, bytes), info),
            ParseStatus::Ok);
  ASSERT_EQ(info.resources.size(), 1u);
  EXPECT_EQ(info.resources[0].space, 0u);
  EXPECT_EQ(info.resources[0].id, 0u);
}

TEST(DxilResourceDef, RejectsMalformedTableAndStringRanges) {
  using namespace dxmt::dxil;
  constexpr size_t cbuffer_offset = kResourceDefHeaderSize;
  constexpr size_t resource_offset =
      cbuffer_offset + kResourceDefConstantBufferSize;
  std::vector<uint8_t> valid(resource_offset +
                             kResourceDefResourceBindingExtendedSize + 4);
  Store<uint32_t>(valid, 0, 1u);
  Store<uint32_t>(valid, 4, cbuffer_offset);
  Store<uint32_t>(valid, 8, 1u);
  Store<uint32_t>(valid, 12, resource_offset);
  Store<uint32_t>(valid, 16, 0x51u);

  ResourceDefInfo info;
  const auto expect_invalid = [&](std::string_view case_name,
                                  std::vector<uint8_t> bytes) {
    SCOPED_TRACE(case_name);
    EXPECT_EQ(ParseResourceDef(Part(fourcc::ResourceDef, bytes), info),
              ParseStatus::InvalidResourceDef);
  };

  auto malformed = valid;
  Store<uint32_t>(malformed, 24, malformed.size());
  expect_invalid("unterminated creator", malformed);

  malformed = valid;
  Store<uint32_t>(malformed, 0, std::numeric_limits<uint32_t>::max());
  expect_invalid("constant buffer count overflow", malformed);

  malformed = valid;
  Store<uint32_t>(malformed, 8, std::numeric_limits<uint32_t>::max());
  expect_invalid("resource count overflow", malformed);

  malformed = valid;
  Store<uint32_t>(malformed, 4, 0u);
  expect_invalid("constant buffer offset before header", malformed);

  malformed = valid;
  Store<uint32_t>(malformed, 4, cbuffer_offset + 1);
  expect_invalid("unaligned constant buffer offset", malformed);

  malformed = valid;
  Store<uint32_t>(malformed, 12, 0u);
  expect_invalid("resource offset before header", malformed);

  malformed = valid;
  Store<uint32_t>(malformed, 12, resource_offset + 1);
  expect_invalid("unaligned resource offset", malformed);

  malformed = valid;
  Store<uint32_t>(malformed, 12, cbuffer_offset + 4);
  expect_invalid("overlapping tables", malformed);

  malformed = valid;
  Store<uint32_t>(malformed, cbuffer_offset, malformed.size());
  expect_invalid("unterminated constant buffer name", malformed);

  malformed = valid;
  Store<uint32_t>(malformed, resource_offset, malformed.size());
  expect_invalid("unterminated resource name", malformed);
}
