#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

#include <array>

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

class UavBarrierSpec : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_TRUE(SUCCEEDED(context_.Initialize())); }

  void ExpectOrderedClears(UINT resource_count, bool global_barrier) {
    constexpr UINT element_count = 64;
    constexpr UINT first_value = 0x11223344;
    constexpr UINT second_value = 0xaabbccdd;
    constexpr UINT64 buffer_size = element_count * sizeof(UINT);
    std::array<ComPtr<ID3D12Resource>, 2> resources;
    ASSERT_LE(resource_count, resources.size());

    auto heap = context_.CreateDescriptorHeap(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2 * resource_count, true);
    auto readback = context_.CreateBuffer(
        resource_count * buffer_size, D3D12_HEAP_TYPE_READBACK,
        D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
    ASSERT_TRUE(heap);
    ASSERT_TRUE(readback);

    D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
    uav.Format = DXGI_FORMAT_R32_UINT;
    uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uav.Buffer.NumElements = element_count;
    for (UINT index = 0; index < resource_count; ++index) {
      resources[index] =
          context_.CreateBuffer(buffer_size, D3D12_HEAP_TYPE_DEFAULT,
                                D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
      ASSERT_TRUE(resources[index]);
      context_.device()->CreateUnorderedAccessView(
          resources[index].get(), nullptr, &uav,
          context_.CpuDescriptorHandle(heap.get(), 2 * index));
      uav.Buffer.FirstElement = element_count / 2;
      uav.Buffer.NumElements = element_count / 2;
      context_.device()->CreateUnorderedAccessView(
          resources[index].get(), nullptr, &uav,
          context_.CpuDescriptorHandle(heap.get(), 2 * index + 1));
      uav.Buffer.FirstElement = 0;
      uav.Buffer.NumElements = element_count;
    }

    ID3D12DescriptorHeap *heaps[] = {heap.get()};
    context_.list()->SetDescriptorHeaps(1, heaps);
    const std::array<UINT, 4> first_clear = {first_value, 0, 0, 0};
    const std::array<UINT, 4> second_clear = {second_value, 0, 0, 0};
    for (UINT index = 0; index < resource_count; ++index) {
      context_.list()->ClearUnorderedAccessViewUint(
          context_.GpuDescriptorHandle(heap.get(), 2 * index),
          context_.CpuDescriptorHandle(heap.get(), 2 * index),
          resources[index].get(), first_clear.data(), 0, nullptr);
    }
    D3D12TestContext::UavBarrier(context_.list(),
                                 global_barrier ? nullptr : resources[0].get());
    for (UINT index = 0; index < resource_count; ++index) {
      context_.list()->ClearUnorderedAccessViewUint(
          context_.GpuDescriptorHandle(heap.get(), 2 * index + 1),
          context_.CpuDescriptorHandle(heap.get(), 2 * index + 1),
          resources[index].get(), second_clear.data(), 0, nullptr);
      D3D12TestContext::Transition(context_.list(), resources[index].get(),
                                   D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                   D3D12_RESOURCE_STATE_COPY_SOURCE);
      context_.list()->CopyBufferRegion(readback.get(), index * buffer_size,
                                        resources[index].get(), 0, buffer_size);
    }
    ASSERT_TRUE(SUCCEEDED(context_.ExecuteAndWait()));

    UINT *mapping = nullptr;
    const D3D12_RANGE read_range = {
        0, static_cast<SIZE_T>(resource_count * buffer_size)};
    ASSERT_TRUE(SUCCEEDED(
        readback->Map(0, &read_range, reinterpret_cast<void **>(&mapping))));
    for (UINT resource = 0; resource < resource_count; ++resource) {
      for (UINT element = 0; element < element_count; ++element) {
        const UINT expected =
            element < element_count / 2 ? first_value : second_value;
        EXPECT_EQ(mapping[resource * element_count + element], expected)
            << "resource " << resource << ", element " << element;
      }
    }
    const D3D12_RANGE no_write = {0, 0};
    readback->Unmap(0, &no_write);
  }

  D3D12TestContext context_;
};

TEST_F(UavBarrierSpec, ResourceBarrierOrdersWrites) {
  ExpectOrderedClears(1, false);
}

TEST_F(UavBarrierSpec, GlobalBarrierOrdersMultipleResources) {
  ExpectOrderedClears(2, true);
}

} // namespace
