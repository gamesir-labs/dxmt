#include <dxmt_test.hpp>

#include "DXILParser/DXILFormatConstants.hpp"
#include "DXILParser/DXILParser.hpp"

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

} // namespace

TEST(DxilNames, PreservesAllFourBytesAndMapsKnownEnums) {
  using namespace dxmt::dxil;
  EXPECT_EQ(FourCCString(fourcc::Dxil), "DXIL");
  EXPECT_EQ(FourCCString(MakeFourCC('A', '\0', 'B', 'C')),
            std::string("A\0BC", 4));
  EXPECT_STREQ(StatusName(ParseStatus::InvalidPartSize), "invalid part size");
  EXPECT_STREQ(StatusName(static_cast<ParseStatus>(999)), "unknown");
  EXPECT_STREQ(RuntimeDataPartTypeName(rdat::ResourceTable), "ResourceTable");
  EXPECT_STREQ(RuntimeDataPartTypeName(0xffff), "Unknown");
  EXPECT_STREQ(PsvShaderKindName(13), "Mesh");
  EXPECT_STREQ(PsvShaderKindName(0xff), "Invalid");
  EXPECT_STREQ(DxilValidationSeverityName(DxilValidationSeverity::Warning),
               "warning");
  EXPECT_STREQ(DxilValidationCategoryName(
                   DxilValidationCategory::PipelineStateValidation),
               "pipeline-state-validation");
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
