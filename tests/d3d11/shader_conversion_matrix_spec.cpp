#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d11_test_context.hpp"

#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iterator>
#include <vector>

// Public-D3D11 shader conversion semantics. Every logical case is available
// for exact replay, while the default run executes all conversions in one
// compute dispatch and validates them with one staging readback.

namespace {

using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

constexpr std::uint32_t kConversionCaseCount = 8192;

const dxmt::test::LogicalCaseFamilyRegistration kConversionCases(
    "D3D11ShaderConversionMatrixSpec."
    "Executes8192NumericConversionCasesInOneDispatch",
    "D3D11.Shader.Conversion.Numeric.", kConversionCaseCount, 4,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "5_0", "Immediate", "ComputeShader,TypedSRV,TypedUAV"},
     dxmt::test::kGpuBatchTestCost,
     "selected logical IDs in an immutable typed buffer and a "
     "poison-initialized typed UAV",
     "dispatch one invocation per selected ID and perform i32/u32 to f32, "
     "bounded f32 to i32/u32, float/int bitcast, or f16/f32 conversion",
     "every selected conversion matches the bit-exact CPU evaluator and "
     "every unselected output remains poison",
     "logical ID, selection state, operation selector, source bits, "
     "expected/actual, and exact replay argument"});

const dxmt::test::TestCostRegistration
    kConversionCost("D3D11ShaderConversionMatrixSpec."
                    "Executes8192NumericConversionCasesInOneDispatch",
                    dxmt::test::kGpuBatchTestCost);

std::uint32_t CaseValue(std::uint32_t case_index) {
  constexpr std::uint32_t edges[] = {
      0x00000000u, 0x00000001u, 0xffffffffu, 0x7fffffffu,
      0x80000000u, 0x00ffffffu, 0x01000001u, 0x7fc12345u,
  };
  if ((case_index & 31u) < std::size(edges))
    return edges[case_index & 31u];
  return case_index * 0x9e3779b9u + 0x243f6a88u;
}

std::int32_t IntFromBits(std::uint32_t bits) {
  std::int32_t value = 0;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

float FloatFromBits(std::uint32_t bits) {
  float value = 0.0f;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

std::uint32_t FloatBits(float value) {
  std::uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

std::uint32_t SignedFloatInputBits(std::uint32_t case_index) {
  constexpr std::uint32_t edges[] = {
      0x00000000u, 0x80000000u, 0x3f000000u, 0xbf000000u,
      0x3fc00000u, 0xbfc00000u, 0x4effffffu, 0xceffffffu,
  };
  if ((case_index & 31u) < std::size(edges))
    return edges[case_index & 31u];
  const std::uint32_t value = CaseValue(case_index);
  const std::uint32_t sign = (value & 1u) << 31u;
  const std::uint32_t exponent = 118u + ((case_index >> 8u) % 32u);
  return sign | (exponent << 23u) | (value & 0x007fffffu);
}

std::uint32_t UnsignedFloatInputBits(std::uint32_t case_index) {
  constexpr std::uint32_t edges[] = {
      0x00000000u, 0x3f000000u, 0x3fc00000u, 0x4f7fffffu,
  };
  if ((case_index & 31u) < std::size(edges))
    return edges[case_index & 31u];
  const std::uint32_t value = CaseValue(case_index ^ 0xa5a5a5a5u);
  const std::uint32_t exponent = 118u + ((case_index >> 8u) % 32u);
  return (exponent << 23u) | (value & 0x007fffffu);
}

std::uint16_t HalfInput(std::uint32_t case_index) {
  constexpr std::uint16_t edges[] = {
      0x0000u, 0x8000u, 0x0001u, 0x8001u, 0x03ffu,
      0x0400u, 0x7bffu, 0x7c00u, 0xfc00u,
  };
  if ((case_index & 31u) < std::size(edges))
    return edges[case_index & 31u];
  std::uint16_t half =
      static_cast<std::uint16_t>(CaseValue(case_index) & 0xffffu);
  if ((half & 0x7c00u) == 0x7c00u && (half & 0x03ffu) != 0)
    half &= 0xfc00u;
  return half;
}

std::uint32_t HalfToFloatBits(std::uint16_t half) {
  const std::uint32_t sign =
      (static_cast<std::uint32_t>(half) & 0x8000u) << 16u;
  std::uint32_t exponent = (half >> 10u) & 0x1fu;
  std::uint32_t mantissa = half & 0x03ffu;

  if (exponent == 0) {
    if (mantissa == 0)
      return sign;
    exponent = 113u;
    while ((mantissa & 0x0400u) == 0) {
      mantissa <<= 1u;
      --exponent;
    }
    mantissa &= 0x03ffu;
    return sign | (exponent << 23u) | (mantissa << 13u);
  }
  if (exponent == 31u)
    return sign | 0x7f800000u | (mantissa << 13u);
  return sign | ((exponent + 112u) << 23u) | (mantissa << 13u);
}

std::uint32_t ConversionSourceBits(std::uint32_t case_index) {
  switch ((case_index >> 5u) & 7u) {
  case 2:
    return SignedFloatInputBits(case_index);
  case 3:
    return UnsignedFloatInputBits(case_index);
  case 6:
  case 7:
    return HalfInput(case_index);
  default:
    return CaseValue(case_index);
  }
}

std::uint32_t EvaluateConversionCase(std::uint32_t case_index) {
  const std::uint32_t selector = (case_index >> 5u) & 7u;
  const std::uint32_t value = CaseValue(case_index);
  switch (selector) {
  case 0:
    return FloatBits(static_cast<float>(IntFromBits(value)));
  case 1:
    return FloatBits(static_cast<float>(value));
  case 2:
    return static_cast<std::uint32_t>(static_cast<std::int32_t>(
        FloatFromBits(SignedFloatInputBits(case_index))));
  case 3:
    return static_cast<std::uint32_t>(
        FloatFromBits(UnsignedFloatInputBits(case_index)));
  case 4:
  case 5:
    return value;
  case 6:
    return HalfToFloatBits(HalfInput(case_index));
  default:
    return HalfInput(case_index);
  }
}

HRESULT CreateTypedBuffer(ID3D11Device *device,
                          const std::vector<std::uint32_t> &values,
                          UINT bind_flags, D3D11_USAGE usage,
                          ID3D11Buffer **buffer) {
  D3D11_BUFFER_DESC desc = {};
  desc.ByteWidth = static_cast<UINT>(values.size() * sizeof(values[0]));
  desc.Usage = usage;
  desc.BindFlags = bind_flags;
  D3D11_SUBRESOURCE_DATA initial = {};
  initial.pSysMem = values.data();
  return device->CreateBuffer(&desc, &initial, buffer);
}

HRESULT CreateTypedBufferSrv(ID3D11Device *device, ID3D11Buffer *buffer,
                             UINT element_count,
                             ID3D11ShaderResourceView **view) {
  D3D11_SHADER_RESOURCE_VIEW_DESC desc = {};
  desc.Format = DXGI_FORMAT_R32_UINT;
  desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
  desc.Buffer.NumElements = element_count;
  return device->CreateShaderResourceView(buffer, &desc, view);
}

HRESULT CreateTypedBufferUav(ID3D11Device *device, ID3D11Buffer *buffer,
                             ID3D11UnorderedAccessView **view) {
  D3D11_UNORDERED_ACCESS_VIEW_DESC desc = {};
  desc.Format = DXGI_FORMAT_R32_UINT;
  desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
  desc.Buffer.NumElements = kConversionCaseCount;
  return device->CreateUnorderedAccessView(buffer, &desc, view);
}

class D3D11ShaderConversionMatrixSpec : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  D3D11TestContext context_;
};

TEST_F(D3D11ShaderConversionMatrixSpec,
       Executes8192NumericConversionCasesInOneDispatch) {
  std::vector<std::uint32_t> expected(kConversionCaseCount);
  std::vector<std::uint32_t> poison(kConversionCaseCount);
  std::vector<bool> selected(kConversionCaseCount, false);
  std::vector<std::uint32_t> selected_cases;
  selected_cases.reserve(kConversionCaseCount);
  for (std::uint32_t logical = 0; logical < kConversionCaseCount; ++logical) {
    expected[logical] = EvaluateConversionCase(logical);
    poison[logical] = expected[logical] ^ 0xa55a3cc3u;
    if (dxmt::test::LogicalCaseSelected(kConversionCases.family(), logical)) {
      selected[logical] = true;
      selected_cases.push_back(logical);
    }
  }
  ASSERT_FALSE(selected_cases.empty());

  const auto shader = CompileShader(R"(
    Buffer<uint> case_indices : register(t0);
    RWBuffer<uint> output : register(u0);

    cbuffer Parameters : register(b0) {
      uint selected_count;
      uint3 padding;
    };

    uint case_value(uint case_index) {
      switch (case_index & 31u) {
      case 0: return 0x00000000u;
      case 1: return 0x00000001u;
      case 2: return 0xffffffffu;
      case 3: return 0x7fffffffu;
      case 4: return 0x80000000u;
      case 5: return 0x00ffffffu;
      case 6: return 0x01000001u;
      case 7: return 0x7fc12345u;
      }
      return case_index * 0x9e3779b9u + 0x243f6a88u;
    }

    uint signed_float_bits(uint case_index) {
      switch (case_index & 31u) {
      case 0: return 0x00000000u;
      case 1: return 0x80000000u;
      case 2: return 0x3f000000u;
      case 3: return 0xbf000000u;
      case 4: return 0x3fc00000u;
      case 5: return 0xbfc00000u;
      case 6: return 0x4effffffu;
      case 7: return 0xceffffffu;
      }
      uint value = case_value(case_index);
      uint sign = (value & 1u) << 31u;
      uint exponent = 118u + ((case_index >> 8u) % 32u);
      return sign | (exponent << 23u) | (value & 0x007fffffu);
    }

    uint unsigned_float_bits(uint case_index) {
      switch (case_index & 31u) {
      case 0: return 0x00000000u;
      case 1: return 0x3f000000u;
      case 2: return 0x3fc00000u;
      case 3: return 0x4f7fffffu;
      }
      uint mixed_index = case_index ^ 0xa5a5a5a5u;
      uint value = case_value(mixed_index);
      uint exponent = 118u + ((case_index >> 8u) % 32u);
      return (exponent << 23u) | (value & 0x007fffffu);
    }

    uint half_input(uint case_index) {
      switch (case_index & 31u) {
      case 0: return 0x0000u;
      case 1: return 0x8000u;
      case 2: return 0x0001u;
      case 3: return 0x8001u;
      case 4: return 0x03ffu;
      case 5: return 0x0400u;
      case 6: return 0x7bffu;
      case 7: return 0x7c00u;
      case 8: return 0xfc00u;
      }
      uint half = case_value(case_index) & 0xffffu;
      if ((half & 0x7c00u) == 0x7c00u && (half & 0x03ffu) != 0)
        half &= 0xfc00u;
      return half;
    }

    uint evaluate(uint case_index) {
      uint selector = (case_index >> 5u) & 7u;
      uint value = case_value(case_index);
      switch (selector) {
      case 0: return asuint(float(asint(value)));
      case 1: return asuint(float(value));
      case 2: return asuint(int(asfloat(signed_float_bits(case_index))));
      case 3: return uint(asfloat(unsigned_float_bits(case_index)));
      case 4: return asuint(asfloat(value));
      case 5: return asuint(asint(value));
      case 6: return asuint(f16tof32(half_input(case_index)));
      default:
        return f32tof16(f16tof32(half_input(case_index)));
      }
    }

    [numthreads(64, 1, 1)]
    void main(uint3 id : SV_DispatchThreadID) {
      if (id.x >= selected_count)
        return;
      uint case_index = case_indices[id.x];
      output[case_index] = evaluate(case_index);
    }
  )",
                                    "cs_5_0");
  ASSERT_EQ(shader.result, S_OK) << shader.diagnostic_text();

  ComPtr<ID3D11ComputeShader> compute_shader;
  ASSERT_EQ(context_.device()->CreateComputeShader(
                shader.bytecode->GetBufferPointer(),
                shader.bytecode->GetBufferSize(), nullptr,
                compute_shader.put()),
            S_OK);

  ComPtr<ID3D11Buffer> selected_buffer;
  ASSERT_EQ(CreateTypedBuffer(context_.device(), selected_cases,
                              D3D11_BIND_SHADER_RESOURCE, D3D11_USAGE_IMMUTABLE,
                              selected_buffer.put()),
            S_OK);
  ComPtr<ID3D11ShaderResourceView> selected_srv;
  ASSERT_EQ(CreateTypedBufferSrv(context_.device(), selected_buffer.get(),
                                 static_cast<UINT>(selected_cases.size()),
                                 selected_srv.put()),
            S_OK);

  ComPtr<ID3D11Buffer> output_buffer;
  ASSERT_EQ(CreateTypedBuffer(context_.device(), poison,
                              D3D11_BIND_UNORDERED_ACCESS, D3D11_USAGE_DEFAULT,
                              output_buffer.put()),
            S_OK);
  ComPtr<ID3D11UnorderedAccessView> output_uav;
  ASSERT_EQ(CreateTypedBufferUav(context_.device(), output_buffer.get(),
                                 output_uav.put()),
            S_OK);

  const std::vector<std::uint32_t> parameters = {
      static_cast<std::uint32_t>(selected_cases.size()), 0, 0, 0};
  ComPtr<ID3D11Buffer> constant_buffer;
  ASSERT_EQ(CreateTypedBuffer(context_.device(), parameters,
                              D3D11_BIND_CONSTANT_BUFFER, D3D11_USAGE_IMMUTABLE,
                              constant_buffer.put()),
            S_OK);

  ID3D11ShaderResourceView *srvs[] = {selected_srv.get()};
  ID3D11UnorderedAccessView *uavs[] = {output_uav.get()};
  ID3D11Buffer *constant_buffers[] = {constant_buffer.get()};
  context_.context()->CSSetShader(compute_shader.get(), nullptr, 0);
  context_.context()->CSSetShaderResources(0, 1, srvs);
  context_.context()->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
  context_.context()->CSSetConstantBuffers(0, 1, constant_buffers);
  const UINT selected_count = static_cast<UINT>(selected_cases.size());
  context_.context()->Dispatch((selected_count + 63u) / 64u, 1, 1);

  ID3D11ShaderResourceView *null_srv = nullptr;
  ID3D11UnorderedAccessView *null_uav = nullptr;
  ID3D11Buffer *null_buffer = nullptr;
  context_.context()->CSSetShaderResources(0, 1, &null_srv);
  context_.context()->CSSetUnorderedAccessViews(0, 1, &null_uav, nullptr);
  context_.context()->CSSetConstantBuffers(0, 1, &null_buffer);
  context_.context()->CSSetShader(nullptr, nullptr, 0);

  D3D11_BUFFER_DESC staging_desc = {};
  staging_desc.ByteWidth = kConversionCaseCount * sizeof(std::uint32_t);
  staging_desc.Usage = D3D11_USAGE_STAGING;
  staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Buffer> staging;
  ASSERT_EQ(context_.device()->CreateBuffer(&staging_desc, nullptr,
                                            staging.put()),
            S_OK);
  context_.context()->CopyResource(staging.get(), output_buffer.get());
  context_.context()->Flush();

  D3D11_MAPPED_SUBRESOURCE mapped = {};
  ASSERT_EQ(
      context_.context()->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped),
      S_OK);
  const auto *actual = static_cast<const std::uint32_t *>(mapped.pData);
  RecordProperty("logical_cases_executed", selected_count);
  RecordProperty("logical_case_prefix",
                 kConversionCases.family().case_id_prefix);
  for (std::uint32_t logical = 0; logical < kConversionCaseCount; ++logical) {
    const std::uint32_t desired =
        selected[logical] ? expected[logical] : poison[logical];
    if (actual[logical] == desired)
      continue;

    const auto case_id =
        dxmt::test::LogicalCaseId(kConversionCases.family(), logical);
    const auto replay_case_id =
        selected[logical]
            ? case_id
            : dxmt::test::LogicalCaseId(kConversionCases.family(),
                                        selected_cases.front());
    ADD_FAILURE() << "LogicalCaseId: " << case_id << '\n'
                  << "Class: "
                  << dxmt::test::TestClassName(
                         kConversionCases.family().traits.test_class)
                  << '\n'
                  << "Requirements: feature_level=11_0 shader_model=5_0 "
                     "queue=Immediate capability=ComputeShader,TypedSRV,"
                     "TypedUAV\n"
                  << "ExecutionPath: "
                  << dxmt::test::ExecutionPathName(
                         kConversionCases.family().traits.execution_path)
                  << '\n'
                  << "Parameters: logical=" << logical
                  << " selected=" << (selected[logical] ? "true" : "false")
                  << " selector=" << ((logical >> 5u) & 7u)
                  << " source_bits=0x" << std::hex
                  << ConversionSourceBits(logical) << std::dec << '\n'
                  << "GpuCaseResult: status="
                  << (selected[logical] ? 1u : 2u)
                  << " first_mismatch_index=" << logical
                  << " expected=0x" << std::hex << desired << " actual=0x"
                  << actual[logical] << std::dec << '\n'
                  << "Replay: --dxmt-case-id=" << replay_case_id;
    break;
  }
  context_.context()->Unmap(staging.get(), 0);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
