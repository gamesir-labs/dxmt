#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d11_test_context.hpp"

#include <cstdint>
#include <cstring>
#include <iomanip>
#include <vector>

// Batched public-D3D11 shader semantics. The scheduler exposes every logical
// arithmetic case as a stable CaseId while the default run uses one compute
// dispatch and one staging readback. Exact replay writes only the selected
// output slots and verifies that every unselected slot remains poison.

namespace {

using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

constexpr std::uint32_t kArithmeticCaseCount = 16384;
constexpr std::uint32_t kBitOpCaseCount = 8192;
constexpr std::uint32_t kFloatCaseCount = 8192;

const dxmt::test::LogicalCaseFamilyRegistration kArithmeticCases(
    "D3D11ShaderArithmeticMatrixSpec."
    "Executes16384UintArithmeticCasesInOneDispatch",
    "D3D11.Shader.Arithmetic.Uint32.", kArithmeticCaseCount, 5,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "5_0", "Immediate", "ComputeShader,TypedSRV,TypedUAV"},
     dxmt::test::kGpuBatchTestCost,
     "selected logical IDs in an immutable typed buffer and a "
     "poison-initialized typed UAV",
     "dispatch one invocation per selected ID, apply the indexed uint32 "
     "operation, unbind the UAV, copy to staging, and map for readback",
     "every selected output equals the bit-exact CPU uint32 evaluator and "
     "every unselected output remains poison",
     "logical ID, selection state, operation selector, shift, operands, "
     "expected/actual, and exact replay argument"});

const dxmt::test::TestCostRegistration
    kArithmeticCost("D3D11ShaderArithmeticMatrixSpec."
                    "Executes16384UintArithmeticCasesInOneDispatch",
                    dxmt::test::kGpuBatchTestCost);

const dxmt::test::LogicalCaseFamilyRegistration kBitOpCases(
    "D3D11ShaderArithmeticMatrixSpec."
    "Executes8192IntegerBitOperationCasesInOneDispatch",
    "D3D11.Shader.BitOps.Uint32.", kBitOpCaseCount, 4,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "5_0", "Immediate", "ComputeShader,TypedSRV,TypedUAV"},
     dxmt::test::kGpuBatchTestCost,
     "selected logical IDs in an immutable typed buffer and a "
     "poison-initialized typed UAV",
     "dispatch one invocation per selected ID and evaluate countbits, "
     "first-bit, reverse-bit, signed-shift, signed-min/max, or signed-divide",
     "every selected output equals the bit-exact CPU evaluator and every "
     "unselected output remains poison",
     "logical ID, selection state, operation selector, shift, operands, "
     "expected/actual, and exact replay argument"});

const dxmt::test::TestCostRegistration
    kBitOpCost("D3D11ShaderArithmeticMatrixSpec."
               "Executes8192IntegerBitOperationCasesInOneDispatch",
               dxmt::test::kGpuBatchTestCost);

const dxmt::test::LogicalCaseFamilyRegistration kFloatCases(
    "D3D11ShaderArithmeticMatrixSpec."
    "Executes8192FloatArithmeticCasesInOneDispatch",
    "D3D11.Shader.Arithmetic.Float32.", kFloatCaseCount, 4,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "5_0", "Immediate", "ComputeShader,TypedSRV,TypedUAV"},
     dxmt::test::kGpuBatchTestCost,
     "selected logical IDs in an immutable typed buffer and a "
     "poison-initialized typed UAV",
     "dispatch one invocation per selected ID and evaluate float32 add, "
     "subtract, multiply, power-of-two divide, min/max, comparisons, or "
     "saturate",
     "integer-valued results and passthrough operations match bit-exactly; "
     "rounded arithmetic is within one ULP; unselected output stays poison",
     "logical ID, selection state, operation selector, operand bits and "
     "values, expected/actual bits, ULP distance, and exact replay argument"});

const dxmt::test::TestCostRegistration
    kFloatCost("D3D11ShaderArithmeticMatrixSpec."
               "Executes8192FloatArithmeticCasesInOneDispatch",
               dxmt::test::kGpuBatchTestCost);

std::uint32_t RotateLeft(std::uint32_t value, std::uint32_t shift) {
  if (shift == 0)
    return value;
  return (value << shift) | (value >> (32u - shift));
}

std::uint32_t CaseOperandX(std::uint32_t case_index) {
  return case_index * 0x9e3779b9u + 0x243f6a88u;
}

std::uint32_t CaseOperandY(std::uint32_t case_index) {
  return (case_index ^ 0xa5a5a5a5u) * 0x85ebca6bu + 0xc2b2ae35u;
}

std::uint32_t EvaluateArithmeticCase(std::uint32_t case_index) {
  const std::uint32_t shift = case_index & 31u;
  const std::uint32_t selector = (case_index >> 5u) & 7u;
  const std::uint32_t x = CaseOperandX(case_index);
  const std::uint32_t y = CaseOperandY(case_index);
  const std::uint32_t rotated = RotateLeft(x, shift);

  switch (selector) {
  case 0:
    return x + y;
  case 1:
    return x - y;
  case 2:
    return x * (y | 1u);
  case 3:
    return x ^ rotated;
  case 4:
    return (x & y) | (~x & rotated);
  case 5:
    return (x < y ? x : y) + (x > rotated ? x : rotated);
  case 6:
    return (x >> shift) ^ (y << shift);
  default:
    return (x % ((case_index & 255u) + 1u)) ^ rotated;
  }
}

std::uint32_t PopCount(std::uint32_t value) {
  std::uint32_t count = 0;
  while (value) {
    count += value & 1u;
    value >>= 1u;
  }
  return count;
}

std::uint32_t FirstBitLow(std::uint32_t value) {
  if (!value)
    return UINT32_MAX;
  std::uint32_t index = 0;
  while ((value & 1u) == 0) {
    value >>= 1u;
    ++index;
  }
  return index;
}

std::uint32_t FirstBitHigh(std::uint32_t value) {
  if (!value)
    return UINT32_MAX;
  std::uint32_t index = 0;
  while (value >>= 1u)
    ++index;
  return index;
}

std::uint32_t ReverseBits(std::uint32_t value) {
  std::uint32_t reversed = 0;
  for (std::uint32_t bit = 0; bit < 32u; ++bit) {
    reversed = (reversed << 1u) | (value & 1u);
    value >>= 1u;
  }
  return reversed;
}

std::uint32_t ArithmeticShiftRight(std::uint32_t value, std::uint32_t shift) {
  if (!shift)
    return value;
  const std::uint32_t sign_fill =
      (value & 0x80000000u) ? (~0u << (32u - shift)) : 0u;
  return (value >> shift) | sign_fill;
}

std::int32_t AsSigned(std::uint32_t value) {
  std::int32_t result = 0;
  std::memcpy(&result, &value, sizeof(result));
  return result;
}

std::uint32_t AsUnsigned(std::int32_t value) {
  std::uint32_t result = 0;
  std::memcpy(&result, &value, sizeof(result));
  return result;
}

std::uint32_t EvaluateBitOpCase(std::uint32_t case_index) {
  const std::uint32_t shift = case_index & 31u;
  const std::uint32_t selector = (case_index >> 5u) & 7u;
  const std::uint32_t x = CaseOperandX(case_index);
  const std::uint32_t y = CaseOperandY(case_index);
  const std::int32_t signed_x = AsSigned(x);
  const std::int32_t signed_y = AsSigned(y);

  switch (selector) {
  case 0:
    return PopCount(x);
  case 1:
    return FirstBitLow(x);
  case 2:
    return FirstBitHigh(x);
  case 3:
    return ReverseBits(x);
  case 4:
    return ArithmeticShiftRight(x, shift) ^ (y >> shift);
  case 5:
    return AsUnsigned(signed_x < signed_y ? signed_x : signed_y);
  case 6:
    return AsUnsigned(signed_x > signed_y ? signed_x : signed_y);
  default: {
    std::int32_t divisor = static_cast<std::int32_t>((y & 0x7ffeu) + 2u);
    if (case_index & 1u)
      divisor = -divisor;
    const std::int32_t quotient =
        static_cast<std::int32_t>(static_cast<std::int64_t>(signed_x) /
                                  static_cast<std::int64_t>(divisor));
    const std::int32_t remainder =
        static_cast<std::int32_t>(static_cast<std::int64_t>(signed_x) %
                                  static_cast<std::int64_t>(divisor));
    return AsUnsigned(quotient) ^ RotateLeft(AsUnsigned(remainder), 16u);
  }
  }
}

std::uint32_t FloatOperandXBits(std::uint32_t case_index) {
  const std::uint32_t sign = (case_index & 1u) << 31u;
  const std::uint32_t exponent = 121u + ((case_index >> 8u) & 7u);
  return sign | (exponent << 23u) |
         (CaseOperandX(case_index) & 0x007fffffu);
}

std::uint32_t FloatOperandYBits(std::uint32_t case_index) {
  const std::uint32_t sign = ((case_index >> 1u) & 1u) << 31u;
  const std::uint32_t exponent = 121u + ((case_index >> 11u) & 7u);
  return sign | (exponent << 23u) |
         (CaseOperandY(case_index) & 0x007fffffu);
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

std::uint32_t EvaluateFloatCase(std::uint32_t case_index) {
  const std::uint32_t selector = (case_index >> 5u) & 7u;
  const float x = FloatFromBits(FloatOperandXBits(case_index));
  const float y = FloatFromBits(FloatOperandYBits(case_index));

  switch (selector) {
  case 0:
    return FloatBits(x + y);
  case 1:
    return FloatBits(x - y);
  case 2:
    return FloatBits(x * y);
  case 3: {
    const int scale_exponent =
        static_cast<int>((case_index >> 2u) & 7u) - 3;
    const float scale = FloatFromBits(
        static_cast<std::uint32_t>(127 + scale_exponent) << 23u);
    return FloatBits(x / scale);
  }
  case 4:
    return FloatBits(x < y ? x : y);
  case 5:
    return FloatBits(x > y ? x : y);
  case 6:
    return (x < y ? 1u : 0u) | (x <= y ? 2u : 0u) |
           (x == y ? 4u : 0u) | (x != y ? 8u : 0u) |
           (x >= y ? 16u : 0u) | (x > y ? 32u : 0u);
  default:
    return FloatBits(x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x));
  }
}

std::uint32_t OrderedFloatBits(std::uint32_t bits) {
  return (bits & 0x80000000u) ? ~bits : (bits | 0x80000000u);
}

std::uint32_t FloatUlpDistance(std::uint32_t left, std::uint32_t right) {
  if ((left & 0x7fffffffu) == 0 && (right & 0x7fffffffu) == 0)
    return 0;
  const std::uint32_t ordered_left = OrderedFloatBits(left);
  const std::uint32_t ordered_right = OrderedFloatBits(right);
  return ordered_left > ordered_right ? ordered_left - ordered_right
                                      : ordered_right - ordered_left;
}

bool FloatCaseMatches(std::uint32_t case_index, std::uint32_t expected,
                      std::uint32_t actual) {
  if (expected == actual)
    return true;
  const std::uint32_t selector = (case_index >> 5u) & 7u;
  if (selector > 3u)
    return false;
  if ((expected & 0x7f800000u) == 0x7f800000u ||
      (actual & 0x7f800000u) == 0x7f800000u)
    return false;
  return FloatUlpDistance(expected, actual) <= 1u;
}

HRESULT CreateTypedBuffer(ID3D11Device *device,
                          const std::vector<std::uint32_t> &values,
                          UINT bind_flags, D3D11_USAGE usage,
                          ID3D11Buffer **buffer) {
  if (!device || !buffer || values.empty())
    return E_INVALIDARG;

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
  desc.Buffer.FirstElement = 0;
  desc.Buffer.NumElements = element_count;
  return device->CreateShaderResourceView(buffer, &desc, view);
}

HRESULT CreateTypedBufferUav(ID3D11Device *device, ID3D11Buffer *buffer,
                             UINT element_count,
                             ID3D11UnorderedAccessView **view) {
  D3D11_UNORDERED_ACCESS_VIEW_DESC desc = {};
  desc.Format = DXGI_FORMAT_R32_UINT;
  desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
  desc.Buffer.FirstElement = 0;
  desc.Buffer.NumElements = element_count;
  return device->CreateUnorderedAccessView(buffer, &desc, view);
}

HRESULT ReadBuffer(ID3D11Device *device, ID3D11DeviceContext *context,
                   ID3D11Buffer *source, UINT byte_width,
                   std::vector<std::uint32_t> *output) {
  if (!device || !context || !source || !output || byte_width == 0 ||
      byte_width % sizeof(std::uint32_t) != 0)
    return E_INVALIDARG;

  D3D11_BUFFER_DESC staging_desc = {};
  staging_desc.ByteWidth = byte_width;
  staging_desc.Usage = D3D11_USAGE_STAGING;
  staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

  ComPtr<ID3D11Buffer> staging;
  HRESULT hr = device->CreateBuffer(&staging_desc, nullptr, staging.put());
  if (FAILED(hr))
    return hr;

  context->CopyResource(staging.get(), source);
  context->Flush();

  D3D11_MAPPED_SUBRESOURCE mapped = {};
  hr = context->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped);
  if (FAILED(hr))
    return hr;

  output->resize(byte_width / sizeof(std::uint32_t));
  std::memcpy(output->data(), mapped.pData, byte_width);
  context->Unmap(staging.get(), 0);
  return S_OK;
}

class D3D11ShaderArithmeticMatrixSpec : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  D3D11TestContext context_;
};

TEST_F(D3D11ShaderArithmeticMatrixSpec,
       Executes16384UintArithmeticCasesInOneDispatch) {
  std::vector<std::uint32_t> expected(kArithmeticCaseCount);
  std::vector<std::uint32_t> poison(kArithmeticCaseCount);
  std::vector<bool> selected(kArithmeticCaseCount, false);
  std::vector<std::uint32_t> selected_cases;
  selected_cases.reserve(kArithmeticCaseCount);
  for (std::uint32_t index = 0; index < kArithmeticCaseCount; ++index) {
    expected[index] = EvaluateArithmeticCase(index);
    poison[index] = ~expected[index];
    if (dxmt::test::LogicalCaseSelected(kArithmeticCases.family(), index)) {
      selected[index] = true;
      selected_cases.push_back(index);
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

    uint rotate_left(uint value, uint shift) {
      return shift == 0 ? value : (value << shift) | (value >> (32 - shift));
    }

    uint evaluate(uint case_index) {
      uint shift = case_index & 31u;
      uint selector = (case_index >> 5u) & 7u;
      uint x = case_index * 0x9e3779b9u + 0x243f6a88u;
      uint y = (case_index ^ 0xa5a5a5a5u) * 0x85ebca6bu + 0xc2b2ae35u;
      uint rotated = rotate_left(x, shift);

      switch (selector) {
      case 0: return x + y;
      case 1: return x - y;
      case 2: return x * (y | 1u);
      case 3: return x ^ rotated;
      case 4: return (x & y) | (~x & rotated);
      case 5: return min(x, y) + max(x, rotated);
      case 6: return (x >> shift) ^ (y << shift);
      default: return (x % ((case_index & 255u) + 1u)) ^ rotated;
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
                                 kArithmeticCaseCount, output_uav.put()),
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

  std::vector<std::uint32_t> actual;
  ASSERT_EQ(ReadBuffer(context_.device(), context_.context(),
                       output_buffer.get(),
                       kArithmeticCaseCount * sizeof(std::uint32_t), &actual),
            S_OK);
  ASSERT_EQ(actual.size(), static_cast<std::size_t>(kArithmeticCaseCount));

  RecordProperty("logical_cases_executed", selected_count);
  RecordProperty("logical_case_prefix",
                 kArithmeticCases.family().case_id_prefix);
  for (std::uint32_t logical = 0; logical < kArithmeticCaseCount; ++logical) {
    const std::uint32_t desired =
        selected[logical] ? expected[logical] : poison[logical];
    if (actual[logical] == desired)
      continue;

    const dxmt::test::GpuCaseResult result = {
        selected[logical] ? 1u : 2u, logical, desired, actual[logical]};
    const auto case_id =
        dxmt::test::LogicalCaseId(kArithmeticCases.family(), logical);
    const auto replay_case_id =
        selected[logical] ? case_id
                          : dxmt::test::LogicalCaseId(kArithmeticCases.family(),
                                                      selected_cases.front());
    ADD_FAILURE() << "LogicalCaseId: " << case_id << '\n'
                  << "Class: "
                  << dxmt::test::TestClassName(
                         kArithmeticCases.family().traits.test_class)
                  << '\n'
                  << "Requirements: feature_level=11_0 shader_model=5_0 "
                     "queue=Immediate capability=ComputeShader,TypedSRV,"
                     "TypedUAV\n"
                  << "ExecutionPath: "
                  << dxmt::test::ExecutionPathName(
                         kArithmeticCases.family().traits.execution_path)
                  << '\n'
                  << "Parameters: logical=" << logical
                  << " selected=" << (selected[logical] ? "true" : "false")
                  << " selector=" << ((logical >> 5u) & 7u)
                  << " shift=" << (logical & 31u) << " x=0x" << std::hex
                  << CaseOperandX(logical) << " y=0x" << CaseOperandY(logical)
                  << std::dec << '\n'
                  << "GpuCaseResult: status=" << result.status
                  << " first_mismatch_index=" << result.first_mismatch_index
                  << " expected=0x" << std::hex << result.expected
                  << " actual=0x" << result.actual << std::dec << '\n'
                  << "Replay: --dxmt-case-id=" << replay_case_id;
    break;
  }
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11ShaderArithmeticMatrixSpec,
       Executes8192IntegerBitOperationCasesInOneDispatch) {
  std::vector<std::uint32_t> expected(kBitOpCaseCount);
  std::vector<std::uint32_t> poison(kBitOpCaseCount);
  std::vector<bool> selected(kBitOpCaseCount, false);
  std::vector<std::uint32_t> selected_cases;
  selected_cases.reserve(kBitOpCaseCount);
  for (std::uint32_t index = 0; index < kBitOpCaseCount; ++index) {
    expected[index] = EvaluateBitOpCase(index);
    poison[index] = ~expected[index];
    if (dxmt::test::LogicalCaseSelected(kBitOpCases.family(), index)) {
      selected[index] = true;
      selected_cases.push_back(index);
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

    uint rotate_left(uint value, uint shift) {
      return shift == 0 ? value : (value << shift) | (value >> (32 - shift));
    }

    uint reverse_bits(uint value) {
      value = ((value & 0x55555555u) << 1u) | ((value >> 1u) & 0x55555555u);
      value = ((value & 0x33333333u) << 2u) | ((value >> 2u) & 0x33333333u);
      value = ((value & 0x0f0f0f0fu) << 4u) | ((value >> 4u) & 0x0f0f0f0fu);
      value = ((value & 0x00ff00ffu) << 8u) | ((value >> 8u) & 0x00ff00ffu);
      return (value << 16u) | (value >> 16u);
    }

    uint evaluate(uint case_index) {
      uint shift = case_index & 31u;
      uint selector = (case_index >> 5u) & 7u;
      uint x = case_index * 0x9e3779b9u + 0x243f6a88u;
      uint y = (case_index ^ 0xa5a5a5a5u) * 0x85ebca6bu + 0xc2b2ae35u;
      int signed_x = asint(x);
      int signed_y = asint(y);

      switch (selector) {
      case 0: return countbits(x);
      case 1: return firstbitlow(x);
      case 2: return firstbithigh(x);
      case 3: return reverse_bits(x);
      case 4: return asuint(signed_x >> shift) ^ (y >> shift);
      case 5: return asuint(min(signed_x, signed_y));
      case 6: return asuint(max(signed_x, signed_y));
      default:
        int divisor = int((y & 0x7ffeu) + 2u);
        if (case_index & 1u)
          divisor = -divisor;
        int quotient = signed_x / divisor;
        int remainder = signed_x % divisor;
        return asuint(quotient) ^ rotate_left(asuint(remainder), 16u);
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
                                 kBitOpCaseCount, output_uav.put()),
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

  std::vector<std::uint32_t> actual;
  ASSERT_EQ(ReadBuffer(context_.device(), context_.context(),
                       output_buffer.get(),
                       kBitOpCaseCount * sizeof(std::uint32_t), &actual),
            S_OK);
  ASSERT_EQ(actual.size(), static_cast<std::size_t>(kBitOpCaseCount));

  RecordProperty("logical_cases_executed", selected_count);
  RecordProperty("logical_case_prefix", kBitOpCases.family().case_id_prefix);
  for (std::uint32_t logical = 0; logical < kBitOpCaseCount; ++logical) {
    const std::uint32_t desired =
        selected[logical] ? expected[logical] : poison[logical];
    if (actual[logical] == desired)
      continue;

    const dxmt::test::GpuCaseResult result = {
        selected[logical] ? 1u : 2u, logical, desired, actual[logical]};
    const auto case_id =
        dxmt::test::LogicalCaseId(kBitOpCases.family(), logical);
    const auto replay_case_id =
        selected[logical] ? case_id
                          : dxmt::test::LogicalCaseId(kBitOpCases.family(),
                                                      selected_cases.front());
    ADD_FAILURE() << "LogicalCaseId: " << case_id << '\n'
                  << "Class: "
                  << dxmt::test::TestClassName(
                         kBitOpCases.family().traits.test_class)
                  << '\n'
                  << "Requirements: feature_level=11_0 shader_model=5_0 "
                     "queue=Immediate capability=ComputeShader,TypedSRV,"
                     "TypedUAV\n"
                  << "ExecutionPath: "
                  << dxmt::test::ExecutionPathName(
                         kBitOpCases.family().traits.execution_path)
                  << '\n'
                  << "Parameters: logical=" << logical
                  << " selected=" << (selected[logical] ? "true" : "false")
                  << " selector=" << ((logical >> 5u) & 7u)
                  << " shift=" << (logical & 31u) << " x=0x" << std::hex
                  << CaseOperandX(logical) << " y=0x" << CaseOperandY(logical)
                  << std::dec << '\n'
                  << "GpuCaseResult: status=" << result.status
                  << " first_mismatch_index=" << result.first_mismatch_index
                  << " expected=0x" << std::hex << result.expected
                  << " actual=0x" << result.actual << std::dec << '\n'
                  << "Replay: --dxmt-case-id=" << replay_case_id;
    break;
  }
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11ShaderArithmeticMatrixSpec,
       Executes8192FloatArithmeticCasesInOneDispatch) {
  std::vector<std::uint32_t> expected(kFloatCaseCount);
  std::vector<std::uint32_t> poison(kFloatCaseCount);
  std::vector<bool> selected(kFloatCaseCount, false);
  std::vector<std::uint32_t> selected_cases;
  selected_cases.reserve(kFloatCaseCount);
  for (std::uint32_t index = 0; index < kFloatCaseCount; ++index) {
    expected[index] = EvaluateFloatCase(index);
    poison[index] = expected[index] ^ 0x5a5aa5a5u;
    if (dxmt::test::LogicalCaseSelected(kFloatCases.family(), index)) {
      selected[index] = true;
      selected_cases.push_back(index);
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

    uint operand_x_bits(uint case_index) {
      uint sign = (case_index & 1u) << 31u;
      uint exponent = 121u + ((case_index >> 8u) & 7u);
      uint value = case_index * 0x9e3779b9u + 0x243f6a88u;
      return sign | (exponent << 23u) | (value & 0x007fffffu);
    }

    uint operand_y_bits(uint case_index) {
      uint sign = ((case_index >> 1u) & 1u) << 31u;
      uint exponent = 121u + ((case_index >> 11u) & 7u);
      uint value =
          (case_index ^ 0xa5a5a5a5u) * 0x85ebca6bu + 0xc2b2ae35u;
      return sign | (exponent << 23u) | (value & 0x007fffffu);
    }

    uint evaluate(uint case_index) {
      uint selector = (case_index >> 5u) & 7u;
      float x = asfloat(operand_x_bits(case_index));
      float y = asfloat(operand_y_bits(case_index));

      switch (selector) {
      case 0: return asuint(x + y);
      case 1: return asuint(x - y);
      case 2: return asuint(x * y);
      case 3: {
        int scale_exponent = int((case_index >> 2u) & 7u) - 3;
        float scale = asfloat(uint(127 + scale_exponent) << 23u);
        return asuint(x / scale);
      }
      case 4: return asuint(min(x, y));
      case 5: return asuint(max(x, y));
      case 6:
        return (x < y ? 1u : 0u) | (x <= y ? 2u : 0u) |
               (x == y ? 4u : 0u) | (x != y ? 8u : 0u) |
               (x >= y ? 16u : 0u) | (x > y ? 32u : 0u);
      default: return asuint(saturate(x));
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
                                 kFloatCaseCount, output_uav.put()),
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

  std::vector<std::uint32_t> actual;
  ASSERT_EQ(ReadBuffer(context_.device(), context_.context(),
                       output_buffer.get(),
                       kFloatCaseCount * sizeof(std::uint32_t), &actual),
            S_OK);
  ASSERT_EQ(actual.size(), static_cast<std::size_t>(kFloatCaseCount));

  RecordProperty("logical_cases_executed", selected_count);
  RecordProperty("logical_case_prefix", kFloatCases.family().case_id_prefix);
  for (std::uint32_t logical = 0; logical < kFloatCaseCount; ++logical) {
    const std::uint32_t desired =
        selected[logical] ? expected[logical] : poison[logical];
    const bool matches = selected[logical]
                             ? FloatCaseMatches(logical, desired, actual[logical])
                             : actual[logical] == desired;
    if (matches)
      continue;

    const dxmt::test::GpuCaseResult result = {
        selected[logical] ? 1u : 2u, logical, desired, actual[logical]};
    const auto case_id =
        dxmt::test::LogicalCaseId(kFloatCases.family(), logical);
    const auto replay_case_id =
        selected[logical] ? case_id
                          : dxmt::test::LogicalCaseId(kFloatCases.family(),
                                                      selected_cases.front());
    ADD_FAILURE() << "LogicalCaseId: " << case_id << '\n'
                  << "Class: "
                  << dxmt::test::TestClassName(
                         kFloatCases.family().traits.test_class)
                  << '\n'
                  << "Requirements: feature_level=11_0 shader_model=5_0 "
                     "queue=Immediate capability=ComputeShader,TypedSRV,"
                     "TypedUAV\n"
                  << "ExecutionPath: "
                  << dxmt::test::ExecutionPathName(
                         kFloatCases.family().traits.execution_path)
                  << '\n'
                  << "Parameters: logical=" << logical
                  << " selected=" << (selected[logical] ? "true" : "false")
                  << " selector=" << ((logical >> 5u) & 7u)
                  << " x_bits=0x" << std::hex << FloatOperandXBits(logical)
                  << " y_bits=0x" << FloatOperandYBits(logical) << std::dec
                  << " x=" << FloatFromBits(FloatOperandXBits(logical))
                  << " y=" << FloatFromBits(FloatOperandYBits(logical))
                  << '\n'
                  << "GpuCaseResult: status=" << result.status
                  << " first_mismatch_index=" << result.first_mismatch_index
                  << " expected=0x" << std::hex << result.expected
                  << " actual=0x" << result.actual << std::dec
                  << " ulp_distance="
                  << FloatUlpDistance(result.expected, result.actual) << '\n'
                  << "Replay: --dxmt-case-id=" << replay_case_id;
    break;
  }
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
