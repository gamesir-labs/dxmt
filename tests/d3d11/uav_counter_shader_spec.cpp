#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

// Public D3D11 append/consume coverage using precompiled SM5 DXBC. Wine's
// HLSL frontend does not recognize AppendStructuredBuffer,
// ConsumeStructuredBuffer, or IncrementCounter, so source compilation would
// skip the behavior that DXMT needs to execute. The bytecode below comes from
// Wine's public D3D conformance tests and is submitted only through
// ID3D11Device::CreateComputeShader.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

// One thread appends {4,2,1}, {4,1,1}, and {3,1,1} in order.
constexpr DWORD kAppendShader[] = {
    0x43425844, 0x954de75a, 0x8bb1b78b, 0x84ded464, 0x9d9532b7, 0x00000001,
    0x00000158, 0x00000003, 0x0000002c, 0x0000003c, 0x0000004c, 0x4e475349,
    0x00000008, 0x00000000, 0x00000008, 0x4e47534f, 0x00000008, 0x00000000,
    0x00000008, 0x58454853, 0x00000104, 0x00050050, 0x00000041, 0x0100086a,
    0x0400009e, 0x0011e000, 0x00000000, 0x0000000c, 0x02000068, 0x00000001,
    0x0400009b, 0x00000001, 0x00000001, 0x00000001, 0x050000b2, 0x00100012,
    0x00000000, 0x0011e000, 0x00000000, 0x0c0000a8, 0x0011e072, 0x00000000,
    0x0010000a, 0x00000000, 0x00004001, 0x00000000, 0x00004002, 0x00000004,
    0x00000002, 0x00000001, 0x00000000, 0x050000b2, 0x00100012, 0x00000000,
    0x0011e000, 0x00000000, 0x0c0000a8, 0x0011e072, 0x00000000, 0x0010000a,
    0x00000000, 0x00004001, 0x00000000, 0x00004002, 0x00000004, 0x00000001,
    0x00000001, 0x00000000, 0x050000b2, 0x00100012, 0x00000000, 0x0011e000,
    0x00000000, 0x0c0000a8, 0x0011e072, 0x00000000, 0x0010000a, 0x00000000,
    0x00004001, 0x00000000, 0x00004002, 0x00000003, 0x00000001, 0x00000001,
    0x00000000, 0x0100003e,
};

// Four threads decrement u0's hidden counter and copy the claimed u0 element
// into the corresponding u1 thread index. This is the DXBC lowering shared by
// RWStructuredBuffer.DecrementCounter and ConsumeStructuredBuffer.Consume.
constexpr DWORD kConsumeShader[] = {
    0x43425844, 0x957ef3dd, 0x9f317559, 0x09c8f12d, 0xdbfd98c8, 0x00000001,
    0x00000100, 0x00000003, 0x0000002c, 0x0000003c, 0x0000004c, 0x4e475349,
    0x00000008, 0x00000000, 0x00000008, 0x4e47534f, 0x00000008, 0x00000000,
    0x00000008, 0x58454853, 0x000000ac, 0x00050050, 0x0000002b, 0x0100086a,
    0x0480009e, 0x0011e000, 0x00000000, 0x00000004, 0x0400009e, 0x0011e000,
    0x00000001, 0x00000004, 0x02000068, 0x00000001, 0x0400009b, 0x00000004,
    0x00000001, 0x00000001, 0x050000b3, 0x00100012, 0x00000000, 0x0011e000,
    0x00000000, 0x8b0000a7, 0x80002302, 0x00199983, 0x00100022, 0x00000000,
    0x0010000a, 0x00000000, 0x00004001, 0x00000000, 0x0011e006, 0x00000000,
    0x090000a8, 0x0011e012, 0x00000001, 0x0010000a, 0x00000000, 0x00004001,
    0x00000000, 0x0010001a, 0x00000000, 0x0100003e,
};

HRESULT CreateStructuredBuffer(ID3D11Device *device, UINT element_count,
                               UINT structure_stride, const void *initial_data,
                               ID3D11Buffer **buffer) {
  D3D11_BUFFER_DESC desc = {};
  desc.ByteWidth = element_count * structure_stride;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
  desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
  desc.StructureByteStride = structure_stride;
  D3D11_SUBRESOURCE_DATA initial = {};
  initial.pSysMem = initial_data;
  return device->CreateBuffer(&desc, initial_data ? &initial : nullptr, buffer);
}

HRESULT CreateStructuredUav(ID3D11Device *device, ID3D11Buffer *buffer,
                            UINT element_count, UINT flags,
                            ID3D11UnorderedAccessView **uav) {
  D3D11_UNORDERED_ACCESS_VIEW_DESC desc = {};
  desc.Format = DXGI_FORMAT_UNKNOWN;
  desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
  desc.Buffer.NumElements = element_count;
  desc.Buffer.Flags = flags;
  return device->CreateUnorderedAccessView(buffer, &desc, uav);
}

HRESULT ReadBuffer(ID3D11Device *device, ID3D11DeviceContext *context,
                   ID3D11Buffer *source, std::vector<UINT> *values) {
  D3D11_BUFFER_DESC desc = {};
  source->GetDesc(&desc);
  D3D11_BUFFER_DESC staging_desc = {};
  staging_desc.ByteWidth = desc.ByteWidth;
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
  values->resize(desc.ByteWidth / sizeof(UINT));
  std::memcpy(values->data(), mapped.pData, desc.ByteWidth);
  context->Unmap(staging.get(), 0);
  return S_OK;
}

HRESULT ReadCounter(ID3D11Device *device, ID3D11DeviceContext *context,
                    ID3D11UnorderedAccessView *uav, UINT *value) {
  D3D11_BUFFER_DESC desc = {};
  desc.ByteWidth = sizeof(UINT);
  desc.Usage = D3D11_USAGE_STAGING;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Buffer> staging;
  HRESULT hr = device->CreateBuffer(&desc, nullptr, staging.put());
  if (FAILED(hr))
    return hr;
  context->CopyStructureCount(staging.get(), 0, uav);
  context->Flush();
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  hr = context->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped);
  if (FAILED(hr))
    return hr;
  std::memcpy(value, mapped.pData, sizeof(*value));
  context->Unmap(staging.get(), 0);
  return S_OK;
}

class D3D11UavCounterShaderSpec : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  D3D11TestContext context_;
};

TEST_F(D3D11UavCounterShaderSpec,
       AppendRespectsNonzeroInitialCountAndKeepsCounterAcrossBindings) {
  constexpr UINT kElementCount = 8;
  constexpr UINT kStructureStride = 3 * sizeof(UINT);
  constexpr UINT kPoison = 0xaaaaaaaau;
  constexpr std::array<UINT, 9> kAppendedRecords = {
      4, 2, 1, 4, 1, 1, 3, 1, 1,
  };
  std::vector<UINT> initial(kElementCount * 3u, kPoison);

  ComPtr<ID3D11Buffer> buffer;
  ASSERT_EQ(CreateStructuredBuffer(context_.device(), kElementCount,
                                   kStructureStride, initial.data(),
                                   buffer.put()),
            S_OK);
  ComPtr<ID3D11UnorderedAccessView> uav;
  ASSERT_EQ(CreateStructuredUav(context_.device(), buffer.get(), kElementCount,
                                D3D11_BUFFER_UAV_FLAG_APPEND, uav.put()),
            S_OK);
  ComPtr<ID3D11ComputeShader> shader;
  ASSERT_EQ(context_.device()->CreateComputeShader(
                kAppendShader, sizeof(kAppendShader), nullptr, shader.put()),
            S_OK);

  ID3D11UnorderedAccessView *bound_uav = uav.get();
  UINT initial_count = 1;
  context_.context()->CSSetShader(shader.get(), nullptr, 0);
  context_.context()->CSSetUnorderedAccessViews(0, 1, &bound_uav,
                                                &initial_count);
  context_.context()->Dispatch(1, 1, 1);
  ID3D11UnorderedAccessView *null_uav = nullptr;
  context_.context()->CSSetUnorderedAccessViews(0, 1, &null_uav, nullptr);

  UINT count = 0;
  ASSERT_EQ(
      ReadCounter(context_.device(), context_.context(), uav.get(), &count),
      S_OK);
  EXPECT_EQ(count, 4u);

  UINT keep_count = ~0u;
  context_.context()->CSSetUnorderedAccessViews(0, 1, &bound_uav, &keep_count);
  context_.context()->Dispatch(1, 1, 1);
  context_.context()->CSSetUnorderedAccessViews(0, 1, &null_uav, nullptr);
  context_.context()->CSSetShader(nullptr, nullptr, 0);

  ASSERT_EQ(
      ReadCounter(context_.device(), context_.context(), uav.get(), &count),
      S_OK);
  EXPECT_EQ(count, 7u);

  std::vector<UINT> actual;
  ASSERT_EQ(
      ReadBuffer(context_.device(), context_.context(), buffer.get(), &actual),
      S_OK);
  auto expected = initial;
  std::copy(kAppendedRecords.begin(), kAppendedRecords.end(),
            expected.begin() + 3u);
  std::copy(kAppendedRecords.begin(), kAppendedRecords.end(),
            expected.begin() + 12u);
  EXPECT_EQ(actual, expected);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11UavCounterShaderSpec,
       ConsumeReturnsEveryRecordAndDecrementsCounterToZero) {
  constexpr std::array<UINT, 4> kInput = {0x10203040u, 0x55667788u, 0x90abcdefu,
                                          0xfedcba09u};
  constexpr std::array<UINT, 4> kOutputPoison = {0xaaaaaaaau, 0xbbbbbbbbu,
                                                 0xccccccccu, 0xddddddddu};

  ComPtr<ID3D11Buffer> input;
  ASSERT_EQ(CreateStructuredBuffer(context_.device(), kInput.size(),
                                   sizeof(UINT), kInput.data(), input.put()),
            S_OK);
  ComPtr<ID3D11UnorderedAccessView> input_uav;
  ASSERT_EQ(CreateStructuredUav(context_.device(), input.get(), kInput.size(),
                                D3D11_BUFFER_UAV_FLAG_COUNTER, input_uav.put()),
            S_OK);
  ComPtr<ID3D11Buffer> output;
  ASSERT_EQ(CreateStructuredBuffer(context_.device(), kOutputPoison.size(),
                                   sizeof(UINT), kOutputPoison.data(),
                                   output.put()),
            S_OK);
  ComPtr<ID3D11UnorderedAccessView> output_uav;
  ASSERT_EQ(CreateStructuredUav(context_.device(), output.get(),
                                kOutputPoison.size(), 0, output_uav.put()),
            S_OK);
  ComPtr<ID3D11ComputeShader> shader;
  ASSERT_EQ(context_.device()->CreateComputeShader(
                kConsumeShader, sizeof(kConsumeShader), nullptr, shader.put()),
            S_OK);

  ID3D11UnorderedAccessView *uavs[] = {input_uav.get(), output_uav.get()};
  UINT initial_counts[] = {static_cast<UINT>(kInput.size()), ~0u};
  context_.context()->CSSetShader(shader.get(), nullptr, 0);
  context_.context()->CSSetUnorderedAccessViews(0, 2, uavs, initial_counts);
  context_.context()->Dispatch(1, 1, 1);
  ID3D11UnorderedAccessView *null_uavs[] = {nullptr, nullptr};
  context_.context()->CSSetUnorderedAccessViews(0, 2, null_uavs, nullptr);
  context_.context()->CSSetShader(nullptr, nullptr, 0);

  UINT count = ~0u;
  ASSERT_EQ(ReadCounter(context_.device(), context_.context(), input_uav.get(),
                        &count),
            S_OK);
  EXPECT_EQ(count, 0u);

  std::vector<UINT> actual_output;
  ASSERT_EQ(ReadBuffer(context_.device(), context_.context(), output.get(),
                       &actual_output),
            S_OK);
  auto sorted_output = actual_output;
  auto sorted_input = kInput;
  std::ranges::sort(sorted_output);
  std::ranges::sort(sorted_input);
  EXPECT_EQ(sorted_output,
            std::vector<UINT>(sorted_input.begin(), sorted_input.end()));

  std::vector<UINT> actual_input;
  ASSERT_EQ(ReadBuffer(context_.device(), context_.context(), input.get(),
                       &actual_input),
            S_OK);
  EXPECT_EQ(actual_input, std::vector<UINT>(kInput.begin(), kInput.end()));
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
