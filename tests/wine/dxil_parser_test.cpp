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
