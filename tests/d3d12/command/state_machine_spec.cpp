#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

class CommandStateMachineSpec : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  D3D12TestContext context_;
};

TEST_F(CommandStateMachineSpec, ValidSequences32FixedSeeds) {
  constexpr UINT case_count = 32;
  std::array<std::uint32_t, case_count> source_values = {};
  std::array<std::uint32_t, case_count> expected = {};
  for (UINT i = 0; i < case_count; ++i)
    source_values[i] = 0x10000u + i;

  auto source = context_.CreateUploadBuffer(
      sizeof(source_values), source_values.data(), sizeof(source_values));
  auto zero = context_.CreateUploadBuffer(sizeof(expected), expected.data(),
                                           sizeof(expected));
  auto output = context_.CreateBuffer(
      sizeof(expected), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(source);
  ASSERT_TRUE(zero);
  ASSERT_TRUE(output);
  context_.list()->CopyBufferRegion(output.get(), 0, zero.get(), 0,
                                    sizeof(expected));
  ASSERT_EQ(context_.ExecuteAndWait(), S_OK);
  ASSERT_EQ(context_.ResetCommandList(), S_OK);

  ComPtr<ID3D12CommandAllocator> alternate_allocator;
  ASSERT_EQ(context_.device()->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                __uuidof(ID3D12CommandAllocator),
                reinterpret_cast<void **>(alternate_allocator.put())),
            S_OK);
  ID3D12CommandAllocator *allocators[] = {context_.allocator(),
                                          alternate_allocator.get()};
  UINT current_allocator = 0;
  std::uint32_t seed = 0x6d2b79f5u;
  for (UINT i = 0; i < case_count; ++i) {
    seed = seed * 1664525u + 1013904223u;
    SCOPED_TRACE(::testing::Message() << "case " << i << ", seed " << seed);
    const UINT64 offset = UINT64(i) * sizeof(std::uint32_t);
    context_.list()->CopyBufferRegion(output.get(), offset, source.get(),
                                      offset, sizeof(std::uint32_t));
    if (seed & 8u)
      context_.list()->CopyBufferRegion(output.get(), offset, source.get(),
                                        offset, sizeof(std::uint32_t));
    ASSERT_EQ(context_.list()->Close(), S_OK);

    if (seed & 1u) {
      ID3D12CommandList *lists[] = {context_.list()};
      context_.queue()->ExecuteCommandLists(1, lists);
      ASSERT_EQ(context_.SignalAndWait(), S_OK);
      expected[i] = source_values[i];
    }
    if (seed & 2u) {
      ASSERT_EQ(allocators[current_allocator]->Reset(), S_OK);
    }
    current_allocator = (seed >> 2) & 1u;
    ASSERT_EQ(context_.list()->Reset(allocators[current_allocator], nullptr),
              S_OK);
  }

  D3D12TestContext::Transition(
      context_.list(), output.get(), D3D12_RESOURCE_STATE_COPY_DEST,
      D3D12_RESOURCE_STATE_COPY_SOURCE);
  std::vector<std::uint8_t> bytes;
  ASSERT_EQ(context_.ReadbackBuffer(output.get(), sizeof(expected), &bytes),
            S_OK);
  ASSERT_EQ(bytes.size(), sizeof(expected));
  std::array<std::uint32_t, case_count> actual = {};
  std::memcpy(actual.data(), bytes.data(), bytes.size());
  EXPECT_EQ(actual, expected);
}

} // namespace
