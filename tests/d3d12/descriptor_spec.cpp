#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"
#include "shaders/runtime_test_shaders.hpp"

#include <array>
#include <atomic>
#include <barrier>
#include <chrono>
#include <cstring>
#include <limits>
#include <thread>
#include <vector>

namespace {

using dxmt::test::ColorsMatch;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;
using dxmt::test::DescriptorTablePixelShader;
using dxmt::test::CopyTextureComputeShader;
using dxmt::test::TextureReadback;
using dxmt::test::TextureUavPixelShader;

struct RenderTarget {
  ComPtr<ID3D12Resource> texture;
  ComPtr<ID3D12DescriptorHeap> heap;
  D3D12_CPU_DESCRIPTOR_HANDLE view = {};
  D3D12_VIEWPORT viewport = {};
  D3D12_RECT scissor = {};
};

RenderTarget CreateRenderTarget(D3D12TestContext &context) {
  RenderTarget target;
  target.texture = context.CreateTexture2D(
      32, 32, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
      D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
      D3D12_RESOURCE_STATE_RENDER_TARGET);
  target.heap = context.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1,
                                             false);
  if (!target.texture || !target.heap)
    return target;

  target.view = target.heap->GetCPUDescriptorHandleForHeapStart();
  context.device()->CreateRenderTargetView(target.texture.get(), nullptr,
                                           target.view);
  target.viewport.Width = 32.0f;
  target.viewport.Height = 32.0f;
  target.viewport.MaxDepth = 1.0f;
  target.scissor.right = 32;
  target.scissor.bottom = 32;
  return target;
}

void ExpectSolidColor(const TextureReadback &readback, std::uint32_t expected,
                      unsigned int max_channel_difference) {
  ASSERT_EQ(readback.width, 32u);
  ASSERT_EQ(readback.height, 32u);
  for (UINT y = 0; y < readback.height; ++y) {
    for (UINT x = 0; x < readback.width; ++x) {
      std::uint32_t actual = 0;
      std::memcpy(&actual,
                  readback.data.data() + y * readback.row_pitch +
                      x * sizeof(actual),
                  sizeof(actual));
      ASSERT_TRUE(ColorsMatch(actual, expected, max_channel_difference))
          << "pixel (" << x << ", " << y << ") was 0x" << std::hex
          << actual << ", expected 0x" << expected;
    }
  }
}

void ExpectSplitColor(const TextureReadback &readback,
                      std::uint32_t expected_left,
                      std::uint32_t expected_right,
                      unsigned int max_channel_difference) {
  ASSERT_EQ(readback.width, 32u);
  ASSERT_EQ(readback.height, 32u);
  for (UINT y = 0; y < readback.height; ++y) {
    for (UINT x = 0; x < readback.width; ++x) {
      std::uint32_t actual = 0;
      std::memcpy(&actual,
                  readback.data.data() + y * readback.row_pitch +
                      x * sizeof(actual),
                  sizeof(actual));
      const std::uint32_t expected =
          x < readback.width / 2 ? expected_left : expected_right;
      ASSERT_TRUE(ColorsMatch(actual, expected, max_channel_difference))
          << "pixel (" << x << ", " << y << ") was 0x" << std::hex
          << actual << ", expected 0x" << expected;
    }
  }
}

class D3D12DescriptorSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_TRUE(SUCCEEDED(context_.Initialize()));
  }

  D3D12TestContext context_;
};

struct DescriptorTableDrawOptions {
  bool execute_indirect = false;
  bool test_occlusion_queries = false;
  UINT overwrite_count = 0;
  bool write_unused_slots_concurrently = false;
  bool write_other_heap_concurrently = false;
  bool copy_from_released_cpu_heaps = false;
  bool release_bound_heaps_after_submit = false;
};

void RunDescriptorTableDraw(
    D3D12TestContext &context,
    const DescriptorTableDrawOptions &options = {}) {
  RenderTarget render_target = CreateRenderTarget(context);
  ASSERT_TRUE(render_target.texture);
  ASSERT_TRUE(render_target.heap);

  D3D12_DESCRIPTOR_RANGE ranges[4] = {};
  ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  ranges[0].NumDescriptors = 2;
  ranges[0].BaseShaderRegister = 0;
  ranges[0].OffsetInDescriptorsFromTableStart = 1;
  ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
  ranges[1].NumDescriptors = 1;
  ranges[1].BaseShaderRegister = 0;
  ranges[1].OffsetInDescriptorsFromTableStart =
      D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
  ranges[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  ranges[2].NumDescriptors = 2;
  ranges[2].BaseShaderRegister = 2;
  ranges[2].OffsetInDescriptorsFromTableStart = 0;
  ranges[3].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
  ranges[3].NumDescriptors = 1;
  ranges[3].BaseShaderRegister = 0;
  ranges[3].OffsetInDescriptorsFromTableStart =
      D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

  D3D12_ROOT_PARAMETER parameters[3] = {};
  parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[0].DescriptorTable.NumDescriptorRanges = 1;
  parameters[0].DescriptorTable.pDescriptorRanges = &ranges[0];
  parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[1].DescriptorTable.NumDescriptorRanges = 1;
  parameters[1].DescriptorTable.pDescriptorRanges = &ranges[1];
  parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[2].DescriptorTable.NumDescriptorRanges = 2;
  parameters[2].DescriptorTable.pDescriptorRanges = &ranges[2];
  parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  root_desc.NumParameters = 3;
  root_desc.pParameters = parameters;
  ComPtr<ID3D12RootSignature> root_signature =
      context.CreateRootSignature(root_desc);
  ASSERT_TRUE(root_signature);
  ComPtr<ID3D12PipelineState> pipeline = context.CreateGraphicsPipeline(
      root_signature.get(), DXGI_FORMAT_R8G8B8A8_UNORM,
      DescriptorTablePixelShader());
  ASSERT_TRUE(pipeline);

  std::array<std::uint8_t, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT>
      constant_data = {};
  const std::array<float, 4> constant = {0.1f, 0.2f, 0.3f, 0.1f};
  std::memcpy(constant_data.data(), constant.data(), sizeof(constant));
  ComPtr<ID3D12Resource> constant_buffer = context.CreateUploadBuffer(
      constant_data.size(), constant_data.data(), constant_data.size());
  ASSERT_TRUE(constant_buffer);
  ComPtr<ID3D12Resource> other_constant_buffer;
  if (options.write_other_heap_concurrently) {
    std::array<std::uint8_t, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT>
        other_constant_data = {};
    const std::array<float, 4> other_constant = {0.0f, 1.0f, 0.0f,
                                                 0.0f};
    std::memcpy(other_constant_data.data(), other_constant.data(),
                sizeof(other_constant));
    other_constant_buffer = context.CreateUploadBuffer(
        other_constant_data.size(), other_constant_data.data(),
        other_constant_data.size());
    ASSERT_TRUE(other_constant_buffer);
  }

  ComPtr<ID3D12CommandSignature> command_signature;
  ComPtr<ID3D12Resource> argument_buffer;
  if (options.execute_indirect) {
    D3D12_INDIRECT_ARGUMENT_DESC argument_desc = {};
    argument_desc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
    D3D12_COMMAND_SIGNATURE_DESC signature_desc = {};
    signature_desc.ByteStride = sizeof(D3D12_DRAW_ARGUMENTS);
    signature_desc.NumArgumentDescs = 1;
    signature_desc.pArgumentDescs = &argument_desc;
    HRESULT hr = context.device()->CreateCommandSignature(
        &signature_desc, nullptr, __uuidof(ID3D12CommandSignature),
        reinterpret_cast<void **>(command_signature.put()));
    ASSERT_TRUE(SUCCEEDED(hr));

    D3D12_DRAW_ARGUMENTS arguments = {};
    arguments.VertexCountPerInstance = 3;
    arguments.InstanceCount = 1;
    argument_buffer = context.CreateUploadBuffer(sizeof(arguments), &arguments,
                                                 sizeof(arguments));
    ASSERT_TRUE(argument_buffer);
  }

  const bool write_concurrently =
      options.write_unused_slots_concurrently ||
      options.write_other_heap_concurrently;
  const UINT resource_descriptor_count = write_concurrently ? 64 : 6;
  ComPtr<ID3D12DescriptorHeap> resource_heap = context.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, resource_descriptor_count,
      true);
  ComPtr<ID3D12DescriptorHeap> sampler_heap = context.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 1, true);
  ASSERT_TRUE(resource_heap);
  ASSERT_TRUE(sampler_heap);
  ComPtr<ID3D12DescriptorHeap> other_resource_heap;
  if (options.write_other_heap_concurrently) {
    other_resource_heap = context.CreateDescriptorHeap(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, resource_descriptor_count,
        true);
    ASSERT_TRUE(other_resource_heap);
  }

  ComPtr<ID3D12DescriptorHeap> source_resource_heap;
  ComPtr<ID3D12DescriptorHeap> source_sampler_heap;
  if (options.copy_from_released_cpu_heaps) {
    source_resource_heap = context.CreateDescriptorHeap(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, resource_descriptor_count,
        false);
    source_sampler_heap = context.CreateDescriptorHeap(
        D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 1, false);
    ASSERT_TRUE(source_resource_heap);
    ASSERT_TRUE(source_sampler_heap);
  }
  ID3D12DescriptorHeap *resource_write_heap =
      source_resource_heap ? source_resource_heap.get() : resource_heap.get();
  ID3D12DescriptorHeap *sampler_write_heap =
      source_sampler_heap ? source_sampler_heap.get() : sampler_heap.get();

  const std::array<std::uint32_t, 4> texture_data = {
      0xff0000ff, 0xff00ff00, 0xffff0000, 0xffffff00};
  std::array<ComPtr<ID3D12Resource>, 4> textures;
  for (std::size_t i = 0; i < textures.size(); ++i) {
    textures[i] = context.CreateTexture2D(
        1, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_NONE,
        D3D12_RESOURCE_STATE_COPY_DEST);
    ASSERT_TRUE(textures[i]);
    ASSERT_TRUE(SUCCEEDED(context.UploadTextureAndReset(
        textures[i].get(), &texture_data[i], sizeof(texture_data[i]),
        sizeof(texture_data[i]))));
  }

  for (auto &texture : textures) {
    D3D12TestContext::Transition(
        context.list(), texture.get(), D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  }

  if (!other_resource_heap) {
    for (UINT i = 0; i < textures.size(); ++i) {
      context.device()->CreateShaderResourceView(
          textures[i].get(), nullptr,
          context.CpuDescriptorHandle(resource_write_heap, i + 1));
    }
  }
  D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_desc = {};
  cbv_desc.BufferLocation = constant_buffer->GetGPUVirtualAddress();
  cbv_desc.SizeInBytes = constant_data.size();
  D3D12_CONSTANT_BUFFER_VIEW_DESC other_cbv_desc = cbv_desc;
  if (other_constant_buffer)
    other_cbv_desc.BufferLocation =
        other_constant_buffer->GetGPUVirtualAddress();

  // Replacing a descriptor must release the previous backend allocation and
  // reuse slot-owned native table entries. This deliberately exceeds the old
  // fixed append-only table capacity before publishing the descriptor used by
  // the draw.
  std::vector<ComPtr<ID3D12Resource>> overwritten_buffers;
  overwritten_buffers.reserve(options.overwrite_count);
  for (UINT i = 0; i < options.overwrite_count; ++i) {
    auto buffer = context.CreateUploadBuffer(
        constant_data.size(), constant_data.data(), constant_data.size());
    ASSERT_TRUE(buffer);
    D3D12_CONSTANT_BUFFER_VIEW_DESC overwritten_desc = cbv_desc;
    overwritten_desc.BufferLocation = buffer->GetGPUVirtualAddress();
    context.device()->CreateConstantBufferView(
        &overwritten_desc,
        context.CpuDescriptorHandle(resource_write_heap, 5));
    overwritten_buffers.push_back(std::move(buffer));
  }
  if (!other_resource_heap) {
    context.device()->CreateConstantBufferView(
        &cbv_desc, context.CpuDescriptorHandle(resource_write_heap, 5));
  }

  // D3D12 permits independent descriptor slots in one heap to be populated by
  // different application threads. The descriptors below are intentionally
  // unused by the draw: they stress publication and resource bookkeeping
  // without introducing shader-side ordering requirements. This is also a
  // useful TSAN scenario for the app-thread/encode-thread mirror protocol.
  std::vector<ComPtr<ID3D12Resource>> concurrent_buffers;
  if (write_concurrently) {
    concurrent_buffers.reserve(2 * (resource_descriptor_count - 6));
    for (UINT version = 0; version < 2; ++version)
      for (UINT slot = 6; slot < resource_descriptor_count; ++slot) {
        auto buffer = context.CreateUploadBuffer(
            constant_data.size(), constant_data.data(), constant_data.size());
        ASSERT_TRUE(buffer);
        concurrent_buffers.push_back(std::move(buffer));
      }
  }

  D3D12_SAMPLER_DESC sampler_desc = {};
  sampler_desc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
  sampler_desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  sampler_desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  sampler_desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  context.device()->CreateSampler(
      &sampler_desc, sampler_write_heap->GetCPUDescriptorHandleForHeapStart());

  if (options.copy_from_released_cpu_heaps) {
    // The shader-visible destination must own a complete copy of every
    // descriptor payload; it must not retain a raw dependency on the CPU-only
    // source heap's records or sampler state.
    const D3D12_CPU_DESCRIPTOR_HANDLE destination_starts[] = {
        context.CpuDescriptorHandle(resource_heap.get(), 1),
        context.CpuDescriptorHandle(resource_heap.get(), 3)};
    const UINT destination_sizes[] = {2, 3};
    const D3D12_CPU_DESCRIPTOR_HANDLE source_starts[] = {
        context.CpuDescriptorHandle(source_resource_heap.get(), 1)};
    const UINT source_sizes[] = {5};
    context.device()->CopyDescriptors(
        2, destination_starts, destination_sizes, 1, source_starts,
        source_sizes, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    context.device()->CopyDescriptorsSimple(
        1, sampler_heap->GetCPUDescriptorHandleForHeapStart(),
        source_sampler_heap->GetCPUDescriptorHandleForHeapStart(),
        D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    source_resource_heap.reset();
    source_sampler_heap.reset();
  }

  const float clear_color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  context.list()->ClearRenderTargetView(render_target.view, clear_color, 0,
                                        nullptr);
  context.list()->OMSetRenderTargets(1, &render_target.view, FALSE, nullptr);
  context.list()->SetGraphicsRootSignature(root_signature.get());
  context.list()->SetPipelineState(pipeline.get());
  auto bind_resource_heap = [&](ID3D12DescriptorHeap *heap) {
    ID3D12DescriptorHeap *heaps[] = {heap, sampler_heap.get()};
    context.list()->SetDescriptorHeaps(2, heaps);
    context.list()->SetGraphicsRootDescriptorTable(
        0, context.GpuDescriptorHandle(heap, 0));
    context.list()->SetGraphicsRootDescriptorTable(
        1, sampler_heap->GetGPUDescriptorHandleForHeapStart());
    context.list()->SetGraphicsRootDescriptorTable(
        2, context.GpuDescriptorHandle(heap, 3));
  };
  bind_resource_heap(resource_heap.get());
  context.list()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context.list()->RSSetViewports(1, &render_target.viewport);
  context.list()->RSSetScissorRects(1, &render_target.scissor);
  auto draw = [&]() {
    if (options.execute_indirect) {
      context.list()->ExecuteIndirect(command_signature.get(), 1,
                                      argument_buffer.get(), 0, nullptr, 0);
    } else {
      context.list()->DrawInstanced(3, 1, 0, 0);
    }
  };

  ComPtr<ID3D12QueryHeap> query_heap;
  ComPtr<ID3D12Resource> query_results;
  if (options.test_occlusion_queries) {
    D3D12_QUERY_HEAP_DESC query_desc = {};
    query_desc.Type = D3D12_QUERY_HEAP_TYPE_OCCLUSION;
    query_desc.Count = 1;
    ASSERT_TRUE(SUCCEEDED(context.device()->CreateQueryHeap(
        &query_desc, __uuidof(ID3D12QueryHeap),
        reinterpret_cast<void **>(query_heap.put()))));
    query_results = context.CreateBuffer(
        2 * sizeof(UINT64), D3D12_HEAP_TYPE_READBACK,
        D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
    ASSERT_TRUE(query_results);

    // Draws on both sides exercise Begin/End ordering against pass batching;
    // only the two draws inside each query may contribute samples.
    draw();
    D3D12_RECT empty_scissor = {};
    context.list()->BeginQuery(query_heap.get(), D3D12_QUERY_TYPE_OCCLUSION,
                               0);
    context.list()->RSSetScissorRects(1, &empty_scissor);
    draw();
    context.list()->EndQuery(query_heap.get(), D3D12_QUERY_TYPE_OCCLUSION, 0);
    context.list()->ResolveQueryData(
        query_heap.get(), D3D12_QUERY_TYPE_OCCLUSION, 0, 1,
        query_results.get(), 0);

    // Reuse the same heap slot before the first deferred CPU resolve is
    // materialized. Each ResolveQueryData operation must retain the query
    // version that was current at that point in the command stream.
    context.list()->BeginQuery(
        query_heap.get(), D3D12_QUERY_TYPE_BINARY_OCCLUSION, 0);
    context.list()->RSSetScissorRects(1, &render_target.scissor);
    draw();
    draw();
    context.list()->EndQuery(
        query_heap.get(), D3D12_QUERY_TYPE_BINARY_OCCLUSION, 0);
    draw();
    context.list()->ResolveQueryData(
        query_heap.get(), D3D12_QUERY_TYPE_BINARY_OCCLUSION, 0, 1,
        query_results.get(), sizeof(UINT64));
  } else {
    const UINT draw_count = write_concurrently ? 128 : 1;
    for (UINT i = 0; i < draw_count; ++i) {
      if (other_resource_heap) {
        D3D12_RECT heap_scissor = render_target.scissor;
        if (i & 1)
          heap_scissor.left = heap_scissor.right / 2;
        else
          heap_scissor.right /= 2;
        context.list()->RSSetScissorRects(1, &heap_scissor);
        bind_resource_heap((i & 1) ? other_resource_heap.get()
                                   : resource_heap.get());
      }
      draw();
    }
  }

  D3D12TestContext::Transition(
      context.list(), render_target.texture.get(),
      D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
  TextureReadback readback;
  if (other_resource_heap) {
    // Descriptor contents may legally change after command-list recording and
    // before submission. These used slots were deliberately left empty above:
    // publish them on separate application threads, then prove both heaps via
    // the left/right draw/readback stream. The deterministic unit test covers
    // the complementary revision-clock ordering invariant.
    std::barrier publish_barrier(3);
    auto publish_used_descriptors =
        [&](ID3D12DescriptorHeap *heap,
            D3D12_CONSTANT_BUFFER_VIEW_DESC descriptor) {
      publish_barrier.arrive_and_wait();
      for (UINT i = 0; i < textures.size(); ++i) {
        context.device()->CreateShaderResourceView(
            textures[i].get(), nullptr,
            context.CpuDescriptorHandle(heap, i + 1));
      }
      context.device()->CreateConstantBufferView(
          &descriptor, context.CpuDescriptorHandle(heap, 5));
    };
    std::thread first_heap_writer(publish_used_descriptors,
                                  resource_heap.get(), cbv_desc);
    std::thread second_heap_writer(publish_used_descriptors,
                                   other_resource_heap.get(), other_cbv_desc);
    publish_barrier.arrive_and_wait();
    first_heap_writer.join();
    second_heap_writer.join();
  }

  constexpr UINT kWriterCount = 8;
  std::array<std::thread, kWriterCount> writers;
  std::atomic<bool> stop_writers = false;
  std::atomic<UINT> ready_writers = 0;
  if (write_concurrently) {
    const UINT slot_count = resource_descriptor_count - 6;
    for (UINT writer = 0; writer < kWriterCount; ++writer) {
      writers[writer] = std::thread([&, writer]() {
        ID3D12DescriptorHeap *writer_heap =
            options.write_other_heap_concurrently && (writer & 1)
                ? other_resource_heap.get()
                : resource_write_heap;
        UINT version = 0;
        bool published_ready = false;
        while (!stop_writers.load(std::memory_order_acquire)) {
          for (UINT slot = 6 + writer; slot < resource_descriptor_count;
               slot += kWriterCount) {
            auto *buffer =
                concurrent_buffers[version * slot_count + slot - 6].get();
            const auto handle =
                context.CpuDescriptorHandle(writer_heap, slot);
            if (!version) {
              D3D12_CONSTANT_BUFFER_VIEW_DESC desc = cbv_desc;
              desc.BufferLocation = buffer->GetGPUVirtualAddress();
              context.device()->CreateConstantBufferView(&desc, handle);
            } else {
              D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
              desc.Format = DXGI_FORMAT_UNKNOWN;
              desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
              desc.Shader4ComponentMapping =
                  D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
              desc.Buffer.NumElements = constant_data.size() / sizeof(UINT);
              desc.Buffer.StructureByteStride = sizeof(UINT);
              context.device()->CreateShaderResourceView(buffer, &desc,
                                                         handle);
            }
          }
          if (!published_ready) {
            // Readback starts only after every writer has completed a full
            // pass over all slots assigned to that thread.
            ready_writers.fetch_add(1, std::memory_order_release);
            published_ready = true;
          }
          version ^= 1;
          // Keep writers active throughout submission without continuously
          // monopolizing the heap-level publication lock.
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
      });
    }
    while (ready_writers.load(std::memory_order_acquire) != kWriterCount)
      std::this_thread::yield();
  }

  HRESULT readback_hr = S_OK;
  if (options.release_bound_heaps_after_submit) {
    // Defensive backend-hardening scenario: D3D12 requires GPU descriptor
    // handles to remain valid through execution, but an early application
    // release must not turn DXMT's already-captured deferred snapshots into
    // host UAFs. Hold the queue so the release deterministically precedes
    // deferred replay/encoding of texture and sampler payloads.
    const D3D12_RESOURCE_DESC texture_desc = render_target.texture->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
    UINT row_count = 0;
    UINT64 row_size = 0;
    UINT64 total_size = 0;
    context.device()->GetCopyableFootprints(
        &texture_desc, 0, 1, 0, &footprint, &row_count, &row_size,
        &total_size);
    ComPtr<ID3D12Resource> readback_buffer = context.CreateBuffer(
        total_size, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE,
        D3D12_RESOURCE_STATE_COPY_DEST);
    ASSERT_TRUE(readback_buffer);

    D3D12_TEXTURE_COPY_LOCATION destination = {};
    destination.pResource = readback_buffer.get();
    destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    destination.PlacedFootprint = footprint;
    D3D12_TEXTURE_COPY_LOCATION source = {};
    source.pResource = render_target.texture.get();
    source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    source.SubresourceIndex = 0;
    context.list()->CopyTextureRegion(&destination, 0, 0, 0, &source,
                                      nullptr);

    ComPtr<ID3D12Fence> blocker;
    ASSERT_TRUE(SUCCEEDED(context.device()->CreateFence(
        0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence),
        reinterpret_cast<void **>(blocker.put()))));
    ComPtr<ID3D12CommandAllocator> replacement_allocator;
    ASSERT_TRUE(SUCCEEDED(context.device()->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator),
        reinterpret_cast<void **>(replacement_allocator.put()))));

    ASSERT_TRUE(SUCCEEDED(context.list()->Close()));
    ASSERT_TRUE(SUCCEEDED(context.queue()->Wait(blocker.get(), 1)));
    ID3D12CommandList *lists[] = {context.list()};
    context.queue()->ExecuteCommandLists(1, lists);

    // Resetting with a different allocator is legal after submission and
    // releases the command list's recorded descriptor-heap references while
    // the original allocator remains alive and in flight.
    ASSERT_TRUE(
        SUCCEEDED(context.list()->Reset(replacement_allocator.get(), nullptr)));
    resource_heap.reset();
    sampler_heap.reset();
    ASSERT_TRUE(SUCCEEDED(blocker->Signal(1)));
    readback_hr = context.SignalAndWait();

    if (SUCCEEDED(readback_hr)) {
      void *mapped = nullptr;
      D3D12_RANGE read_range = {0, static_cast<SIZE_T>(total_size)};
      readback_hr = readback_buffer->Map(0, &read_range, &mapped);
      if (SUCCEEDED(readback_hr)) {
        readback.data.resize(static_cast<std::size_t>(total_size));
        std::memcpy(readback.data.data(), mapped, readback.data.size());
        D3D12_RANGE written_range = {0, 0};
        readback_buffer->Unmap(0, &written_range);
        readback.row_pitch = footprint.Footprint.RowPitch;
        readback.width = footprint.Footprint.Width;
        readback.height = footprint.Footprint.Height;
      }
    }
  } else {
    readback_hr =
        context.ReadbackTexture(render_target.texture.get(), &readback);
  }
  stop_writers.store(true, std::memory_order_release);
  if (write_concurrently)
    for (auto &writer : writers)
      writer.join();
  ASSERT_TRUE(SUCCEEDED(readback_hr));
  if (other_resource_heap)
    ExpectSplitColor(readback, 0xb2664c19, 0xff00ff00, 2);
  else
    ExpectSolidColor(readback, 0xb2664c19, 2);

  if (query_results) {
    void *mapped = nullptr;
    D3D12_RANGE read_range = {0, 2 * sizeof(UINT64)};
    ASSERT_TRUE(SUCCEEDED(query_results->Map(0, &read_range, &mapped)));
    const auto *results = static_cast<const UINT64 *>(mapped);
    EXPECT_EQ(results[0], 0u);
    EXPECT_EQ(results[1], 1u);
    D3D12_RANGE written_range = {0, 0};
    query_results->Unmap(0, &written_range);
  }
}

TEST_F(D3D12DescriptorSpec, DrawsWithSplitDescriptorTables) {
  RunDescriptorTableDraw(context_);
}

TEST_F(D3D12DescriptorSpec, DrawsIndirectWithSplitDescriptorTables) {
  RunDescriptorTableDraw(context_, {.execute_indirect = true});
}

TEST_F(D3D12DescriptorSpec, ResolvesOcclusionAcrossBatchedDrawBoundaries) {
  RunDescriptorTableDraw(context_, {.test_occlusion_queries = true});
}

TEST_F(D3D12DescriptorSpec, ReusesBackendEntriesWhenDescriptorsAreOverwritten) {
  RunDescriptorTableDraw(context_, {.overwrite_count = 32});
}

TEST_F(D3D12DescriptorSpec, PopulatesIndependentDescriptorSlotsConcurrently) {
  RunDescriptorTableDraw(
      context_, {.write_unused_slots_concurrently = true});
}

TEST_F(D3D12DescriptorSpec, PublishesDescriptorsAcrossHeapsConcurrently) {
  RunDescriptorTableDraw(
      context_, {.write_unused_slots_concurrently = true,
                 .write_other_heap_concurrently = true});
}

TEST_F(D3D12DescriptorSpec, CopiesDescriptorsFromReleasedCpuHeaps) {
  RunDescriptorTableDraw(context_, {.copy_from_released_cpu_heaps = true});
}

TEST_F(D3D12DescriptorSpec,
       DefensivelyHandlesEarlyDescriptorHeapReleaseAfterSubmission) {
  RunDescriptorTableDraw(context_, {.release_bound_heaps_after_submit = true});
}

TEST_F(D3D12DescriptorSpec, IgnoresInvalidAndStaleCpuDescriptorHandles) {
  D3D12_SAMPLER_DESC sampler_desc = {};
  sampler_desc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
  sampler_desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  sampler_desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  sampler_desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;

  // Invalid application input must be diagnosed and ignored rather than
  // dereferenced as a host pointer.
  context_.device()->CreateSampler(&sampler_desc, {1});

  D3D12_CPU_DESCRIPTOR_HANDLE stale = {};
  {
    auto heap = context_.CreateDescriptorHeap(
        D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 1, true);
    ASSERT_TRUE(heap);
    stale = heap->GetCPUDescriptorHandleForHeapStart();
  }
  context_.device()->CreateSampler(&sampler_desc, stale);

  // Ensure invalid lookups do not poison a later valid descriptor-backed draw.
  RunDescriptorTableDraw(context_);
}

TEST_F(D3D12DescriptorSpec, CopiesTextureThroughComputeDescriptorTable) {
  D3D12_DESCRIPTOR_RANGE ranges[2] = {};
  ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  ranges[0].NumDescriptors = 1;
  ranges[0].BaseShaderRegister = 0;
  ranges[0].OffsetInDescriptorsFromTableStart = 0;
  ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  ranges[1].NumDescriptors = 1;
  ranges[1].BaseShaderRegister = 0;
  ranges[1].OffsetInDescriptorsFromTableStart = 1;
  D3D12_ROOT_PARAMETER parameter = {};
  parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameter.DescriptorTable.NumDescriptorRanges = 2;
  parameter.DescriptorTable.pDescriptorRanges = ranges;
  parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  root_desc.NumParameters = 1;
  root_desc.pParameters = &parameter;

  ComPtr<ID3D12RootSignature> root_signature =
      context_.CreateRootSignature(root_desc);
  ASSERT_TRUE(root_signature);
  ComPtr<ID3D12PipelineState> pipeline = context_.CreateComputePipeline(
      root_signature.get(), CopyTextureComputeShader());
  ASSERT_TRUE(pipeline);

  const std::array<float, 4> expected = {1.0f, 2.0f, 3.0f, 4.0f};
  std::array<std::array<float, 4>, 16> input;
  input.fill(expected);
  ComPtr<ID3D12Resource> source = context_.CreateTexture2D(
      4, 4, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  ComPtr<ID3D12Resource> destination = context_.CreateTexture2D(
      4, 4, 1, DXGI_FORMAT_R32G32B32A32_FLOAT,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  ASSERT_TRUE(source);
  ASSERT_TRUE(destination);
  ASSERT_TRUE(SUCCEEDED(context_.UploadTextureAndReset(
      source.get(), input.data(), 4 * sizeof(input[0]), sizeof(input))));
  D3D12TestContext::Transition(
      context_.list(), source.get(), D3D12_RESOURCE_STATE_COPY_DEST,
      D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

  ComPtr<ID3D12DescriptorHeap> heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, true);
  ASSERT_TRUE(heap);
  context_.device()->CreateShaderResourceView(
      source.get(), nullptr, context_.CpuDescriptorHandle(heap.get(), 0));
  context_.device()->CreateUnorderedAccessView(
      destination.get(), nullptr, nullptr,
      context_.CpuDescriptorHandle(heap.get(), 1));
  context_.list()->SetComputeRootSignature(root_signature.get());
  context_.list()->SetPipelineState(pipeline.get());
  ID3D12DescriptorHeap *heaps[] = {heap.get()};
  context_.list()->SetDescriptorHeaps(1, heaps);
  context_.list()->SetComputeRootDescriptorTable(
      0, heap->GetGPUDescriptorHandleForHeapStart());
  context_.list()->Dispatch(1, 1, 1);
  D3D12TestContext::Transition(
      context_.list(), destination.get(),
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_COPY_SOURCE);

  TextureReadback readback;
  ASSERT_TRUE(
      SUCCEEDED(context_.ReadbackTexture(destination.get(), &readback)));
  for (UINT y = 0; y < 4; ++y) {
    for (UINT x = 0; x < 4; ++x) {
      std::array<float, 4> actual = {};
      std::memcpy(actual.data(),
                  readback.data.data() + y * readback.row_pitch +
                      x * sizeof(actual),
                  sizeof(actual));
      EXPECT_EQ(actual, expected) << "pixel (" << x << ", " << y << ")";
    }
  }
}

TEST_F(D3D12DescriptorSpec, CopiesTextureUavIntoFallbackDescriptorTable) {
  RenderTarget render_target = CreateRenderTarget(context_);
  ASSERT_TRUE(render_target.texture);
  ASSERT_TRUE(render_target.heap);

  D3D12_DESCRIPTOR_RANGE range = {};
  range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  range.NumDescriptors = std::numeric_limits<UINT>::max();
  range.BaseShaderRegister = 1;
  range.OffsetInDescriptorsFromTableStart = 0;
  D3D12_ROOT_PARAMETER parameters[2] = {};
  parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[0].DescriptorTable.NumDescriptorRanges = 1;
  parameters[0].DescriptorTable.pDescriptorRanges = &range;
  parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  parameters[1].Constants.ShaderRegister = 0;
  parameters[1].Constants.Num32BitValues = 1;
  parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  root_desc.NumParameters = 2;
  root_desc.pParameters = parameters;
  ComPtr<ID3D12RootSignature> root_signature =
      context_.CreateRootSignature(root_desc);
  ASSERT_TRUE(root_signature);
  ComPtr<ID3D12PipelineState> pipeline = context_.CreateGraphicsPipeline(
      root_signature.get(), DXGI_FORMAT_R8G8B8A8_UNORM,
      TextureUavPixelShader());
  ASSERT_TRUE(pipeline);

  const float value = 1.0f;
  ComPtr<ID3D12Resource> texture = context_.CreateTexture2D(
      1, 1, 1, DXGI_FORMAT_R32_FLOAT,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(texture);
  ASSERT_TRUE(SUCCEEDED(context_.UploadTextureAndReset(
      texture.get(), &value, sizeof(value), sizeof(value))));
  D3D12TestContext::Transition(
      context_.list(), texture.get(), D3D12_RESOURCE_STATE_COPY_DEST,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

  ComPtr<ID3D12DescriptorHeap> cpu_heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, false);
  ComPtr<ID3D12DescriptorHeap> gpu_heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true);
  ASSERT_TRUE(cpu_heap);
  ASSERT_TRUE(gpu_heap);
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
  uav_desc.Format = DXGI_FORMAT_R32_FLOAT;
  uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
  context_.device()->CreateUnorderedAccessView(
      texture.get(), nullptr, &uav_desc,
      cpu_heap->GetCPUDescriptorHandleForHeapStart());
  context_.device()->CopyDescriptorsSimple(
      1, gpu_heap->GetCPUDescriptorHandleForHeapStart(),
      cpu_heap->GetCPUDescriptorHandleForHeapStart(),
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

  context_.list()->SetGraphicsRootSignature(root_signature.get());
  context_.list()->SetPipelineState(pipeline.get());
  ID3D12DescriptorHeap *heaps[] = {gpu_heap.get()};
  context_.list()->SetDescriptorHeaps(1, heaps);
  context_.list()->SetGraphicsRootDescriptorTable(
      0, gpu_heap->GetGPUDescriptorHandleForHeapStart());
  context_.list()->SetGraphicsRoot32BitConstant(1, 0, 0);
  context_.list()->OMSetRenderTargets(1, &render_target.view, FALSE, nullptr);
  context_.list()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context_.list()->RSSetViewports(1, &render_target.viewport);
  context_.list()->RSSetScissorRects(1, &render_target.scissor);
  context_.list()->DrawInstanced(3, 1, 0, 0);
  D3D12TestContext::Transition(
      context_.list(), render_target.texture.get(),
      D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

  TextureReadback readback;
  ASSERT_TRUE(SUCCEEDED(
      context_.ReadbackTexture(render_target.texture.get(), &readback)));
  ExpectSolidColor(readback, 0xffffffff, 0);
}

} // namespace
