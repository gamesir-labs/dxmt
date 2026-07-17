#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <thread>
#include <vector>

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

class D3D12DeviceConcurrencySpec : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  D3D12TestContext context_;
};

TEST_F(D3D12DeviceConcurrencySpec,
       ParallelResourceDescriptorHeapAndFenceCreationIsStable) {
  constexpr unsigned int kThreadCount = 8;
  constexpr unsigned int kIterations = 16;
  std::atomic_uint failures = 0;
  std::vector<std::thread> threads;
  threads.reserve(kThreadCount);
  for (unsigned int thread_index = 0; thread_index < kThreadCount;
       ++thread_index) {
    threads.emplace_back([&, thread_index] {
      for (unsigned int iteration = 0; iteration < kIterations; ++iteration) {
        auto upload = context_.CreateBuffer(
            4096, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE,
            D3D12_RESOURCE_STATE_GENERIC_READ);
        auto heap = context_.CreateDescriptorHeap(
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 8, false);
        ComPtr<ID3D12Fence> fence;
        const UINT64 initial_value = thread_index * kIterations + iteration;
        const HRESULT fence_hr = context_.device()->CreateFence(
            initial_value, D3D12_FENCE_FLAG_NONE,
            __uuidof(ID3D12Fence), reinterpret_cast<void **>(fence.put()));
        if (!upload || !heap || FAILED(fence_hr) || !fence ||
            fence->GetCompletedValue() != initial_value) {
          ++failures;
          continue;
        }

        void *mapping = nullptr;
        const D3D12_RANGE no_read = {0, 0};
        if (FAILED(upload->Map(0, &no_read, &mapping)) || !mapping) {
          ++failures;
          continue;
        }
        const std::uint8_t pattern =
            static_cast<std::uint8_t>(thread_index * 17 + iteration);
        std::memset(mapping, pattern, 4096);
        const D3D12_RANGE written = {0, 4096};
        upload->Unmap(0, &written);
      }
    });
  }
  for (auto &thread : threads)
    thread.join();

  EXPECT_EQ(failures.load(), 0u);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D12DeviceConcurrencySpec,
       SeparateAllocatorsAndListsRecordAndCloseInParallel) {
  constexpr unsigned int kThreadCount = 8;
  constexpr unsigned int kIterations = 8;
  std::atomic_uint failures = 0;
  std::vector<std::thread> threads;
  threads.reserve(kThreadCount);
  for (unsigned int thread_index = 0; thread_index < kThreadCount;
       ++thread_index) {
    threads.emplace_back([&] {
      for (unsigned int iteration = 0; iteration < kIterations; ++iteration) {
        const std::array<std::uint32_t, 4> data = {
            iteration, iteration + 1, iteration + 2, iteration + 3};
        auto source = context_.CreateUploadBuffer(sizeof(data), data.data(),
                                                   sizeof(data));
        auto destination = context_.CreateBuffer(
            sizeof(data), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
            D3D12_RESOURCE_STATE_COPY_DEST);
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> list;
        const HRESULT allocator_hr =
            context_.device()->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                __uuidof(ID3D12CommandAllocator),
                reinterpret_cast<void **>(allocator.put()));
        const HRESULT list_hr = SUCCEEDED(allocator_hr)
                                    ? context_.device()->CreateCommandList(
                                          0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                          allocator.get(), nullptr,
                                          __uuidof(ID3D12GraphicsCommandList),
                                          reinterpret_cast<void **>(list.put()))
                                    : allocator_hr;
        if (!source || !destination || FAILED(list_hr) || !list) {
          ++failures;
          continue;
        }

        list->CopyBufferRegion(destination.get(), 0, source.get(), 0,
                               sizeof(data));
        D3D12TestContext::Transition(
            list.get(), destination.get(), D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_COPY_SOURCE);
        if (FAILED(list->Close()))
          ++failures;
      }
    });
  }
  for (auto &thread : threads)
    thread.join();

  EXPECT_EQ(failures.load(), 0u);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D12DeviceConcurrencySpec,
       ParallelRootSignatureAndQueryHeapCreationIsStable) {
  constexpr unsigned int kThreadCount = 8;
  constexpr unsigned int kIterations = 16;
  const D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  std::atomic_uint failures = 0;
  std::vector<std::thread> threads;
  threads.reserve(kThreadCount);
  for (unsigned int thread_index = 0; thread_index < kThreadCount;
       ++thread_index) {
    threads.emplace_back([&] {
      for (unsigned int iteration = 0; iteration < kIterations; ++iteration) {
        auto root = context_.CreateRootSignature(root_desc);
        D3D12_QUERY_HEAP_DESC query_desc = {};
        query_desc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        query_desc.Count = 4;
        ComPtr<ID3D12QueryHeap> query;
        const HRESULT query_hr = context_.device()->CreateQueryHeap(
            &query_desc, __uuidof(ID3D12QueryHeap),
            reinterpret_cast<void **>(query.put()));
        if (!root || FAILED(query_hr) || !query)
          ++failures;
      }
    });
  }
  for (auto &thread : threads)
    thread.join();

  EXPECT_EQ(failures.load(), 0u);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D12DeviceConcurrencySpec,
       ConcurrentFenceEventRegistrationsAllSignal) {
  constexpr unsigned int kEventCount = 8;
  ComPtr<ID3D12Fence> fence;
  ASSERT_EQ(context_.device()->CreateFence(
                0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence),
                reinterpret_cast<void **>(fence.put())),
            S_OK);
  ASSERT_TRUE(fence);

  std::array<HANDLE, kEventCount> events = {};
  for (auto &event : events) {
    event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ASSERT_NE(event, nullptr);
  }

  std::array<HRESULT, kEventCount> registrations = {};
  std::vector<std::thread> threads;
  threads.reserve(kEventCount);
  for (unsigned int index = 0; index < kEventCount; ++index) {
    threads.emplace_back([&, index] {
      registrations[index] =
          fence->SetEventOnCompletion(index + 1, events[index]);
    });
  }
  for (auto &thread : threads)
    thread.join();
  for (const HRESULT registration : registrations)
    EXPECT_EQ(registration, S_OK);

  ASSERT_EQ(fence->Signal(kEventCount), S_OK);
  EXPECT_EQ(WaitForMultipleObjects(kEventCount, events.data(), TRUE, 5000),
            WAIT_OBJECT_0);
  for (const HANDLE event : events)
    CloseHandle(event);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
