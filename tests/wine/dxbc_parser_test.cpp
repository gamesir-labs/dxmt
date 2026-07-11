#include <dxmt_test.hpp>

#include "DXBCParser/BlobContainer.h"
#include "DXBCParser/DXBCUtils.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace {

template <typename T>
void Store(std::vector<uint8_t> &bytes, size_t offset, T value) {
  ASSERT_LE(offset + sizeof(value), bytes.size());
  std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

class DxbcContainerBuilder {
public:
  DxbcContainerBuilder &add(microsoft::DXBCFourCC fourcc,
                            std::vector<uint8_t> data) {
    parts_.push_back({fourcc, std::move(data)});
    return *this;
  }

  std::vector<uint8_t> build() const {
    const size_t index_end =
        sizeof(microsoft::DXBCHeader) + parts_.size() * sizeof(uint32_t);
    size_t total_size = index_end;
    for (const auto &part : parts_)
      total_size += sizeof(microsoft::DXBCBlobHeader) + part.data.size();

    std::vector<uint8_t> bytes(total_size);
    Store(bytes, offsetof(microsoft::DXBCHeader, DXBCHeaderFourCC),
          microsoft::DXBC_FOURCC_NAME);
    Store<uint16_t>(bytes, offsetof(microsoft::DXBCHeader, Version),
                    DXBC_MAJOR_VERSION);
    Store<uint16_t>(bytes, offsetof(microsoft::DXBCHeader, Version) + 2,
                    DXBC_MINOR_VERSION);
    Store<uint32_t>(bytes,
                    offsetof(microsoft::DXBCHeader, ContainerSizeInBytes),
                    bytes.size());
    Store<uint32_t>(bytes, offsetof(microsoft::DXBCHeader, BlobCount),
                    parts_.size());

    size_t offset = index_end;
    for (size_t i = 0; i < parts_.size(); ++i) {
      Store<uint32_t>(bytes, sizeof(microsoft::DXBCHeader) + i * 4, offset);
      Store<uint32_t>(bytes, offset, parts_[i].fourcc);
      Store<uint32_t>(bytes, offset + 4, parts_[i].data.size());
      std::copy(parts_[i].data.begin(), parts_[i].data.end(),
                bytes.begin() + offset + sizeof(microsoft::DXBCBlobHeader));
      offset += sizeof(microsoft::DXBCBlobHeader) + parts_[i].data.size();
    }
    return bytes;
  }

private:
  struct Part {
    microsoft::DXBCFourCC fourcc;
    std::vector<uint8_t> data;
  };

  std::vector<Part> parts_;
};

enum class ContainerCorruption {
  TruncatedHeader,
  BadMagic,
  WrongVersion,
  DeclaredSizeMismatch,
  TruncatedIndex,
  OffsetInsideIndex,
  TruncatedBlobHeader,
  TruncatedBlobPayload,
};

class DxbcInvalidContainerTest
    : public testing::TestWithParam<ContainerCorruption> {};

struct SignatureParameter4 {
  uint32_t semantic_name;
  uint32_t semantic_index;
  D3D10_NAME system_value;
  D3D10_REGISTER_COMPONENT_TYPE component_type;
  uint32_t register_index;
  uint8_t mask;
  uint8_t read_write_mask;
  uint16_t padding = 0;
};

static_assert(sizeof(SignatureParameter4) == 24);

class SignatureBuilder {
public:
  SignatureBuilder &add(std::string name, uint32_t register_index,
                        uint8_t read_write_mask = 0,
                        D3D10_NAME system_value = D3D10_NAME_UNDEFINED) {
    parameters_.push_back(
        {std::move(name), register_index, read_write_mask, system_value});
    return *this;
  }

  std::vector<uint8_t> build() const {
    constexpr size_t header_size = 2 * sizeof(uint32_t);
    const size_t strings_offset =
        header_size + parameters_.size() * sizeof(SignatureParameter4);
    size_t total_size = strings_offset;
    for (const auto &parameter : parameters_)
      total_size += parameter.name.size() + 1;

    std::vector<uint8_t> bytes(total_size);
    Store<uint32_t>(bytes, 0, parameters_.size());
    Store<uint32_t>(bytes, 4, header_size);

    size_t name_offset = strings_offset;
    for (size_t i = 0; i < parameters_.size(); ++i) {
      const auto &source = parameters_[i];
      const SignatureParameter4 parameter = {
          .semantic_name = static_cast<uint32_t>(name_offset),
          .semantic_index = 0,
          .system_value = source.system_value,
          .component_type = D3D10_REGISTER_COMPONENT_FLOAT32,
          .register_index = source.register_index,
          .mask = 0xf,
          .read_write_mask = source.read_write_mask,
      };
      Store(bytes, header_size + i * sizeof(parameter), parameter);
      std::copy(source.name.begin(), source.name.end(),
                bytes.begin() + name_offset);
      name_offset += source.name.size() + 1;
    }
    return bytes;
  }

private:
  struct Parameter {
    std::string name;
    uint32_t register_index;
    uint8_t read_write_mask;
    D3D10_NAME system_value;
  };

  std::vector<Parameter> parameters_;
};

enum class SignatureCorruption {
  ParameterTableBeforeHeader,
  ParameterTableOutOfBounds,
  SemanticNameOutOfBounds,
  UnterminatedSemanticName,
  DescendingRegisters,
};

class DxbcInvalidSignatureTest
    : public testing::TestWithParam<SignatureCorruption> {};

} // namespace

TEST(DxbcContainer, DefaultParserHasNoObservableContainer) {
  microsoft::CDXBCParser parser;
  EXPECT_EQ(parser.GetVersion(), nullptr);
  EXPECT_EQ(parser.GetHash(), nullptr);
  EXPECT_EQ(parser.GetBlobCount(), 0u);
  EXPECT_EQ(parser.GetBlob(0), nullptr);
  EXPECT_EQ(parser.GetBlobSize(0), 0u);
  EXPECT_EQ(parser.GetBlobFourCC(0), 0u);
  EXPECT_EQ(parser.FindNextMatchingBlob(microsoft::DXBC_GenericShader),
            DXBC_BLOB_NOT_FOUND);
  EXPECT_EQ(parser.RelocateBytecode(4), E_FAIL);
  EXPECT_EQ(microsoft::DXBCGetSizeAssumingValidPointer(nullptr), 0u);
}

TEST(DxbcContainer, EnumeratesAndSearchesContiguousParts) {
  const auto bytes =
      DxbcContainerBuilder()
          .add(microsoft::DXBC_GenericShader, {1, 2, 3, 4})
          .add(microsoft::DXBC_ShaderFeatureInfo, {5, 6, 7, 8, 9, 10, 11, 12})
          .add(microsoft::DXBC_GenericShader, {})
          .build();

  microsoft::CDXBCParser parser;
  ASSERT_EQ(parser.ReadDXBC(bytes.data(), bytes.size()), S_OK);
  ASSERT_NE(parser.GetVersion(), nullptr);
  EXPECT_EQ(parser.GetVersion()->Major, DXBC_MAJOR_VERSION);
  EXPECT_EQ(parser.GetVersion()->Minor, DXBC_MINOR_VERSION);
  EXPECT_EQ(parser.GetBlobCount(), 3u);
  EXPECT_EQ(parser.GetBlobFourCC(1), microsoft::DXBC_ShaderFeatureInfo);
  EXPECT_EQ(parser.GetBlobSize(0), 4u);
  EXPECT_TRUE(std::equal(static_cast<const uint8_t *>(parser.GetBlob(0)),
                         static_cast<const uint8_t *>(parser.GetBlob(0)) + 4,
                         std::array<uint8_t, 4>{1, 2, 3, 4}.begin()));
  EXPECT_EQ(parser.FindNextMatchingBlob(microsoft::DXBC_GenericShader), 0u);
  EXPECT_EQ(parser.FindNextMatchingBlob(microsoft::DXBC_GenericShader, 1), 2u);
  EXPECT_EQ(parser.FindNextMatchingBlob(microsoft::DXBC_InputSignature),
            DXBC_BLOB_NOT_FOUND);
  EXPECT_EQ(parser.GetBlob(3), nullptr);
}

TEST(DxbcContainer, AcceptsAnEmptyContainer) {
  const auto bytes = DxbcContainerBuilder().build();
  microsoft::CDXBCParser parser;
  EXPECT_EQ(parser.ReadDXBCAssumingValidSize(bytes.data()), S_OK);
  EXPECT_EQ(parser.GetBlobCount(), 0u);
}

TEST(DxbcContainer, RelocatesPointersWhenBackingStorageMoves) {
  const auto bytes =
      DxbcContainerBuilder().add(microsoft::DXBC_GenericShader, {42}).build();
  constexpr size_t offset = 16;
  std::vector<uint8_t> storage(bytes.size() + offset);
  std::copy(bytes.begin(), bytes.end(), storage.begin());

  microsoft::CDXBCParser parser;
  ASSERT_EQ(parser.ReadDXBC(storage.data(), bytes.size()), S_OK);

  std::memmove(storage.data() + offset, storage.data(), bytes.size());
  ASSERT_EQ(parser.RelocateBytecode(offset), S_OK);
  EXPECT_EQ(*static_cast<const uint8_t *>(parser.GetBlob(0)), 42u);
  EXPECT_EQ(parser.GetBlob(0), storage.data() + offset +
                                   sizeof(microsoft::DXBCHeader) + 4 +
                                   sizeof(microsoft::DXBCBlobHeader));
}

TEST_P(DxbcInvalidContainerTest, RejectsMalformedBoundsAndClearsOldState) {
  auto bytes = DxbcContainerBuilder()
                   .add(microsoft::DXBC_GenericShader, {1, 2, 3, 4})
                   .build();

  switch (GetParam()) {
  case ContainerCorruption::TruncatedHeader:
    bytes.resize(sizeof(microsoft::DXBCHeader) - 1);
    break;
  case ContainerCorruption::BadMagic:
    Store<uint32_t>(bytes, 0, 0u);
    break;
  case ContainerCorruption::WrongVersion:
    Store<uint16_t>(bytes, offsetof(microsoft::DXBCHeader, Version), 2u);
    break;
  case ContainerCorruption::DeclaredSizeMismatch:
    Store<uint32_t>(bytes,
                    offsetof(microsoft::DXBCHeader, ContainerSizeInBytes),
                    bytes.size() + 1);
    break;
  case ContainerCorruption::TruncatedIndex:
    bytes.resize(sizeof(microsoft::DXBCHeader) + sizeof(uint32_t) - 1);
    Store<uint32_t>(bytes,
                    offsetof(microsoft::DXBCHeader, ContainerSizeInBytes),
                    bytes.size());
    break;
  case ContainerCorruption::OffsetInsideIndex:
    Store<uint32_t>(bytes, sizeof(microsoft::DXBCHeader),
                    sizeof(microsoft::DXBCHeader));
    break;
  case ContainerCorruption::TruncatedBlobHeader:
    bytes.resize(sizeof(microsoft::DXBCHeader) + sizeof(uint32_t) + 4);
    Store<uint32_t>(bytes,
                    offsetof(microsoft::DXBCHeader, ContainerSizeInBytes),
                    bytes.size());
    break;
  case ContainerCorruption::TruncatedBlobPayload:
    bytes.pop_back();
    Store<uint32_t>(bytes,
                    offsetof(microsoft::DXBCHeader, ContainerSizeInBytes),
                    bytes.size());
    break;
  }

  const auto valid = DxbcContainerBuilder().build();
  microsoft::CDXBCParser parser;
  ASSERT_EQ(parser.ReadDXBC(valid.data(), valid.size()), S_OK);
  EXPECT_EQ(parser.ReadDXBC(bytes.data(), bytes.size()), E_FAIL);
  EXPECT_EQ(parser.GetBlobCount(), 0u);
  EXPECT_EQ(parser.GetVersion(), nullptr);
}

INSTANTIATE_TEST_SUITE_P(
    HeaderAndPartBounds, DxbcInvalidContainerTest,
    testing::Values(ContainerCorruption::TruncatedHeader,
                    ContainerCorruption::BadMagic,
                    ContainerCorruption::WrongVersion,
                    ContainerCorruption::DeclaredSizeMismatch,
                    ContainerCorruption::TruncatedIndex,
                    ContainerCorruption::OffsetInsideIndex,
                    ContainerCorruption::TruncatedBlobHeader,
                    ContainerCorruption::TruncatedBlobPayload));

TEST(DxbcSignature, ParsesCopiesAndFindsParametersCaseInsensitively) {
  auto bytes = SignatureBuilder()
                   .add("POSITION", 0, 0, D3D10_NAME_POSITION)
                   .add("TEXCOORD", 2)
                   .build();
  microsoft::CSignatureParser parser;
  ASSERT_EQ(parser.ReadSignature4(bytes.data(), bytes.size()), S_OK);

  const microsoft::D3D11_SIGNATURE_PARAMETER *parameters = nullptr;
  ASSERT_EQ(parser.GetParameters(&parameters), 2u);
  ASSERT_NE(parameters, nullptr);
  EXPECT_STREQ(parameters[0].SemanticName, "POSITION");
  EXPECT_EQ(parameters[0].SystemValue, microsoft::D3D10_SB_NAME_POSITION);
  EXPECT_EQ(parameters[1].Register, 2u);
  EXPECT_EQ(parameters[1].ComponentType,
            microsoft::D3D10_SB_REGISTER_COMPONENT_FLOAT32);

  std::fill(bytes.begin(), bytes.end(), 0);
  EXPECT_STREQ(parameters[0].SemanticName, "POSITION");

  microsoft::D3D11_SIGNATURE_PARAMETER *found = nullptr;
  EXPECT_EQ(parser.FindParameter("position", 0, &found), S_OK);
  EXPECT_EQ(found, parameters);
  UINT register_index = 0;
  EXPECT_EQ(parser.FindParameterRegister("TeXcOoRd", 0, &register_index), S_OK);
  EXPECT_EQ(register_index, 2u);
  EXPECT_EQ(parser.FindParameter(nullptr, 0, &found), E_FAIL);
  EXPECT_EQ(found, nullptr);
  EXPECT_EQ(parser.GetSemanticNameCharSum(2), 0u);
}

TEST(DxbcSignature, ExtractsInputSignatureFromContainer) {
  const auto signature = SignatureBuilder().add("COLOR", 1).build();
  const auto container = DxbcContainerBuilder()
                             .add(microsoft::DXBC_InputSignature, signature)
                             .add(microsoft::DXBC_GenericShader, {0, 0, 0, 0})
                             .build();

  microsoft::CSignatureParser parser;
  ASSERT_EQ(microsoft::DXBCGetInputSignature(container.data(), &parser), S_OK);
  const microsoft::D3D11_SIGNATURE_PARAMETER *parameters = nullptr;
  ASSERT_EQ(parser.GetParameters(&parameters), 1u);
  EXPECT_STREQ(parameters[0].SemanticName, "COLOR");
  EXPECT_EQ(parameters[0].Register, 1u);
}

TEST(DxbcSignature, ChecksLinkageMasksAndCanClearThem) {
  const auto source_bytes = SignatureBuilder().add("COLOR", 0, 0x1).build();
  const auto target_bytes = SignatureBuilder().add("COLOR", 0, 0x1).build();
  microsoft::CSignatureParser source;
  microsoft::CSignatureParser target;
  ASSERT_EQ(source.ReadSignature4(source_bytes.data(), source_bytes.size()),
            S_OK);
  ASSERT_EQ(target.ReadSignature4(target_bytes.data(), target_bytes.size()),
            S_OK);

  EXPECT_FALSE(source.CanOutputTo(&target));
  source.ClearAlwaysReadsNeverWritesMask();
  target.ClearAlwaysReadsNeverWritesMask();
  EXPECT_TRUE(source.CanOutputTo(&target));
  EXPECT_FALSE(source.CanOutputTo(nullptr));
}

TEST(DxbcSignature, AcceptsAnEmptySignature) {
  const auto bytes = SignatureBuilder().build();
  microsoft::CSignatureParser parser;
  EXPECT_EQ(parser.ReadSignature4(bytes.data(), bytes.size()), S_OK);
  EXPECT_EQ(parser.GetNumParameters(), 0u);
}

TEST_P(DxbcInvalidSignatureTest, RejectsMalformedTableAndStringRanges) {
  auto bytes = SignatureBuilder().add("A", 0).add("B", 1).build();

  switch (GetParam()) {
  case SignatureCorruption::ParameterTableBeforeHeader:
    Store<uint32_t>(bytes, 4, 0u);
    break;
  case SignatureCorruption::ParameterTableOutOfBounds:
    Store<uint32_t>(bytes, 4, bytes.size() - 4);
    break;
  case SignatureCorruption::SemanticNameOutOfBounds:
    Store<uint32_t>(bytes, 8, bytes.size());
    break;
  case SignatureCorruption::UnterminatedSemanticName:
    bytes.back() = 'B';
    break;
  case SignatureCorruption::DescendingRegisters:
    Store<uint32_t>(bytes, 8 + sizeof(SignatureParameter4) + 16, 0u);
    Store<uint32_t>(bytes, 8 + 16, 1u);
    break;
  }

  microsoft::CSignatureParser parser;
  EXPECT_EQ(parser.ReadSignature4(bytes.data(), bytes.size()), E_FAIL);
  EXPECT_EQ(parser.GetNumParameters(), 0u);
}

INSTANTIATE_TEST_SUITE_P(
    TableAndStringBounds, DxbcInvalidSignatureTest,
    testing::Values(SignatureCorruption::ParameterTableBeforeHeader,
                    SignatureCorruption::ParameterTableOutOfBounds,
                    SignatureCorruption::SemanticNameOutOfBounds,
                    SignatureCorruption::UnterminatedSemanticName,
                    SignatureCorruption::DescendingRegisters));
