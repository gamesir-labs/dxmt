#include <dxmt_test.hpp>

#include "DXBCParser/BlobContainer.h"
#include "DXBCParser/DXBCUtils.h"

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

struct SignatureParameter5 {
  uint32_t stream;
  uint32_t semantic_name;
  uint32_t semantic_index;
  D3D10_NAME system_value;
  D3D10_REGISTER_COMPONENT_TYPE component_type;
  uint32_t register_index;
  uint8_t mask;
  uint8_t read_write_mask;
  uint16_t padding = 0;
};

static_assert(sizeof(SignatureParameter5) == 28);

struct SignatureParameter11_1 {
  uint32_t stream;
  uint32_t semantic_name;
  uint32_t semantic_index;
  D3D10_NAME system_value;
  D3D10_REGISTER_COMPONENT_TYPE component_type;
  uint32_t register_index;
  uint8_t mask;
  uint8_t read_write_mask;
  uint16_t padding = 0;
  D3D_MIN_PRECISION min_precision;
};

static_assert(sizeof(SignatureParameter11_1) == 32);

template <typename Parameter>
std::vector<uint8_t>
BuildStreamSignature(std::vector<Parameter> parameters,
                     const std::vector<std::string> &names) {
  if (parameters.size() != names.size())
    return {};

  constexpr size_t header_size = 2 * sizeof(uint32_t);
  const size_t strings_offset = header_size + parameters.size() * sizeof(Parameter);
  size_t total_size = strings_offset;
  for (const auto &name : names)
    total_size += name.size() + 1;

  std::vector<uint8_t> bytes(total_size);
  Store<uint32_t>(bytes, 0, parameters.size());
  Store<uint32_t>(bytes, 4, header_size);

  size_t name_offset = strings_offset;
  for (size_t i = 0; i < parameters.size(); ++i) {
    parameters[i].semantic_name = name_offset;
    Store(bytes, header_size + i * sizeof(Parameter), parameters[i]);
    std::copy(names[i].begin(), names[i].end(), bytes.begin() + name_offset);
    name_offset += names[i].size() + 1;
  }
  return bytes;
}

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

TEST(DxbcContainer, AcceptsUnknownAndDuplicatePartKinds) {
  constexpr auto unknown =
      static_cast<microsoft::DXBCFourCC>(0x5a5a5a5au);
  const auto bytes = DxbcContainerBuilder()
                         .add(unknown, {1, 2})
                         .add(microsoft::DXBC_ShaderFeatureInfo, {3})
                         .add(microsoft::DXBC_ShaderFeatureInfo, {4, 5})
                         .build();

  microsoft::CDXBCParser parser;
  ASSERT_EQ(parser.ReadDXBC(bytes.data(), bytes.size()), S_OK);
  ASSERT_EQ(parser.GetBlobCount(), 3u);
  EXPECT_EQ(parser.GetBlobFourCC(0), static_cast<uint32_t>(unknown));
  EXPECT_EQ(parser.FindNextMatchingBlob(microsoft::DXBC_ShaderFeatureInfo),
            1u);
  EXPECT_EQ(
      parser.FindNextMatchingBlob(microsoft::DXBC_ShaderFeatureInfo, 2), 2u);
}

TEST(DxbcContainer, RejectsAliasedAndOverflowingBlobRanges) {
  const auto seed = DxbcContainerBuilder().build();
  microsoft::CDXBCParser parser;
  const auto expect_rejected = [&](std::string_view case_name,
                                   const std::vector<uint8_t> &bytes) {
    SCOPED_TRACE(case_name);
    ASSERT_EQ(parser.ReadDXBC(seed.data(), seed.size()), S_OK);
    EXPECT_EQ(parser.ReadDXBC(bytes.data(), bytes.size()), E_FAIL);
    EXPECT_EQ(parser.GetBlobCount(), 0u);
    EXPECT_EQ(parser.GetVersion(), nullptr);
  };

  auto bytes = DxbcContainerBuilder()
                   .add(microsoft::DXBC_GenericShader, {1, 2, 3, 4})
                   .add(microsoft::DXBC_ShaderFeatureInfo, {5, 6, 7, 8})
                   .build();
  uint32_t first_offset = 0;
  std::memcpy(&first_offset, bytes.data() + sizeof(microsoft::DXBCHeader),
              sizeof(first_offset));
  Store<uint32_t>(bytes, sizeof(microsoft::DXBCHeader) + sizeof(uint32_t),
                  first_offset);
  expect_rejected("aliased blob", bytes);

  bytes = DxbcContainerBuilder()
              .add(microsoft::DXBC_GenericShader, {1, 2, 3, 4})
              .build();
  std::memcpy(&first_offset, bytes.data() + sizeof(microsoft::DXBCHeader),
              sizeof(first_offset));
  Store<uint32_t>(bytes,
                  first_offset + offsetof(microsoft::DXBCBlobHeader, BlobSize),
                  std::numeric_limits<uint32_t>::max());
  expect_rejected("blob size overflow", bytes);

  bytes = DxbcContainerBuilder()
              .add(microsoft::DXBC_GenericShader, {1, 2, 3, 4})
              .build();
  Store<uint32_t>(bytes, sizeof(microsoft::DXBCHeader),
                  std::numeric_limits<uint32_t>::max());
  expect_rejected("blob offset overflow", bytes);

  bytes = DxbcContainerBuilder().build();
  Store<uint32_t>(bytes, offsetof(microsoft::DXBCHeader, BlobCount),
                  std::numeric_limits<uint32_t>::max());
  expect_rejected("blob count overflow", bytes);
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

TEST(DxbcSignature, ParsesSignature5IntoIndependentStreams) {
  const auto bytes = BuildStreamSignature<SignatureParameter5>(
      {
          {.stream = 0,
           .semantic_index = 0,
           .system_value = D3D10_NAME_UNDEFINED,
           .component_type = D3D10_REGISTER_COMPONENT_FLOAT32,
           .register_index = 1,
           .mask = 0xf,
           .read_write_mask = 1},
          {.stream = 1,
           .semantic_index = 2,
           .system_value = D3D10_NAME_PRIMITIVE_ID,
           .component_type = D3D10_REGISTER_COMPONENT_UINT32,
           .register_index = 0,
           .mask = 1,
           .read_write_mask = 1},
          {.stream = 3,
           .semantic_index = 0,
           .system_value = D3D10_NAME_UNDEFINED,
           .component_type = D3D10_REGISTER_COMPONENT_FLOAT32,
           .register_index = 4,
           .mask = 3,
           .read_write_mask = 2},
      },
      {"POSITION", "PRIMITIVE", "COLOR"});

  microsoft::CSignatureParser5 parser;
  ASSERT_EQ(parser.ReadSignature5(bytes.data(), bytes.size()), S_OK);
  EXPECT_EQ(parser.NumStreams(), 4u);
  EXPECT_EQ(parser.GetTotalParameters(), 3u);
  EXPECT_EQ(parser.Signature(0)->GetNumParameters(), 1u);
  EXPECT_EQ(parser.Signature(1)->GetNumParameters(), 1u);
  EXPECT_EQ(parser.Signature(2)->GetNumParameters(), 0u);
  EXPECT_EQ(parser.Signature(3)->GetNumParameters(), 1u);

  const microsoft::D3D11_SIGNATURE_PARAMETER *parameters = nullptr;
  ASSERT_EQ(parser.Signature(1)->GetParameters(&parameters), 1u);
  ASSERT_NE(parameters, nullptr);
  EXPECT_EQ(parameters[0].Stream, 1u);
  EXPECT_STREQ(parameters[0].SemanticName, "PRIMITIVE");
  EXPECT_EQ(parameters[0].SemanticIndex, 2u);
  EXPECT_EQ(parameters[0].Register, 0u);
  EXPECT_EQ(parameters[0].SystemValue,
            microsoft::D3D10_SB_NAME_PRIMITIVE_ID);
  EXPECT_EQ(parameters[0].MinPrecision, D3D_MIN_PRECISION_DEFAULT);

  EXPECT_EQ(parser.RasterizedStream(), 0u);
  parser.SetRasterizedStream(3);
  EXPECT_EQ(parser.RastSignature(), parser.Signature(3));
  parser.SetRasterizedStream(D3D11_SO_STREAM_COUNT);
  EXPECT_EQ(parser.RastSignature(), nullptr);
}

TEST(DxbcSignature, PreservesSignature11_1MinimumPrecision) {
  const auto bytes = BuildStreamSignature<SignatureParameter11_1>(
      {
          {.stream = 0,
           .semantic_index = 0,
           .system_value = D3D10_NAME_UNDEFINED,
           .component_type = D3D10_REGISTER_COMPONENT_FLOAT32,
           .register_index = 0,
           .mask = 0xf,
           .read_write_mask = 0xf,
           .min_precision = D3D_MIN_PRECISION_FLOAT_16},
          {.stream = 0,
           .semantic_index = 1,
           .system_value = D3D10_NAME_UNDEFINED,
           .component_type = D3D10_REGISTER_COMPONENT_FLOAT32,
           .register_index = 1,
           .mask = 0x3,
           .read_write_mask = 0x1,
           .min_precision = D3D_MIN_PRECISION_DEFAULT},
      },
      {"TEXCOORD", "TEXCOORD"});

  microsoft::CSignatureParser5 parser;
  ASSERT_EQ(parser.ReadSignature11_1(bytes.data(), bytes.size()), S_OK);
  EXPECT_EQ(parser.NumStreams(), 1u);
  EXPECT_EQ(parser.GetTotalParameters(), 2u);
  const microsoft::D3D11_SIGNATURE_PARAMETER *parameters = nullptr;
  ASSERT_EQ(parser.Signature(0)->GetParameters(&parameters), 2u);
  ASSERT_NE(parameters, nullptr);
  EXPECT_EQ(parameters[0].MinPrecision, D3D_MIN_PRECISION_FLOAT_16);
  EXPECT_EQ(parameters[1].MinPrecision, D3D_MIN_PRECISION_DEFAULT);
  EXPECT_EQ(parameters[1].SemanticIndex, 1u);
}

TEST(DxbcSignature, RejectsMalformedMultiStreamSignatures) {
  const auto make_signature = [](uint32_t first_stream,
                                 uint32_t second_stream,
                                 uint32_t first_register,
                                 uint32_t second_register) {
    return BuildStreamSignature<SignatureParameter5>(
        {
            {.stream = first_stream,
             .system_value = D3D10_NAME_UNDEFINED,
             .component_type = D3D10_REGISTER_COMPONENT_FLOAT32,
             .register_index = first_register,
             .mask = 0xf},
            {.stream = second_stream,
             .system_value = D3D10_NAME_UNDEFINED,
             .component_type = D3D10_REGISTER_COMPONENT_FLOAT32,
             .register_index = second_register,
             .mask = 0xf},
        },
        {"A", "B"});
  };

  microsoft::CSignatureParser5 parser;
  auto bytes = make_signature(4, 4, 0, 1);
  EXPECT_EQ(parser.ReadSignature5(bytes.data(), bytes.size()), E_FAIL);

  bytes = make_signature(1, 0, 0, 1);
  EXPECT_EQ(parser.ReadSignature5(bytes.data(), bytes.size()), E_FAIL);

  bytes = make_signature(0, 0, 2, 1);
  EXPECT_EQ(parser.ReadSignature5(bytes.data(), bytes.size()), E_FAIL);

  bytes = make_signature(0, 1, 0, 0);
  bytes.resize(2 * sizeof(uint32_t) + 2 * sizeof(SignatureParameter5) - 1);
  EXPECT_EQ(parser.ReadSignature5(bytes.data(), bytes.size()), E_FAIL);

  bytes = make_signature(0, 1, 0, 0);
  bytes.pop_back();
  EXPECT_EQ(parser.ReadSignature5(bytes.data(), bytes.size()), E_FAIL);
}

TEST(DxbcSignature, ExtractsSignature11_1OutputFromContainer) {
  const auto signature = BuildStreamSignature<SignatureParameter11_1>(
      {{.stream = 2,
        .semantic_index = 0,
        .system_value = D3D10_NAME_UNDEFINED,
        .component_type = D3D10_REGISTER_COMPONENT_FLOAT32,
        .register_index = 0,
        .mask = 0xf,
        .min_precision = D3D_MIN_PRECISION_FLOAT_16}},
      {"COLOR"});
  const auto container =
      DxbcContainerBuilder()
          .add(microsoft::DXBC_OutputSignature11_1, signature)
          .build();

  microsoft::CSignatureParser5 parser;
  ASSERT_EQ(microsoft::DXBCGetOutputSignature(container.data(), &parser),
            S_OK);
  EXPECT_EQ(parser.NumStreams(), 3u);
  EXPECT_EQ(parser.Signature(2)->GetNumParameters(), 1u);
}

TEST(DxbcSignature, ExtractsEveryContainerVariantAndHonorsPrecedence) {
  const auto signature4 =
      SignatureBuilder().add("LEGACY", 1).build();
  const auto signature5 = BuildStreamSignature<SignatureParameter5>(
      {{.stream = 2,
        .semantic_index = 5,
        .system_value = D3D10_NAME_UNDEFINED,
        .component_type = D3D10_REGISTER_COMPONENT_UINT32,
        .register_index = 3,
        .mask = 0x3}},
      {"STREAM"});
  const auto signature11 = BuildStreamSignature<SignatureParameter11_1>(
      {{.stream = 0,
        .semantic_index = 7,
        .system_value = D3D10_NAME_UNDEFINED,
        .component_type = D3D10_REGISTER_COMPONENT_FLOAT32,
        .register_index = 4,
        .mask = 0xf,
        .min_precision = D3D_MIN_PRECISION_FLOAT_16}},
      {"MODERN"});

  const auto input_container =
      DxbcContainerBuilder()
          .add(microsoft::DXBC_InputSignature, signature4)
          .add(microsoft::DXBC_InputSignature11_1, signature11)
          .build();
  microsoft::CSignatureParser input;
  ASSERT_EQ(microsoft::DXBCGetInputSignature(input_container.data(), &input),
            S_OK);
  const microsoft::D3D11_SIGNATURE_PARAMETER *parameters = nullptr;
  ASSERT_EQ(input.GetParameters(&parameters), 1u);
  EXPECT_STREQ(parameters[0].SemanticName, "MODERN");
  EXPECT_EQ(parameters[0].SemanticIndex, 7u);
  EXPECT_EQ(parameters[0].MinPrecision, D3D_MIN_PRECISION_FLOAT_16);

  const auto output_container =
      DxbcContainerBuilder()
          .add(microsoft::DXBC_OutputSignature, signature4)
          .add(microsoft::DXBC_OutputSignature5, signature5)
          .add(microsoft::DXBC_OutputSignature11_1, signature11)
          .build();
  microsoft::CSignatureParser5 output;
  ASSERT_EQ(microsoft::DXBCGetOutputSignature(output_container.data(),
                                               &output),
            S_OK);
  EXPECT_EQ(output.NumStreams(), 1u);
  ASSERT_EQ(output.Signature(0)->GetParameters(&parameters), 1u);
  EXPECT_STREQ(parameters[0].SemanticName, "MODERN");

  const auto output5_container =
      DxbcContainerBuilder()
          .add(microsoft::DXBC_OutputSignature5, signature5)
          .build();
  ASSERT_EQ(microsoft::DXBCGetOutputSignature(output5_container.data(),
                                               &output),
            S_OK);
  EXPECT_EQ(output.NumStreams(), 3u);
  ASSERT_EQ(output.Signature(2)->GetParameters(&parameters), 1u);
  EXPECT_STREQ(parameters[0].SemanticName, "STREAM");
  EXPECT_EQ(parameters[0].Stream, 2u);

  const auto output4_container =
      DxbcContainerBuilder()
          .add(microsoft::DXBC_OutputSignature, signature4)
          .build();
  ASSERT_EQ(microsoft::DXBCGetOutputSignature(output4_container.data(),
                                               &output),
            S_OK);
  EXPECT_EQ(output.NumStreams(), 1u);
  ASSERT_EQ(output.Signature(0)->GetParameters(&parameters), 1u);
  EXPECT_STREQ(parameters[0].SemanticName, "LEGACY");

  microsoft::CSignatureParser flat_output;
  ASSERT_EQ(microsoft::DXBCGetOutputSignature(output_container.data(),
                                               &flat_output),
            S_OK);
  ASSERT_EQ(flat_output.GetParameters(&parameters), 1u);
  EXPECT_STREQ(parameters[0].SemanticName, "MODERN");
  ASSERT_EQ(microsoft::DXBCGetOutputSignature(output4_container.data(),
                                               &flat_output),
            S_OK);
  ASSERT_EQ(flat_output.GetParameters(&parameters), 1u);
  EXPECT_STREQ(parameters[0].SemanticName, "LEGACY");
  EXPECT_EQ(microsoft::DXBCGetOutputSignature(output5_container.data(),
                                               &flat_output),
            E_FAIL);

  const auto patch_container =
      DxbcContainerBuilder()
          .add(microsoft::DXBC_PatchConstantSignature, signature4)
          .add(microsoft::DXBC_PatchConstantSignature11_1, signature11)
          .build();
  microsoft::CSignatureParser patch;
  ASSERT_EQ(microsoft::DXBCGetPatchConstantSignature(patch_container.data(),
                                                      &patch),
            S_OK);
  ASSERT_EQ(patch.GetParameters(&parameters), 1u);
  EXPECT_STREQ(parameters[0].SemanticName, "MODERN");
  const auto legacy_patch_container =
      DxbcContainerBuilder()
          .add(microsoft::DXBC_PatchConstantSignature, signature4)
          .build();
  ASSERT_EQ(microsoft::DXBCGetPatchConstantSignature(
                legacy_patch_container.data(), &patch),
            S_OK);
  ASSERT_EQ(patch.GetParameters(&parameters), 1u);
  EXPECT_STREQ(parameters[0].SemanticName, "LEGACY");

  const auto empty_container = DxbcContainerBuilder().build();
  EXPECT_EQ(microsoft::DXBCGetInputSignature(empty_container.data(), &input),
            E_FAIL);
  EXPECT_EQ(microsoft::DXBCGetOutputSignature(empty_container.data(), &output),
            E_FAIL);
  EXPECT_EQ(microsoft::DXBCGetPatchConstantSignature(empty_container.data(),
                                                      &patch),
            E_FAIL);
  const auto empty_input_blob =
      DxbcContainerBuilder()
          .add(microsoft::DXBC_InputSignature, {})
          .build();
  EXPECT_EQ(microsoft::DXBCGetInputSignature(empty_input_blob.data(), &input),
            E_FAIL);
}

TEST(DxbcSignature, SupportsReferencedStringsAndExternalParameterStorage) {
  auto signature = SignatureBuilder().add("REFERENCE", 6).build();
  const auto string_offset =
      2u * sizeof(uint32_t) + sizeof(SignatureParameter4);
  microsoft::CSignatureParser parser;
  ASSERT_EQ(parser.ReadSignature4(signature.data(), signature.size(), true),
            S_OK);
  const microsoft::D3D11_SIGNATURE_PARAMETER *parameters = nullptr;
  ASSERT_EQ(parser.GetParameters(&parameters), 1u);
  ASSERT_NE(parameters, nullptr);
  EXPECT_EQ(parameters[0].SemanticName,
            reinterpret_cast<char *>(signature.data() + string_offset));
  signature[string_offset] = 'X';
  EXPECT_STREQ(parameters[0].SemanticName, "XEFERENCE");
  EXPECT_EQ(parser.GetParameters(nullptr), 1u);

  std::array<char, 4> name0 = {'T', 'E', 'X', 0};
  std::array<char, 4> name1 = {'C', 'O', 'L', 0};
  std::array<microsoft::D3D11_SIGNATURE_PARAMETER, 2> external = {{
      {.SemanticName = name0.data(),
       .SemanticIndex = 0,
       .ComponentType = microsoft::D3D10_SB_REGISTER_COMPONENT_FLOAT32,
       .Register = 2,
       .Mask = 0xf},
      {.SemanticName = name1.data(),
       .SemanticIndex = 1,
       .ComponentType = microsoft::D3D10_SB_REGISTER_COMPONENT_UINT32,
       .Register = 3,
       .Mask = 0x3},
  }};
  const auto char_sum = [](std::string_view text) {
    UINT sum = 0;
    for (const auto value : text)
      sum += static_cast<unsigned char>(value >= 'A' && value <= 'Z'
                                            ? value - 'A' + 'a'
                                            : value);
    return sum;
  };
  std::array<UINT, 2> sums = {char_sum("TEX"), char_sum("COL")};
  ASSERT_EQ(parser.ReadSignature5(external.data(), sums.data(),
                                  external.size()),
            S_OK);
  ASSERT_EQ(parser.GetParameters(&parameters), external.size());
  EXPECT_EQ(parameters, external.data());
  microsoft::D3D11_SIGNATURE_PARAMETER *found = nullptr;
  EXPECT_EQ(parser.FindParameter("col", 1, &found), S_OK);
  EXPECT_EQ(found, &external[1]);
  EXPECT_EQ(parser.ReadSignature11_1(nullptr, nullptr, 0), S_OK);
  EXPECT_EQ(parser.GetNumParameters(), 0u);
  name0[0] = 'Y';
  EXPECT_EQ(name0[0], 'Y');
}

TEST(DxbcSignature, ChecksEveryLinkageCompatibilityField) {
  const auto can_link = [](SignatureParameter4 source_parameter,
                           SignatureParameter4 target_parameter,
                           std::string source_name = "VALUE",
                           std::string target_name = "VALUE") {
    const auto source_bytes = BuildStreamSignature<SignatureParameter4>(
        {source_parameter}, {std::move(source_name)});
    const auto target_bytes = BuildStreamSignature<SignatureParameter4>(
        {target_parameter}, {std::move(target_name)});
    microsoft::CSignatureParser source;
    microsoft::CSignatureParser target;
    if (source.ReadSignature4(source_bytes.data(), source_bytes.size()) != S_OK ||
        target.ReadSignature4(target_bytes.data(), target_bytes.size()) != S_OK)
      return false;
    return source.CanOutputTo(&target);
  };
  const SignatureParameter4 base = {
      .semantic_index = 1,
      .system_value = D3D10_NAME_POSITION,
      .component_type = D3D10_REGISTER_COMPONENT_FLOAT32,
      .register_index = 2,
      .mask = 0xf,
  };
  EXPECT_TRUE(can_link(base, base));
  EXPECT_FALSE(can_link(base, base, "VALUE", "OTHER"));

  auto target = base;
  target.semantic_index = 2;
  EXPECT_FALSE(can_link(base, target));
  target = base;
  target.register_index = 3;
  EXPECT_FALSE(can_link(base, target));
  target = base;
  target.system_value = D3D10_NAME_PRIMITIVE_ID;
  EXPECT_FALSE(can_link(base, target));
  target = base;
  target.component_type = D3D10_REGISTER_COMPONENT_UINT32;
  EXPECT_FALSE(can_link(base, target));
  target = base;
  target.mask = 0x1;
  auto source = base;
  source.mask = 0x2;
  EXPECT_FALSE(can_link(source, target));
  source = base;
  target = base;
  source.read_write_mask = 0x4;
  target.read_write_mask = 0x4;
  EXPECT_FALSE(can_link(source, target));

  const auto one = BuildStreamSignature<SignatureParameter4>({base}, {"A"});
  auto second = base;
  second.register_index = 3;
  const auto two = BuildStreamSignature<SignatureParameter4>({base, second},
                                                              {"A", "B"});
  microsoft::CSignatureParser short_source;
  microsoft::CSignatureParser long_target;
  ASSERT_EQ(short_source.ReadSignature4(one.data(), one.size()), S_OK);
  ASSERT_EQ(long_target.ReadSignature4(two.data(), two.size()), S_OK);
  EXPECT_FALSE(short_source.CanOutputTo(&long_target));
}

TEST(DxbcSignature, MapsEverySupportedSystemValueAndComponentType) {
  struct SystemValueCase {
    D3D10_NAME value;
    uint32_t semantic_index;
    microsoft::D3D10_SB_NAME expected;
  };
  constexpr std::array system_values = {
      SystemValueCase{D3D10_NAME_TARGET, 0,
                      microsoft::D3D10_SB_NAME_UNDEFINED},
      SystemValueCase{D3D10_NAME_DEPTH, 0,
                      microsoft::D3D10_SB_NAME_UNDEFINED},
      SystemValueCase{D3D10_NAME_COVERAGE, 0,
                      microsoft::D3D10_SB_NAME_UNDEFINED},
      SystemValueCase{D3D10_NAME_UNDEFINED, 0,
                      microsoft::D3D10_SB_NAME_UNDEFINED},
      SystemValueCase{D3D10_NAME_POSITION, 0,
                      microsoft::D3D10_SB_NAME_POSITION},
      SystemValueCase{D3D10_NAME_CLIP_DISTANCE, 0,
                      microsoft::D3D10_SB_NAME_CLIP_DISTANCE},
      SystemValueCase{D3D10_NAME_CULL_DISTANCE, 0,
                      microsoft::D3D10_SB_NAME_CULL_DISTANCE},
      SystemValueCase{D3D10_NAME_RENDER_TARGET_ARRAY_INDEX, 0,
                      microsoft::D3D10_SB_NAME_RENDER_TARGET_ARRAY_INDEX},
      SystemValueCase{D3D10_NAME_VIEWPORT_ARRAY_INDEX, 0,
                      microsoft::D3D10_SB_NAME_VIEWPORT_ARRAY_INDEX},
      SystemValueCase{D3D10_NAME_VERTEX_ID, 0,
                      microsoft::D3D10_SB_NAME_VERTEX_ID},
      SystemValueCase{D3D10_NAME_PRIMITIVE_ID, 0,
                      microsoft::D3D10_SB_NAME_PRIMITIVE_ID},
      SystemValueCase{D3D10_NAME_INSTANCE_ID, 0,
                      microsoft::D3D10_SB_NAME_INSTANCE_ID},
      SystemValueCase{D3D10_NAME_IS_FRONT_FACE, 0,
                      microsoft::D3D10_SB_NAME_IS_FRONT_FACE},
      SystemValueCase{D3D10_NAME_SAMPLE_INDEX, 0,
                      microsoft::D3D10_SB_NAME_SAMPLE_INDEX},
      SystemValueCase{D3D11_NAME_FINAL_QUAD_EDGE_TESSFACTOR, 0,
                      microsoft::D3D11_SB_NAME_FINAL_QUAD_U_EQ_0_EDGE_TESSFACTOR},
      SystemValueCase{D3D11_NAME_FINAL_QUAD_EDGE_TESSFACTOR, 1,
                      microsoft::D3D11_SB_NAME_FINAL_QUAD_V_EQ_0_EDGE_TESSFACTOR},
      SystemValueCase{D3D11_NAME_FINAL_QUAD_EDGE_TESSFACTOR, 2,
                      microsoft::D3D11_SB_NAME_FINAL_QUAD_U_EQ_1_EDGE_TESSFACTOR},
      SystemValueCase{D3D11_NAME_FINAL_QUAD_EDGE_TESSFACTOR, 3,
                      microsoft::D3D11_SB_NAME_FINAL_QUAD_V_EQ_1_EDGE_TESSFACTOR},
      SystemValueCase{D3D11_NAME_FINAL_QUAD_INSIDE_TESSFACTOR, 0,
                      microsoft::D3D11_SB_NAME_FINAL_QUAD_U_INSIDE_TESSFACTOR},
      SystemValueCase{D3D11_NAME_FINAL_QUAD_INSIDE_TESSFACTOR, 1,
                      microsoft::D3D11_SB_NAME_FINAL_QUAD_V_INSIDE_TESSFACTOR},
      SystemValueCase{D3D11_NAME_FINAL_TRI_EDGE_TESSFACTOR, 0,
                      microsoft::D3D11_SB_NAME_FINAL_TRI_U_EQ_0_EDGE_TESSFACTOR},
      SystemValueCase{D3D11_NAME_FINAL_TRI_EDGE_TESSFACTOR, 1,
                      microsoft::D3D11_SB_NAME_FINAL_TRI_V_EQ_0_EDGE_TESSFACTOR},
      SystemValueCase{D3D11_NAME_FINAL_TRI_EDGE_TESSFACTOR, 2,
                      microsoft::D3D11_SB_NAME_FINAL_TRI_W_EQ_0_EDGE_TESSFACTOR},
      SystemValueCase{D3D11_NAME_FINAL_TRI_INSIDE_TESSFACTOR, 0,
                      microsoft::D3D11_SB_NAME_FINAL_TRI_INSIDE_TESSFACTOR},
      SystemValueCase{D3D11_NAME_FINAL_LINE_DETAIL_TESSFACTOR, 1,
                      microsoft::D3D11_SB_NAME_FINAL_LINE_DETAIL_TESSFACTOR},
      SystemValueCase{D3D11_NAME_FINAL_LINE_DENSITY_TESSFACTOR, 0,
                      microsoft::D3D11_SB_NAME_FINAL_LINE_DENSITY_TESSFACTOR},
  };
  constexpr std::array component_types = {
      std::pair{D3D10_REGISTER_COMPONENT_UNKNOWN,
                microsoft::D3D10_SB_REGISTER_COMPONENT_UNKNOWN},
      std::pair{D3D10_REGISTER_COMPONENT_UINT32,
                microsoft::D3D10_SB_REGISTER_COMPONENT_UINT32},
      std::pair{D3D10_REGISTER_COMPONENT_SINT32,
                microsoft::D3D10_SB_REGISTER_COMPONENT_SINT32},
      std::pair{D3D10_REGISTER_COMPONENT_FLOAT32,
                microsoft::D3D10_SB_REGISTER_COMPONENT_FLOAT32},
  };

  std::vector<SignatureParameter4> encoded;
  std::vector<std::string> names;
  encoded.reserve(system_values.size());
  names.reserve(system_values.size());
  for (size_t i = 0; i < system_values.size(); ++i) {
    encoded.push_back({
        .semantic_index = system_values[i].semantic_index,
        .system_value = system_values[i].value,
        .component_type = component_types[i % component_types.size()].first,
        .register_index = static_cast<uint32_t>(i),
        .mask = 0xf,
    });
    names.push_back("SEMANTIC" + std::to_string(i));
  }

  const auto bytes =
      BuildStreamSignature<SignatureParameter4>(encoded, names);
  microsoft::CSignatureParser parser;
  ASSERT_EQ(parser.ReadSignature4(bytes.data(), bytes.size()), S_OK);
  const microsoft::D3D11_SIGNATURE_PARAMETER *parameters = nullptr;
  ASSERT_EQ(parser.GetParameters(&parameters), system_values.size());
  ASSERT_NE(parameters, nullptr);
  for (size_t i = 0; i < system_values.size(); ++i) {
    SCOPED_TRACE(i);
    EXPECT_EQ(parameters[i].SystemValue, system_values[i].expected);
    EXPECT_EQ(parameters[i].ComponentType,
              component_types[i % component_types.size()].second);
    EXPECT_EQ(parameters[i].SemanticIndex,
              system_values[i].semantic_index);
  }
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
