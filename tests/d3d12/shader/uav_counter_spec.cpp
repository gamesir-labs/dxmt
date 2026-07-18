#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>
#include "d3d12_test_context.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

using dxmt::test::ComPtr;
using dxmt::test::CompileShader;
using dxmt::test::D3D12TestContext;

UINT ReadUint(const std::vector<std::uint8_t> &bytes, UINT64 offset) {
  UINT value = 0;
  if (offset + sizeof(value) <= bytes.size())
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
  return value;
}

HRESULT MapReadbackBuffer(ID3D12Resource *readback, UINT64 size,
                          std::vector<std::uint8_t> *bytes) {
  void *mapped = nullptr;
  D3D12_RANGE read_range = {0, static_cast<SIZE_T>(size)};
  HRESULT hr = readback->Map(0, &read_range, &mapped);
  if (FAILED(hr))
    return hr;
  bytes->resize(static_cast<std::size_t>(size));
  std::memcpy(bytes->data(), mapped, bytes->size());
  D3D12_RANGE written_range = {0, 0};
  readback->Unmap(0, &written_range);
  return S_OK;
}

// Precompiled SM5 DXBC from Wine's indirect-dispatch append-buffer test.
// One thread appends {4,2,1}, {4,1,1}, and {3,1,1} in order.
constexpr DWORD kAppendShader[] = {
    0x43425844, 0x954de75a, 0x8bb1b78b, 0x84ded464, 0x9d9532b7,
    0x00000001, 0x00000158, 0x00000003, 0x0000002c, 0x0000003c,
    0x0000004c, 0x4e475349, 0x00000008, 0x00000000, 0x00000008,
    0x4e47534f, 0x00000008, 0x00000000, 0x00000008, 0x58454853,
    0x00000104, 0x00050050, 0x00000041, 0x0100086a, 0x0400009e,
    0x0011e000, 0x00000000, 0x0000000c, 0x02000068, 0x00000001,
    0x0400009b, 0x00000001, 0x00000001, 0x00000001, 0x050000b2,
    0x00100012, 0x00000000, 0x0011e000, 0x00000000, 0x0c0000a8,
    0x0011e072, 0x00000000, 0x0010000a, 0x00000000, 0x00004001,
    0x00000000, 0x00004002, 0x00000004, 0x00000002, 0x00000001,
    0x00000000, 0x050000b2, 0x00100012, 0x00000000, 0x0011e000,
    0x00000000, 0x0c0000a8, 0x0011e072, 0x00000000, 0x0010000a,
    0x00000000, 0x00004001, 0x00000000, 0x00004002, 0x00000004,
    0x00000001, 0x00000001, 0x00000000, 0x050000b2, 0x00100012,
    0x00000000, 0x0011e000, 0x00000000, 0x0c0000a8, 0x0011e072,
    0x00000000, 0x0010000a, 0x00000000, 0x00004001, 0x00000000,
    0x00004002, 0x00000003, 0x00000001, 0x00000001, 0x00000000,
    0x0100003e,
};

// Precompiled SM5 DXBC from Wine's UAV-counter test. Four threads atomically
// decrement u0's counter and copy the claimed element into the same u1 index.
constexpr DWORD kDecrementShader[] = {
    0x43425844, 0x957ef3dd, 0x9f317559, 0x09c8f12d, 0xdbfd98c8,
    0x00000001, 0x00000100, 0x00000003, 0x0000002c, 0x0000003c,
    0x0000004c, 0x4e475349, 0x00000008, 0x00000000, 0x00000008,
    0x4e47534f, 0x00000008, 0x00000000, 0x00000008, 0x58454853,
    0x000000ac, 0x00050050, 0x0000002b, 0x0100086a, 0x0480009e,
    0x0011e000, 0x00000000, 0x00000004, 0x0400009e, 0x0011e000,
    0x00000001, 0x00000004, 0x02000068, 0x00000001, 0x0400009b,
    0x00000004, 0x00000001, 0x00000001, 0x050000b3, 0x00100012,
    0x00000000, 0x0011e000, 0x00000000, 0x8b0000a7, 0x80002302,
    0x00199983, 0x00100022, 0x00000000, 0x0010000a, 0x00000000,
    0x00004001, 0x00000000, 0x0011e006, 0x00000000, 0x090000a8,
    0x0011e012, 0x00000001, 0x0010000a, 0x00000000, 0x00004001,
    0x00000000, 0x0010001a, 0x00000000, 0x0100003e,
};

class D3D12UavCounterSpec : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  ComPtr<ID3D12RootSignature> CreateRootSignature(UINT descriptor_count) {
    D3D12_DESCRIPTOR_RANGE range = {};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    range.NumDescriptors = descriptor_count;
    D3D12_ROOT_PARAMETER parameter = {};
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameter.DescriptorTable.NumDescriptorRanges = 1;
    parameter.DescriptorTable.pDescriptorRanges = &range;
    D3D12_ROOT_SIGNATURE_DESC desc = {};
    desc.NumParameters = 1;
    desc.pParameters = &parameter;
    return context_.CreateRootSignature(desc);
  }

  ComPtr<ID3D12PipelineState>
  CreatePipeline(ID3D12RootSignature *root, const DWORD *shader,
                 std::size_t shader_size) {
    return context_.CreateComputePipeline(root, {shader, shader_size});
  }

  ComPtr<ID3D12Resource>
  CreateInitializedBuffer(const void *data, UINT64 size,
                          D3D12_RESOURCE_FLAGS flags) {
    auto upload = context_.CreateUploadBuffer(size, data, size);
    auto buffer = context_.CreateBuffer(size, D3D12_HEAP_TYPE_DEFAULT, flags,
                                        D3D12_RESOURCE_STATE_COPY_DEST);
    EXPECT_TRUE(upload);
    EXPECT_TRUE(buffer);
    if (!upload || !buffer)
      return {};
    context_.list()->CopyBufferRegion(buffer.get(), 0, upload.get(), 0, size);
    uploads_.push_back(std::move(upload));
    return buffer;
  }

  void TransitionToUav(ID3D12Resource *resource) {
    D3D12TestContext::Transition(context_.list(), resource,
                                 D3D12_RESOURCE_STATE_COPY_DEST,
                                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  }

  void Dispatch(ID3D12RootSignature *root, ID3D12PipelineState *pipeline,
                ID3D12DescriptorHeap *heap) {
    ID3D12DescriptorHeap *heaps[] = {heap};
    context_.list()->SetDescriptorHeaps(1, heaps);
    context_.list()->SetComputeRootSignature(root);
    context_.list()->SetPipelineState(pipeline);
    context_.list()->SetComputeRootDescriptorTable(
        0, heap->GetGPUDescriptorHandleForHeapStart());
    context_.list()->Dispatch(1, 1, 1);
  }

  void RunAppendCase(UINT64 counter_offset,
                     bool counter_in_data_resource = false) {
    constexpr UINT kSentinel = 0xaaaaaaaau;
    constexpr std::array<UINT, 15> kInitialData = {
        kSentinel, kSentinel, kSentinel, kSentinel, kSentinel,
        kSentinel, kSentinel, kSentinel, kSentinel, kSentinel,
        kSentinel, kSentinel, kSentinel, kSentinel, kSentinel};
    const UINT64 counter_size = counter_offset + sizeof(UINT);
    std::vector<std::uint8_t> initial_counter(counter_size);
    constexpr UINT kOffsetZeroMarker = 0x13579bdfu;
    constexpr UINT kInitialCounter = 1;
    std::memcpy(initial_counter.data(), &kOffsetZeroMarker,
                sizeof(kOffsetZeroMarker));
    std::memcpy(initial_counter.data() + counter_offset, &kInitialCounter,
                sizeof(kInitialCounter));

    auto root = CreateRootSignature(1);
    auto pipeline =
        CreatePipeline(root.get(), kAppendShader, sizeof(kAppendShader));
    const UINT64 data_size = counter_in_data_resource
                                 ? counter_size
                                 : sizeof(kInitialData);
    std::vector<std::uint8_t> initial_data(data_size);
    std::memcpy(initial_data.data(), kInitialData.data(),
                sizeof(kInitialData));
    if (counter_in_data_resource) {
      std::memcpy(initial_data.data() + counter_offset, &kInitialCounter,
                  sizeof(kInitialCounter));
    }
    auto data = CreateInitializedBuffer(
        initial_data.data(), initial_data.size(),
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    ComPtr<ID3D12Resource> counter;
    if (!counter_in_data_resource) {
      counter = CreateInitializedBuffer(
          initial_counter.data(), initial_counter.size(),
          D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    }
    auto heap = context_.CreateDescriptorHeap(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true);
    ASSERT_TRUE(root);
    ASSERT_TRUE(pipeline);
    ASSERT_TRUE(data);
    ASSERT_TRUE(counter_in_data_resource || counter);
    ASSERT_TRUE(heap);

    D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
    uav.Format = DXGI_FORMAT_UNKNOWN;
    uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uav.Buffer.NumElements = kInitialData.size() / 3;
    uav.Buffer.StructureByteStride = 3 * sizeof(UINT);
    uav.Buffer.CounterOffsetInBytes = counter_offset;
    context_.device()->CreateUnorderedAccessView(
        data.get(), counter_in_data_resource ? data.get() : counter.get(), &uav,
        heap->GetCPUDescriptorHandleForHeapStart());
    TransitionToUav(data.get());
    if (!counter_in_data_resource)
      TransitionToUav(counter.get());
    Dispatch(root.get(), pipeline.get(), heap.get());
    D3D12TestContext::Transition(
        context_.list(), data.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_SOURCE);
    if (!counter_in_data_resource) {
      D3D12TestContext::Transition(
          context_.list(), counter.get(),
          D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
          D3D12_RESOURCE_STATE_COPY_SOURCE);
    }

    auto data_readback = context_.CreateBuffer(
        data_size, D3D12_HEAP_TYPE_READBACK,
        D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
    ComPtr<ID3D12Resource> counter_readback;
    if (!counter_in_data_resource) {
      counter_readback = context_.CreateBuffer(
          counter_size, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE,
          D3D12_RESOURCE_STATE_COPY_DEST);
    }
    ASSERT_TRUE(data_readback);
    ASSERT_TRUE(counter_in_data_resource || counter_readback);
    context_.list()->CopyBufferRegion(data_readback.get(), 0, data.get(), 0,
                                      data_size);
    if (!counter_in_data_resource) {
      context_.list()->CopyBufferRegion(
          counter_readback.get(), 0, counter.get(), 0, counter_size);
    }
    ASSERT_EQ(context_.ExecuteAndWait(), S_OK);

    std::vector<std::uint8_t> data_bytes;
    ASSERT_EQ(MapReadbackBuffer(data_readback.get(), data_size, &data_bytes),
              S_OK);
    EXPECT_EQ(ReadUint(data_bytes, 0), kSentinel);
    EXPECT_EQ(ReadUint(data_bytes, sizeof(UINT)), kSentinel);
    EXPECT_EQ(ReadUint(data_bytes, 2 * sizeof(UINT)), kSentinel);
    const std::array<UINT, 9> expected_appended = {4, 2, 1, 4, 1,
                                                   1, 3, 1, 1};
    for (std::size_t i = 0; i < expected_appended.size(); ++i) {
      EXPECT_EQ(ReadUint(data_bytes, (i + 3) * sizeof(UINT)),
                expected_appended[i])
          << "component=" << i;
    }
    EXPECT_EQ(ReadUint(data_bytes, 12 * sizeof(UINT)), kSentinel);
    EXPECT_EQ(ReadUint(data_bytes, 13 * sizeof(UINT)), kSentinel);
    EXPECT_EQ(ReadUint(data_bytes, 14 * sizeof(UINT)), kSentinel);

    std::vector<std::uint8_t> counter_bytes;
    if (counter_in_data_resource) {
      counter_bytes = data_bytes;
    } else {
      ASSERT_EQ(MapReadbackBuffer(counter_readback.get(), counter_size,
                                  &counter_bytes),
                S_OK);
    }
    EXPECT_EQ(ReadUint(counter_bytes, counter_offset), 4u);
    if (counter_offset && !counter_in_data_resource) {
      EXPECT_EQ(ReadUint(counter_bytes, 0), kOffsetZeroMarker);
    }
  }

  D3D12TestContext context_;
  std::vector<ComPtr<ID3D12Resource>> uploads_;
};

TEST_F(D3D12UavCounterSpec, AppendUsesInitialCounterAndUpdatesResource) {
  RunAppendCase(0);
}

TEST_F(D3D12UavCounterSpec, AppendHonorsNonzeroAlignedCounterOffset) {
  RunAppendCase(D3D12_UAV_COUNTER_PLACEMENT_ALIGNMENT);
}

TEST_F(D3D12UavCounterSpec, AppendSupportsCounterInDataResource) {
  RunAppendCase(D3D12_UAV_COUNTER_PLACEMENT_ALIGNMENT, true);
}

TEST_F(D3D12UavCounterSpec, DecrementCounterCopiesAllValuesAndReachesZero) {
  constexpr std::array<UINT, 4> kInitialData = {10, 20, 30, 40};
  constexpr UINT kInitialCounter = 4;
  constexpr std::array<UINT, 4> kInitialOutput = {};
  auto root = CreateRootSignature(2);
  auto pipeline =
      CreatePipeline(root.get(), kDecrementShader, sizeof(kDecrementShader));
  auto data = CreateInitializedBuffer(
      kInitialData.data(), sizeof(kInitialData),
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
  auto counter = CreateInitializedBuffer(
      &kInitialCounter, sizeof(kInitialCounter),
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
  auto output = CreateInitializedBuffer(
      kInitialOutput.data(), sizeof(kInitialOutput),
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
  auto heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, true);
  ASSERT_TRUE(root);
  ASSERT_TRUE(pipeline);
  ASSERT_TRUE(data);
  ASSERT_TRUE(counter);
  ASSERT_TRUE(output);
  ASSERT_TRUE(heap);

  D3D12_UNORDERED_ACCESS_VIEW_DESC counter_uav = {};
  counter_uav.Format = DXGI_FORMAT_UNKNOWN;
  counter_uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  counter_uav.Buffer.NumElements = kInitialData.size();
  counter_uav.Buffer.StructureByteStride = sizeof(UINT);
  context_.device()->CreateUnorderedAccessView(
      data.get(), counter.get(), &counter_uav,
      context_.CpuDescriptorHandle(heap.get(), 0));
  D3D12_UNORDERED_ACCESS_VIEW_DESC output_uav = {};
  output_uav.Format = DXGI_FORMAT_UNKNOWN;
  output_uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  output_uav.Buffer.NumElements = kInitialOutput.size();
  output_uav.Buffer.StructureByteStride = sizeof(UINT);
  context_.device()->CreateUnorderedAccessView(
      output.get(), nullptr, &output_uav,
      context_.CpuDescriptorHandle(heap.get(), 1));
  TransitionToUav(data.get());
  TransitionToUav(counter.get());
  TransitionToUav(output.get());
  Dispatch(root.get(), pipeline.get(), heap.get());
  D3D12TestContext::Transition(
      context_.list(), counter.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_COPY_SOURCE);
  D3D12TestContext::Transition(
      context_.list(), output.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_COPY_SOURCE);

  auto output_readback = context_.CreateBuffer(
      sizeof(kInitialOutput), D3D12_HEAP_TYPE_READBACK,
      D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
  auto counter_readback = context_.CreateBuffer(
      sizeof(kInitialCounter), D3D12_HEAP_TYPE_READBACK,
      D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(output_readback);
  ASSERT_TRUE(counter_readback);
  context_.list()->CopyBufferRegion(output_readback.get(), 0, output.get(), 0,
                                    sizeof(kInitialOutput));
  context_.list()->CopyBufferRegion(counter_readback.get(), 0, counter.get(), 0,
                                    sizeof(kInitialCounter));
  ASSERT_EQ(context_.ExecuteAndWait(), S_OK);

  std::vector<std::uint8_t> output_bytes;
  ASSERT_EQ(MapReadbackBuffer(output_readback.get(), sizeof(kInitialOutput),
                              &output_bytes),
            S_OK);
  for (std::size_t i = 0; i < kInitialData.size(); ++i) {
    EXPECT_EQ(ReadUint(output_bytes, i * sizeof(UINT)), kInitialData[i])
        << "element=" << i;
  }
  std::vector<std::uint8_t> counter_bytes;
  ASSERT_EQ(MapReadbackBuffer(counter_readback.get(), sizeof(kInitialCounter),
                              &counter_bytes),
            S_OK);
  EXPECT_EQ(ReadUint(counter_bytes, 0), 0u);
}

enum class InvalidUavCounterKind {
  MisalignedOffset,
  OutOfRangeOffset,
  OffsetWithoutResource,
  TextureCounter,
  MissingDescription,
};

struct InvalidUavCounterCase {
  InvalidUavCounterKind kind;
  const char *name;
};

class D3D12InvalidUavCounterSpec
    : public D3D12UavCounterSpec,
      public ::testing::WithParamInterface<InvalidUavCounterCase> {};

TEST_P(D3D12InvalidUavCounterSpec, InvalidDescriptorMaterializesAsInertUav) {
  constexpr UINT kSentinel = 0x13579bdfu;
  const auto shader = CompileShader(R"(
    RWStructuredBuffer<uint> output : register(u0);
    [numthreads(1, 1, 1)]
    void main() {
      output[0] = 0xdeadbeef;
    }
  )", "cs_5_0");
  ASSERT_EQ(shader.result, S_OK) << shader.diagnostic_text();

  auto root = CreateRootSignature(1);
  auto pipeline = context_.CreateComputePipeline(
      root.get(), {shader.bytecode->GetBufferPointer(),
                   shader.bytecode->GetBufferSize()});
  auto data = CreateInitializedBuffer(
      &kSentinel, sizeof(kSentinel),
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
  constexpr UINT64 kCounterSize =
      D3D12_UAV_COUNTER_PLACEMENT_ALIGNMENT + sizeof(UINT);
  std::vector<std::uint8_t> counter_data(kCounterSize);
  auto counter = CreateInitializedBuffer(
      counter_data.data(), counter_data.size(),
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
  auto texture_counter = context_.CreateTexture2D(
      1, 1, 1, DXGI_FORMAT_R32_UINT,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  auto heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true);
  ASSERT_TRUE(root);
  ASSERT_TRUE(pipeline);
  ASSERT_TRUE(data);
  ASSERT_TRUE(counter);
  ASSERT_TRUE(texture_counter);
  ASSERT_TRUE(heap);

  D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
  uav.Format = DXGI_FORMAT_UNKNOWN;
  uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav.Buffer.NumElements = 1;
  uav.Buffer.StructureByteStride = sizeof(UINT);
  ID3D12Resource *counter_resource = counter.get();
  const D3D12_UNORDERED_ACCESS_VIEW_DESC *uav_desc = &uav;
  switch (GetParam().kind) {
  case InvalidUavCounterKind::MisalignedOffset:
    uav.Buffer.CounterOffsetInBytes = sizeof(UINT);
    break;
  case InvalidUavCounterKind::OutOfRangeOffset:
    uav.Buffer.CounterOffsetInBytes =
        2 * D3D12_UAV_COUNTER_PLACEMENT_ALIGNMENT;
    break;
  case InvalidUavCounterKind::OffsetWithoutResource:
    uav.Buffer.CounterOffsetInBytes =
        D3D12_UAV_COUNTER_PLACEMENT_ALIGNMENT;
    counter_resource = nullptr;
    break;
  case InvalidUavCounterKind::TextureCounter:
    counter_resource = texture_counter.get();
    break;
  case InvalidUavCounterKind::MissingDescription:
    uav_desc = nullptr;
    break;
  }

  context_.device()->CreateUnorderedAccessView(
      data.get(), counter_resource, uav_desc,
      heap->GetCPUDescriptorHandleForHeapStart());
  TransitionToUav(data.get());
  Dispatch(root.get(), pipeline.get(), heap.get());
  D3D12TestContext::Transition(
      context_.list(), data.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_COPY_SOURCE);

  std::vector<std::uint8_t> bytes;
  ASSERT_EQ(context_.ReadbackBuffer(data.get(), sizeof(kSentinel), &bytes),
            S_OK);
  EXPECT_EQ(ReadUint(bytes, 0), kSentinel);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

std::string InvalidUavCounterCaseName(
    const ::testing::TestParamInfo<InvalidUavCounterCase> &info) {
  return info.param.name;
}

INSTANTIATE_TEST_SUITE_P(
    ValidationMatrix, D3D12InvalidUavCounterSpec,
    ::testing::Values(
        InvalidUavCounterCase{InvalidUavCounterKind::MisalignedOffset,
                              "MisalignedOffset"},
        InvalidUavCounterCase{InvalidUavCounterKind::OutOfRangeOffset,
                              "OutOfRangeOffset"},
        InvalidUavCounterCase{InvalidUavCounterKind::OffsetWithoutResource,
                              "OffsetWithoutResource"},
        InvalidUavCounterCase{InvalidUavCounterKind::TextureCounter,
                              "TextureCounter"},
        InvalidUavCounterCase{InvalidUavCounterKind::MissingDescription,
                              "MissingDescription"}),
    InvalidUavCounterCaseName);

} // namespace
