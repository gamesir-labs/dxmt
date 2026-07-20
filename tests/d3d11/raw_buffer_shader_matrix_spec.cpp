#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d11_test_context.hpp"

#include <cstdint>
#include <iomanip>
#include <vector>

// Batched public-D3D11 raw-buffer shader operations. Logical cases select
// scalar/vector ByteAddressBuffer loads at varied aligned byte offsets and
// store one result into a disjoint RWByteAddressBuffer slot.

namespace {

using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

constexpr std::uint32_t kRawCaseCount = 4096;
constexpr std::uint32_t kSourceWordCount = 8192;

const dxmt::test::LogicalCaseFamilyRegistration kRawCases(
    "D3D11RawBufferShaderMatrixSpec."
    "Executes4096RawLoadStoreCasesInOneDispatch",
    "D3D11.Shader.Buffer.RawLoadStore.", kRawCaseCount, 4,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "5_0", "Immediate", "ComputeShader,RawSRV,RawUAV"},
     dxmt::test::kGpuBatchTestCost,
     "an immutable raw source buffer, selected logical IDs in a typed SRV, "
     "and a poison-initialized raw UAV",
     "dispatch one invocation per selected ID, issue ByteAddressBuffer "
     "Load/Load2/Load3/Load4 at a varied aligned byte offset, combine the "
     "loaded words, and Store to the logical case slot",
     "every selected slot matches the bit-exact CPU evaluator and every "
     "unselected slot remains poison",
     "logical ID, selection state, load width, source word/byte offset, "
     "loaded words, expected/actual, and exact replay argument"});

const dxmt::test::TestCostRegistration
    kRawCost("D3D11RawBufferShaderMatrixSpec."
             "Executes4096RawLoadStoreCasesInOneDispatch",
             dxmt::test::kGpuBatchTestCost);

std::uint32_t RotateLeft(std::uint32_t value, std::uint32_t shift) {
  return (value << shift) | (value >> (32u - shift));
}

std::uint32_t SourceWord(std::uint32_t index) {
  return index * 0x9e3779b9u + 0x7f4a7c15u;
}

std::uint32_t SourceOffset(std::uint32_t logical) {
  return (logical * 37u + (logical >> 5u)) % (kSourceWordCount - 4u);
}

std::uint32_t EvaluateRawCase(std::uint32_t logical) {
  const std::uint32_t offset = SourceOffset(logical);
  const std::uint32_t x = SourceWord(offset);
  const std::uint32_t y = SourceWord(offset + 1u);
  const std::uint32_t z = SourceWord(offset + 2u);
  const std::uint32_t w = SourceWord(offset + 3u);
  switch ((logical >> 4u) & 3u) {
  case 0:
    return x ^ logical;
  case 1:
    return x + RotateLeft(y, 7u) + logical;
  case 2:
    return (x & y) ^ (z + logical);
  default:
    return (x + RotateLeft(y, 3u)) ^ (RotateLeft(z, 11u) + w);
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

HRESULT CreateTypedSrv(ID3D11Device *device, ID3D11Buffer *buffer,
                       UINT element_count, ID3D11ShaderResourceView **view) {
  D3D11_SHADER_RESOURCE_VIEW_DESC desc = {};
  desc.Format = DXGI_FORMAT_R32_UINT;
  desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
  desc.Buffer.NumElements = element_count;
  return device->CreateShaderResourceView(buffer, &desc, view);
}

class D3D11RawBufferShaderMatrixSpec : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  D3D11TestContext context_;
};

TEST_F(D3D11RawBufferShaderMatrixSpec,
       Executes4096RawLoadStoreCasesInOneDispatch) {
  std::vector<std::uint32_t> source(kSourceWordCount);
  for (std::uint32_t index = 0; index < kSourceWordCount; ++index)
    source[index] = SourceWord(index);

  std::vector<std::uint32_t> expected(kRawCaseCount);
  std::vector<std::uint32_t> poison(kRawCaseCount);
  std::vector<bool> selected(kRawCaseCount, false);
  std::vector<std::uint32_t> selected_cases;
  selected_cases.reserve(kRawCaseCount);
  for (std::uint32_t logical = 0; logical < kRawCaseCount; ++logical) {
    expected[logical] = EvaluateRawCase(logical);
    poison[logical] = expected[logical] ^ 0xc33ca55au;
    if (dxmt::test::LogicalCaseSelected(kRawCases.family(), logical)) {
      selected[logical] = true;
      selected_cases.push_back(logical);
    }
  }
  ASSERT_FALSE(selected_cases.empty());

  const auto shader = CompileShader(R"(
    Buffer<uint> case_indices : register(t0);
    ByteAddressBuffer source : register(t1);
    RWByteAddressBuffer output : register(u0);

    cbuffer Parameters : register(b0) {
      uint selected_count;
      uint3 padding;
    };

    uint rotate_left(uint value, uint shift) {
      return (value << shift) | (value >> (32u - shift));
    }

    uint source_offset(uint logical) {
      return (logical * 37u + (logical >> 5u)) % (8192u - 4u);
    }

    uint evaluate(uint logical) {
      uint byte_offset = source_offset(logical) * 4u;
      switch ((logical >> 4u) & 3u) {
      case 0: {
        uint x = source.Load(byte_offset);
        return x ^ logical;
      }
      case 1: {
        uint2 values = source.Load2(byte_offset);
        return values.x + rotate_left(values.y, 7u) + logical;
      }
      case 2: {
        uint3 values = source.Load3(byte_offset);
        return (values.x & values.y) ^ (values.z + logical);
      }
      default: {
        uint4 values = source.Load4(byte_offset);
        return (values.x + rotate_left(values.y, 3u)) ^
               (rotate_left(values.z, 11u) + values.w);
      }
      }
    }

    [numthreads(64, 1, 1)]
    void main(uint3 id : SV_DispatchThreadID) {
      if (id.x >= selected_count)
        return;
      uint logical = case_indices[id.x];
      output.Store(logical * 4u, evaluate(logical));
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

  D3D11_BUFFER_DESC raw_source_desc = {};
  raw_source_desc.ByteWidth = kSourceWordCount * sizeof(std::uint32_t);
  raw_source_desc.Usage = D3D11_USAGE_IMMUTABLE;
  raw_source_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  raw_source_desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
  D3D11_SUBRESOURCE_DATA source_initial = {};
  source_initial.pSysMem = source.data();
  ComPtr<ID3D11Buffer> source_buffer;
  ASSERT_EQ(context_.device()->CreateBuffer(&raw_source_desc, &source_initial,
                                            source_buffer.put()),
            S_OK);

  D3D11_SHADER_RESOURCE_VIEW_DESC raw_srv_desc = {};
  raw_srv_desc.Format = DXGI_FORMAT_R32_TYPELESS;
  raw_srv_desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
  raw_srv_desc.BufferEx.NumElements = kSourceWordCount;
  raw_srv_desc.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;
  ComPtr<ID3D11ShaderResourceView> raw_srv;
  ASSERT_EQ(context_.device()->CreateShaderResourceView(
                source_buffer.get(), &raw_srv_desc, raw_srv.put()),
            S_OK);

  D3D11_BUFFER_DESC raw_output_desc = {};
  raw_output_desc.ByteWidth = kRawCaseCount * sizeof(std::uint32_t);
  raw_output_desc.Usage = D3D11_USAGE_DEFAULT;
  raw_output_desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
  raw_output_desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
  D3D11_SUBRESOURCE_DATA output_initial = {};
  output_initial.pSysMem = poison.data();
  ComPtr<ID3D11Buffer> output_buffer;
  ASSERT_EQ(context_.device()->CreateBuffer(&raw_output_desc, &output_initial,
                                            output_buffer.put()),
            S_OK);

  D3D11_UNORDERED_ACCESS_VIEW_DESC raw_uav_desc = {};
  raw_uav_desc.Format = DXGI_FORMAT_R32_TYPELESS;
  raw_uav_desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
  raw_uav_desc.Buffer.NumElements = kRawCaseCount;
  raw_uav_desc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
  ComPtr<ID3D11UnorderedAccessView> raw_uav;
  ASSERT_EQ(context_.device()->CreateUnorderedAccessView(
                output_buffer.get(), &raw_uav_desc, raw_uav.put()),
            S_OK);

  const std::vector<std::uint32_t> parameters = {
      static_cast<std::uint32_t>(selected_cases.size()), 0, 0, 0};
  ComPtr<ID3D11Buffer> constant_buffer;
  ASSERT_EQ(CreateTypedBuffer(context_.device(), parameters,
                              D3D11_BIND_CONSTANT_BUFFER, D3D11_USAGE_IMMUTABLE,
                              constant_buffer.put()),
            S_OK);

  ID3D11ShaderResourceView *srvs[] = {selected_srv.get(), raw_srv.get()};
  ID3D11UnorderedAccessView *uavs[] = {raw_uav.get()};
  ID3D11Buffer *constant_buffers[] = {constant_buffer.get()};
  context_.context()->CSSetShader(compute_shader.get(), nullptr, 0);
  context_.context()->CSSetShaderResources(0, 2, srvs);
  context_.context()->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
  context_.context()->CSSetConstantBuffers(0, 1, constant_buffers);
  const UINT selected_count = static_cast<UINT>(selected_cases.size());
  context_.context()->Dispatch((selected_count + 63u) / 64u, 1, 1);

  ID3D11ShaderResourceView *null_srvs[] = {nullptr, nullptr};
  ID3D11UnorderedAccessView *null_uav = nullptr;
  ID3D11Buffer *null_buffer = nullptr;
  context_.context()->CSSetShaderResources(0, 2, null_srvs);
  context_.context()->CSSetUnorderedAccessViews(0, 1, &null_uav, nullptr);
  context_.context()->CSSetConstantBuffers(0, 1, &null_buffer);
  context_.context()->CSSetShader(nullptr, nullptr, 0);

  D3D11_BUFFER_DESC staging_desc = {};
  staging_desc.ByteWidth = raw_output_desc.ByteWidth;
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
  RecordProperty("logical_case_prefix", kRawCases.family().case_id_prefix);
  for (std::uint32_t logical = 0; logical < kRawCaseCount; ++logical) {
    const std::uint32_t desired =
        selected[logical] ? expected[logical] : poison[logical];
    if (actual[logical] == desired)
      continue;

    const std::uint32_t source_offset = SourceOffset(logical);
    const auto case_id = dxmt::test::LogicalCaseId(kRawCases.family(), logical);
    const auto replay_case_id =
        selected[logical]
            ? case_id
            : dxmt::test::LogicalCaseId(kRawCases.family(),
                                        selected_cases.front());
    ADD_FAILURE() << "LogicalCaseId: " << case_id << '\n'
                  << "Class: "
                  << dxmt::test::TestClassName(
                         kRawCases.family().traits.test_class)
                  << '\n'
                  << "Requirements: feature_level=11_0 shader_model=5_0 "
                     "queue=Immediate capability=ComputeShader,RawSRV,RawUAV\n"
                  << "ExecutionPath: "
                  << dxmt::test::ExecutionPathName(
                         kRawCases.family().traits.execution_path)
                  << '\n'
                  << "Parameters: logical=" << logical
                  << " selected=" << (selected[logical] ? "true" : "false")
                  << " load_width=" << (((logical >> 4u) & 3u) + 1u)
                  << " source_word_offset=" << source_offset
                  << " source_byte_offset=" << source_offset * 4u
                  << " words=0x" << std::hex << SourceWord(source_offset)
                  << ",0x" << SourceWord(source_offset + 1u) << ",0x"
                  << SourceWord(source_offset + 2u) << ",0x"
                  << SourceWord(source_offset + 3u) << std::dec << '\n'
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
