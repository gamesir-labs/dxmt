#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <vector>

// Public D3D11 raw-UAV atomic coverage using precompiled SM5 DXBC from Wine's
// D3D11 conformance tests. This keeps the GPU behavior executable when the
// local HLSL frontend lacks RWByteAddressBuffer.Interlocked* methods.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

// Executes And, CompareExchange, Add, Or, signed/unsigned Max and Min, and Xor
// on u0, then stores each operation's original value into the same slot of u1.
constexpr DWORD kRawAtomicShader[] = {
    0x43425844, 0x859a96e3, 0x1a35e463, 0x1e89ce58, 0x5cfe430a, 0x00000001,
    0x0000026c, 0x00000003, 0x0000002c, 0x0000003c, 0x0000004c, 0x4e475349,
    0x00000008, 0x00000000, 0x00000008, 0x4e47534f, 0x00000008, 0x00000000,
    0x00000008, 0x58454853, 0x00000218, 0x00050050, 0x00000086, 0x0100086a,
    0x04000059, 0x00208e46, 0x00000000, 0x00000002, 0x0300009d, 0x0011e000,
    0x00000000, 0x0300009d, 0x0011e000, 0x00000001, 0x02000068, 0x00000001,
    0x0400009b, 0x00000001, 0x00000001, 0x00000001, 0x0a0000b5, 0x00100012,
    0x00000000, 0x0011e000, 0x00000000, 0x00004001, 0x00000000, 0x0020800a,
    0x00000000, 0x00000000, 0x0d0000b9, 0x00100022, 0x00000000, 0x0011e000,
    0x00000000, 0x00004001, 0x00000004, 0x0020801a, 0x00000000, 0x00000000,
    0x0020800a, 0x00000000, 0x00000000, 0x0a0000b4, 0x00100042, 0x00000000,
    0x0011e000, 0x00000000, 0x00004001, 0x00000008, 0x0020800a, 0x00000000,
    0x00000000, 0x0a0000b6, 0x00100082, 0x00000000, 0x0011e000, 0x00000000,
    0x00004001, 0x0000000c, 0x0020800a, 0x00000000, 0x00000000, 0x070000a6,
    0x0011e0f2, 0x00000001, 0x00004001, 0x00000000, 0x00100e46, 0x00000000,
    0x0a0000ba, 0x00100012, 0x00000000, 0x0011e000, 0x00000000, 0x00004001,
    0x00000010, 0x0020800a, 0x00000000, 0x00000001, 0x0a0000bb, 0x00100022,
    0x00000000, 0x0011e000, 0x00000000, 0x00004001, 0x00000014, 0x0020800a,
    0x00000000, 0x00000001, 0x0a0000bc, 0x00100042, 0x00000000, 0x0011e000,
    0x00000000, 0x00004001, 0x00000018, 0x0020800a, 0x00000000, 0x00000000,
    0x0a0000bd, 0x00100082, 0x00000000, 0x0011e000, 0x00000000, 0x00004001,
    0x0000001c, 0x0020800a, 0x00000000, 0x00000000, 0x070000a6, 0x0011e0f2,
    0x00000001, 0x00004001, 0x00000010, 0x00100e46, 0x00000000, 0x0a0000b7,
    0x00100012, 0x00000000, 0x0011e000, 0x00000000, 0x00004001, 0x00000020,
    0x0020800a, 0x00000000, 0x00000000, 0x070000a6, 0x0011e012, 0x00000001,
    0x00004001, 0x00000020, 0x0010000a, 0x00000000, 0x0100003e,
};

// Thirty-two threads per group reduce group-shared values, then all exchange
// the same sum into u0[group]. The returned prior value is stored in u1[group].
constexpr DWORD kRawExchangeShader[] = {
    0x43425844, 0x9d906c94, 0x81f5ad92, 0x11e860b2, 0x3623c824, 0x00000001,
    0x000002c0, 0x00000003, 0x0000002c, 0x0000003c, 0x0000004c, 0x4e475349,
    0x00000008, 0x00000000, 0x00000008, 0x4e47534f, 0x00000008, 0x00000000,
    0x00000008, 0x58454853, 0x0000026c, 0x00050050, 0x0000009b, 0x0100086a,
    0x0300009d, 0x0011e000, 0x00000000, 0x0300009d, 0x0011e000, 0x00000001,
    0x0200005f, 0x00024000, 0x0200005f, 0x00021012, 0x02000068, 0x00000002,
    0x050000a0, 0x0011f000, 0x00000000, 0x00000004, 0x00000020, 0x0400009b,
    0x00000020, 0x00000001, 0x00000001, 0x0200001f, 0x0002400a, 0x06000029,
    0x00100012, 0x00000000, 0x0002100a, 0x00004001, 0x00000001, 0x05000036,
    0x00100022, 0x00000000, 0x00004001, 0x00000000, 0x01000030, 0x07000050,
    0x00100042, 0x00000000, 0x0010001a, 0x00000000, 0x00004001, 0x00000020,
    0x03040003, 0x0010002a, 0x00000000, 0x090000a8, 0x0011f012, 0x00000000,
    0x0010001a, 0x00000000, 0x00004001, 0x00000000, 0x0010000a, 0x00000000,
    0x0700001e, 0x00100022, 0x00000000, 0x0010001a, 0x00000000, 0x00004001,
    0x00000001, 0x01000016, 0x01000015, 0x010018be, 0x04000036, 0x00100012,
    0x00000000, 0x0002400a, 0x05000036, 0x00100022, 0x00000000, 0x00004001,
    0x00000000, 0x070000ad, 0x0011f000, 0x00000000, 0x00100046, 0x00000000,
    0x00004001, 0x00000001, 0x010018be, 0x08000036, 0x00100032, 0x00000000,
    0x00004002, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x01000030,
    0x07000050, 0x00100042, 0x00000000, 0x0010001a, 0x00000000, 0x00004001,
    0x00000020, 0x03040003, 0x0010002a, 0x00000000, 0x0700001e, 0x00100022,
    0x00000001, 0x0010001a, 0x00000000, 0x00004001, 0x00000001, 0x090000a7,
    0x00100042, 0x00000000, 0x0010001a, 0x00000000, 0x00004001, 0x00000000,
    0x0011f006, 0x00000000, 0x0700001e, 0x00100012, 0x00000001, 0x0010000a,
    0x00000000, 0x0010002a, 0x00000000, 0x05000036, 0x00100032, 0x00000000,
    0x00100046, 0x00000001, 0x01000016, 0x06000029, 0x00100022, 0x00000000,
    0x0002100a, 0x00004001, 0x00000002, 0x090000b8, 0x00100012, 0x00000001,
    0x0011e000, 0x00000000, 0x0010001a, 0x00000000, 0x0010000a, 0x00000000,
    0x070000a6, 0x0011e012, 0x00000001, 0x0010001a, 0x00000000, 0x0010000a,
    0x00000001, 0x0100003e,
};

struct AtomicConstants {
  std::array<UINT, 4> unsigned_values;
  std::array<INT, 4> signed_values;
};
static_assert(sizeof(AtomicConstants) == 32);

HRESULT CreateRawBufferAndUav(ID3D11Device *device,
                              const std::vector<UINT> &initial,
                              ID3D11Buffer **buffer,
                              ID3D11UnorderedAccessView **uav) {
  D3D11_BUFFER_DESC buffer_desc = {};
  buffer_desc.ByteWidth = static_cast<UINT>(initial.size() * sizeof(UINT));
  buffer_desc.Usage = D3D11_USAGE_DEFAULT;
  buffer_desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
  buffer_desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
  D3D11_SUBRESOURCE_DATA initial_data = {};
  initial_data.pSysMem = initial.data();
  HRESULT hr = device->CreateBuffer(&buffer_desc, &initial_data, buffer);
  if (FAILED(hr))
    return hr;

  D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
  uav_desc.Format = DXGI_FORMAT_R32_TYPELESS;
  uav_desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
  uav_desc.Buffer.NumElements = static_cast<UINT>(initial.size());
  uav_desc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
  return device->CreateUnorderedAccessView(*buffer, &uav_desc, uav);
}

HRESULT ReadBuffer(ID3D11Device *device, ID3D11DeviceContext *context,
                   ID3D11Buffer *source, std::vector<UINT> *values) {
  D3D11_BUFFER_DESC source_desc = {};
  source->GetDesc(&source_desc);
  D3D11_BUFFER_DESC staging_desc = {};
  staging_desc.ByteWidth = source_desc.ByteWidth;
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
  values->resize(source_desc.ByteWidth / sizeof(UINT));
  std::memcpy(values->data(), mapped.pData, source_desc.ByteWidth);
  context->Unmap(staging.get(), 0);
  return S_OK;
}

class D3D11RawUavAtomicSpec : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  D3D11TestContext context_;
};

TEST_F(D3D11RawUavAtomicSpec,
       ExecutesIntegerOperationsAndReturnsEveryOriginalValue) {
  struct TestCase {
    AtomicConstants constants;
    std::array<UINT, 9> input;
    std::array<UINT, 9> expected;
  };
  constexpr std::array<TestCase, 2> kCases = {
      TestCase{AtomicConstants{{1u, 0u, 0u, 0u}, {-1, 0, 0, 0}},
               std::array<UINT, 9>{0xffffu, 0u, 1u, 0u, 0u, 0u, 0u, 0u, 0xffu},
               std::array<UINT, 9>{1u, 1u, 2u, 1u, 0u, ~0u, 1u, 0u, 0xfeu}},
      TestCase{
          AtomicConstants{{~0u, ~0u, 0u, 0u}, {0, 0, 0, 0}},
          std::array<UINT, 9>{0xffffu, 0xfu, 1u, 0u, 0u, 0u, 0u, 9u, ~0u},
          std::array<UINT, 9>{0xffffu, 0xfu, 0u, ~0u, 0u, 0u, ~0u, 9u, 0u}},
  };

  std::vector<UINT> initial(9, 0u);
  ComPtr<ID3D11Buffer> target;
  ComPtr<ID3D11UnorderedAccessView> target_uav;
  ASSERT_EQ(CreateRawBufferAndUav(context_.device(), initial, target.put(),
                                  target_uav.put()),
            S_OK);
  ComPtr<ID3D11Buffer> originals;
  ComPtr<ID3D11UnorderedAccessView> originals_uav;
  ASSERT_EQ(CreateRawBufferAndUav(context_.device(), initial, originals.put(),
                                  originals_uav.put()),
            S_OK);

  D3D11_BUFFER_DESC constant_desc = {};
  constant_desc.ByteWidth = sizeof(AtomicConstants);
  constant_desc.Usage = D3D11_USAGE_DEFAULT;
  constant_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  ComPtr<ID3D11Buffer> constant_buffer;
  ASSERT_EQ(context_.device()->CreateBuffer(&constant_desc, nullptr,
                                            constant_buffer.put()),
            S_OK);
  ComPtr<ID3D11ComputeShader> shader;
  ASSERT_EQ(context_.device()->CreateComputeShader(kRawAtomicShader,
                                                   sizeof(kRawAtomicShader),
                                                   nullptr, shader.put()),
            S_OK);

  ID3D11UnorderedAccessView *uavs[] = {target_uav.get(), originals_uav.get()};
  ID3D11Buffer *constants = constant_buffer.get();
  context_.context()->CSSetShader(shader.get(), nullptr, 0);
  context_.context()->CSSetConstantBuffers(0, 1, &constants);
  for (std::size_t case_index = 0; case_index < kCases.size(); ++case_index) {
    const auto &test = kCases[case_index];
    std::vector<UINT> input(test.input.begin(), test.input.end());
    std::vector<UINT> poison(9, 0xc33ca55au);
    context_.context()->UpdateSubresource(target.get(), 0, nullptr,
                                          input.data(), 0, 0);
    context_.context()->UpdateSubresource(originals.get(), 0, nullptr,
                                          poison.data(), 0, 0);
    context_.context()->UpdateSubresource(constant_buffer.get(), 0, nullptr,
                                          &test.constants, 0, 0);
    context_.context()->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);
    context_.context()->Dispatch(1, 1, 1);
    ID3D11UnorderedAccessView *null_uavs[] = {nullptr, nullptr};
    context_.context()->CSSetUnorderedAccessViews(0, 2, null_uavs, nullptr);

    std::vector<UINT> actual;
    ASSERT_EQ(ReadBuffer(context_.device(), context_.context(), target.get(),
                         &actual),
              S_OK);
    EXPECT_EQ(actual,
              std::vector<UINT>(test.expected.begin(), test.expected.end()))
        << "case " << case_index;
    std::vector<UINT> actual_originals;
    ASSERT_EQ(ReadBuffer(context_.device(), context_.context(), originals.get(),
                         &actual_originals),
              S_OK);
    EXPECT_EQ(actual_originals, input) << "case " << case_index;
  }

  ID3D11Buffer *null_buffer = nullptr;
  context_.context()->CSSetConstantBuffers(0, 1, &null_buffer);
  context_.context()->CSSetShader(nullptr, nullptr, 0);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11RawUavAtomicSpec,
       ExchangeUsesDynamicByteAddressesAcrossThreadGroups) {
  constexpr UINT kGroupCount = 32;
  std::vector<UINT> initial(kGroupCount);
  std::vector<UINT> output_poison(kGroupCount, 0xc33ca55au);
  for (UINT group = 0; group < kGroupCount; ++group)
    initial[group] = 0x80000000u | group;

  ComPtr<ID3D11Buffer> target;
  ComPtr<ID3D11UnorderedAccessView> target_uav;
  ASSERT_EQ(CreateRawBufferAndUav(context_.device(), initial, target.put(),
                                  target_uav.put()),
            S_OK);
  ComPtr<ID3D11Buffer> originals;
  ComPtr<ID3D11UnorderedAccessView> originals_uav;
  ASSERT_EQ(CreateRawBufferAndUav(context_.device(), output_poison,
                                  originals.put(), originals_uav.put()),
            S_OK);
  ComPtr<ID3D11ComputeShader> shader;
  ASSERT_EQ(context_.device()->CreateComputeShader(kRawExchangeShader,
                                                   sizeof(kRawExchangeShader),
                                                   nullptr, shader.put()),
            S_OK);

  ID3D11UnorderedAccessView *uavs[] = {target_uav.get(), originals_uav.get()};
  context_.context()->CSSetShader(shader.get(), nullptr, 0);
  context_.context()->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);
  context_.context()->Dispatch(kGroupCount, 1, 1);
  ID3D11UnorderedAccessView *null_uavs[] = {nullptr, nullptr};
  context_.context()->CSSetUnorderedAccessViews(0, 2, null_uavs, nullptr);
  context_.context()->CSSetShader(nullptr, nullptr, 0);

  std::vector<UINT> actual;
  ASSERT_EQ(
      ReadBuffer(context_.device(), context_.context(), target.get(), &actual),
      S_OK);
  std::vector<UINT> actual_originals;
  ASSERT_EQ(ReadBuffer(context_.device(), context_.context(), originals.get(),
                       &actual_originals),
            S_OK);
  for (UINT group = 0; group < kGroupCount; ++group) {
    const UINT expected = 64u * group + 32u;
    EXPECT_EQ(actual[group], expected) << "group " << group;
    // Exactly one lane observes the initial value and the other lanes observe
    // the common exchanged value; their stores to u1[group] intentionally race.
    EXPECT_TRUE(actual_originals[group] == initial[group] ||
                actual_originals[group] == expected)
        << "group " << group << " original=0x" << std::hex
        << actual_originals[group] << " initial=0x" << initial[group]
        << " exchanged=0x" << expected;
  }
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
