#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// Plan §15.10: WriteBufferImmediate values, modes, multi-slot matrix.
// Public D3D12 API only (ID3D12GraphicsCommandList2).

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

struct ImmediateValueCase {
  UINT value;
  UINT slot; // dword index in destination buffer
  D3D12_WRITEBUFFERIMMEDIATE_MODE mode;
};

std::vector<ImmediateValueCase> BuildImmediateValueCases() {
  std::vector<ImmediateValueCase> cases;
  const UINT values[] = {
      0u, 1u, 0xffffffffu, 0x80000000u, 0x7fffffffu, 0xa5a5a5a5u, 0x5a5a5a5au,
      0x12345678u, 0xdeadbeefu, 0x0f0f0f0fu, 0xf0f0f0f0u, 42u, 100u, 255u,
      256u, 65535u, 65536u,
  };
  const D3D12_WRITEBUFFERIMMEDIATE_MODE modes[] = {
      D3D12_WRITEBUFFERIMMEDIATE_MODE_DEFAULT,
      D3D12_WRITEBUFFERIMMEDIATE_MODE_MARKER_IN,
      D3D12_WRITEBUFFERIMMEDIATE_MODE_MARKER_OUT,
  };
  for (const UINT value : values) {
    for (UINT slot = 0; slot < 8; ++slot) {
      for (const auto mode : modes)
        cases.push_back({value, slot, mode});
    }
  }
  return cases;
}

struct ImmediateBurstCase {
  UINT count; // 1..8 consecutive dwords
  D3D12_WRITEBUFFERIMMEDIATE_MODE mode;
};

std::vector<ImmediateBurstCase> BuildImmediateBurstCases() {
  std::vector<ImmediateBurstCase> cases;
  const D3D12_WRITEBUFFERIMMEDIATE_MODE modes[] = {
      D3D12_WRITEBUFFERIMMEDIATE_MODE_DEFAULT,
      D3D12_WRITEBUFFERIMMEDIATE_MODE_MARKER_IN,
      D3D12_WRITEBUFFERIMMEDIATE_MODE_MARKER_OUT,
  };
  for (UINT count = 1; count <= 8; ++count) {
    for (const auto mode : modes)
      cases.push_back({count, mode});
  }
  return cases;
}

class WriteBufferImmediateMatrixSpec
    : public ::testing::TestWithParam<ImmediateValueCase> {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_EQ(context_.list()->QueryInterface(
                  __uuidof(ID3D12GraphicsCommandList2),
                  reinterpret_cast<void **>(list2_.put())),
              S_OK);
  }

  D3D12TestContext context_;
  ComPtr<ID3D12GraphicsCommandList2> list2_;
};

TEST_P(WriteBufferImmediateMatrixSpec, WritesSingleDwordAtSlot) {
  const auto &test = GetParam();
  constexpr UINT kWords = 8;
  auto destination = context_.CreateBuffer(
      kWords * sizeof(UINT), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(destination);

  // Zero the buffer first via upload copy.
  std::vector<UINT> zeros(kWords, 0u);
  auto upload = context_.CreateUploadBuffer(kWords * sizeof(UINT), zeros.data(),
                                            zeros.size() * sizeof(UINT));
  ASSERT_TRUE(upload);
  context_.list()->CopyBufferRegion(destination.get(), 0, upload.get(), 0,
                                    kWords * sizeof(UINT));

  const D3D12_WRITEBUFFERIMMEDIATE_PARAMETER param = {
      destination->GetGPUVirtualAddress() + test.slot * sizeof(UINT),
      test.value};
  const D3D12_WRITEBUFFERIMMEDIATE_MODE mode = test.mode;
  list2_->WriteBufferImmediate(1, &param, &mode);

  D3D12TestContext::Transition(context_.list(), destination.get(),
                               D3D12_RESOURCE_STATE_COPY_DEST,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
  std::vector<std::uint8_t> bytes;
  ASSERT_EQ(context_.ReadbackBuffer(destination.get(), kWords * sizeof(UINT),
                                    &bytes),
            S_OK);
  for (UINT i = 0; i < kWords; ++i) {
    UINT word = 0;
    std::memcpy(&word, bytes.data() + i * sizeof(UINT), sizeof(word));
    EXPECT_EQ(word, i == test.slot ? test.value : 0u)
        << "slot=" << i << " mode=" << static_cast<UINT>(test.mode);
  }
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

class WriteBufferImmediateBurstSpec
    : public ::testing::TestWithParam<ImmediateBurstCase> {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_EQ(context_.list()->QueryInterface(
                  __uuidof(ID3D12GraphicsCommandList2),
                  reinterpret_cast<void **>(list2_.put())),
              S_OK);
  }

  D3D12TestContext context_;
  ComPtr<ID3D12GraphicsCommandList2> list2_;
};

TEST_P(WriteBufferImmediateBurstSpec, WritesConsecutiveDwordsInOneCall) {
  const auto &test = GetParam();
  constexpr UINT kWords = 8;
  auto destination = context_.CreateBuffer(
      kWords * sizeof(UINT), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(destination);
  std::vector<UINT> zeros(kWords, 0u);
  auto upload = context_.CreateUploadBuffer(kWords * sizeof(UINT), zeros.data(),
                                            zeros.size() * sizeof(UINT));
  ASSERT_TRUE(upload);
  context_.list()->CopyBufferRegion(destination.get(), 0, upload.get(), 0,
                                    kWords * sizeof(UINT));

  std::vector<D3D12_WRITEBUFFERIMMEDIATE_PARAMETER> params(test.count);
  std::vector<D3D12_WRITEBUFFERIMMEDIATE_MODE> modes(test.count, test.mode);
  for (UINT i = 0; i < test.count; ++i) {
    params[i].Dest = destination->GetGPUVirtualAddress() + i * sizeof(UINT);
    params[i].Value = 0x1000u + i;
  }
  list2_->WriteBufferImmediate(test.count, params.data(), modes.data());

  D3D12TestContext::Transition(context_.list(), destination.get(),
                               D3D12_RESOURCE_STATE_COPY_DEST,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
  std::vector<std::uint8_t> bytes;
  ASSERT_EQ(context_.ReadbackBuffer(destination.get(), kWords * sizeof(UINT),
                                    &bytes),
            S_OK);
  for (UINT i = 0; i < kWords; ++i) {
    UINT word = 0;
    std::memcpy(&word, bytes.data() + i * sizeof(UINT), sizeof(word));
    EXPECT_EQ(word, i < test.count ? (0x1000u + i) : 0u) << "i=" << i;
  }
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

std::string ImmediateValueName(
    const ::testing::TestParamInfo<ImmediateValueCase> &info) {
  return "V" + std::to_string(info.param.value) + "S" +
         std::to_string(info.param.slot) + "M" +
         std::to_string(static_cast<UINT>(info.param.mode)) + "I" +
         std::to_string(info.index);
}

std::string ImmediateBurstName(
    const ::testing::TestParamInfo<ImmediateBurstCase> &info) {
  return "C" + std::to_string(info.param.count) + "M" +
         std::to_string(static_cast<UINT>(info.param.mode));
}

INSTANTIATE_TEST_SUITE_P(ValueMatrix, WriteBufferImmediateMatrixSpec,
                         ::testing::ValuesIn(BuildImmediateValueCases()),
                         ImmediateValueName);

INSTANTIATE_TEST_SUITE_P(BurstMatrix, WriteBufferImmediateBurstSpec,
                         ::testing::ValuesIn(BuildImmediateBurstCases()),
                         ImmediateBurstName);

} // namespace
