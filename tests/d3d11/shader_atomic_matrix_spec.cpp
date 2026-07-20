#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d11_test_context.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iomanip>
#include <string_view>
#include <vector>

// Batched public-D3D11 structured-UAV atomics. Every logical case owns one
// target element, while eight threads in its group contend on that element.
// This keeps cases independent under the parallel scheduler and makes every
// final value deterministic even though the individual atomic order is not.

namespace {

using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

constexpr std::uint32_t kAtomicCaseCount = 4096;
constexpr std::uint32_t kAtomicThreadCount = 8;

const dxmt::test::LogicalCaseFamilyRegistration kAtomicCases(
    "D3D11ShaderAtomicMatrixSpec."
    "Executes4096StructuredUavAtomicCasesInOneDispatch",
    "D3D11.Shader.Atomic.StructuredUav.", kAtomicCaseCount, 4,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "5_0", "Immediate",
      "ComputeShader,StructuredUAV,AtomicOperations"},
     dxmt::test::kGpuBatchTestCost,
     "selected logical IDs in an immutable typed SRV and one independently "
     "initialized RWStructuredBuffer element per logical case",
     "dispatch one eight-thread group per selected ID and contend on its "
     "element with InterlockedAdd/And/Or/Xor/Min/Max/Exchange/"
     "CompareExchange",
     "every selected element matches the deterministic CPU reduction and "
     "every unselected element remains poison",
     "logical ID, operation, success/failure variant, initial value, first "
     "and last operands, expected/actual, and exact replay argument"});

const dxmt::test::LogicalCaseFamilyRegistration kTypedAtomicCases(
    "D3D11ShaderAtomicMatrixSpec."
    "Executes4096TypedUavAtomicCasesInOneDispatch",
    "D3D11.Shader.Atomic.TypedUav.", kAtomicCaseCount, 4,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "5_0", "Immediate", "ComputeShader,TypedUAV,AtomicOperations"},
     dxmt::test::kGpuBatchTestCost,
     "selected logical IDs in an immutable typed SRV and an R32_UINT UAV "
     "whose view starts at backing-buffer element one",
     "dispatch one eight-thread group per selected ID and contend on its "
     "typed element with all SM5 uint atomic operations",
     "every selected element matches the deterministic CPU reduction while "
     "unselected elements and backing-buffer guards remain poison",
     "logical ID, typed view index, backing index, operation, "
     "success/failure variant, expected/actual, and exact replay argument"});

const dxmt::test::TestCostRegistration
    kAtomicCost("D3D11ShaderAtomicMatrixSpec."
                "Executes4096StructuredUavAtomicCasesInOneDispatch",
                dxmt::test::kGpuBatchTestCost);

const dxmt::test::TestCostRegistration
    kTypedAtomicCost("D3D11ShaderAtomicMatrixSpec."
                     "Executes4096TypedUavAtomicCasesInOneDispatch",
                     dxmt::test::kGpuBatchTestCost);

constexpr std::array<const char *, 8> kAtomicOperationNames = {
    "add", "and", "or", "xor", "min", "max", "exchange", "compare_exchange"};

std::uint32_t AtomicValue(std::uint32_t logical, std::uint32_t lane) {
  std::uint32_t value =
      logical * 0x9e3779b9u + lane * 0x85ebca6bu + 0x243f6a88u;
  value ^= value >> 16u;
  value *= 0x7feb352du;
  value ^= value >> 15u;
  return value;
}

std::uint32_t AtomicSelector(std::uint32_t logical) { return logical & 7u; }

bool CompareExchangeSucceeds(std::uint32_t logical) {
  return ((logical >> 3u) & 1u) == 0u;
}

std::uint32_t AtomicInitial(std::uint32_t logical) {
  switch (AtomicSelector(logical)) {
  case 0:
    return AtomicValue(logical, 31u);
  case 1:
    return AtomicValue(logical, 29u) | 0xf000000fu;
  case 2:
    return AtomicValue(logical, 27u) & 0x0ffff0f0u;
  case 3:
    return AtomicValue(logical, 25u);
  case 4:
    return 0xffffffffu;
  case 5:
    return 0u;
  case 6:
    return AtomicValue(logical, 23u);
  default: {
    const std::uint32_t compare = AtomicValue(logical, 19u);
    return CompareExchangeSucceeds(logical) ? compare : ~compare;
  }
  }
}

std::uint32_t AtomicOperand(std::uint32_t logical, std::uint32_t lane) {
  switch (AtomicSelector(logical)) {
  case 0:
    return ((logical >> 3u) & 15u) + 1u;
  case 1:
    return ~(1u << ((logical + lane * 5u) & 31u));
  case 2:
    return 1u << ((logical * 3u + lane * 7u) & 31u);
  case 3:
  case 4:
  case 5:
    return AtomicValue(logical, lane);
  case 6:
    return AtomicValue(logical, 17u);
  default:
    return AtomicValue(logical, 21u);
  }
}

std::uint32_t EvaluateAtomicCase(std::uint32_t logical) {
  std::uint32_t result = AtomicInitial(logical);
  switch (AtomicSelector(logical)) {
  case 0:
    for (std::uint32_t lane = 0; lane < kAtomicThreadCount; ++lane)
      result += AtomicOperand(logical, lane);
    break;
  case 1:
    for (std::uint32_t lane = 0; lane < kAtomicThreadCount; ++lane)
      result &= AtomicOperand(logical, lane);
    break;
  case 2:
    for (std::uint32_t lane = 0; lane < kAtomicThreadCount; ++lane)
      result |= AtomicOperand(logical, lane);
    break;
  case 3:
    for (std::uint32_t lane = 0; lane < kAtomicThreadCount; ++lane)
      result ^= AtomicOperand(logical, lane);
    break;
  case 4:
    for (std::uint32_t lane = 0; lane < kAtomicThreadCount; ++lane)
      result = std::min(result, AtomicOperand(logical, lane));
    break;
  case 5:
    for (std::uint32_t lane = 0; lane < kAtomicThreadCount; ++lane)
      result = std::max(result, AtomicOperand(logical, lane));
    break;
  case 6:
    result = AtomicOperand(logical, 0u);
    break;
  default:
    if (CompareExchangeSucceeds(logical))
      result = AtomicOperand(logical, 0u);
    break;
  }
  return result;
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

HRESULT CreateTypedSrv(ID3D11Device *device, ID3D11Buffer *buffer,
                       UINT element_count, ID3D11ShaderResourceView **view) {
  D3D11_SHADER_RESOURCE_VIEW_DESC desc = {};
  desc.Format = DXGI_FORMAT_R32_UINT;
  desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
  desc.Buffer.NumElements = element_count;
  return device->CreateShaderResourceView(buffer, &desc, view);
}

HRESULT CreateStructuredBuffer(ID3D11Device *device,
                               const std::vector<std::uint32_t> &values,
                               ID3D11Buffer **buffer) {
  D3D11_BUFFER_DESC desc = {};
  desc.ByteWidth = static_cast<UINT>(values.size() * sizeof(values[0]));
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
  desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
  desc.StructureByteStride = sizeof(values[0]);
  D3D11_SUBRESOURCE_DATA initial = {};
  initial.pSysMem = values.data();
  return device->CreateBuffer(&desc, &initial, buffer);
}

HRESULT CreateStructuredUav(ID3D11Device *device, ID3D11Buffer *buffer,
                            UINT element_count,
                            ID3D11UnorderedAccessView **view) {
  D3D11_UNORDERED_ACCESS_VIEW_DESC desc = {};
  desc.Format = DXGI_FORMAT_UNKNOWN;
  desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
  desc.Buffer.NumElements = element_count;
  return device->CreateUnorderedAccessView(buffer, &desc, view);
}

constexpr std::string_view kTypedAtomicShader = R"(
    #define DXMT_TARGET RWBuffer<uint>
    #define DXMT_ADD(index, value, original) \
      InterlockedAdd(target[index], value, original)
    #define DXMT_AND(index, value, original) \
      InterlockedAnd(target[index], value, original)
    #define DXMT_OR(index, value, original) \
      InterlockedOr(target[index], value, original)
    #define DXMT_XOR(index, value, original) \
      InterlockedXor(target[index], value, original)
    #define DXMT_MIN(index, value, original) \
      InterlockedMin(target[index], value, original)
    #define DXMT_MAX(index, value, original) \
      InterlockedMax(target[index], value, original)
    #define DXMT_EXCHANGE(index, value, original) \
      InterlockedExchange(target[index], value, original)
    #define DXMT_COMPARE_EXCHANGE(index, compare, value, original) \
      InterlockedCompareExchange(target[index], compare, value, original)

    Buffer<uint> case_indices : register(t0);
    DXMT_TARGET target : register(u0);

    cbuffer Parameters : register(b0) {
      uint selected_count;
      uint3 padding;
    };

    uint atomic_value(uint logical, uint lane) {
      uint value = logical * 0x9e3779b9u + lane * 0x85ebca6bu +
                   0x243f6a88u;
      value ^= value >> 16u;
      value *= 0x7feb352du;
      value ^= value >> 15u;
      return value;
    }

    uint atomic_operand(uint logical, uint lane, uint selector) {
      switch (selector) {
      case 0:
        return ((logical >> 3u) & 15u) + 1u;
      case 1:
        return ~(1u << ((logical + lane * 5u) & 31u));
      case 2:
        return 1u << ((logical * 3u + lane * 7u) & 31u);
      case 3:
      case 4:
      case 5:
        return atomic_value(logical, lane);
      case 6:
        return atomic_value(logical, 17u);
      default:
        return atomic_value(logical, 21u);
      }
    }

    [numthreads(8, 1, 1)]
    void main(uint3 group : SV_GroupID, uint3 lane : SV_GroupThreadID) {
      if (group.x >= selected_count)
        return;
      uint logical = case_indices[group.x];
      uint selector = logical & 7u;
      uint operand = atomic_operand(logical, lane.x, selector);
      uint original;
      switch (selector) {
      case 0:
        DXMT_ADD(logical, operand, original);
        break;
      case 1:
        DXMT_AND(logical, operand, original);
        break;
      case 2:
        DXMT_OR(logical, operand, original);
        break;
      case 3:
        DXMT_XOR(logical, operand, original);
        break;
      case 4:
        DXMT_MIN(logical, operand, original);
        break;
      case 5:
        DXMT_MAX(logical, operand, original);
        break;
      case 6:
        DXMT_EXCHANGE(logical, operand, original);
        break;
      default:
        DXMT_COMPARE_EXCHANGE(logical, atomic_value(logical, 19u), operand,
                              original);
        break;
      }
    }
  )";

HRESULT CreateTypedAtomicBuffer(ID3D11Device *device,
                                const std::vector<std::uint32_t> &values,
                                ID3D11Buffer **buffer) {
  D3D11_BUFFER_DESC desc = {};
  desc.ByteWidth = static_cast<UINT>(values.size() * sizeof(values[0]));
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
  D3D11_SUBRESOURCE_DATA initial = {};
  initial.pSysMem = values.data();
  return device->CreateBuffer(&desc, &initial, buffer);
}

HRESULT CreateTypedAtomicUav(ID3D11Device *device, ID3D11Buffer *buffer,
                             ID3D11UnorderedAccessView **view) {
  D3D11_UNORDERED_ACCESS_VIEW_DESC desc = {};
  desc.Format = DXGI_FORMAT_R32_UINT;
  desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
  desc.Buffer.FirstElement = 1;
  desc.Buffer.NumElements = kAtomicCaseCount;
  return device->CreateUnorderedAccessView(buffer, &desc, view);
}

void RunTypedAtomicMatrix(
    D3D11TestContext &context,
    const dxmt::test::LogicalCaseFamilyRegistration &cases) {
  const std::size_t backing_count = kAtomicCaseCount + 2u;
  std::vector<std::uint32_t> initial(backing_count);
  for (std::size_t index = 0; index < backing_count; ++index)
    initial[index] =
        AtomicValue(static_cast<std::uint32_t>(index), 37u) ^ 0x6a09e667u;
  std::vector<std::uint32_t> expected = initial;
  std::vector<bool> selected(kAtomicCaseCount, false);
  std::vector<std::uint32_t> selected_cases;
  selected_cases.reserve(kAtomicCaseCount);
  for (std::uint32_t logical = 0; logical < kAtomicCaseCount; ++logical) {
    const std::size_t target_index = logical + 1u;
    const std::uint32_t poison = EvaluateAtomicCase(logical) ^ 0x3c6ef372u;
    if (dxmt::test::LogicalCaseSelected(cases.family(), logical)) {
      selected[logical] = true;
      selected_cases.push_back(logical);
      initial[target_index] = AtomicInitial(logical);
      expected[target_index] = EvaluateAtomicCase(logical);
    } else {
      initial[target_index] = poison;
      expected[target_index] = poison;
    }
  }
  ASSERT_FALSE(selected_cases.empty());

  const auto shader = CompileShader(kTypedAtomicShader, "cs_5_0");
  ASSERT_EQ(shader.result, S_OK) << shader.diagnostic_text();
  ComPtr<ID3D11ComputeShader> compute_shader;
  ASSERT_EQ(
      context.device()->CreateComputeShader(shader.bytecode->GetBufferPointer(),
                                            shader.bytecode->GetBufferSize(),
                                            nullptr, compute_shader.put()),
      S_OK);

  ComPtr<ID3D11Buffer> selected_buffer;
  ASSERT_EQ(CreateTypedBuffer(context.device(), selected_cases,
                              D3D11_BIND_SHADER_RESOURCE, D3D11_USAGE_IMMUTABLE,
                              selected_buffer.put()),
            S_OK);
  ComPtr<ID3D11ShaderResourceView> selected_srv;
  ASSERT_EQ(CreateTypedSrv(context.device(), selected_buffer.get(),
                           static_cast<UINT>(selected_cases.size()),
                           selected_srv.put()),
            S_OK);

  ComPtr<ID3D11Buffer> target_buffer;
  ASSERT_EQ(
      CreateTypedAtomicBuffer(context.device(), initial, target_buffer.put()),
      S_OK);
  ComPtr<ID3D11UnorderedAccessView> target_uav;
  ASSERT_EQ(CreateTypedAtomicUav(context.device(), target_buffer.get(),
                                 target_uav.put()),
            S_OK);

  const std::vector<std::uint32_t> parameters = {
      static_cast<std::uint32_t>(selected_cases.size()), 0, 0, 0};
  ComPtr<ID3D11Buffer> constant_buffer;
  ASSERT_EQ(CreateTypedBuffer(context.device(), parameters,
                              D3D11_BIND_CONSTANT_BUFFER, D3D11_USAGE_IMMUTABLE,
                              constant_buffer.put()),
            S_OK);

  ID3D11ShaderResourceView *srv = selected_srv.get();
  ID3D11UnorderedAccessView *uav = target_uav.get();
  ID3D11Buffer *constant = constant_buffer.get();
  context.context()->CSSetShader(compute_shader.get(), nullptr, 0);
  context.context()->CSSetShaderResources(0, 1, &srv);
  context.context()->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
  context.context()->CSSetConstantBuffers(0, 1, &constant);
  context.context()->Dispatch(static_cast<UINT>(selected_cases.size()), 1, 1);

  ID3D11ShaderResourceView *null_srv = nullptr;
  ID3D11UnorderedAccessView *null_uav = nullptr;
  ID3D11Buffer *null_buffer = nullptr;
  context.context()->CSSetShaderResources(0, 1, &null_srv);
  context.context()->CSSetUnorderedAccessViews(0, 1, &null_uav, nullptr);
  context.context()->CSSetConstantBuffers(0, 1, &null_buffer);
  context.context()->CSSetShader(nullptr, nullptr, 0);

  D3D11_BUFFER_DESC staging_desc = {};
  staging_desc.ByteWidth =
      static_cast<UINT>(backing_count * sizeof(std::uint32_t));
  staging_desc.Usage = D3D11_USAGE_STAGING;
  staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Buffer> staging;
  ASSERT_EQ(
      context.device()->CreateBuffer(&staging_desc, nullptr, staging.put()),
      S_OK);
  context.context()->CopyResource(staging.get(), target_buffer.get());
  context.context()->Flush();

  D3D11_MAPPED_SUBRESOURCE mapped = {};
  ASSERT_EQ(
      context.context()->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped),
      S_OK);
  const auto *mapped_values = static_cast<const std::uint32_t *>(mapped.pData);
  std::vector<std::uint32_t> actual(backing_count);
  std::copy_n(mapped_values, backing_count, actual.begin());
  context.context()->Unmap(staging.get(), 0);

  ::testing::Test::RecordProperty("logical_cases_executed",
                                  selected_cases.size());
  ::testing::Test::RecordProperty("logical_case_prefix",
                                  cases.family().case_id_prefix);
  for (std::size_t backing_index = 0; backing_index < backing_count;
       ++backing_index) {
    if (actual[backing_index] == expected[backing_index])
      continue;

    const bool target = backing_index > 0u && backing_index <= kAtomicCaseCount;
    const std::uint32_t logical =
        target ? static_cast<std::uint32_t>(backing_index - 1u)
               : selected_cases.front();
    const std::uint32_t selector = AtomicSelector(logical);
    const auto case_id = dxmt::test::LogicalCaseId(cases.family(), logical);
    const auto replay_case_id =
        target && selected[logical]
            ? case_id
            : dxmt::test::LogicalCaseId(cases.family(), selected_cases.front());
    ADD_FAILURE() << "LogicalCaseId: " << case_id << '\n'
                  << "Class: "
                  << dxmt::test::TestClassName(cases.family().traits.test_class)
                  << '\n'
                  << "Requirements: feature_level=11_0 shader_model=5_0 "
                     "queue=Immediate capability=ComputeShader,TypedUAV,"
                     "AtomicOperations\n"
                  << "ExecutionPath: "
                  << dxmt::test::ExecutionPathName(
                         cases.family().traits.execution_path)
                  << '\n'
                  << "Parameters: logical=" << logical
                  << " selected=" << (selected[logical] ? "true" : "false")
                  << " target=" << (target ? "true" : "false")
                  << " operation=" << kAtomicOperationNames[selector]
                  << " compare_success="
                  << (CompareExchangeSucceeds(logical) ? "true" : "false")
                  << " backing_index=" << backing_index
                  << " shader_address=" << logical << " initial=0x" << std::hex
                  << initial[backing_index] << std::dec << '\n'
                  << "GpuCaseResult: status="
                  << (target && selected[logical] ? 1u : 2u)
                  << " first_mismatch_index=" << backing_index << " expected=0x"
                  << std::hex << expected[backing_index] << " actual=0x"
                  << actual[backing_index] << std::dec << '\n'
                  << "Replay: --dxmt-case-id=" << replay_case_id;
    break;
  }
  EXPECT_EQ(context.device()->GetDeviceRemovedReason(), S_OK);
}

class D3D11ShaderAtomicMatrixSpec : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  D3D11TestContext context_;
};

TEST_F(D3D11ShaderAtomicMatrixSpec,
       Executes4096StructuredUavAtomicCasesInOneDispatch) {
  std::vector<std::uint32_t> expected(kAtomicCaseCount);
  std::vector<std::uint32_t> initial(kAtomicCaseCount);
  std::vector<std::uint32_t> poison(kAtomicCaseCount);
  std::vector<bool> selected(kAtomicCaseCount, false);
  std::vector<std::uint32_t> selected_cases;
  selected_cases.reserve(kAtomicCaseCount);
  for (std::uint32_t logical = 0; logical < kAtomicCaseCount; ++logical) {
    expected[logical] = EvaluateAtomicCase(logical);
    poison[logical] = expected[logical] ^ 0xc33ca55au;
    if (dxmt::test::LogicalCaseSelected(kAtomicCases.family(), logical)) {
      selected[logical] = true;
      selected_cases.push_back(logical);
      initial[logical] = AtomicInitial(logical);
    } else {
      initial[logical] = poison[logical];
    }
  }
  ASSERT_FALSE(selected_cases.empty());

  const auto shader = CompileShader(R"(
    Buffer<uint> case_indices : register(t0);
    RWStructuredBuffer<uint> target : register(u0);

    cbuffer Parameters : register(b0) {
      uint selected_count;
      uint3 padding;
    };

    uint atomic_value(uint logical, uint lane) {
      uint value = logical * 0x9e3779b9u + lane * 0x85ebca6bu +
                   0x243f6a88u;
      value ^= value >> 16u;
      value *= 0x7feb352du;
      value ^= value >> 15u;
      return value;
    }

    uint atomic_operand(uint logical, uint lane, uint selector) {
      switch (selector) {
      case 0:
        return ((logical >> 3u) & 15u) + 1u;
      case 1:
        return ~(1u << ((logical + lane * 5u) & 31u));
      case 2:
        return 1u << ((logical * 3u + lane * 7u) & 31u);
      case 3:
      case 4:
      case 5:
        return atomic_value(logical, lane);
      case 6:
        return atomic_value(logical, 17u);
      default:
        return atomic_value(logical, 21u);
      }
    }

    [numthreads(8, 1, 1)]
    void main(uint3 group : SV_GroupID, uint3 lane : SV_GroupThreadID) {
      if (group.x >= selected_count)
        return;
      uint logical = case_indices[group.x];
      uint selector = logical & 7u;
      uint operand = atomic_operand(logical, lane.x, selector);
      uint original;
      switch (selector) {
      case 0:
        InterlockedAdd(target[logical], operand, original);
        break;
      case 1:
        InterlockedAnd(target[logical], operand, original);
        break;
      case 2:
        InterlockedOr(target[logical], operand, original);
        break;
      case 3:
        InterlockedXor(target[logical], operand, original);
        break;
      case 4:
        InterlockedMin(target[logical], operand, original);
        break;
      case 5:
        InterlockedMax(target[logical], operand, original);
        break;
      case 6:
        InterlockedExchange(target[logical], operand, original);
        break;
      default:
        InterlockedCompareExchange(target[logical], atomic_value(logical, 19u),
                                   operand, original);
        break;
      }
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
  ASSERT_EQ(CreateTypedSrv(context_.device(), selected_buffer.get(),
                           static_cast<UINT>(selected_cases.size()),
                           selected_srv.put()),
            S_OK);

  ComPtr<ID3D11Buffer> target_buffer;
  ASSERT_EQ(
      CreateStructuredBuffer(context_.device(), initial, target_buffer.put()),
      S_OK);
  ComPtr<ID3D11UnorderedAccessView> target_uav;
  ASSERT_EQ(CreateStructuredUav(context_.device(), target_buffer.get(),
                                kAtomicCaseCount, target_uav.put()),
            S_OK);

  const std::vector<std::uint32_t> parameters = {
      static_cast<std::uint32_t>(selected_cases.size()), 0, 0, 0};
  ComPtr<ID3D11Buffer> constant_buffer;
  ASSERT_EQ(CreateTypedBuffer(context_.device(), parameters,
                              D3D11_BIND_CONSTANT_BUFFER, D3D11_USAGE_IMMUTABLE,
                              constant_buffer.put()),
            S_OK);

  ID3D11ShaderResourceView *srv = selected_srv.get();
  ID3D11UnorderedAccessView *uav = target_uav.get();
  ID3D11Buffer *constant = constant_buffer.get();
  context_.context()->CSSetShader(compute_shader.get(), nullptr, 0);
  context_.context()->CSSetShaderResources(0, 1, &srv);
  context_.context()->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
  context_.context()->CSSetConstantBuffers(0, 1, &constant);
  context_.context()->Dispatch(static_cast<UINT>(selected_cases.size()), 1, 1);

  ID3D11ShaderResourceView *null_srv = nullptr;
  ID3D11UnorderedAccessView *null_uav = nullptr;
  ID3D11Buffer *null_buffer = nullptr;
  context_.context()->CSSetShaderResources(0, 1, &null_srv);
  context_.context()->CSSetUnorderedAccessViews(0, 1, &null_uav, nullptr);
  context_.context()->CSSetConstantBuffers(0, 1, &null_buffer);
  context_.context()->CSSetShader(nullptr, nullptr, 0);

  D3D11_BUFFER_DESC staging_desc = {};
  staging_desc.ByteWidth = kAtomicCaseCount * sizeof(std::uint32_t);
  staging_desc.Usage = D3D11_USAGE_STAGING;
  staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Buffer> staging;
  ASSERT_EQ(
      context_.device()->CreateBuffer(&staging_desc, nullptr, staging.put()),
      S_OK);
  context_.context()->CopyResource(staging.get(), target_buffer.get());
  context_.context()->Flush();

  D3D11_MAPPED_SUBRESOURCE mapped = {};
  ASSERT_EQ(
      context_.context()->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped),
      S_OK);
  const auto *actual = static_cast<const std::uint32_t *>(mapped.pData);
  RecordProperty("logical_cases_executed", selected_cases.size());
  RecordProperty("logical_case_prefix", kAtomicCases.family().case_id_prefix);
  for (std::uint32_t logical = 0; logical < kAtomicCaseCount; ++logical) {
    const std::uint32_t desired =
        selected[logical] ? expected[logical] : poison[logical];
    if (actual[logical] == desired)
      continue;

    const std::uint32_t selector = AtomicSelector(logical);
    const auto case_id =
        dxmt::test::LogicalCaseId(kAtomicCases.family(), logical);
    const auto replay_case_id =
        selected[logical] ? case_id
                          : dxmt::test::LogicalCaseId(kAtomicCases.family(),
                                                      selected_cases.front());
    ADD_FAILURE() << "LogicalCaseId: " << case_id << '\n'
                  << "Class: "
                  << dxmt::test::TestClassName(
                         kAtomicCases.family().traits.test_class)
                  << '\n'
                  << "Requirements: feature_level=11_0 shader_model=5_0 "
                     "queue=Immediate capability=ComputeShader,StructuredUAV,"
                     "AtomicOperations\n"
                  << "ExecutionPath: "
                  << dxmt::test::ExecutionPathName(
                         kAtomicCases.family().traits.execution_path)
                  << '\n'
                  << "Parameters: logical=" << logical
                  << " selected=" << (selected[logical] ? "true" : "false")
                  << " operation=" << kAtomicOperationNames[selector]
                  << " compare_success="
                  << (CompareExchangeSucceeds(logical) ? "true" : "false")
                  << " initial=0x" << std::hex << AtomicInitial(logical)
                  << " first_operand=0x" << AtomicOperand(logical, 0u)
                  << " last_operand=0x"
                  << AtomicOperand(logical, kAtomicThreadCount - 1u) << std::dec
                  << '\n'
                  << "GpuCaseResult: status=" << (selected[logical] ? 1u : 2u)
                  << " first_mismatch_index=" << logical << " expected=0x"
                  << std::hex << desired << " actual=0x" << actual[logical]
                  << std::dec << '\n'
                  << "Replay: --dxmt-case-id=" << replay_case_id;
    break;
  }
  context_.context()->Unmap(staging.get(), 0);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11ShaderAtomicMatrixSpec,
       Executes4096TypedUavAtomicCasesInOneDispatch) {
  RunTypedAtomicMatrix(context_, kTypedAtomicCases);
}

} // namespace
