#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <string>
#include <utility>
#include <vector>

namespace {

using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;
using dxmt::test::TextureReadback;

enum class ShaderScalar : UINT {
  Float,
  Uint,
  Sint,
};

enum class TexelEncoding : UINT {
  Unorm,
  Snorm,
  Float16,
  Float32,
  Uint,
  Sint,
  PackedUnorm16,
  PackedUnorm32,
  Rgb10A2Uint,
  Rg11B10Float,
  Rgb9E5Float,
};

struct TextureUavCase {
  DXGI_FORMAT format;
  DXGI_FORMAT view_format;
  ShaderScalar scalar;
  TexelEncoding encoding;
  UINT component_bits;
  UINT components;
  const char *name;
};

constexpr TextureUavCase Typed(
    DXGI_FORMAT format, ShaderScalar scalar, TexelEncoding encoding,
    UINT component_bits, UINT components, const char *name) {
  return {format, format, scalar, encoding, component_bits, components, name};
}

constexpr TextureUavCase Typeless(
    DXGI_FORMAT format, DXGI_FORMAT view_format, ShaderScalar scalar,
    TexelEncoding encoding, UINT component_bits, UINT components,
    const char *name) {
  return {format, view_format, scalar, encoding, component_bits, components,
          name};
}

constexpr std::array kTextureUavCases = {
    // Typeless resources are a separate casting path. Their exact format must
    // never advertise typed-UAV operations; execution is gated exclusively by
    // the concrete typed view and CAST_WITHIN_BIT_LAYOUT support.
    Typeless(DXGI_FORMAT_R32G32B32A32_TYPELESS,
             DXGI_FORMAT_R32G32B32A32_UINT, ShaderScalar::Uint,
             TexelEncoding::Uint, 32, 4, "Rgba32TypelessAsUint"),
    Typeless(DXGI_FORMAT_R16G16B16A16_TYPELESS,
             DXGI_FORMAT_R16G16B16A16_UINT, ShaderScalar::Uint,
             TexelEncoding::Uint, 16, 4, "Rgba16TypelessAsUint"),
    Typeless(DXGI_FORMAT_R32G32_TYPELESS, DXGI_FORMAT_R32G32_UINT,
             ShaderScalar::Uint, TexelEncoding::Uint, 32, 2,
             "Rg32TypelessAsUint"),
    Typeless(DXGI_FORMAT_R10G10B10A2_TYPELESS,
             DXGI_FORMAT_R10G10B10A2_UINT, ShaderScalar::Uint,
             TexelEncoding::Rgb10A2Uint, 0, 4, "Rgb10A2TypelessAsUint"),
    Typeless(DXGI_FORMAT_R8G8B8A8_TYPELESS, DXGI_FORMAT_R8G8B8A8_UINT,
             ShaderScalar::Uint, TexelEncoding::Uint, 8, 4,
             "Rgba8TypelessAsUint"),
    Typeless(DXGI_FORMAT_R16G16_TYPELESS, DXGI_FORMAT_R16G16_UINT,
             ShaderScalar::Uint, TexelEncoding::Uint, 16, 2,
             "Rg16TypelessAsUint"),
    Typeless(DXGI_FORMAT_R32_TYPELESS, DXGI_FORMAT_R32_UINT,
             ShaderScalar::Uint, TexelEncoding::Uint, 32, 1,
             "R32TypelessAsUint"),
    Typeless(DXGI_FORMAT_R8G8_TYPELESS, DXGI_FORMAT_R8G8_UINT,
             ShaderScalar::Uint, TexelEncoding::Uint, 8, 2,
             "Rg8TypelessAsUint"),
    Typeless(DXGI_FORMAT_R16_TYPELESS, DXGI_FORMAT_R16_UINT,
             ShaderScalar::Uint, TexelEncoding::Uint, 16, 1,
             "R16TypelessAsUint"),
    Typeless(DXGI_FORMAT_R8_TYPELESS, DXGI_FORMAT_R8_UINT,
             ShaderScalar::Uint, TexelEncoding::Uint, 8, 1,
             "R8TypelessAsUint"),
    Typeless(DXGI_FORMAT_B8G8R8A8_TYPELESS,
             DXGI_FORMAT_B8G8R8A8_UNORM, ShaderScalar::Float,
             TexelEncoding::Unorm, 8, 4, "Bgra8TypelessAsUnorm"),

    Typed(DXGI_FORMAT_R8_UNORM, ShaderScalar::Float, TexelEncoding::Unorm,
          8, 1, "R8Unorm"),
    Typed(DXGI_FORMAT_R8_SNORM, ShaderScalar::Float, TexelEncoding::Snorm,
          8, 1, "R8Snorm"),
    Typed(DXGI_FORMAT_R8G8_UNORM, ShaderScalar::Float,
          TexelEncoding::Unorm, 8, 2, "Rg8Unorm"),
    Typed(DXGI_FORMAT_R8G8_SNORM, ShaderScalar::Float,
          TexelEncoding::Snorm, 8, 2, "Rg8Snorm"),
    Typed(DXGI_FORMAT_R8G8B8A8_UNORM, ShaderScalar::Float,
          TexelEncoding::Unorm, 8, 4, "Rgba8Unorm"),
    Typed(DXGI_FORMAT_R8G8B8A8_SNORM, ShaderScalar::Float,
          TexelEncoding::Snorm, 8, 4, "Rgba8Snorm"),
    Typed(DXGI_FORMAT_R16_FLOAT, ShaderScalar::Float,
          TexelEncoding::Float16, 16, 1, "R16Float"),
    Typed(DXGI_FORMAT_R16_UNORM, ShaderScalar::Float,
          TexelEncoding::Unorm, 16, 1, "R16Unorm"),
    Typed(DXGI_FORMAT_R16_SNORM, ShaderScalar::Float,
          TexelEncoding::Snorm, 16, 1, "R16Snorm"),
    Typed(DXGI_FORMAT_R16G16_FLOAT, ShaderScalar::Float,
          TexelEncoding::Float16, 16, 2, "Rg16Float"),
    Typed(DXGI_FORMAT_R16G16_UNORM, ShaderScalar::Float,
          TexelEncoding::Unorm, 16, 2, "Rg16Unorm"),
    Typed(DXGI_FORMAT_R16G16_SNORM, ShaderScalar::Float,
          TexelEncoding::Snorm, 16, 2, "Rg16Snorm"),
    Typed(DXGI_FORMAT_R16G16B16A16_FLOAT, ShaderScalar::Float,
          TexelEncoding::Float16, 16, 4, "Rgba16Float"),
    Typed(DXGI_FORMAT_R16G16B16A16_UNORM, ShaderScalar::Float,
          TexelEncoding::Unorm, 16, 4, "Rgba16Unorm"),
    Typed(DXGI_FORMAT_R16G16B16A16_SNORM, ShaderScalar::Float,
          TexelEncoding::Snorm, 16, 4, "Rgba16Snorm"),
    Typed(DXGI_FORMAT_R32_FLOAT, ShaderScalar::Float,
          TexelEncoding::Float32, 32, 1, "R32Float"),
    Typed(DXGI_FORMAT_R32G32_FLOAT, ShaderScalar::Float,
          TexelEncoding::Float32, 32, 2, "Rg32Float"),
    Typed(DXGI_FORMAT_R32G32B32A32_FLOAT, ShaderScalar::Float,
          TexelEncoding::Float32, 32, 4, "Rgba32Float"),

    Typed(DXGI_FORMAT_R8_UINT, ShaderScalar::Uint, TexelEncoding::Uint, 8, 1,
          "R8Uint"),
    Typed(DXGI_FORMAT_R8G8_UINT, ShaderScalar::Uint, TexelEncoding::Uint, 8,
          2, "Rg8Uint"),
    Typed(DXGI_FORMAT_R8G8B8A8_UINT, ShaderScalar::Uint,
          TexelEncoding::Uint, 8, 4, "Rgba8Uint"),
    Typed(DXGI_FORMAT_R16_UINT, ShaderScalar::Uint, TexelEncoding::Uint, 16,
          1, "R16Uint"),
    Typed(DXGI_FORMAT_R16G16_UINT, ShaderScalar::Uint, TexelEncoding::Uint,
          16, 2, "Rg16Uint"),
    Typed(DXGI_FORMAT_R16G16B16A16_UINT, ShaderScalar::Uint,
          TexelEncoding::Uint, 16, 4, "Rgba16Uint"),
    Typed(DXGI_FORMAT_R32_UINT, ShaderScalar::Uint, TexelEncoding::Uint, 32,
          1, "R32Uint"),
    Typed(DXGI_FORMAT_R32G32_UINT, ShaderScalar::Uint, TexelEncoding::Uint,
          32, 2, "Rg32Uint"),
    Typed(DXGI_FORMAT_R32G32B32A32_UINT, ShaderScalar::Uint,
          TexelEncoding::Uint, 32, 4, "Rgba32Uint"),

    Typed(DXGI_FORMAT_R8_SINT, ShaderScalar::Sint, TexelEncoding::Sint, 8, 1,
          "R8Sint"),
    Typed(DXGI_FORMAT_R8G8_SINT, ShaderScalar::Sint, TexelEncoding::Sint, 8,
          2, "Rg8Sint"),
    Typed(DXGI_FORMAT_R8G8B8A8_SINT, ShaderScalar::Sint,
          TexelEncoding::Sint, 8, 4, "Rgba8Sint"),
    Typed(DXGI_FORMAT_R16_SINT, ShaderScalar::Sint, TexelEncoding::Sint, 16,
          1, "R16Sint"),
    Typed(DXGI_FORMAT_R16G16_SINT, ShaderScalar::Sint, TexelEncoding::Sint,
          16, 2, "Rg16Sint"),
    Typed(DXGI_FORMAT_R16G16B16A16_SINT, ShaderScalar::Sint,
          TexelEncoding::Sint, 16, 4, "Rgba16Sint"),
    Typed(DXGI_FORMAT_R32_SINT, ShaderScalar::Sint, TexelEncoding::Sint, 32,
          1, "R32Sint"),
    Typed(DXGI_FORMAT_R32G32_SINT, ShaderScalar::Sint, TexelEncoding::Sint,
          32, 2, "Rg32Sint"),
    Typed(DXGI_FORMAT_R32G32B32A32_SINT, ShaderScalar::Sint,
          TexelEncoding::Sint, 32, 4, "Rgba32Sint"),

    Typed(DXGI_FORMAT_A8_UNORM, ShaderScalar::Float, TexelEncoding::Unorm, 8,
          4, "A8Unorm"),
    Typed(DXGI_FORMAT_R10G10B10A2_UNORM, ShaderScalar::Float,
          TexelEncoding::PackedUnorm32, 0, 4, "Rgb10A2Unorm"),
    Typed(DXGI_FORMAT_R10G10B10A2_UINT, ShaderScalar::Uint,
          TexelEncoding::Rgb10A2Uint, 0, 4, "Rgb10A2Uint"),
    Typed(DXGI_FORMAT_R11G11B10_FLOAT, ShaderScalar::Float,
          TexelEncoding::Rg11B10Float, 0, 3, "Rg11B10Float"),
    Typed(DXGI_FORMAT_B5G6R5_UNORM, ShaderScalar::Float,
          TexelEncoding::PackedUnorm16, 0, 4, "B5G6R5Unorm"),
    Typed(DXGI_FORMAT_B5G5R5A1_UNORM, ShaderScalar::Float,
          TexelEncoding::PackedUnorm16, 0, 4, "B5G5R5A1Unorm"),
    Typed(DXGI_FORMAT_B4G4R4A4_UNORM, ShaderScalar::Float,
          TexelEncoding::PackedUnorm16, 0, 4, "B4G4R4A4Unorm"),
    Typed(DXGI_FORMAT_B8G8R8A8_UNORM, ShaderScalar::Float,
          TexelEncoding::Unorm, 8, 4, "Bgra8Unorm"),
};

const TextureUavCase *FindExactCase(DXGI_FORMAT format) {
  const auto found = std::find_if(
      kTextureUavCases.begin(), kTextureUavCases.end(),
      [format](const TextureUavCase &test) {
        return test.format == format && test.view_format == format;
      });
  return found == kTextureUavCases.end() ? nullptr : &*found;
}

bool IsDefinedDxgiFormatValue(UINT value) {
  return value <= static_cast<UINT>(DXGI_FORMAT_B4G4R4A4_UNORM) ||
         value >= static_cast<UINT>(DXGI_FORMAT_P208);
}

bool IsCoreTypedLoadFormat(DXGI_FORMAT format) {
  return format == DXGI_FORMAT_R32_FLOAT || format == DXGI_FORMAT_R32_UINT ||
         format == DXGI_FORMAT_R32_SINT;
}

bool IsAllOrNothingTypedLoadFormat(DXGI_FORMAT format) {
  switch (format) {
  case DXGI_FORMAT_R32G32B32A32_FLOAT:
  case DXGI_FORMAT_R32G32B32A32_UINT:
  case DXGI_FORMAT_R32G32B32A32_SINT:
  case DXGI_FORMAT_R16G16B16A16_FLOAT:
  case DXGI_FORMAT_R16G16B16A16_UINT:
  case DXGI_FORMAT_R16G16B16A16_SINT:
  case DXGI_FORMAT_R8G8B8A8_UNORM:
  case DXGI_FORMAT_R8G8B8A8_UINT:
  case DXGI_FORMAT_R8G8B8A8_SINT:
  case DXGI_FORMAT_R16_FLOAT:
  case DXGI_FORMAT_R16_UINT:
  case DXGI_FORMAT_R16_SINT:
  case DXGI_FORMAT_R8_UNORM:
  case DXGI_FORMAT_R8_UINT:
  case DXGI_FORMAT_R8_SINT:
    return true;
  default:
    return false;
  }
}

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

bool IsNormalized(TexelEncoding encoding) {
  return encoding == TexelEncoding::Unorm ||
         encoding == TexelEncoding::Snorm ||
         encoding == TexelEncoding::PackedUnorm16 ||
         encoding == TexelEncoding::PackedUnorm32;
}

std::string ValueType(const TextureUavCase &test) {
  std::string type = ScalarName(test.scalar);
  if (test.components > 1)
    type += std::to_string(test.components);
  return type;
}

std::string ResourceType(const TextureUavCase &test) {
  std::string type = ValueType(test);
  if (IsNormalized(test.encoding))
    type = (test.encoding == TexelEncoding::Snorm ? "snorm " : "unorm ") +
           type;
  return type;
}

std::string StoreValue(const TextureUavCase &test) {
  if (test.view_format == DXGI_FORMAT_A8_UNORM)
    return "float4(0.0, 0.0, 0.0, 1.0)";

  const char *component = nullptr;
  switch (test.encoding) {
  case TexelEncoding::Uint:
    component = "7u";
    break;
  case TexelEncoding::Rgb10A2Uint:
    component = "1u";
    break;
  case TexelEncoding::Sint:
    component = "-7";
    break;
  default:
    component = "1.0";
    break;
  }
  if (test.components == 1)
    return component;

  std::string value = ValueType(test) + "(";
  for (UINT index = 0; index < test.components; ++index) {
    if (index)
      value += ", ";
    value += component;
  }
  return value + ")";
}

std::vector<std::uint8_t> ExpectedTexel(const TextureUavCase &test) {
  if (test.view_format == DXGI_FORMAT_A8_UNORM)
    return {0xff};

  switch (test.encoding) {
  case TexelEncoding::PackedUnorm16:
    return {0xff, 0xff};
  case TexelEncoding::PackedUnorm32:
    return {0xff, 0xff, 0xff, 0xff};
  case TexelEncoding::Rgb10A2Uint:
    return {0x01, 0x04, 0x10, 0x40};
  case TexelEncoding::Rg11B10Float:
    return {0xc0, 0x03, 0x1e, 0x78};
  case TexelEncoding::Rgb9E5Float:
    return {0x00, 0x01, 0x02, 0x84};
  default:
    break;
  }

  std::vector<std::uint8_t> component;
  switch (test.encoding) {
  case TexelEncoding::Unorm:
    component.assign(test.component_bits / 8, 0xff);
    break;
  case TexelEncoding::Snorm:
    component = test.component_bits == 8
                    ? std::vector<std::uint8_t>{0x7f}
                    : std::vector<std::uint8_t>{0xff, 0x7f};
    break;
  case TexelEncoding::Float16:
    component = {0x00, 0x3c};
    break;
  case TexelEncoding::Float32:
    component = {0x00, 0x00, 0x80, 0x3f};
    break;
  case TexelEncoding::Uint:
    component.assign(test.component_bits / 8, 0x00);
    component[0] = 0x07;
    break;
  case TexelEncoding::Sint:
    component.assign(test.component_bits / 8, 0xff);
    component[0] = 0xf9;
    break;
  default:
    break;
  }

  std::vector<std::uint8_t> result;
  result.reserve(component.size() * test.components);
  for (UINT index = 0; index < test.components; ++index)
    result.insert(result.end(), component.begin(), component.end());
  return result;
}

std::vector<UINT> ExpectedLoad(const TextureUavCase &test) {
  if (test.view_format == DXGI_FORMAT_A8_UNORM)
    return {0u, 0u, 0u, std::bit_cast<UINT>(1.0f)};

  UINT component = std::bit_cast<UINT>(1.0f);
  if (test.encoding == TexelEncoding::Uint)
    component = 7u;
  else if (test.encoding == TexelEncoding::Rgb10A2Uint)
    component = 1u;
  else if (test.encoding == TexelEncoding::Sint)
    component = static_cast<UINT>(-7);
  return std::vector<UINT>(test.components, component);
}

bool IsDefinedTexelByte(const TextureUavCase &test, std::size_t index) {
  return !((test.view_format == DXGI_FORMAT_B8G8R8X8_UNORM ||
            test.view_format == DXGI_FORMAT_B8G8R8X8_UNORM_SRGB) &&
           index % 4 == 3);
}

enum class Operation { Load, Store };

std::string ShaderSource(const TextureUavCase &test, Operation operation) {
  const std::string resource_type = ResourceType(test);
  if (operation == Operation::Store) {
    return "RWTexture2D<" + resource_type +
           "> target : register(u0);\n"
           "[numthreads(1, 1, 1)] void main() {\n"
           "  target[uint2(0, 0)] = " +
           StoreValue(test) + ";\n}\n";
  }

  std::string source =
      "RWTexture2D<" + resource_type +
      "> source_texture : register(u0);\n"
      "RWByteAddressBuffer output : register(u1);\n"
      "[numthreads(1, 1, 1)] void main() {\n  " +
      ValueType(test) +
      " loaded = source_texture[uint2(0, 0)];\n";
  constexpr char components[] = "xyzw";
  for (UINT index = 0; index < test.components; ++index) {
    std::string value = "loaded";
    if (test.components > 1) {
      value += ".";
      value += components[index];
    }
    if (test.scalar != ShaderScalar::Uint)
      value = "asuint(" + value + ")";
    source += "  output.Store(" + std::to_string(index * sizeof(UINT)) +
              ", " + value + ");\n";
  }
  return source + "}\n";
}

struct CachedPipeline {
  std::string source;
  ComPtr<ID3D12PipelineState> pipeline;
};

class FormatTextureUavExecutionSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_EQ(context_.device()->CheckFeatureSupport(
                  D3D12_FEATURE_D3D12_OPTIONS, &options_, sizeof(options_)),
              S_OK);
    D3D12_DESCRIPTOR_RANGE range = {};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    range.NumDescriptors = 1;
    range.BaseShaderRegister = 0;
    D3D12_ROOT_PARAMETER parameters[2] = {};
    parameters[0].ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[0].DescriptorTable.NumDescriptorRanges = 1;
    parameters[0].DescriptorTable.pDescriptorRanges = &range;
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    parameters[1].Descriptor.ShaderRegister = 1;
    D3D12_ROOT_SIGNATURE_DESC desc = {};
    desc.NumParameters = 2;
    desc.pParameters = parameters;
    root_ = context_.CreateRootSignature(desc);
    ASSERT_TRUE(root_);
    ASSERT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
  }

  HRESULT QuerySupport(DXGI_FORMAT format,
                       D3D12_FEATURE_DATA_FORMAT_SUPPORT *support) {
    *support = {};
    support->Format = format;
    return context_.device()->CheckFeatureSupport(
        D3D12_FEATURE_FORMAT_SUPPORT, support, sizeof(*support));
  }

  ID3D12PipelineState *Pipeline(const TextureUavCase &test,
                                Operation operation) {
    const auto source = ShaderSource(test, operation);
    for (const auto &cached : pipelines) {
      if (cached.source == source)
        return cached.pipeline.get();
    }

    const auto shader = CompileShader(source, "cs_5_0");
    if (shader.result != S_OK) {
      ADD_FAILURE() << test.name << ": shader compilation failed: "
                    << shader.diagnostic_text();
      return nullptr;
    }
    const D3D12_SHADER_BYTECODE bytecode = {
        shader.bytecode->GetBufferPointer(), shader.bytecode->GetBufferSize()};
    auto pipeline = context_.CreateComputePipeline(root_.get(), bytecode);
    if (!pipeline) {
      ADD_FAILURE() << test.name << ": compute pipeline creation failed";
      return nullptr;
    }
    pipelines.push_back({source, std::move(pipeline)});
    return pipelines.back().pipeline.get();
  }

  ::testing::AssertionResult ExecuteStore(const TextureUavCase &test) {
    const auto expected = ExpectedTexel(test);
    auto *pipeline = Pipeline(test, Operation::Store);
    if (!pipeline || expected.empty())
      return ::testing::AssertionFailure()
             << test.name << ": no store pipeline or texel oracle";

    std::vector<std::uint8_t> poison(expected.size());
    for (std::size_t index = 0; index < expected.size(); ++index)
      poison[index] = IsDefinedTexelByte(test, index)
                          ? static_cast<std::uint8_t>(~expected[index])
                          : 0;
    auto texture = context_.CreateTexture2D(
        1, 1, 1, test.format,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_DEST);
    auto heap = context_.CreateDescriptorHeap(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true);
    if (!texture || !heap)
      return ::testing::AssertionFailure()
             << test.name << ": store resource creation failed";
    const HRESULT upload_hr = context_.UploadTextureAndReset(
        texture.get(), poison.data(), poison.size(), poison.size());
    if (upload_hr != S_OK)
      return ::testing::AssertionFailure()
             << test.name << ": store poison upload returned 0x" << std::hex
             << static_cast<unsigned long>(upload_hr);

    D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
    uav.Format = test.view_format;
    uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    context_.device()->CreateUnorderedAccessView(
        texture.get(), nullptr, &uav,
        heap->GetCPUDescriptorHandleForHeapStart());
    D3D12TestContext::Transition(
        context_.list(), texture.get(), D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    ID3D12DescriptorHeap *heaps[] = {heap.get()};
    context_.list()->SetDescriptorHeaps(1, heaps);
    context_.list()->SetComputeRootSignature(root_.get());
    context_.list()->SetPipelineState(pipeline);
    context_.list()->SetComputeRootDescriptorTable(
        0, heap->GetGPUDescriptorHandleForHeapStart());
    context_.list()->Dispatch(1, 1, 1);
    D3D12TestContext::Transition(
        context_.list(), texture.get(),
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_SOURCE);

    TextureReadback readback;
    const HRESULT readback_hr =
        context_.ReadbackTexture(texture.get(), &readback);
    if (readback_hr != S_OK)
      return ::testing::AssertionFailure()
             << test.name << ": store readback returned 0x" << std::hex
             << static_cast<unsigned long>(readback_hr);
    const HRESULT reset_hr = context_.ResetCommandList();
    if (reset_hr != S_OK)
      return ::testing::AssertionFailure()
             << test.name << ": store reset returned 0x" << std::hex
             << static_cast<unsigned long>(reset_hr);
    const HRESULT device_reason =
        context_.device()->GetDeviceRemovedReason();
    if (device_reason != S_OK)
      return ::testing::AssertionFailure()
             << test.name << ": store removed device with 0x" << std::hex
             << static_cast<unsigned long>(device_reason);
    if (readback.width != 1 || readback.height != 1 ||
        readback.row_pitch < expected.size() ||
        readback.data.size() < expected.size())
      return ::testing::AssertionFailure()
             << test.name << ": invalid store readback layout";
    for (std::size_t index = 0; index < expected.size(); ++index) {
      if (!IsDefinedTexelByte(test, index))
        continue;
      if (readback.data[index] != expected[index])
        return ::testing::AssertionFailure()
               << test.name << ": stored byte " << index << " was 0x"
               << std::hex << UINT(readback.data[index]) << ", expected 0x"
               << UINT(expected[index]);
    }
    return ::testing::AssertionSuccess();
  }

  ::testing::AssertionResult ExecuteLoad(const TextureUavCase &test) {
    const auto texel = ExpectedTexel(test);
    const auto expected = ExpectedLoad(test);
    auto *pipeline = Pipeline(test, Operation::Load);
    if (!pipeline || texel.empty() || expected.empty())
      return ::testing::AssertionFailure()
             << test.name << ": no load pipeline or oracle";

    auto texture = context_.CreateTexture2D(
        1, 1, 1, test.format,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_DEST);
    auto output = context_.CreateBuffer(
        expected.size() * sizeof(UINT), D3D12_HEAP_TYPE_DEFAULT,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_DEST);
    std::vector<UINT> output_poison(expected.size());
    for (std::size_t index = 0; index < expected.size(); ++index)
      output_poison[index] = ~expected[index];
    auto output_poison_upload = context_.CreateUploadBuffer(
        output_poison.size() * sizeof(UINT), output_poison.data(),
        output_poison.size() * sizeof(UINT));
    auto heap = context_.CreateDescriptorHeap(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true);
    if (!texture || !output || !output_poison_upload || !heap)
      return ::testing::AssertionFailure()
             << test.name << ": load resource creation failed";

    const HRESULT upload_hr = context_.UploadTextureAndReset(
        texture.get(), texel.data(), texel.size(), texel.size());
    if (upload_hr != S_OK)
      return ::testing::AssertionFailure()
             << test.name << ": load upload returned 0x" << std::hex
             << static_cast<unsigned long>(upload_hr);

    D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
    uav.Format = test.view_format;
    uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    context_.device()->CreateUnorderedAccessView(
        texture.get(), nullptr, &uav,
        heap->GetCPUDescriptorHandleForHeapStart());
    context_.list()->CopyBufferRegion(
        output.get(), 0, output_poison_upload.get(), 0,
        output_poison.size() * sizeof(UINT));
    D3D12TestContext::Transition(
        context_.list(), texture.get(), D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    D3D12TestContext::Transition(
        context_.list(), output.get(), D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    const HRESULT setup_hr = context_.ExecuteAndWait();
    if (setup_hr != S_OK)
      return ::testing::AssertionFailure()
             << test.name << ": load poison setup returned 0x" << std::hex
             << static_cast<unsigned long>(setup_hr);
    const HRESULT setup_reset_hr = context_.ResetCommandList();
    if (setup_reset_hr != S_OK)
      return ::testing::AssertionFailure()
             << test.name << ": load setup reset returned 0x" << std::hex
             << static_cast<unsigned long>(setup_reset_hr);

    ID3D12DescriptorHeap *heaps[] = {heap.get()};
    context_.list()->SetDescriptorHeaps(1, heaps);
    context_.list()->SetComputeRootSignature(root_.get());
    context_.list()->SetPipelineState(pipeline);
    context_.list()->SetComputeRootDescriptorTable(
        0, heap->GetGPUDescriptorHandleForHeapStart());
    context_.list()->SetComputeRootUnorderedAccessView(
        1, output->GetGPUVirtualAddress());
    context_.list()->Dispatch(1, 1, 1);
    D3D12TestContext::Transition(
        context_.list(), output.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_SOURCE);

    std::vector<std::uint8_t> bytes;
    const HRESULT readback_hr = context_.ReadbackBuffer(
        output.get(), expected.size() * sizeof(UINT), &bytes);
    if (readback_hr != S_OK)
      return ::testing::AssertionFailure()
             << test.name << ": load readback returned 0x" << std::hex
             << static_cast<unsigned long>(readback_hr);
    const HRESULT reset_hr = context_.ResetCommandList();
    if (reset_hr != S_OK)
      return ::testing::AssertionFailure()
             << test.name << ": load reset returned 0x" << std::hex
             << static_cast<unsigned long>(reset_hr);
    const HRESULT device_reason =
        context_.device()->GetDeviceRemovedReason();
    if (device_reason != S_OK)
      return ::testing::AssertionFailure()
             << test.name << ": load removed device with 0x" << std::hex
             << static_cast<unsigned long>(device_reason);
    if (bytes.size() != expected.size() * sizeof(UINT))
      return ::testing::AssertionFailure()
             << test.name << ": load readback size was " << bytes.size();

    std::vector<UINT> actual(expected.size());
    std::memcpy(actual.data(), bytes.data(), bytes.size());
    for (std::size_t index = 0; index < expected.size(); ++index) {
      if (actual[index] != expected[index])
        return ::testing::AssertionFailure()
               << test.name << ": loaded component " << index << " was 0x"
               << std::hex << actual[index] << ", expected 0x"
               << expected[index];
    }
    return ::testing::AssertionSuccess();
  }

  D3D12TestContext context_;
  ComPtr<ID3D12RootSignature> root_;
  D3D12_FEATURE_DATA_D3D12_OPTIONS options_ = {};
  std::vector<CachedPipeline> pipelines;
};

TEST_F(FormatTextureUavExecutionSpec,
       EveryAdvertisedTexture2DTypedUavFormatHasAnExecutionDefinition) {
  const D3D12_FORMAT_SUPPORT1 required1 =
      D3D12_FORMAT_SUPPORT1_TEXTURE2D |
      D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW;
  const UINT atomic_operations =
      D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_ADD |
      D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_BITWISE_OPS |
      D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_COMPARE_STORE_OR_COMPARE_EXCHANGE |
      D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_EXCHANGE |
      D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_SIGNED_MIN_OR_MAX |
      D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_UNSIGNED_MIN_OR_MAX;
  UINT advertised = 0;

  for (UINT value = DXGI_FORMAT_R32G32B32A32_TYPELESS;
       value <= DXGI_FORMAT_V408; ++value) {
    if (!IsDefinedDxgiFormatValue(value))
      continue;
    const auto format = static_cast<DXGI_FORMAT>(value);
    D3D12_FEATURE_DATA_FORMAT_SUPPORT support = {};
    const HRESULT support_hr = QuerySupport(format, &support);
    if (FAILED(support_hr))
      continue;
    const bool typed_view =
        support.Support1 &
        D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW;
    const bool texture2d =
        support.Support1 & D3D12_FORMAT_SUPPORT1_TEXTURE2D;
    const bool load =
        support.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD;
    const bool store =
        support.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE;
    const UINT atomics =
        static_cast<UINT>(support.Support2) & atomic_operations;

    if (load || store) {
      EXPECT_TRUE(typed_view)
          << "typed operation advertised without typed UAV view: " << value;
    }
    if (load) {
      EXPECT_TRUE(store) << "typed UAV load advertised without typed store: "
                         << value;
      if (!IsCoreTypedLoadFormat(format)) {
        EXPECT_TRUE(options_.TypedUAVLoadAdditionalFormats)
            << "additional typed UAV load advertised without the D3D12_OPTIONS "
               "gate: "
            << value;
      }
    }
    if (atomics) {
      EXPECT_TRUE(typed_view && store)
          << "typed atomics advertised without writable typed UAV: " << value;
    }
    if (IsCoreTypedLoadFormat(format)) {
      EXPECT_TRUE(load) << "core typed UAV load missing: " << value;
    }
    if (IsAllOrNothingTypedLoadFormat(format)) {
      EXPECT_EQ(load, options_.TypedUAVLoadAdditionalFormats != FALSE)
          << "all-or-nothing typed UAV load disagrees with D3D12_OPTIONS: "
          << value;
    }

    if (!texture2d || (!typed_view && !load && !store))
      continue;

    ++advertised;
    EXPECT_EQ(support.Support1 & required1, required1)
        << "Texture2D typed operation has incoherent Support1: " << value;
    EXPECT_NE(FindExactCase(format), nullptr)
        << "advertised Texture2D typed UAV format has no execution case: "
        << value << " Support1=" << static_cast<UINT>(support.Support1)
        << " Support2=" << static_cast<UINT>(support.Support2);
  }

  RecordProperty("advertised_formats", static_cast<int>(advertised));
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(FormatTextureUavExecutionSpec,
       EveryAdvertisedTexture2DTypedUavOperationExecutesAndReadsBack) {
  const D3D12_FORMAT_SUPPORT1 required1 =
      D3D12_FORMAT_SUPPORT1_TEXTURE2D |
      D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW;
  UINT load_executed = 0;
  UINT store_executed = 0;
  UINT unsupported = 0;

  for (const auto &test : kTextureUavCases) {
    SCOPED_TRACE(::testing::Message()
                 << test.name << " format=" << static_cast<UINT>(test.format)
                 << " view=" << static_cast<UINT>(test.view_format));
    D3D12_FEATURE_DATA_FORMAT_SUPPORT resource_support = {};
    D3D12_FEATURE_DATA_FORMAT_SUPPORT view_support = {};
    ASSERT_EQ(QuerySupport(test.format, &resource_support), S_OK);
    ASSERT_EQ(QuerySupport(test.view_format, &view_support), S_OK);
    const bool load =
        view_support.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD;
    const bool store =
        view_support.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE;
    if (!load && !store) {
      ++unsupported;
      continue;
    }

    ASSERT_TRUE(resource_support.Support1 & D3D12_FORMAT_SUPPORT1_TEXTURE2D)
        << "typed view format advertised for a non-Texture2D resource family";
    if (test.format != test.view_format) {
      ASSERT_TRUE(resource_support.Support1 &
                  D3D12_FORMAT_SUPPORT1_CAST_WITHIN_BIT_LAYOUT)
          << "typeless resource family is missing cast-within-bit-layout";
    }
    ASSERT_EQ(view_support.Support1 & required1, required1)
        << "typed operation advertised without Texture2D typed UAV support";
    if (store) {
      ASSERT_TRUE(ExecuteStore(test));
      ++store_executed;
    }
    if (load) {
      ASSERT_TRUE(ExecuteLoad(test));
      ++load_executed;
    }
  }

  RecordProperty("load_formats_executed", static_cast<int>(load_executed));
  RecordProperty("store_formats_executed", static_cast<int>(store_executed));
  RecordProperty("formats_without_typed_operations",
                 static_cast<int>(unsupported));
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
