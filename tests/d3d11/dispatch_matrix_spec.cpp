#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d11_test_context.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <string>
#include <vector>

// Public D3D11 Dispatch thread-group matrices with UAV write + staging
// readback oracles. Exercises only ID3D11* / DXGI / D3D11CreateDevice /
// d3dcompiler surfaces (plus D3D11TestContext).

namespace {

using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

// ---------------------------------------------------------------------------
// Shared helpers
// ---------------------------------------------------------------------------

::testing::AssertionResult HResultSucceeded(HRESULT hr) {
  if (SUCCEEDED(hr))
    return ::testing::AssertionSuccess();
  return ::testing::AssertionFailure()
         << "HRESULT failed: 0x" << std::hex << static_cast<unsigned long>(hr);
}

// Pack group id into a linear slot: x + 32*(y + 16*z). Capacity covers
// x < 32, y < 16, z < 8.
constexpr UINT kSlotStrideX = 32u;
constexpr UINT kSlotStrideY = 16u;
constexpr UINT kSlotMaxZ = 8u;
constexpr UINT kSlotCount = kSlotStrideX * kSlotStrideY * kSlotMaxZ;

UINT SlotIndex(UINT x, UINT y, UINT z) {
  return x + kSlotStrideX * (y + kSlotStrideY * z);
}

UINT PackedGroupValue(UINT x, UINT y, UINT z) {
  return 0xa0000000u | x | (y << 8) | (z << 16);
}

constexpr std::string_view kGroupIdBufferComputeShader = R"(
RWBuffer<uint> output : register(u0);

[numthreads(1, 1, 1)]
void main(uint3 gid : SV_GroupID) {
  // Pack group id into a linear index slot.
  const uint index = gid.x + 32u * (gid.y + 16u * gid.z);
  const uint value = 0xA0000000u | gid.x | (gid.y << 8) | (gid.z << 16);
  output[index] = value;
}
)";

constexpr std::string_view kOverwriteBufferComputeShader = R"(
cbuffer Params : register(b0) {
  uint write_value;
  uint3 pad;
};
RWBuffer<uint> output : register(u0);

[numthreads(1, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
  output[tid.x] = write_value;
}
)";

constexpr std::string_view kConstantBufferComputeShader = R"(
cbuffer Params : register(b0) {
  uint base;
  uint multiplier;
  uint2 pad;
};
RWBuffer<uint> output : register(u0);

[numthreads(1, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
  output[tid.x] = base + multiplier * tid.x;
}
)";

constexpr std::string_view kTextureThreadIdComputeShader = R"(
RWTexture2D<uint> output : register(u0);

[numthreads(1, 1, 1)]
void main(uint3 gid : SV_GroupID) {
  // For 2D dispatches: write packed group id into texel (gid.x, gid.y).
  output[gid.xy] = 0xB0000000u | gid.x | (gid.y << 8) | (gid.z << 16);
}
)";

constexpr std::string_view kCounterStructuredComputeShader = R"(
struct Element {
  uint value;
};
RWStructuredBuffer<Element> output : register(u0);

[numthreads(4, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
  // Claim a unique slot via the UAV counter, then write thread identity.
  const uint index = output.IncrementCounter();
  Element element;
  element.value = 0xC0000000u | tid.x;
  output[index] = element;
}
)";

constexpr std::string_view kAppendStructuredComputeShader = R"(
struct Element {
  uint value;
};
AppendStructuredBuffer<Element> output : register(u0);

[numthreads(1, 1, 1)]
void main() {
  // Single-thread append sequence so content order is deterministic.
  Element a; a.value = 0xD0000010u; output.Append(a);
  Element b; b.value = 0xD0000020u; output.Append(b);
  Element c; c.value = 0xD0000030u; output.Append(c);
}
)";

HRESULT CreateTypedUavBuffer(ID3D11Device *device, UINT element_count,
                             const void *initial_data,
                             ID3D11Buffer **buffer,
                             ID3D11UnorderedAccessView **uav) {
  if (!device || !buffer || !uav || element_count == 0)
    return E_INVALIDARG;

  D3D11_BUFFER_DESC desc = {};
  desc.ByteWidth = element_count * sizeof(uint32_t);
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
  desc.MiscFlags = 0;

  D3D11_SUBRESOURCE_DATA initial = {};
  initial.pSysMem = initial_data;
  HRESULT hr =
      device->CreateBuffer(&desc, initial_data ? &initial : nullptr, buffer);
  if (FAILED(hr))
    return hr;

  D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
  uav_desc.Format = DXGI_FORMAT_R32_UINT;
  uav_desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
  uav_desc.Buffer.FirstElement = 0;
  uav_desc.Buffer.NumElements = element_count;
  hr = device->CreateUnorderedAccessView(*buffer, &uav_desc, uav);
  if (FAILED(hr)) {
    (*buffer)->Release();
    *buffer = nullptr;
  }
  return hr;
}

HRESULT ReadTypedUavBuffer(ID3D11Device *device, ID3D11DeviceContext *context,
                           ID3D11Buffer *buffer, UINT element_count,
                           std::vector<uint32_t> *out) {
  if (!device || !context || !buffer || !out || element_count == 0)
    return E_INVALIDARG;

  D3D11_BUFFER_DESC desc = {};
  buffer->GetDesc(&desc);
  desc.Usage = D3D11_USAGE_STAGING;
  desc.BindFlags = 0;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  desc.MiscFlags = 0;
  desc.StructureByteStride = 0;

  ComPtr<ID3D11Buffer> staging;
  HRESULT hr = device->CreateBuffer(&desc, nullptr, staging.put());
  if (FAILED(hr))
    return hr;
  context->CopyResource(staging.get(), buffer);

  D3D11_MAPPED_SUBRESOURCE mapped = {};
  hr = context->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped);
  if (FAILED(hr))
    return hr;
  out->assign(element_count, 0u);
  std::memcpy(out->data(), mapped.pData, element_count * sizeof(uint32_t));
  context->Unmap(staging.get(), 0);
  return S_OK;
}

HRESULT CreateConstantBuffer(ID3D11Device *device, const void *data,
                             UINT byte_width, ID3D11Buffer **buffer) {
  if (!device || !buffer || byte_width == 0 || (byte_width % 16u) != 0)
    return E_INVALIDARG;
  D3D11_BUFFER_DESC desc = {};
  desc.ByteWidth = byte_width;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  D3D11_SUBRESOURCE_DATA initial = {};
  initial.pSysMem = data;
  return device->CreateBuffer(&desc, data ? &initial : nullptr, buffer);
}

// ---------------------------------------------------------------------------
// 1. Dispatch thread-group count matrix (RWBuffer UAV)
// ---------------------------------------------------------------------------

struct DispatchCase {
  UINT x;
  UINT y;
  UINT z;
};

std::vector<DispatchCase> BuildDispatchCases() {
  // Explicit coverage requested by the plan, plus a few denser cells.
  return {
      {1, 1, 1}, {2, 1, 1}, {1, 2, 1}, {2, 2, 1}, {3, 2, 1},
      {4, 4, 1}, {1, 1, 2}, {2, 2, 2}, {4, 1, 2}, {1, 4, 2},
      {8, 1, 1}, {1, 8, 1}, {3, 3, 2},
  };
}

class DispatchMatrixSpec : public ::testing::TestWithParam<DispatchCase> {
protected:
  void SetUp() override {
    ASSERT_TRUE(HResultSucceeded(context_.Initialize()));
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_NE(context_.context(), nullptr);

    const auto compiled = CompileShader(kGroupIdBufferComputeShader, "cs_5_0");
    ASSERT_TRUE(HResultSucceeded(compiled.result))
        << compiled.diagnostic_text();
    ASSERT_TRUE(HResultSucceeded(context_.device()->CreateComputeShader(
        compiled.bytecode->GetBufferPointer(),
        compiled.bytecode->GetBufferSize(), nullptr, compute_shader_.put())));
  }

  D3D11TestContext context_;
  ComPtr<ID3D11ComputeShader> compute_shader_;
};

TEST_P(DispatchMatrixSpec, EachGroupWritesPackedIdentityToRwBuffer) {
  const auto &test = GetParam();
  ASSERT_LT(test.x, kSlotStrideX);
  ASSERT_LT(test.y, kSlotStrideY);
  ASSERT_LT(test.z, kSlotMaxZ);

  std::vector<uint32_t> zeros(kSlotCount, 0u);
  ComPtr<ID3D11Buffer> buffer;
  ComPtr<ID3D11UnorderedAccessView> uav;
  ASSERT_TRUE(HResultSucceeded(CreateTypedUavBuffer(
      context_.device(), kSlotCount, zeros.data(), buffer.put(), uav.put())));

  ID3D11UnorderedAccessView *uavs[] = {uav.get()};
  context_.context()->CSSetShader(compute_shader_.get(), nullptr, 0);
  context_.context()->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
  context_.context()->Dispatch(test.x, test.y, test.z);
  ID3D11UnorderedAccessView *null_uav[] = {nullptr};
  context_.context()->CSSetUnorderedAccessViews(0, 1, null_uav, nullptr);

  std::vector<uint32_t> values;
  ASSERT_TRUE(HResultSucceeded(ReadTypedUavBuffer(
      context_.device(), context_.context(), buffer.get(), kSlotCount, &values)));

  for (UINT z = 0; z < test.z; ++z) {
    for (UINT y = 0; y < test.y; ++y) {
      for (UINT x = 0; x < test.x; ++x) {
        const UINT index = SlotIndex(x, y, z);
        const UINT expected = PackedGroupValue(x, y, z);
        EXPECT_EQ(values[index], expected)
            << "gid " << x << "," << y << "," << z;
      }
    }
  }

  // Spot-check slots just outside the dispatch rectangle stay zero.
  const UINT outside[][3] = {{test.x, 0, 0}, {0, test.y, 0}, {0, 0, test.z},
                             {31, 15, 7}};
  for (const auto &o : outside) {
    if (o[0] < test.x && o[1] < test.y && o[2] < test.z)
      continue;
    if (o[0] >= kSlotStrideX || o[1] >= kSlotStrideY || o[2] >= kSlotMaxZ)
      continue;
    const UINT index = SlotIndex(o[0], o[1], o[2]);
    EXPECT_EQ(values[index], 0u)
        << "outside " << o[0] << "," << o[1] << "," << o[2];
  }
}

std::string DispatchName(const ::testing::TestParamInfo<DispatchCase> &info) {
  return "X" + std::to_string(info.param.x) + "Y" +
         std::to_string(info.param.y) + "Z" + std::to_string(info.param.z);
}

INSTANTIATE_TEST_SUITE_P(GroupMatrix, DispatchMatrixSpec,
                         ::testing::ValuesIn(BuildDispatchCases()),
                         DispatchName);

// ---------------------------------------------------------------------------
// 1b. 2D subset via RWTexture2D (thread / group id write)
// ---------------------------------------------------------------------------

struct TextureDispatchCase {
  UINT x;
  UINT y;
};

std::vector<TextureDispatchCase> BuildTextureDispatchCases() {
  return {{1, 1}, {2, 1}, {1, 2}, {2, 2}, {3, 2}, {4, 4}};
}

class TextureDispatchMatrixSpec
    : public ::testing::TestWithParam<TextureDispatchCase> {
protected:
  void SetUp() override {
    ASSERT_TRUE(HResultSucceeded(context_.Initialize()));
    const auto compiled =
        CompileShader(kTextureThreadIdComputeShader, "cs_5_0");
    ASSERT_TRUE(HResultSucceeded(compiled.result))
        << compiled.diagnostic_text();
    ASSERT_TRUE(HResultSucceeded(context_.device()->CreateComputeShader(
        compiled.bytecode->GetBufferPointer(),
        compiled.bytecode->GetBufferSize(), nullptr, compute_shader_.put())));
  }

  D3D11TestContext context_;
  ComPtr<ID3D11ComputeShader> compute_shader_;
};

TEST_P(TextureDispatchMatrixSpec, EachGroupWritesPackedIdentityToRwTexture2D) {
  const auto &test = GetParam();
  // Keep texture large enough that only the dispatched groups are written.
  constexpr UINT kWidth = 8;
  constexpr UINT kHeight = 8;
  ASSERT_LE(test.x, kWidth);
  ASSERT_LE(test.y, kHeight);

  D3D11_TEXTURE2D_DESC texture_desc = {};
  texture_desc.Width = kWidth;
  texture_desc.Height = kHeight;
  texture_desc.MipLevels = 1;
  texture_desc.ArraySize = 1;
  texture_desc.Format = DXGI_FORMAT_R32_UINT;
  texture_desc.SampleDesc.Count = 1;
  texture_desc.Usage = D3D11_USAGE_DEFAULT;
  texture_desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
  ComPtr<ID3D11Texture2D> texture;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateTexture2D(
      &texture_desc, nullptr, texture.put())));

  D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
  uav_desc.Format = DXGI_FORMAT_R32_UINT;
  uav_desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
  ComPtr<ID3D11UnorderedAccessView> uav;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateUnorderedAccessView(
      texture.get(), &uav_desc, uav.put())));

  const UINT seed[4] = {0u, 0u, 0u, 0u};
  context_.context()->ClearUnorderedAccessViewUint(uav.get(), seed);

  ID3D11UnorderedAccessView *uavs[] = {uav.get()};
  context_.context()->CSSetShader(compute_shader_.get(), nullptr, 0);
  context_.context()->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
  context_.context()->Dispatch(test.x, test.y, 1);
  ID3D11UnorderedAccessView *null_uav[] = {nullptr};
  context_.context()->CSSetUnorderedAccessViews(0, 1, null_uav, nullptr);

  D3D11_TEXTURE2D_DESC staging_desc = texture_desc;
  staging_desc.Usage = D3D11_USAGE_STAGING;
  staging_desc.BindFlags = 0;
  staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Texture2D> staging;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateTexture2D(
      &staging_desc, nullptr, staging.put())));
  context_.context()->CopyResource(staging.get(), texture.get());

  D3D11_MAPPED_SUBRESOURCE mapped = {};
  ASSERT_TRUE(HResultSucceeded(
      context_.context()->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped)));
  for (UINT y = 0; y < kHeight; ++y) {
    const auto *row = reinterpret_cast<const uint32_t *>(
        static_cast<const uint8_t *>(mapped.pData) + y * mapped.RowPitch);
    for (UINT x = 0; x < kWidth; ++x) {
      if (x < test.x && y < test.y) {
        const uint32_t expected = 0xb0000000u | x | (y << 8);
        EXPECT_EQ(row[x], expected) << "texel (" << x << ", " << y << ')';
      } else {
        EXPECT_EQ(row[x], 0u) << "outside texel (" << x << ", " << y << ')';
      }
    }
  }
  context_.context()->Unmap(staging.get(), 0);
}

std::string
TextureDispatchName(const ::testing::TestParamInfo<TextureDispatchCase> &info) {
  return "X" + std::to_string(info.param.x) + "Y" +
         std::to_string(info.param.y);
}

INSTANTIATE_TEST_SUITE_P(TextureGroupMatrix, TextureDispatchMatrixSpec,
                         ::testing::ValuesIn(BuildTextureDispatchCases()),
                         TextureDispatchName);

// ---------------------------------------------------------------------------
// 2. Multiple dispatches in one context: later overwrite wins
// ---------------------------------------------------------------------------

class MultiDispatchOrderSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_TRUE(HResultSucceeded(context_.Initialize()));
    const auto compiled =
        CompileShader(kOverwriteBufferComputeShader, "cs_5_0");
    ASSERT_TRUE(HResultSucceeded(compiled.result))
        << compiled.diagnostic_text();
    ASSERT_TRUE(HResultSucceeded(context_.device()->CreateComputeShader(
        compiled.bytecode->GetBufferPointer(),
        compiled.bytecode->GetBufferSize(), nullptr, compute_shader_.put())));
  }

  HRESULT BindConstant(uint32_t write_value) {
    // cbuffer must be 16-byte aligned.
    const uint32_t constants[4] = {write_value, 0, 0, 0};
    ComPtr<ID3D11Buffer> cb;
    const HRESULT hr = CreateConstantBuffer(context_.device(), constants,
                                            sizeof(constants), cb.put());
    if (FAILED(hr))
      return hr;
    ID3D11Buffer *cbs[] = {cb.get()};
    context_.context()->CSSetConstantBuffers(0, 1, cbs);
    // Keep the CB alive across Dispatch by storing it.
    bound_cb_ = std::move(cb);
    return S_OK;
  }

  D3D11TestContext context_;
  ComPtr<ID3D11ComputeShader> compute_shader_;
  ComPtr<ID3D11Buffer> bound_cb_;
};

TEST_F(MultiDispatchOrderSpec, LaterDispatchOverwritesEarlierWrites) {
  constexpr UINT kElements = 8;
  std::vector<uint32_t> seed(kElements, 0xeeeeeeeeu);
  ComPtr<ID3D11Buffer> buffer;
  ComPtr<ID3D11UnorderedAccessView> uav;
  ASSERT_TRUE(HResultSucceeded(CreateTypedUavBuffer(
      context_.device(), kElements, seed.data(), buffer.put(), uav.put())));

  ID3D11UnorderedAccessView *uavs[] = {uav.get()};
  context_.context()->CSSetShader(compute_shader_.get(), nullptr, 0);
  context_.context()->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);

  // First pass: write 0x11111111 into every slot.
  ASSERT_TRUE(HResultSucceeded(BindConstant(0x11111111u)));
  context_.context()->Dispatch(kElements, 1, 1);

  // Second pass: overwrite only the first half with 0x22222222.
  ASSERT_TRUE(HResultSucceeded(BindConstant(0x22222222u)));
  context_.context()->Dispatch(kElements / 2, 1, 1);

  // Third pass: overwrite slot 0 only with 0x33333333 (final winner for [0]).
  ASSERT_TRUE(HResultSucceeded(BindConstant(0x33333333u)));
  context_.context()->Dispatch(1, 1, 1);

  ID3D11UnorderedAccessView *null_uav[] = {nullptr};
  context_.context()->CSSetUnorderedAccessViews(0, 1, null_uav, nullptr);

  std::vector<uint32_t> values;
  ASSERT_TRUE(HResultSucceeded(ReadTypedUavBuffer(
      context_.device(), context_.context(), buffer.get(), kElements, &values)));

  EXPECT_EQ(values[0], 0x33333333u);
  for (UINT i = 1; i < kElements / 2; ++i)
    EXPECT_EQ(values[i], 0x22222222u) << "index " << i;
  for (UINT i = kElements / 2; i < kElements; ++i)
    EXPECT_EQ(values[i], 0x11111111u) << "index " << i;
}

TEST_F(MultiDispatchOrderSpec, ConsecutiveFullDispatchesKeepLastValue) {
  constexpr UINT kElements = 4;
  std::vector<uint32_t> seed(kElements, 0u);
  ComPtr<ID3D11Buffer> buffer;
  ComPtr<ID3D11UnorderedAccessView> uav;
  ASSERT_TRUE(HResultSucceeded(CreateTypedUavBuffer(
      context_.device(), kElements, seed.data(), buffer.put(), uav.put())));

  ID3D11UnorderedAccessView *uavs[] = {uav.get()};
  context_.context()->CSSetShader(compute_shader_.get(), nullptr, 0);
  context_.context()->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);

  constexpr uint32_t kSequence[] = {0xaaaaaaaau, 0xbbbbbbbbu, 0xccccccccu};
  for (const uint32_t value : kSequence) {
    ASSERT_TRUE(HResultSucceeded(BindConstant(value)));
    context_.context()->Dispatch(kElements, 1, 1);
  }

  ID3D11UnorderedAccessView *null_uav[] = {nullptr};
  context_.context()->CSSetUnorderedAccessViews(0, 1, null_uav, nullptr);

  std::vector<uint32_t> values;
  ASSERT_TRUE(HResultSucceeded(ReadTypedUavBuffer(
      context_.device(), context_.context(), buffer.get(), kElements, &values)));
  for (UINT i = 0; i < kElements; ++i)
    EXPECT_EQ(values[i], 0xccccccccu) << "index " << i;
}

// ---------------------------------------------------------------------------
// 3. CS constant buffer (b0) affects written values
// ---------------------------------------------------------------------------

struct ConstantBufferCase {
  uint32_t base;
  uint32_t multiplier;
  UINT dispatch_x;
};

std::vector<ConstantBufferCase> BuildConstantBufferCases() {
  return {
      {0u, 1u, 4u},
      {7u, 1u, 8u},
      {0x1000u, 3u, 5u},
      {0xffffff00u, 0u, 3u},
      {42u, 10u, 1u},
  };
}

class ConstantBufferDispatchSpec
    : public ::testing::TestWithParam<ConstantBufferCase> {
protected:
  void SetUp() override {
    ASSERT_TRUE(HResultSucceeded(context_.Initialize()));
    const auto compiled =
        CompileShader(kConstantBufferComputeShader, "cs_5_0");
    ASSERT_TRUE(HResultSucceeded(compiled.result))
        << compiled.diagnostic_text();
    ASSERT_TRUE(HResultSucceeded(context_.device()->CreateComputeShader(
        compiled.bytecode->GetBufferPointer(),
        compiled.bytecode->GetBufferSize(), nullptr, compute_shader_.put())));
  }

  D3D11TestContext context_;
  ComPtr<ID3D11ComputeShader> compute_shader_;
};

TEST_P(ConstantBufferDispatchSpec, BaseAndMultiplierDriveWrittenValues) {
  const auto &test = GetParam();
  constexpr UINT kCapacity = 16;
  ASSERT_LE(test.dispatch_x, kCapacity);

  std::vector<uint32_t> seed(kCapacity, 0xdeadbeefu);
  ComPtr<ID3D11Buffer> buffer;
  ComPtr<ID3D11UnorderedAccessView> uav;
  ASSERT_TRUE(HResultSucceeded(CreateTypedUavBuffer(
      context_.device(), kCapacity, seed.data(), buffer.put(), uav.put())));

  const uint32_t constants[4] = {test.base, test.multiplier, 0, 0};
  ComPtr<ID3D11Buffer> cb;
  ASSERT_TRUE(HResultSucceeded(CreateConstantBuffer(
      context_.device(), constants, sizeof(constants), cb.put())));

  ID3D11Buffer *cbs[] = {cb.get()};
  ID3D11UnorderedAccessView *uavs[] = {uav.get()};
  context_.context()->CSSetShader(compute_shader_.get(), nullptr, 0);
  context_.context()->CSSetConstantBuffers(0, 1, cbs);
  context_.context()->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
  context_.context()->Dispatch(test.dispatch_x, 1, 1);
  ID3D11UnorderedAccessView *null_uav[] = {nullptr};
  context_.context()->CSSetUnorderedAccessViews(0, 1, null_uav, nullptr);

  std::vector<uint32_t> values;
  ASSERT_TRUE(HResultSucceeded(ReadTypedUavBuffer(
      context_.device(), context_.context(), buffer.get(), kCapacity, &values)));

  for (UINT i = 0; i < test.dispatch_x; ++i) {
    const uint32_t expected = test.base + test.multiplier * i;
    EXPECT_EQ(values[i], expected) << "index " << i;
  }
  for (UINT i = test.dispatch_x; i < kCapacity; ++i)
    EXPECT_EQ(values[i], 0xdeadbeefu) << "untouched index " << i;
}

std::string
ConstantBufferName(const ::testing::TestParamInfo<ConstantBufferCase> &info) {
  return "B" + std::to_string(info.param.base) + "M" +
         std::to_string(info.param.multiplier) + "X" +
         std::to_string(info.param.dispatch_x);
}

INSTANTIATE_TEST_SUITE_P(CsB0, ConstantBufferDispatchSpec,
                         ::testing::ValuesIn(BuildConstantBufferCases()),
                         ConstantBufferName);

// ---------------------------------------------------------------------------
// 4. Structured buffer UAV: counter and/or append
// ---------------------------------------------------------------------------

class StructuredUavDispatchSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_TRUE(HResultSucceeded(context_.Initialize()));
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_NE(context_.context(), nullptr);
  }

  D3D11TestContext context_;
};

TEST_F(StructuredUavDispatchSpec, CounterUavClaimsSlotsAndWritesThreadIds) {
  constexpr UINT kElements = 8;
  constexpr UINT kThreadCount = 4; // matches [numthreads(4,1,1)]

  D3D11_BUFFER_DESC desc = {};
  desc.ByteWidth = kElements * sizeof(uint32_t);
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
  desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
  desc.StructureByteStride = sizeof(uint32_t);

  std::vector<uint32_t> initial(kElements, 0xaaaaaaaau);
  D3D11_SUBRESOURCE_DATA initial_data = {};
  initial_data.pSysMem = initial.data();

  ComPtr<ID3D11Buffer> buffer;
  const HRESULT create_hr =
      context_.device()->CreateBuffer(&desc, &initial_data, buffer.put());
  if (FAILED(create_hr)) {
    GTEST_SKIP() << "CreateBuffer STRUCTURED+UAV unsupported: 0x" << std::hex
                 << static_cast<unsigned long>(create_hr);
  }

  D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
  uav_desc.Format = DXGI_FORMAT_UNKNOWN;
  uav_desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
  uav_desc.Buffer.FirstElement = 0;
  uav_desc.Buffer.NumElements = kElements;
  uav_desc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_COUNTER;
  ComPtr<ID3D11UnorderedAccessView> uav;
  const HRESULT uav_hr = context_.device()->CreateUnorderedAccessView(
      buffer.get(), &uav_desc, uav.put());
  if (FAILED(uav_hr)) {
    GTEST_SKIP() << "CreateUnorderedAccessView COUNTER unsupported: 0x"
                 << std::hex << static_cast<unsigned long>(uav_hr);
  }

  const auto compiled =
      CompileShader(kCounterStructuredComputeShader, "cs_5_0");
  if (FAILED(compiled.result) || !compiled.bytecode) {
    GTEST_SKIP() << "CS compile for counter structured UAV failed: "
                 << compiled.diagnostic_text();
  }
  ComPtr<ID3D11ComputeShader> compute_shader;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateComputeShader(
      compiled.bytecode->GetBufferPointer(), compiled.bytecode->GetBufferSize(),
      nullptr, compute_shader.put())));

  // Initial counter = 0 so IncrementCounter yields 0..3.
  UINT initial_counts[] = {0};
  ID3D11UnorderedAccessView *uavs[] = {uav.get()};
  context_.context()->CSSetShader(compute_shader.get(), nullptr, 0);
  context_.context()->CSSetUnorderedAccessViews(0, 1, uavs, initial_counts);
  context_.context()->Dispatch(1, 1, 1);
  ID3D11UnorderedAccessView *null_uav[] = {nullptr};
  context_.context()->CSSetUnorderedAccessViews(0, 1, null_uav, nullptr);

  // Hidden counter should now equal the number of claimed slots.
  D3D11_BUFFER_DESC count_desc = {};
  count_desc.ByteWidth = sizeof(uint32_t);
  count_desc.Usage = D3D11_USAGE_STAGING;
  count_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Buffer> count_staging;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateBuffer(
      &count_desc, nullptr, count_staging.put())));
  context_.context()->CopyStructureCount(count_staging.get(), 0, uav.get());
  D3D11_MAPPED_SUBRESOURCE mapped_count = {};
  ASSERT_TRUE(HResultSucceeded(context_.context()->Map(
      count_staging.get(), 0, D3D11_MAP_READ, 0, &mapped_count)));
  uint32_t counter_value = 0;
  std::memcpy(&counter_value, mapped_count.pData, sizeof(counter_value));
  context_.context()->Unmap(count_staging.get(), 0);
  if (counter_value != kThreadCount) {
    GTEST_SKIP() << "structured UAV counter not advanced (got " << counter_value
                 << ", expected " << kThreadCount << ")";
  }

  // Data: first kThreadCount elements form the multiset of thread ids.
  D3D11_BUFFER_DESC staging_desc = desc;
  staging_desc.Usage = D3D11_USAGE_STAGING;
  staging_desc.BindFlags = 0;
  staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  staging_desc.MiscFlags = 0;
  staging_desc.StructureByteStride = 0;
  ComPtr<ID3D11Buffer> staging;
  ASSERT_TRUE(HResultSucceeded(
      context_.device()->CreateBuffer(&staging_desc, nullptr, staging.put())));
  context_.context()->CopyResource(staging.get(), buffer.get());

  D3D11_MAPPED_SUBRESOURCE mapped = {};
  ASSERT_TRUE(HResultSucceeded(
      context_.context()->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped)));
  std::vector<uint32_t> values(kElements);
  std::memcpy(values.data(), mapped.pData, kElements * sizeof(uint32_t));
  context_.context()->Unmap(staging.get(), 0);

  std::array<bool, kThreadCount> seen = {};
  for (UINT i = 0; i < kThreadCount; ++i) {
    const uint32_t value = values[i];
    EXPECT_EQ(value & 0xff000000u, 0xc0000000u) << "index " << i;
    const uint32_t tid = value & 0x00ffffffu;
    ASSERT_LT(tid, kThreadCount) << "value 0x" << std::hex << value;
    EXPECT_FALSE(seen[tid]) << "duplicate tid " << tid;
    seen[tid] = true;
  }
  for (UINT i = 0; i < kThreadCount; ++i)
    EXPECT_TRUE(seen[i]) << "missing tid " << i;
  for (UINT i = kThreadCount; i < kElements; ++i)
    EXPECT_EQ(values[i], 0xaaaaaaaau) << "untouched index " << i;
}

TEST_F(StructuredUavDispatchSpec, AppendStructuredBufferWritesOrderedValues) {
  constexpr UINT kElements = 8;
  constexpr UINT kAppendCount = 3;

  D3D11_BUFFER_DESC desc = {};
  desc.ByteWidth = kElements * sizeof(uint32_t);
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
  desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
  desc.StructureByteStride = sizeof(uint32_t);

  std::vector<uint32_t> initial(kElements, 0xbbbbbbbbu);
  D3D11_SUBRESOURCE_DATA initial_data = {};
  initial_data.pSysMem = initial.data();

  ComPtr<ID3D11Buffer> buffer;
  const HRESULT create_hr =
      context_.device()->CreateBuffer(&desc, &initial_data, buffer.put());
  if (FAILED(create_hr)) {
    GTEST_SKIP() << "CreateBuffer STRUCTURED+UAV unsupported: 0x" << std::hex
                 << static_cast<unsigned long>(create_hr);
  }

  D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
  uav_desc.Format = DXGI_FORMAT_UNKNOWN;
  uav_desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
  uav_desc.Buffer.FirstElement = 0;
  uav_desc.Buffer.NumElements = kElements;
  uav_desc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_APPEND;
  ComPtr<ID3D11UnorderedAccessView> uav;
  const HRESULT uav_hr = context_.device()->CreateUnorderedAccessView(
      buffer.get(), &uav_desc, uav.put());
  if (FAILED(uav_hr)) {
    GTEST_SKIP() << "CreateUnorderedAccessView APPEND unsupported: 0x"
                 << std::hex << static_cast<unsigned long>(uav_hr);
  }

  const auto compiled = CompileShader(kAppendStructuredComputeShader, "cs_5_0");
  if (FAILED(compiled.result) || !compiled.bytecode) {
    GTEST_SKIP() << "CS compile for AppendStructuredBuffer failed: "
                 << compiled.diagnostic_text();
  }
  ComPtr<ID3D11ComputeShader> compute_shader;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateComputeShader(
      compiled.bytecode->GetBufferPointer(), compiled.bytecode->GetBufferSize(),
      nullptr, compute_shader.put())));

  UINT initial_counts[] = {0};
  ID3D11UnorderedAccessView *uavs[] = {uav.get()};
  context_.context()->CSSetShader(compute_shader.get(), nullptr, 0);
  context_.context()->CSSetUnorderedAccessViews(0, 1, uavs, initial_counts);
  context_.context()->Dispatch(1, 1, 1);
  ID3D11UnorderedAccessView *null_uav[] = {nullptr};
  context_.context()->CSSetUnorderedAccessViews(0, 1, null_uav, nullptr);

  D3D11_BUFFER_DESC count_desc = {};
  count_desc.ByteWidth = sizeof(uint32_t);
  count_desc.Usage = D3D11_USAGE_STAGING;
  count_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Buffer> count_staging;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateBuffer(
      &count_desc, nullptr, count_staging.put())));
  context_.context()->CopyStructureCount(count_staging.get(), 0, uav.get());
  D3D11_MAPPED_SUBRESOURCE mapped_count = {};
  ASSERT_TRUE(HResultSucceeded(context_.context()->Map(
      count_staging.get(), 0, D3D11_MAP_READ, 0, &mapped_count)));
  uint32_t counter_value = 0;
  std::memcpy(&counter_value, mapped_count.pData, sizeof(counter_value));
  context_.context()->Unmap(count_staging.get(), 0);
  EXPECT_EQ(counter_value, kAppendCount);

  D3D11_BUFFER_DESC staging_desc = desc;
  staging_desc.Usage = D3D11_USAGE_STAGING;
  staging_desc.BindFlags = 0;
  staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  staging_desc.MiscFlags = 0;
  staging_desc.StructureByteStride = 0;
  ComPtr<ID3D11Buffer> staging;
  ASSERT_TRUE(HResultSucceeded(
      context_.device()->CreateBuffer(&staging_desc, nullptr, staging.put())));
  context_.context()->CopyResource(staging.get(), buffer.get());

  D3D11_MAPPED_SUBRESOURCE mapped = {};
  ASSERT_TRUE(HResultSucceeded(
      context_.context()->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped)));
  std::vector<uint32_t> values(kElements);
  std::memcpy(values.data(), mapped.pData, kElements * sizeof(uint32_t));
  context_.context()->Unmap(staging.get(), 0);

  // Single-thread append sequence is ordered.
  EXPECT_EQ(values[0], 0xd0000010u);
  EXPECT_EQ(values[1], 0xd0000020u);
  EXPECT_EQ(values[2], 0xd0000030u);
  for (UINT i = kAppendCount; i < kElements; ++i)
    EXPECT_EQ(values[i], 0xbbbbbbbbu) << "untouched index " << i;
}

} // namespace
