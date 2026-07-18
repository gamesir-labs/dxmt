#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

using dxmt::test::ComPtr;
using dxmt::test::CreateIsolatedD3D12Device;
using dxmt::test::D3D12TestContext;

class WriteBufferImmediateSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_EQ(context_.list()->QueryInterface(
                  __uuidof(ID3D12GraphicsCommandList2),
                  reinterpret_cast<void **>(list2_.put())),
              S_OK);
  }

  std::vector<UINT> Readback(ID3D12Resource *buffer, UINT word_count) {
    D3D12TestContext::Transition(
        context_.list(), buffer, D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_COPY_SOURCE);
    std::vector<std::uint8_t> bytes;
    EXPECT_EQ(context_.ReadbackBuffer(buffer, word_count * sizeof(UINT),
                                      &bytes),
              S_OK);
    std::vector<UINT> words(word_count);
    if (bytes.size() == words.size() * sizeof(UINT))
      std::memcpy(words.data(), bytes.data(), bytes.size());
    return words;
  }

  D3D12TestContext context_;
  ComPtr<ID3D12GraphicsCommandList2> list2_;
};

TEST_F(WriteBufferImmediateSpec, PreservesOrderRelativeToCopiesInSameList) {
  constexpr std::array<UINT, 2> source_values = {
      0x10203040u,
      0x50607080u,
  };
  auto source = context_.CreateUploadBuffer(
      sizeof(source_values), source_values.data(), sizeof(source_values));
  auto destination = context_.CreateBuffer(
      3 * sizeof(UINT), D3D12_HEAP_TYPE_DEFAULT,
      D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(source);
  ASSERT_TRUE(destination);

  const auto base = destination->GetGPUVirtualAddress();
  const D3D12_WRITEBUFFERIMMEDIATE_PARAMETER before_copy = {
      base, 0x11111111u};
  list2_->WriteBufferImmediate(1, &before_copy, nullptr);
  context_.list()->CopyBufferRegion(destination.get(), 0, source.get(), 0,
                                    sizeof(UINT));

  context_.list()->CopyBufferRegion(destination.get(), sizeof(UINT),
                                    source.get(), sizeof(UINT), sizeof(UINT));
  const D3D12_WRITEBUFFERIMMEDIATE_PARAMETER after_copy = {
      base + sizeof(UINT), 0x22222222u};
  list2_->WriteBufferImmediate(1, &after_copy, nullptr);

  const std::array<D3D12_WRITEBUFFERIMMEDIATE_PARAMETER, 2> same_address = {{
      {base + 2 * sizeof(UINT), 0x33333333u},
      {base + 2 * sizeof(UINT), 0x44444444u},
  }};
  list2_->WriteBufferImmediate(static_cast<UINT>(same_address.size()),
                               same_address.data(), nullptr);

  EXPECT_EQ(Readback(destination.get(), 3),
            (std::vector<UINT>{source_values[0], after_copy.Value,
                               same_address.back().Value}));
}

TEST_F(WriteBufferImmediateSpec,
       PreservesOrderRelativeToCopiesAcrossSeparateExecutes) {
  constexpr std::array<UINT, 2> source_values = {
      0x89abcdefu,
      0x76543210u,
  };
  auto source = context_.CreateUploadBuffer(
      sizeof(source_values), source_values.data(), sizeof(source_values));
  auto destination = context_.CreateBuffer(
      sizeof(source_values), D3D12_HEAP_TYPE_DEFAULT,
      D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(source);
  ASSERT_TRUE(destination);

  std::array<ComPtr<ID3D12CommandAllocator>, 2> allocators;
  std::array<ComPtr<ID3D12GraphicsCommandList>, 2> lists;
  std::array<ComPtr<ID3D12GraphicsCommandList2>, 2> lists2;
  for (UINT index = 0; index < lists.size(); ++index) {
    ASSERT_EQ(context_.device()->CreateCommandAllocator(
                  D3D12_COMMAND_LIST_TYPE_DIRECT,
                  __uuidof(ID3D12CommandAllocator),
                  reinterpret_cast<void **>(allocators[index].put())),
              S_OK);
    ASSERT_EQ(context_.device()->CreateCommandList(
                  0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocators[index].get(),
                  nullptr, __uuidof(ID3D12GraphicsCommandList),
                  reinterpret_cast<void **>(lists[index].put())),
              S_OK);
    ASSERT_EQ(lists[index]->QueryInterface(
                  __uuidof(ID3D12GraphicsCommandList2),
                  reinterpret_cast<void **>(lists2[index].put())),
              S_OK);
  }

  const auto base = destination->GetGPUVirtualAddress();
  const D3D12_WRITEBUFFERIMMEDIATE_PARAMETER first_write = {
      base, 0x13579bdfu};
  lists2[0]->WriteBufferImmediate(1, &first_write, nullptr);
  lists[0]->CopyBufferRegion(destination.get(), sizeof(UINT), source.get(),
                             sizeof(UINT), sizeof(UINT));
  ASSERT_EQ(lists[0]->Close(), S_OK);

  lists[1]->CopyBufferRegion(destination.get(), 0, source.get(), 0,
                             sizeof(UINT));
  const D3D12_WRITEBUFFERIMMEDIATE_PARAMETER second_write = {
      base + sizeof(UINT), 0x2468ace0u};
  lists2[1]->WriteBufferImmediate(1, &second_write, nullptr);
  ASSERT_EQ(lists[1]->Close(), S_OK);

  ID3D12CommandList *first = lists[0].get();
  context_.queue()->ExecuteCommandLists(1, &first);
  ID3D12CommandList *second = lists[1].get();
  context_.queue()->ExecuteCommandLists(1, &second);
  ASSERT_EQ(context_.SignalAndWait(), S_OK);

  EXPECT_EQ(Readback(destination.get(), 2),
            (std::vector<UINT>{source_values[0], second_write.Value}));
}

TEST_F(WriteBufferImmediateSpec, WritesEveryModeAtBufferBoundaries) {
  auto destination = context_.CreateBuffer(
      3 * sizeof(UINT), D3D12_HEAP_TYPE_DEFAULT,
      D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(destination);
  const auto base = destination->GetGPUVirtualAddress();
  ASSERT_NE(base, 0u);
  const std::array<D3D12_WRITEBUFFERIMMEDIATE_PARAMETER, 3> parameters = {{
      {base, 0x10203040u},
      {base + sizeof(UINT), 0x50607080u},
      {base + 2 * sizeof(UINT), 0x90a0b0c0u},
  }};
  constexpr std::array<D3D12_WRITEBUFFERIMMEDIATE_MODE, 3> modes = {
      D3D12_WRITEBUFFERIMMEDIATE_MODE_DEFAULT,
      D3D12_WRITEBUFFERIMMEDIATE_MODE_MARKER_IN,
      D3D12_WRITEBUFFERIMMEDIATE_MODE_MARKER_OUT,
  };

  list2_->WriteBufferImmediate(static_cast<UINT>(parameters.size()),
                               parameters.data(), modes.data());

  EXPECT_EQ(Readback(destination.get(), parameters.size()),
            (std::vector<UINT>{parameters[0].Value, parameters[1].Value,
                               parameters[2].Value}));
}

enum class InvalidImmediateWriteCase {
  NullParameters,
  MisalignedAddress,
  RangePastEnd,
  InvalidMode,
  ForeignAddress,
};

class InvalidWriteBufferImmediateSpec
    : public WriteBufferImmediateSpec,
      public ::testing::WithParamInterface<InvalidImmediateWriteCase> {};

TEST_P(InvalidWriteBufferImmediateSpec,
       RejectsWholeBatchAndAllowsFreshListRecovery) {
  auto destination = context_.CreateBuffer(
      10, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(destination);
  const auto base = destination->GetGPUVirtualAddress();
  ASSERT_NE(base, 0u);
  std::array<D3D12_WRITEBUFFERIMMEDIATE_PARAMETER, 2> parameters = {{
      {base, 0x11223344u},
      {base + sizeof(UINT), 0x55667788u},
  }};
  std::array<D3D12_WRITEBUFFERIMMEDIATE_MODE, 2> modes = {
      D3D12_WRITEBUFFERIMMEDIATE_MODE_DEFAULT,
      D3D12_WRITEBUFFERIMMEDIATE_MODE_DEFAULT,
  };
  UINT count = static_cast<UINT>(parameters.size());
  const D3D12_WRITEBUFFERIMMEDIATE_PARAMETER *selected_parameters =
      parameters.data();
  ComPtr<ID3D12Device> foreign_device;
  ComPtr<ID3D12Resource> foreign_destination;

  switch (GetParam()) {
  case InvalidImmediateWriteCase::NullParameters:
    count = 1;
    selected_parameters = nullptr;
    break;
  case InvalidImmediateWriteCase::MisalignedAddress:
    parameters[1].Dest = base + 1;
    break;
  case InvalidImmediateWriteCase::RangePastEnd:
    parameters[1].Dest = base + 2 * sizeof(UINT);
    break;
  case InvalidImmediateWriteCase::InvalidMode:
    modes[1] = static_cast<D3D12_WRITEBUFFERIMMEDIATE_MODE>(3);
    break;
  case InvalidImmediateWriteCase::ForeignAddress: {
    foreign_device = CreateIsolatedD3D12Device();
    ASSERT_TRUE(foreign_device);
    D3D12TestContext foreign_context;
    ASSERT_EQ(foreign_context.Initialize(foreign_device.get()), S_OK);
    foreign_destination = foreign_context.CreateBuffer(
        sizeof(UINT), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
        D3D12_RESOURCE_STATE_COPY_DEST);
    ASSERT_TRUE(foreign_destination);
    parameters[1].Dest = foreign_destination->GetGPUVirtualAddress();
    ASSERT_NE(parameters[1].Dest, 0u);
    break;
  }
  }

  list2_->WriteBufferImmediate(count, selected_parameters, modes.data());
  EXPECT_EQ(context_.list()->Close(), E_INVALIDARG);

  ComPtr<ID3D12CommandAllocator> recovery_allocator;
  ComPtr<ID3D12GraphicsCommandList> recovery_list;
  ComPtr<ID3D12GraphicsCommandList2> recovery_list2;
  ASSERT_EQ(context_.device()->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                IID_PPV_ARGS(recovery_allocator.put())),
            S_OK);
  ASSERT_EQ(context_.device()->CreateCommandList(
                0, D3D12_COMMAND_LIST_TYPE_DIRECT, recovery_allocator.get(),
                nullptr, IID_PPV_ARGS(recovery_list.put())),
            S_OK);
  ASSERT_EQ(recovery_list->QueryInterface(IID_PPV_ARGS(recovery_list2.put())),
            S_OK);
  const D3D12_WRITEBUFFERIMMEDIATE_PARAMETER valid = {base, 0xaabbccddu};
  recovery_list2->WriteBufferImmediate(1, &valid, nullptr);
  EXPECT_EQ(recovery_list->Close(), S_OK);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

std::string InvalidImmediateWriteCaseName(
    const ::testing::TestParamInfo<InvalidImmediateWriteCase> &info) {
  switch (info.param) {
  case InvalidImmediateWriteCase::NullParameters:
    return "NullParameters";
  case InvalidImmediateWriteCase::MisalignedAddress:
    return "MisalignedAddress";
  case InvalidImmediateWriteCase::RangePastEnd:
    return "RangePastEnd";
  case InvalidImmediateWriteCase::InvalidMode:
    return "InvalidMode";
  case InvalidImmediateWriteCase::ForeignAddress:
    return "ForeignAddress";
  }
  return "Unknown";
}

INSTANTIATE_TEST_SUITE_P(
    InvalidMatrix, InvalidWriteBufferImmediateSpec,
    ::testing::Values(InvalidImmediateWriteCase::NullParameters,
                      InvalidImmediateWriteCase::MisalignedAddress,
                      InvalidImmediateWriteCase::RangePastEnd,
                      InvalidImmediateWriteCase::InvalidMode,
                      InvalidImmediateWriteCase::ForeignAddress),
    InvalidImmediateWriteCaseName);

TEST_F(WriteBufferImmediateSpec, ZeroCountWithNullInputsIsNoOp) {
  list2_->WriteBufferImmediate(0, nullptr, nullptr);
  EXPECT_EQ(context_.list()->Close(), S_OK);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
