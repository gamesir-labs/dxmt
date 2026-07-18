#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <numeric>
#include <vector>

namespace {

using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

// Precompiled SM5 DXBC from Wine's atomic-instruction conformance test. It
// applies And, CompareExchange, Add, Or, signed/unsigned Min/Max, and Xor to
// raw UAV u0, then stores every original value to raw UAV u1.
constexpr DWORD kRawAtomicShader[] = {
    0x43425844, 0x859a96e3, 0x1a35e463, 0x1e89ce58, 0x5cfe430a,
    0x00000001, 0x0000026c, 0x00000003, 0x0000002c, 0x0000003c,
    0x0000004c, 0x4e475349, 0x00000008, 0x00000000, 0x00000008,
    0x4e47534f, 0x00000008, 0x00000000, 0x00000008, 0x58454853,
    0x00000218, 0x00050050, 0x00000086, 0x0100086a, 0x04000059,
    0x00208e46, 0x00000000, 0x00000002, 0x0300009d, 0x0011e000,
    0x00000000, 0x0300009d, 0x0011e000, 0x00000001, 0x02000068,
    0x00000001, 0x0400009b, 0x00000001, 0x00000001, 0x00000001,
    0x0a0000b5, 0x00100012, 0x00000000, 0x0011e000, 0x00000000,
    0x00004001, 0x00000000, 0x0020800a, 0x00000000, 0x00000000,
    0x0d0000b9, 0x00100022, 0x00000000, 0x0011e000, 0x00000000,
    0x00004001, 0x00000004, 0x0020801a, 0x00000000, 0x00000000,
    0x0020800a, 0x00000000, 0x00000000, 0x0a0000b4, 0x00100042,
    0x00000000, 0x0011e000, 0x00000000, 0x00004001, 0x00000008,
    0x0020800a, 0x00000000, 0x00000000, 0x0a0000b6, 0x00100082,
    0x00000000, 0x0011e000, 0x00000000, 0x00004001, 0x0000000c,
    0x0020800a, 0x00000000, 0x00000000, 0x070000a6, 0x0011e0f2,
    0x00000001, 0x00004001, 0x00000000, 0x00100e46, 0x00000000,
    0x0a0000ba, 0x00100012, 0x00000000, 0x0011e000, 0x00000000,
    0x00004001, 0x00000010, 0x0020800a, 0x00000000, 0x00000001,
    0x0a0000bb, 0x00100022, 0x00000000, 0x0011e000, 0x00000000,
    0x00004001, 0x00000014, 0x0020800a, 0x00000000, 0x00000001,
    0x0a0000bc, 0x00100042, 0x00000000, 0x0011e000, 0x00000000,
    0x00004001, 0x00000018, 0x0020800a, 0x00000000, 0x00000000,
    0x0a0000bd, 0x00100082, 0x00000000, 0x0011e000, 0x00000000,
    0x00004001, 0x0000001c, 0x0020800a, 0x00000000, 0x00000000,
    0x070000a6, 0x0011e0f2, 0x00000001, 0x00004001, 0x00000010,
    0x00100e46, 0x00000000, 0x0a0000b7, 0x00100012, 0x00000000,
    0x0011e000, 0x00000000, 0x00004001, 0x00000020, 0x0020800a,
    0x00000000, 0x00000000, 0x070000a6, 0x0011e012, 0x00000001,
    0x00004001, 0x00000020, 0x0010000a, 0x00000000, 0x0100003e,
};

class D3D12AtomicMatrixSpec : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  ComPtr<ID3D12RootSignature>
  CreateRootSignature(UINT uav_count = 1, UINT root_constant_count = 0) {
    D3D12_DESCRIPTOR_RANGE range = {};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    range.NumDescriptors = uav_count;
    D3D12_ROOT_PARAMETER parameters[2] = {};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[0].DescriptorTable.NumDescriptorRanges = 1;
    parameters[0].DescriptorTable.pDescriptorRanges = &range;
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[1].Constants.ShaderRegister = 0;
    parameters[1].Constants.Num32BitValues = root_constant_count;
    D3D12_ROOT_SIGNATURE_DESC desc = {};
    desc.NumParameters = root_constant_count ? 2 : 1;
    desc.pParameters = parameters;
    return context_.CreateRootSignature(desc);
  }

  ComPtr<ID3D12PipelineState> CreatePipeline(ID3D12RootSignature *root,
                                              const char *source) {
    const auto shader = CompileShader(source, "cs_5_0");
    EXPECT_EQ(shader.result, S_OK) << shader.diagnostic_text();
    if (shader.result != S_OK)
      return {};
    const D3D12_SHADER_BYTECODE bytecode = {
        shader.bytecode->GetBufferPointer(), shader.bytecode->GetBufferSize()};
    return context_.CreateComputePipeline(root, bytecode);
  }

  ComPtr<ID3D12Resource>
  CreateInitializedBuffer(const std::vector<UINT> &initial) {
    const UINT64 size = initial.size() * sizeof(UINT);
    auto upload = context_.CreateUploadBuffer(size, initial.data(), size);
    auto buffer = context_.CreateBuffer(
        size, D3D12_HEAP_TYPE_DEFAULT,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_DEST);
    EXPECT_TRUE(upload);
    EXPECT_TRUE(buffer);
    if (!upload || !buffer)
      return {};
    context_.list()->CopyBufferRegion(buffer.get(), 0, upload.get(), 0, size);
    D3D12TestContext::Transition(
        context_.list(), buffer.get(), D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    uploads_.push_back(std::move(upload));
    return buffer;
  }

  void Bind(ID3D12GraphicsCommandList *list, ID3D12RootSignature *root,
            ID3D12PipelineState *pipeline, ID3D12DescriptorHeap *heap) {
    ID3D12DescriptorHeap *heaps[] = {heap};
    list->SetDescriptorHeaps(1, heaps);
    list->SetComputeRootSignature(root);
    list->SetPipelineState(pipeline);
    list->SetComputeRootDescriptorTable(
        0, heap->GetGPUDescriptorHandleForHeapStart());
  }

  std::vector<UINT> Readback(ID3D12Resource *buffer, UINT word_count) {
    D3D12TestContext::Transition(
        context_.list(), buffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_SOURCE);
    std::vector<std::uint8_t> bytes;
    EXPECT_EQ(context_.ReadbackBuffer(buffer, word_count * sizeof(UINT), &bytes),
              S_OK);
    std::vector<UINT> words(word_count);
    if (bytes.size() == words.size() * sizeof(UINT))
      std::memcpy(words.data(), bytes.data(), bytes.size());
    return words;
  }

  D3D12TestContext context_;
  std::vector<ComPtr<ID3D12Resource>> uploads_;
};

TEST_F(D3D12AtomicMatrixSpec,
       MultiGroupContentionReturnsEachOriginalValueExactlyOnce) {
  constexpr char kShader[] = R"(
    RWStructuredBuffer<uint> target : register(u0);
    RWByteAddressBuffer originals : register(u1);
    [numthreads(8, 1, 1)]
    void main(uint3 id : SV_DispatchThreadID) {
      uint original;
      InterlockedAdd(target[0], 1u, original);
      originals.Store(id.x * 4u, original);
    }
  )";
  constexpr UINT kThreadCount = 64;
  auto root = CreateRootSignature(2);
  auto pipeline = CreatePipeline(root.get(), kShader);
  auto target = CreateInitializedBuffer(std::vector<UINT>(kThreadCount + 1));
  auto heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, true);
  ASSERT_TRUE(root);
  ASSERT_TRUE(pipeline);
  ASSERT_TRUE(target);
  ASSERT_TRUE(heap);

  D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
  uav.Format = DXGI_FORMAT_UNKNOWN;
  uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav.Buffer.NumElements = 1;
  uav.Buffer.StructureByteStride = sizeof(UINT);
  context_.device()->CreateUnorderedAccessView(
      target.get(), nullptr, &uav,
      context_.CpuDescriptorHandle(heap.get(), 0));
  uav = {};
  uav.Format = DXGI_FORMAT_R32_TYPELESS;
  uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav.Buffer.FirstElement = 1;
  uav.Buffer.NumElements = kThreadCount;
  uav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
  context_.device()->CreateUnorderedAccessView(
      target.get(), nullptr, &uav,
      context_.CpuDescriptorHandle(heap.get(), 1));
  Bind(context_.list(), root.get(), pipeline.get(), heap.get());
  context_.list()->Dispatch(kThreadCount / 8, 1, 1);

  auto actual = Readback(target.get(), kThreadCount + 1);
  ASSERT_EQ(actual.size(), kThreadCount + 1);
  EXPECT_EQ(actual[0], kThreadCount);
  std::vector<UINT> original_values(actual.begin() + 1, actual.end());
  std::sort(original_values.begin(), original_values.end());
  std::vector<UINT> expected(kThreadCount);
  std::iota(expected.begin(), expected.end(), 0u);
  EXPECT_EQ(original_values, expected);
}

TEST_F(D3D12AtomicMatrixSpec, TypedUavAtomicMatchesStructuredSum) {
  constexpr char kShader[] = R"(
    RWBuffer<uint> target : register(u0);
    [numthreads(32, 1, 1)]
    void main(uint3 id : SV_DispatchThreadID) {
      uint original;
      InterlockedAdd(target[0], id.x + 1u, original);
    }
  )";
  auto root = CreateRootSignature();
  auto pipeline = CreatePipeline(root.get(), kShader);
  auto target = CreateInitializedBuffer({0u});
  auto heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true);
  ASSERT_TRUE(root);
  ASSERT_TRUE(pipeline);
  ASSERT_TRUE(target);
  ASSERT_TRUE(heap);
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
  uav.Format = DXGI_FORMAT_R32_UINT;
  uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav.Buffer.NumElements = 1;
  context_.device()->CreateUnorderedAccessView(
      target.get(), nullptr, &uav,
      heap->GetCPUDescriptorHandleForHeapStart());
  Bind(context_.list(), root.get(), pipeline.get(), heap.get());
  context_.list()->Dispatch(1, 1, 1);
  EXPECT_EQ(Readback(target.get(), 1), (std::vector<UINT>{528u}));
}

TEST_F(D3D12AtomicMatrixSpec,
       PrecompiledRawUavMatrixReturnsOriginalValues) {
  constexpr std::array<UINT, 9> kInitial = {
      0x0000f0f0u, 5u, 7u, 8u, 0xfffffffbu, 5u, 2u, 4u, 0xau};
  constexpr std::array<UINT, 9> kExpected = {
      0u, 3u, 10u, 11u, 0xfffffffeu, 0xfffffffeu, 3u, 3u, 9u};
  constexpr std::array<UINT, 8> kConstants = {
      3u, 5u, 0u, 0u, 0xfffffffeu, 0u, 0u, 0u};
  auto root = CreateRootSignature(2, static_cast<UINT>(kConstants.size()));
  const D3D12_SHADER_BYTECODE bytecode = {kRawAtomicShader,
                                          sizeof(kRawAtomicShader)};
  auto pipeline = context_.CreateComputePipeline(root.get(), bytecode);
  auto target = CreateInitializedBuffer(
      std::vector<UINT>(kInitial.begin(), kInitial.end()));
  auto originals = CreateInitializedBuffer(std::vector<UINT>(kInitial.size()));
  auto heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, true);
  ASSERT_TRUE(root);
  ASSERT_TRUE(pipeline);
  ASSERT_TRUE(target);
  ASSERT_TRUE(originals);
  ASSERT_TRUE(heap);

  D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
  uav.Format = DXGI_FORMAT_R32_TYPELESS;
  uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav.Buffer.NumElements = static_cast<UINT>(kInitial.size());
  uav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
  context_.device()->CreateUnorderedAccessView(
      target.get(), nullptr, &uav,
      context_.CpuDescriptorHandle(heap.get(), 0));
  context_.device()->CreateUnorderedAccessView(
      originals.get(), nullptr, &uav,
      context_.CpuDescriptorHandle(heap.get(), 1));
  Bind(context_.list(), root.get(), pipeline.get(), heap.get());
  context_.list()->SetComputeRoot32BitConstants(
      1, static_cast<UINT>(kConstants.size()), kConstants.data(), 0);
  context_.list()->Dispatch(1, 1, 1);
  D3D12TestContext::Transition(
      context_.list(), originals.get(),
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_COPY_SOURCE);
  D3D12TestContext::Transition(
      context_.list(), target.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_COPY_SOURCE);
  auto target_readback = context_.CreateBuffer(
      sizeof(kExpected), D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  auto original_readback = context_.CreateBuffer(
      sizeof(kInitial), D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(target_readback);
  ASSERT_TRUE(original_readback);
  context_.list()->CopyBufferRegion(target_readback.get(), 0, target.get(), 0,
                                    sizeof(kExpected));
  context_.list()->CopyBufferRegion(original_readback.get(), 0,
                                    originals.get(), 0, sizeof(kInitial));
  ASSERT_EQ(context_.ExecuteAndWait(), S_OK);

  void *target_data = nullptr;
  void *original_data = nullptr;
  const D3D12_RANGE read_range = {0, sizeof(kInitial)};
  ASSERT_EQ(target_readback->Map(0, &read_range, &target_data), S_OK);
  ASSERT_EQ(original_readback->Map(0, &read_range, &original_data), S_OK);
  std::array<UINT, kInitial.size()> actual = {};
  std::array<UINT, kInitial.size()> actual_originals = {};
  std::memcpy(actual.data(), target_data, sizeof(actual));
  std::memcpy(actual_originals.data(), original_data,
              sizeof(actual_originals));
  const D3D12_RANGE no_writes = {0, 0};
  target_readback->Unmap(0, &no_writes);
  original_readback->Unmap(0, &no_writes);
  EXPECT_EQ(actual, kExpected);
  EXPECT_EQ(actual_originals, kInitial);
}

TEST_F(D3D12AtomicMatrixSpec,
       UavBarrierPreservesAtomicResultAcrossSeparateExecutes) {
  constexpr char kProducer[] = R"(
    RWStructuredBuffer<uint> target : register(u0);
    [numthreads(32, 1, 1)]
    void main(uint3 id : SV_DispatchThreadID) {
      uint original;
      InterlockedAdd(target[0], id.x + 1u, original);
    }
  )";
  constexpr char kConsumer[] = R"(
    RWStructuredBuffer<uint> target : register(u0);
    [numthreads(1, 1, 1)]
    void main() {
      uint value;
      InterlockedAdd(target[0], 0u, value);
      uint ignored;
      InterlockedExchange(target[1], value, ignored);
    }
  )";
  auto root = CreateRootSignature();
  auto producer = CreatePipeline(root.get(), kProducer);
  auto consumer = CreatePipeline(root.get(), kConsumer);
  auto target = CreateInitializedBuffer({0u, 0u});
  auto heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true);
  ASSERT_TRUE(root);
  ASSERT_TRUE(producer);
  ASSERT_TRUE(consumer);
  ASSERT_TRUE(target);
  ASSERT_TRUE(heap);
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
  uav.Format = DXGI_FORMAT_UNKNOWN;
  uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav.Buffer.NumElements = 2;
  uav.Buffer.StructureByteStride = sizeof(UINT);
  context_.device()->CreateUnorderedAccessView(
      target.get(), nullptr, &uav,
      heap->GetCPUDescriptorHandleForHeapStart());

  ComPtr<ID3D12CommandAllocator> second_allocator;
  ComPtr<ID3D12GraphicsCommandList> second_list;
  ASSERT_EQ(context_.device()->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                __uuidof(ID3D12CommandAllocator),
                reinterpret_cast<void **>(second_allocator.put())),
            S_OK);
  ASSERT_EQ(context_.device()->CreateCommandList(
                0, D3D12_COMMAND_LIST_TYPE_DIRECT, second_allocator.get(),
                nullptr, __uuidof(ID3D12GraphicsCommandList),
                reinterpret_cast<void **>(second_list.put())),
            S_OK);

  Bind(context_.list(), root.get(), producer.get(), heap.get());
  context_.list()->Dispatch(1, 1, 1);
  D3D12TestContext::UavBarrier(second_list.get(), target.get());
  Bind(second_list.get(), root.get(), consumer.get(), heap.get());
  second_list->Dispatch(1, 1, 1);
  ASSERT_EQ(context_.list()->Close(), S_OK);
  ASSERT_EQ(second_list->Close(), S_OK);

  ID3D12CommandList *first = context_.list();
  context_.queue()->ExecuteCommandLists(1, &first);
  ID3D12CommandList *second = second_list.get();
  context_.queue()->ExecuteCommandLists(1, &second);
  ASSERT_EQ(context_.SignalAndWait(), S_OK);
  ASSERT_EQ(context_.ResetCommandList(), S_OK);
  EXPECT_EQ(Readback(target.get(), 2), (std::vector<UINT>{528u, 528u}));
}

} // namespace
