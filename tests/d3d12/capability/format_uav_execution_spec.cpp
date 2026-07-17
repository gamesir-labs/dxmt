#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace {

using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

enum class ShaderScalar : UINT {
  Float,
  Uint,
  Sint,
};

enum class StorageEncoding : UINT {
  Unorm,
  Snorm,
  Float16,
  Float32,
  Uint,
  Sint,
};

struct TypedUavCase {
  DXGI_FORMAT format;
  ShaderScalar scalar;
  StorageEncoding encoding;
  UINT component_bits;
  UINT components;
  const char *name;
};

constexpr std::array kTypedUavCases = {
    // Floating-point shader values, including the normalized 8- and 16-bit
    // formats exposed to HLSL as float vectors.
    TypedUavCase{DXGI_FORMAT_R8_UNORM, ShaderScalar::Float,
                 StorageEncoding::Unorm, 8, 1, "R8Unorm"},
    TypedUavCase{DXGI_FORMAT_R8_SNORM, ShaderScalar::Float,
                 StorageEncoding::Snorm, 8, 1, "R8Snorm"},
    TypedUavCase{DXGI_FORMAT_R8G8_UNORM, ShaderScalar::Float,
                 StorageEncoding::Unorm, 8, 2, "Rg8Unorm"},
    TypedUavCase{DXGI_FORMAT_R8G8_SNORM, ShaderScalar::Float,
                 StorageEncoding::Snorm, 8, 2, "Rg8Snorm"},
    TypedUavCase{DXGI_FORMAT_R8G8B8A8_UNORM, ShaderScalar::Float,
                 StorageEncoding::Unorm, 8, 4, "Rgba8Unorm"},
    TypedUavCase{DXGI_FORMAT_R8G8B8A8_SNORM, ShaderScalar::Float,
                 StorageEncoding::Snorm, 8, 4, "Rgba8Snorm"},
    TypedUavCase{DXGI_FORMAT_R16_FLOAT, ShaderScalar::Float,
                 StorageEncoding::Float16, 16, 1, "R16Float"},
    TypedUavCase{DXGI_FORMAT_R16_UNORM, ShaderScalar::Float,
                 StorageEncoding::Unorm, 16, 1, "R16Unorm"},
    TypedUavCase{DXGI_FORMAT_R16_SNORM, ShaderScalar::Float,
                 StorageEncoding::Snorm, 16, 1, "R16Snorm"},
    TypedUavCase{DXGI_FORMAT_R16G16_FLOAT, ShaderScalar::Float,
                 StorageEncoding::Float16, 16, 2, "Rg16Float"},
    TypedUavCase{DXGI_FORMAT_R16G16_UNORM, ShaderScalar::Float,
                 StorageEncoding::Unorm, 16, 2, "Rg16Unorm"},
    TypedUavCase{DXGI_FORMAT_R16G16_SNORM, ShaderScalar::Float,
                 StorageEncoding::Snorm, 16, 2, "Rg16Snorm"},
    TypedUavCase{DXGI_FORMAT_R16G16B16A16_FLOAT, ShaderScalar::Float,
                 StorageEncoding::Float16, 16, 4, "Rgba16Float"},
    TypedUavCase{DXGI_FORMAT_R16G16B16A16_UNORM, ShaderScalar::Float,
                 StorageEncoding::Unorm, 16, 4, "Rgba16Unorm"},
    TypedUavCase{DXGI_FORMAT_R16G16B16A16_SNORM, ShaderScalar::Float,
                 StorageEncoding::Snorm, 16, 4, "Rgba16Snorm"},
    TypedUavCase{DXGI_FORMAT_R32_FLOAT, ShaderScalar::Float,
                 StorageEncoding::Float32, 32, 1, "R32Float"},
    TypedUavCase{DXGI_FORMAT_R32G32_FLOAT, ShaderScalar::Float,
                 StorageEncoding::Float32, 32, 2, "Rg32Float"},
    TypedUavCase{DXGI_FORMAT_R32G32B32_FLOAT, ShaderScalar::Float,
                 StorageEncoding::Float32, 32, 3, "Rgb32Float"},
    TypedUavCase{DXGI_FORMAT_R32G32B32A32_FLOAT, ShaderScalar::Float,
                 StorageEncoding::Float32, 32, 4, "Rgba32Float"},

    TypedUavCase{DXGI_FORMAT_R8_UINT, ShaderScalar::Uint, StorageEncoding::Uint,
                 8, 1, "R8Uint"},
    TypedUavCase{DXGI_FORMAT_R8G8_UINT, ShaderScalar::Uint,
                 StorageEncoding::Uint, 8, 2, "Rg8Uint"},
    TypedUavCase{DXGI_FORMAT_R8G8B8A8_UINT, ShaderScalar::Uint,
                 StorageEncoding::Uint, 8, 4, "Rgba8Uint"},
    TypedUavCase{DXGI_FORMAT_R16_UINT, ShaderScalar::Uint,
                 StorageEncoding::Uint, 16, 1, "R16Uint"},
    TypedUavCase{DXGI_FORMAT_R16G16_UINT, ShaderScalar::Uint,
                 StorageEncoding::Uint, 16, 2, "Rg16Uint"},
    TypedUavCase{DXGI_FORMAT_R16G16B16A16_UINT, ShaderScalar::Uint,
                 StorageEncoding::Uint, 16, 4, "Rgba16Uint"},
    TypedUavCase{DXGI_FORMAT_R32_UINT, ShaderScalar::Uint,
                 StorageEncoding::Uint, 32, 1, "R32Uint"},
    TypedUavCase{DXGI_FORMAT_R32G32_UINT, ShaderScalar::Uint,
                 StorageEncoding::Uint, 32, 2, "Rg32Uint"},
    TypedUavCase{DXGI_FORMAT_R32G32B32_UINT, ShaderScalar::Uint,
                 StorageEncoding::Uint, 32, 3, "Rgb32Uint"},
    TypedUavCase{DXGI_FORMAT_R32G32B32A32_UINT, ShaderScalar::Uint,
                 StorageEncoding::Uint, 32, 4, "Rgba32Uint"},

    TypedUavCase{DXGI_FORMAT_R8_SINT, ShaderScalar::Sint, StorageEncoding::Sint,
                 8, 1, "R8Sint"},
    TypedUavCase{DXGI_FORMAT_R8G8_SINT, ShaderScalar::Sint,
                 StorageEncoding::Sint, 8, 2, "Rg8Sint"},
    TypedUavCase{DXGI_FORMAT_R8G8B8A8_SINT, ShaderScalar::Sint,
                 StorageEncoding::Sint, 8, 4, "Rgba8Sint"},
    TypedUavCase{DXGI_FORMAT_R16_SINT, ShaderScalar::Sint,
                 StorageEncoding::Sint, 16, 1, "R16Sint"},
    TypedUavCase{DXGI_FORMAT_R16G16_SINT, ShaderScalar::Sint,
                 StorageEncoding::Sint, 16, 2, "Rg16Sint"},
    TypedUavCase{DXGI_FORMAT_R16G16B16A16_SINT, ShaderScalar::Sint,
                 StorageEncoding::Sint, 16, 4, "Rgba16Sint"},
    TypedUavCase{DXGI_FORMAT_R32_SINT, ShaderScalar::Sint,
                 StorageEncoding::Sint, 32, 1, "R32Sint"},
    TypedUavCase{DXGI_FORMAT_R32G32_SINT, ShaderScalar::Sint,
                 StorageEncoding::Sint, 32, 2, "Rg32Sint"},
    TypedUavCase{DXGI_FORMAT_R32G32B32_SINT, ShaderScalar::Sint,
                 StorageEncoding::Sint, 32, 3, "Rgb32Sint"},
    TypedUavCase{DXGI_FORMAT_R32G32B32A32_SINT, ShaderScalar::Sint,
                 StorageEncoding::Sint, 32, 4, "Rgba32Sint"},
};

const char *ScalarName(ShaderScalar scalar) {
  switch (scalar) {
  case ShaderScalar::Float:
    return "float";
  case ShaderScalar::Uint:
    return "uint";
  case ShaderScalar::Sint:
  default:
    return "int";
  }
}

UINT ScalarIndex(ShaderScalar scalar) { return static_cast<UINT>(scalar); }

UINT WidthIndex(UINT component_bits) {
  switch (component_bits) {
  case 8:
    return 0;
  case 16:
    return 1;
  case 32:
  default:
    return 2;
  }
}

std::string VectorType(const TypedUavCase &test) {
  std::string type = ScalarName(test.scalar);
  if (test.components != 1)
    type += std::to_string(test.components);
  return type;
}

std::string StoredValue(const TypedUavCase &test) {
  const char *component = nullptr;
  switch (test.scalar) {
  case ShaderScalar::Float:
    component = "1.0";
    break;
  case ShaderScalar::Uint:
    component = "7u";
    break;
  case ShaderScalar::Sint:
  default:
    component = "-7";
    break;
  }
  if (test.components == 1)
    return component;

  std::string value = VectorType(test) + "(";
  for (UINT index = 0; index < test.components; ++index) {
    if (index)
      value += ", ";
    value += component;
  }
  value += ")";
  return value;
}

std::vector<std::uint8_t> ExpectedElement(const TypedUavCase &test) {
  std::vector<std::uint8_t> component;
  switch (test.encoding) {
  case StorageEncoding::Unorm:
    component = test.component_bits == 8
                    ? std::vector<std::uint8_t>{0xff}
                    : std::vector<std::uint8_t>{0xff, 0xff};
    break;
  case StorageEncoding::Snorm:
    component = test.component_bits == 8
                    ? std::vector<std::uint8_t>{0x7f}
                    : std::vector<std::uint8_t>{0xff, 0x7f};
    break;
  case StorageEncoding::Float16:
    component = {0x00, 0x3c};
    break;
  case StorageEncoding::Float32:
    component = {0x00, 0x00, 0x80, 0x3f};
    break;
  case StorageEncoding::Uint:
    component.assign(test.component_bits / 8, 0x00);
    component[0] = 0x07;
    break;
  case StorageEncoding::Sint:
    component.assign(test.component_bits / 8, 0xff);
    component[0] = 0xf9;
    break;
  }

  std::vector<std::uint8_t> expected;
  expected.reserve(component.size() * test.components);
  for (UINT index = 0; index < test.components; ++index)
    expected.insert(expected.end(), component.begin(), component.end());
  return expected;
}

class FormatTypedUavExecutionSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);

    D3D12_DESCRIPTOR_RANGE range = {};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    range.NumDescriptors = 1;
    D3D12_ROOT_PARAMETER parameter = {};
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameter.DescriptorTable.NumDescriptorRanges = 1;
    parameter.DescriptorTable.pDescriptorRanges = &range;
    D3D12_ROOT_SIGNATURE_DESC desc = {};
    desc.NumParameters = 1;
    desc.pParameters = &parameter;
    root_ = context_.CreateRootSignature(desc);
    ASSERT_TRUE(root_);
  }

  D3D12_FEATURE_DATA_FORMAT_SUPPORT Support(DXGI_FORMAT format) {
    D3D12_FEATURE_DATA_FORMAT_SUPPORT support = {};
    support.Format = format;
    EXPECT_EQ(context_.device()->CheckFeatureSupport(
                  D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support)),
              S_OK);
    return support;
  }

  ComPtr<ID3D12PipelineState> CreatePipeline(const TypedUavCase &test) {
    const std::string value_type = VectorType(test);
    std::string resource_type = value_type;
    if (test.encoding == StorageEncoding::Unorm)
      resource_type = "unorm " + resource_type;
    else if (test.encoding == StorageEncoding::Snorm)
      resource_type = "snorm " + resource_type;
    const std::string source = "RWBuffer<" + resource_type +
                               "> target : register(u0);\n"
                               "[numthreads(1, 1, 1)] void main() {\n"
                               "  " +
                               value_type +
                               " loaded = target[1];\n"
                               "  target[0] = loaded + " +
                               StoredValue(test) +
                               ";\n"
                               "}\n";
    const auto shader = CompileShader(source, "cs_5_0");
    EXPECT_EQ(shader.result, S_OK)
        << test.name << ": " << shader.diagnostic_text();
    if (shader.result != S_OK)
      return {};
    const D3D12_SHADER_BYTECODE bytecode = {shader.bytecode->GetBufferPointer(),
                                            shader.bytecode->GetBufferSize()};
    return context_.CreateComputePipeline(root_.get(), bytecode);
  }

  void Execute(const TypedUavCase &test) {
    const auto expected = ExpectedElement(test);
    const UINT64 element_size = expected.size();
    constexpr UINT kElementCount = 4;
    const UINT64 buffer_size = element_size * kElementCount;
    const std::vector<std::uint8_t> zeroes(
        static_cast<std::size_t>(buffer_size), 0);
    auto upload =
        context_.CreateUploadBuffer(buffer_size, zeroes.data(), zeroes.size());
    auto target =
        context_.CreateBuffer(buffer_size, D3D12_HEAP_TYPE_DEFAULT,
                              D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                              D3D12_RESOURCE_STATE_COPY_DEST);
    auto heap = context_.CreateDescriptorHeap(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true);
    auto pipeline = CreatePipeline(test);
    ASSERT_TRUE(upload);
    ASSERT_TRUE(target);
    ASSERT_TRUE(heap);
    ASSERT_TRUE(pipeline);

    context_.list()->CopyBufferRegion(target.get(), 0, upload.get(), 0,
                                      buffer_size);
    D3D12_UNORDERED_ACCESS_VIEW_DESC view = {};
    view.Format = test.format;
    view.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    view.Buffer.NumElements = kElementCount;
    context_.device()->CreateUnorderedAccessView(
        target.get(), nullptr, &view,
        heap->GetCPUDescriptorHandleForHeapStart());

    D3D12TestContext::Transition(context_.list(), target.get(),
                                 D3D12_RESOURCE_STATE_COPY_DEST,
                                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    ID3D12DescriptorHeap *heaps[] = {heap.get()};
    context_.list()->SetDescriptorHeaps(1, heaps);
    context_.list()->SetComputeRootSignature(root_.get());
    context_.list()->SetPipelineState(pipeline.get());
    context_.list()->SetComputeRootDescriptorTable(
        0, heap->GetGPUDescriptorHandleForHeapStart());
    context_.list()->Dispatch(1, 1, 1);
    D3D12TestContext::Transition(context_.list(), target.get(),
                                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                 D3D12_RESOURCE_STATE_COPY_SOURCE);

    std::vector<std::uint8_t> actual;
    ASSERT_EQ(context_.ReadbackBuffer(target.get(), buffer_size, &actual),
              S_OK);
    ASSERT_EQ(actual.size(), buffer_size);
    for (std::size_t index = 0; index < expected.size(); ++index)
      EXPECT_EQ(actual[index], expected[index]) << "byte=" << index;
    for (std::size_t index = expected.size(); index < actual.size(); ++index)
      EXPECT_EQ(actual[index], 0u) << "untouched byte=" << index;
    ASSERT_EQ(context_.ResetCommandList(), S_OK);
  }

  D3D12TestContext context_;
  ComPtr<ID3D12RootSignature> root_;
};

TEST_F(FormatTypedUavExecutionSpec,
       EveryAdvertisedCommonBufferFormatLoadsStoresAndReadsBack) {
  constexpr D3D12_FORMAT_SUPPORT1 required1 =
      D3D12_FORMAT_SUPPORT1_BUFFER |
      D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW;
  constexpr D3D12_FORMAT_SUPPORT2 required2 =
      D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD |
      D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE;

  UINT executed = 0;
  UINT skipped = 0;
  UINT multicomponent = 0;
  std::array<UINT, 3> scalar_execution = {};
  std::array<UINT, 3> width_execution = {};
  for (const auto &test : kTypedUavCases) {
    SCOPED_TRACE(::testing::Message()
                 << test.name << " format=" << static_cast<UINT>(test.format));
    const auto support = Support(test.format);
    SCOPED_TRACE(::testing::Message()
                 << "Support1=" << static_cast<UINT>(support.Support1)
                 << " Support2=" << static_cast<UINT>(support.Support2));
    if ((support.Support1 & required1) != required1 ||
        (support.Support2 & required2) != required2) {
      ++skipped;
      continue;
    }

    ASSERT_NO_FATAL_FAILURE(Execute(test));
    ++executed;
    ++scalar_execution[ScalarIndex(test.scalar)];
    ++width_execution[WidthIndex(test.component_bits)];
    multicomponent += test.components > 1;
  }

  RecordProperty("formats_executed", static_cast<int>(executed));
  RecordProperty("formats_skipped", static_cast<int>(skipped));
  EXPECT_EQ(executed + skipped, kTypedUavCases.size());
  EXPECT_GT(scalar_execution[ScalarIndex(ShaderScalar::Float)], 0u);
  EXPECT_GT(scalar_execution[ScalarIndex(ShaderScalar::Uint)], 0u);
  EXPECT_GT(scalar_execution[ScalarIndex(ShaderScalar::Sint)], 0u);
  EXPECT_GT(width_execution[0], 0u) << "no advertised 8-bit case executed";
  EXPECT_GT(width_execution[1], 0u) << "no advertised 16-bit case executed";
  EXPECT_GT(width_execution[2], 0u) << "no advertised 32-bit case executed";
  EXPECT_GT(multicomponent, 0u);
}

TEST_F(FormatTypedUavExecutionSpec,
       ThreeComponentFormatsDoNotAdvertiseUnavailableTypedUavLowering) {
  constexpr std::array formats = {
      DXGI_FORMAT_R32G32B32_TYPELESS,
      DXGI_FORMAT_R32G32B32_FLOAT,
      DXGI_FORMAT_R32G32B32_UINT,
      DXGI_FORMAT_R32G32B32_SINT,
  };
  constexpr D3D12_FORMAT_SUPPORT2 uav_support2 =
      D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_ADD |
      D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_BITWISE_OPS |
      D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_COMPARE_STORE_OR_COMPARE_EXCHANGE |
      D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_EXCHANGE |
      D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_SIGNED_MIN_OR_MAX |
      D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_UNSIGNED_MIN_OR_MAX |
      D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD |
      D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE;

  // RGB32 currently has no three-component Metal texture-buffer lowering. If
  // that lowering is implemented, update this gate together with the dynamic
  // execution cases above so newly advertised support executes immediately.
  for (const auto format : formats) {
    SCOPED_TRACE(::testing::Message()
                 << "format=" << static_cast<UINT>(format));
    const auto support = Support(format);
    EXPECT_EQ(support.Support1 &
                  D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW,
              0u);
    EXPECT_EQ(support.Support2 & uav_support2, 0u);
    EXPECT_EQ(support.Support2 & D3D12_FORMAT_SUPPORT2_TILED, 0u);
    if (format == DXGI_FORMAT_R32G32B32_TYPELESS) {
      constexpr UINT kAllowedSupport1 =
          D3D12_FORMAT_SUPPORT1_TEXTURE1D |
          D3D12_FORMAT_SUPPORT1_TEXTURE2D |
          D3D12_FORMAT_SUPPORT1_TEXTURE3D |
          D3D12_FORMAT_SUPPORT1_TEXTURECUBE |
          D3D12_FORMAT_SUPPORT1_MIP |
          D3D12_FORMAT_SUPPORT1_CAST_WITHIN_BIT_LAYOUT;
      EXPECT_EQ(static_cast<UINT>(support.Support1) & ~kAllowedSupport1, 0u);
    }
  }
}

} // namespace
