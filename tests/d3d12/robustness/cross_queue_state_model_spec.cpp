#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {

using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

constexpr UINT kTextureWidth = 8;
constexpr UINT kTextureHeight = 8;
constexpr UINT kMipCount = 3;
constexpr UINT64 kDirectFenceValue = 1;
constexpr UINT64 kComputeFenceValue = 2;
constexpr UINT64 kCopyFenceValue = 3;

enum class QueueRole : std::size_t {
  Direct,
  Compute,
  Copy,
  Count,
};

const char *QueueRoleName(QueueRole role) {
  switch (role) {
  case QueueRole::Direct:
    return "direct";
  case QueueRole::Compute:
    return "compute";
  case QueueRole::Copy:
    return "copy";
  case QueueRole::Count:
    break;
  }
  return "unknown";
}

struct QueueClock {
  std::array<UINT64, static_cast<std::size_t>(QueueRole::Count)> visible = {};
};

struct SubresourceModel {
  D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
  std::optional<QueueRole> last_writer;
  UINT64 last_writer_fence = 0;
};

struct ResourceModel {
  ResourceModel(std::size_t count, D3D12_RESOURCE_STATES initial_state)
      : subresources(count) {
    for (auto &subresource : subresources)
      subresource.state = initial_state;
  }

  std::vector<SubresourceModel> subresources;
};

void TransitionModel(SubresourceModel *subresource,
                     D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after,
                     const char *label) {
  ASSERT_NE(subresource, nullptr);
  EXPECT_EQ(subresource->state, before) << label;
  subresource->state = after;
}

void MarkWrite(SubresourceModel *subresource, QueueRole writer,
               UINT64 fence_value) {
  ASSERT_NE(subresource, nullptr);
  subresource->last_writer = writer;
  subresource->last_writer_fence = fence_value;
}

void Publish(QueueClock *clock, QueueRole queue, UINT64 fence_value) {
  ASSERT_NE(clock, nullptr);
  clock->visible[static_cast<std::size_t>(queue)] = fence_value;
}

void WaitForPublication(QueueClock *consumer, const QueueClock &publication) {
  ASSERT_NE(consumer, nullptr);
  for (std::size_t index = 0; index < consumer->visible.size(); ++index)
    consumer->visible[index] =
        std::max(consumer->visible[index], publication.visible[index]);
}

void ExpectVisible(const SubresourceModel &subresource,
                   const QueueClock &consumer, const char *label) {
  ASSERT_TRUE(subresource.last_writer.has_value()) << label;
  const auto writer = static_cast<std::size_t>(*subresource.last_writer);
  EXPECT_GE(consumer.visible[writer], subresource.last_writer_fence)
      << label << " last writer=" << QueueRoleName(*subresource.last_writer)
      << " fence=" << subresource.last_writer_fence;
}

std::uint32_t NextRandom(std::uint32_t &state) {
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return state;
}

UINT MipExtent(UINT extent, UINT mip) {
  const UINT shifted = extent >> mip;
  return shifted ? shifted : 1;
}

struct ScenarioPlan {
  std::uint32_t seed = 0;
  std::uint32_t initial_shared = 0;
  std::uint32_t descriptor_generation_one = 0;
  std::uint32_t descriptor_generation_two = 0;
  std::uint32_t salt = 0;
  UINT selected_mip = 0;
  std::array<UINT, kMipCount> mip_order = {};
  std::array<std::vector<std::uint32_t>, kMipCount> mip_pixels;
  std::vector<std::string> actions;
};

ScenarioPlan MakeScenarioPlan(std::uint32_t seed) {
  ScenarioPlan plan;
  plan.seed = seed;
  std::uint32_t random = seed;
  plan.initial_shared = NextRandom(random) | 1u;
  plan.descriptor_generation_one = NextRandom(random) | 1u;
  plan.descriptor_generation_two = plan.descriptor_generation_one ^ 0xa5a55a5au;
  plan.salt = NextRandom(random) | 1u;
  plan.selected_mip = NextRandom(random) % kMipCount;
  for (UINT mip = 0; mip < kMipCount; ++mip)
    plan.mip_order[mip] = mip;
  for (UINT index = kMipCount - 1; index > 0; --index) {
    const UINT other = NextRandom(random) % (index + 1);
    std::swap(plan.mip_order[index], plan.mip_order[other]);
  }

  for (UINT mip = 0; mip < kMipCount; ++mip) {
    const UINT width = MipExtent(kTextureWidth, mip);
    const UINT height = MipExtent(kTextureHeight, mip);
    auto &pixels = plan.mip_pixels[mip];
    pixels.resize(width * height);
    for (UINT y = 0; y < height; ++y) {
      for (UINT x = 0; x < width; ++x) {
        const std::uint32_t payload =
            (seed + mip * 0x1021u + y * 0x41u + x * 3u) & 0x00ffffffu;
        pixels[y * width + x] = 0xff000000u | payload;
      }
    }
  }

  auto add_action = [&](const std::string &action) {
    plan.actions.push_back(action);
  };
  {
    std::ostringstream action;
    action << "direct.copy(shared=0x" << std::hex << plan.initial_shared << ")";
    add_action(action.str());
  }
  for (const UINT mip : plan.mip_order) {
    std::ostringstream action;
    action << "direct.copy(texture.mip" << mip << ")->transition(COPY_SOURCE)";
    add_action(action.str());
  }
  add_action("direct.transition(shared:COPY_DEST->UNORDERED_ACCESS)");
  add_action("direct.execute->signal(fence=1)");
  add_action("compute.wait(fence=1)");
  add_action("compute.wait(cpu-gate=1)");
  {
    std::ostringstream action;
    action << "compute.dispatch(snapshot-generation=1,value=0x" << std::hex
           << plan.descriptor_generation_one << ",salt=0x" << plan.salt << ")";
    add_action(action.str());
  }
  {
    std::ostringstream action;
    action << "compute.copy(texture.mip" << plan.selected_mip << "->scratch)";
    add_action(action.str());
  }
  {
    std::ostringstream action;
    action << "cpu.overwrite-descriptor(generation=2,value=0x" << std::hex
           << plan.descriptor_generation_two << ")";
    add_action(action.str());
  }
  add_action("cpu.signal(gate=1)");
  add_action("compute.signal(fence=2)");
  add_action("copy.wait(fence=2)");
  add_action("copy.read(shared,texture-mips,scratch)->signal(fence=3)");
  add_action("cpu.wait(fence=3)->readback-oracles");
  return plan;
}

std::string FormatReplay(const ScenarioPlan &plan) {
  std::ostringstream output;
  output << "seed=0x" << std::hex << plan.seed
         << " replay=\"DXMT_D3D12_CROSS_QUEUE_STATE_SEED=0x" << plan.seed
         << " --gtest_filter=D3D12CrossQueueStateModelSpec."
            "SharedResourceVectorClockAndMipStateAcrossFixedSeeds\""
         << " actions=[";
  for (std::size_t index = 0; index < plan.actions.size(); ++index) {
    if (index)
      output << ", ";
    output << index << ":" << plan.actions[index];
  }
  output << "]";
  return output.str();
}

struct CommandListResources {
  ComPtr<ID3D12CommandAllocator> allocator;
  ComPtr<ID3D12GraphicsCommandList> list;
};

class D3D12CrossQueueStateModelSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);

    const auto shader = CompileShader(R"(
      cbuffer Parameters : register(b0) { uint salt; };
      ByteAddressBuffer input : register(t0);
      RWBuffer<uint> shared_data : register(u0);

      [numthreads(1, 1, 1)]
      void main() {
        shared_data[0] = (shared_data[0] ^ input.Load(0)) + salt;
      }
    )",
                                      "cs_5_0");
    ASSERT_EQ(shader.result, S_OK) << shader.diagnostic_text();

    std::array<D3D12_DESCRIPTOR_RANGE, 2> ranges = {};
    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[0].NumDescriptors = 1;
    ranges[0].OffsetInDescriptorsFromTableStart = 0;
    ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    ranges[1].NumDescriptors = 1;
    ranges[1].OffsetInDescriptorsFromTableStart = 1;
    std::array<D3D12_ROOT_PARAMETER, 2> parameters = {};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[0].DescriptorTable.NumDescriptorRanges = ranges.size();
    parameters[0].DescriptorTable.pDescriptorRanges = ranges.data();
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[1].Constants.ShaderRegister = 0;
    parameters[1].Constants.Num32BitValues = 1;
    D3D12_ROOT_SIGNATURE_DESC root_desc = {};
    root_desc.NumParameters = parameters.size();
    root_desc.pParameters = parameters.data();
    root_signature_ = context_.CreateRootSignature(root_desc);
    ASSERT_TRUE(root_signature_);
    const D3D12_SHADER_BYTECODE bytecode = {shader.bytecode->GetBufferPointer(),
                                            shader.bytecode->GetBufferSize()};
    pipeline_ = context_.CreateComputePipeline(root_signature_.get(), bytecode);
    ASSERT_TRUE(pipeline_);

    direct_queue_ = CreateQueue(D3D12_COMMAND_LIST_TYPE_DIRECT);
    compute_queue_ = CreateQueue(D3D12_COMMAND_LIST_TYPE_COMPUTE);
    copy_queue_ = CreateQueue(D3D12_COMMAND_LIST_TYPE_COPY);
    ASSERT_TRUE(direct_queue_);
    ASSERT_TRUE(compute_queue_);
    ASSERT_TRUE(copy_queue_);
  }

  ComPtr<ID3D12CommandQueue> CreateQueue(D3D12_COMMAND_LIST_TYPE type) {
    D3D12_COMMAND_QUEUE_DESC desc = {};
    desc.Type = type;
    ComPtr<ID3D12CommandQueue> queue;
    EXPECT_EQ(context_.device()->CreateCommandQueue(
                  &desc, __uuidof(ID3D12CommandQueue),
                  reinterpret_cast<void **>(queue.put())),
              S_OK);
    return queue;
  }

  CommandListResources CreateList(D3D12_COMMAND_LIST_TYPE type) {
    CommandListResources resources;
    EXPECT_EQ(context_.device()->CreateCommandAllocator(
                  type, __uuidof(ID3D12CommandAllocator),
                  reinterpret_cast<void **>(resources.allocator.put())),
              S_OK);
    if (!resources.allocator)
      return resources;
    EXPECT_EQ(context_.device()->CreateCommandList(
                  0, type, resources.allocator.get(), nullptr,
                  __uuidof(ID3D12GraphicsCommandList),
                  reinterpret_cast<void **>(resources.list.put())),
              S_OK);
    return resources;
  }

  D3D12TestContext context_;
  ComPtr<ID3D12RootSignature> root_signature_;
  ComPtr<ID3D12PipelineState> pipeline_;
  ComPtr<ID3D12CommandQueue> direct_queue_;
  ComPtr<ID3D12CommandQueue> compute_queue_;
  ComPtr<ID3D12CommandQueue> copy_queue_;
};

TEST_F(D3D12CrossQueueStateModelSpec,
       SharedResourceVectorClockAndMipStateAcrossFixedSeeds) {
  std::uint32_t first_seed = 0xc001d00du;
  std::size_t seed_count = 8;
  if (const char *replay_seed =
          std::getenv("DXMT_D3D12_CROSS_QUEUE_STATE_SEED")) {
    char *end = nullptr;
    const auto parsed = std::strtoul(replay_seed, &end, 0);
    ASSERT_NE(end, replay_seed) << "invalid DXMT_D3D12_CROSS_QUEUE_STATE_SEED";
    ASSERT_EQ(*end, '\0') << "invalid DXMT_D3D12_CROSS_QUEUE_STATE_SEED";
    first_seed = static_cast<std::uint32_t>(parsed);
    seed_count = 1;
  }

  for (std::size_t seed_index = 0; seed_index < seed_count; ++seed_index) {
    const std::uint32_t seed =
        first_seed + static_cast<std::uint32_t>(seed_index * 0x9e3779b9u);
    const ScenarioPlan plan = MakeScenarioPlan(seed);
    SCOPED_TRACE(FormatReplay(plan));

    auto shared_upload = context_.CreateUploadBuffer(
        sizeof(plan.initial_shared), &plan.initial_shared,
        sizeof(plan.initial_shared));
    auto descriptor_one = context_.CreateUploadBuffer(
        sizeof(plan.descriptor_generation_one), &plan.descriptor_generation_one,
        sizeof(plan.descriptor_generation_one));
    auto descriptor_two = context_.CreateUploadBuffer(
        sizeof(plan.descriptor_generation_two), &plan.descriptor_generation_two,
        sizeof(plan.descriptor_generation_two));
    auto shared =
        context_.CreateBuffer(sizeof(std::uint32_t), D3D12_HEAP_TYPE_DEFAULT,
                              D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                              D3D12_RESOURCE_STATE_COPY_DEST);
    auto texture = context_.CreateTexture2D(
        kTextureWidth, kTextureHeight, kMipCount, DXGI_FORMAT_R8G8B8A8_UNORM,
        D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
    ASSERT_TRUE(shared_upload);
    ASSERT_TRUE(descriptor_one);
    ASSERT_TRUE(descriptor_two);
    ASSERT_TRUE(shared);
    ASSERT_TRUE(texture);

    const auto texture_desc = texture->GetDesc();
    std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT, kMipCount> footprints = {};
    std::array<UINT, kMipCount> row_counts = {};
    std::array<UINT64, kMipCount> row_sizes = {};
    UINT64 total_texture_size = 0;
    context_.device()->GetCopyableFootprints(
        &texture_desc, 0, kMipCount, 0, footprints.data(), row_counts.data(),
        row_sizes.data(), &total_texture_size);
    ASSERT_GT(total_texture_size, 0u);

    auto texture_upload = context_.CreateUploadBuffer(total_texture_size);
    auto texture_readback = context_.CreateBuffer(
        total_texture_size, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE,
        D3D12_RESOURCE_STATE_COPY_DEST);
    auto compute_scratch = context_.CreateBuffer(
        total_texture_size, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
        D3D12_RESOURCE_STATE_COPY_DEST);
    auto compute_scratch_readback = context_.CreateBuffer(
        total_texture_size, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE,
        D3D12_RESOURCE_STATE_COPY_DEST);
    auto shared_readback = context_.CreateBuffer(
        sizeof(std::uint32_t), D3D12_HEAP_TYPE_READBACK,
        D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
    ASSERT_TRUE(texture_upload);
    ASSERT_TRUE(texture_readback);
    ASSERT_TRUE(compute_scratch);
    ASSERT_TRUE(compute_scratch_readback);
    ASSERT_TRUE(shared_readback);

    void *mapped = nullptr;
    const D3D12_RANGE no_read = {0, 0};
    ASSERT_EQ(texture_upload->Map(0, &no_read, &mapped), S_OK);
    ASSERT_NE(mapped, nullptr);
    std::memset(mapped, 0, static_cast<std::size_t>(total_texture_size));
    for (UINT mip = 0; mip < kMipCount; ++mip) {
      const UINT width = MipExtent(kTextureWidth, mip);
      ASSERT_EQ(row_counts[mip], MipExtent(kTextureHeight, mip));
      ASSERT_EQ(row_sizes[mip], UINT64(width) * sizeof(std::uint32_t));
      for (UINT row = 0; row < row_counts[mip]; ++row) {
        std::memcpy(static_cast<std::uint8_t *>(mapped) +
                        footprints[mip].Offset +
                        UINT64(row) * footprints[mip].Footprint.RowPitch,
                    plan.mip_pixels[mip].data() + row * width,
                    static_cast<std::size_t>(row_sizes[mip]));
      }
    }
    const D3D12_RANGE written_texture = {
        0, static_cast<SIZE_T>(total_texture_size)};
    texture_upload->Unmap(0, &written_texture);

    auto descriptor_heap = context_.CreateDescriptorHeap(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, true);
    ASSERT_TRUE(descriptor_heap);
    D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
    srv.Format = DXGI_FORMAT_R32_TYPELESS;
    srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Buffer.NumElements = 1;
    srv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
    uav.Format = DXGI_FORMAT_R32_UINT;
    uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uav.Buffer.NumElements = 1;
    context_.device()->CreateShaderResourceView(
        descriptor_one.get(), &srv,
        context_.CpuDescriptorHandle(descriptor_heap.get(), 0));
    context_.device()->CreateUnorderedAccessView(
        shared.get(), nullptr, &uav,
        context_.CpuDescriptorHandle(descriptor_heap.get(), 1));

    auto direct = CreateList(D3D12_COMMAND_LIST_TYPE_DIRECT);
    auto compute = CreateList(D3D12_COMMAND_LIST_TYPE_COMPUTE);
    auto copy = CreateList(D3D12_COMMAND_LIST_TYPE_COPY);
    ASSERT_TRUE(direct.list);
    ASSERT_TRUE(compute.list);
    ASSERT_TRUE(copy.list);

    ResourceModel shared_model(1, D3D12_RESOURCE_STATE_COPY_DEST);
    ResourceModel texture_model(kMipCount, D3D12_RESOURCE_STATE_COPY_DEST);
    ResourceModel scratch_model(1, D3D12_RESOURCE_STATE_COPY_DEST);
    ResourceModel shared_readback_model(1, D3D12_RESOURCE_STATE_COPY_DEST);
    ResourceModel texture_readback_model(1, D3D12_RESOURCE_STATE_COPY_DEST);
    ResourceModel scratch_readback_model(1, D3D12_RESOURCE_STATE_COPY_DEST);
    QueueClock direct_clock;
    QueueClock compute_clock;
    QueueClock copy_clock;
    QueueClock cpu_clock;

    direct.list->CopyBufferRegion(shared.get(), 0, shared_upload.get(), 0,
                                  sizeof(std::uint32_t));
    MarkWrite(&shared_model.subresources[0], QueueRole::Direct,
              kDirectFenceValue);
    for (const UINT mip : plan.mip_order) {
      D3D12_TEXTURE_COPY_LOCATION destination = {};
      destination.pResource = texture.get();
      destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
      destination.SubresourceIndex = mip;
      D3D12_TEXTURE_COPY_LOCATION source = {};
      source.pResource = texture_upload.get();
      source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
      source.PlacedFootprint = footprints[mip];
      direct.list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);

      TransitionModel(&texture_model.subresources[mip],
                      D3D12_RESOURCE_STATE_COPY_DEST,
                      D3D12_RESOURCE_STATE_COPY_SOURCE, "direct texture mip");
      D3D12_RESOURCE_BARRIER barrier = {};
      barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      barrier.Transition.pResource = texture.get();
      barrier.Transition.Subresource = mip;
      barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
      barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
      direct.list->ResourceBarrier(1, &barrier);
      MarkWrite(&texture_model.subresources[mip], QueueRole::Direct,
                kDirectFenceValue);
    }
    TransitionModel(
        &shared_model.subresources[0], D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, "direct shared buffer");
    D3D12TestContext::Transition(direct.list.get(), shared.get(),
                                 D3D12_RESOURCE_STATE_COPY_DEST,
                                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    ASSERT_EQ(direct.list->Close(), S_OK);
    Publish(&direct_clock, QueueRole::Direct, kDirectFenceValue);

    WaitForPublication(&compute_clock, direct_clock);
    ExpectVisible(shared_model.subresources[0], compute_clock,
                  "compute must see direct shared-buffer write");
    ExpectVisible(texture_model.subresources[plan.selected_mip], compute_clock,
                  "compute must see selected direct texture-mip write");

    ID3D12DescriptorHeap *heaps[] = {descriptor_heap.get()};
    compute.list->SetDescriptorHeaps(1, heaps);
    compute.list->SetPipelineState(pipeline_.get());
    compute.list->SetComputeRootSignature(root_signature_.get());
    compute.list->SetComputeRootDescriptorTable(
        0, descriptor_heap->GetGPUDescriptorHandleForHeapStart());
    compute.list->SetComputeRoot32BitConstant(1, plan.salt, 0);
    compute.list->Dispatch(1, 1, 1);
    TransitionModel(&shared_model.subresources[0],
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                    D3D12_RESOURCE_STATE_COPY_SOURCE, "compute shared buffer");
    D3D12TestContext::Transition(compute.list.get(), shared.get(),
                                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                 D3D12_RESOURCE_STATE_COPY_SOURCE);

    D3D12_TEXTURE_COPY_LOCATION scratch_destination = {};
    scratch_destination.pResource = compute_scratch.get();
    scratch_destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    scratch_destination.PlacedFootprint = footprints[plan.selected_mip];
    D3D12_TEXTURE_COPY_LOCATION texture_source = {};
    texture_source.pResource = texture.get();
    texture_source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    texture_source.SubresourceIndex = plan.selected_mip;
    compute.list->CopyTextureRegion(&scratch_destination, 0, 0, 0,
                                    &texture_source, nullptr);
    TransitionModel(&scratch_model.subresources[0],
                    D3D12_RESOURCE_STATE_COPY_DEST,
                    D3D12_RESOURCE_STATE_COPY_SOURCE, "compute mip scratch");
    D3D12TestContext::Transition(compute.list.get(), compute_scratch.get(),
                                 D3D12_RESOURCE_STATE_COPY_DEST,
                                 D3D12_RESOURCE_STATE_COPY_SOURCE);
    ASSERT_EQ(compute.list->Close(), S_OK);
    MarkWrite(&shared_model.subresources[0], QueueRole::Compute,
              kComputeFenceValue);
    MarkWrite(&scratch_model.subresources[0], QueueRole::Compute,
              kComputeFenceValue);
    Publish(&compute_clock, QueueRole::Compute, kComputeFenceValue);

    WaitForPublication(&copy_clock, compute_clock);
    ExpectVisible(shared_model.subresources[0], copy_clock,
                  "copy must see compute shared-buffer write");
    ExpectVisible(scratch_model.subresources[0], copy_clock,
                  "copy must see compute mip-scratch write");
    for (UINT mip = 0; mip < kMipCount; ++mip) {
      ExpectVisible(texture_model.subresources[mip], copy_clock,
                    "copy must transitively see direct texture-mip write");
      EXPECT_EQ(texture_model.subresources[mip].state,
                D3D12_RESOURCE_STATE_COPY_SOURCE);

      D3D12_TEXTURE_COPY_LOCATION destination = {};
      destination.pResource = texture_readback.get();
      destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
      destination.PlacedFootprint = footprints[mip];
      D3D12_TEXTURE_COPY_LOCATION source = {};
      source.pResource = texture.get();
      source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
      source.SubresourceIndex = mip;
      copy.list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
    }
    copy.list->CopyBufferRegion(shared_readback.get(), 0, shared.get(), 0,
                                sizeof(std::uint32_t));
    copy.list->CopyBufferRegion(compute_scratch_readback.get(), 0,
                                compute_scratch.get(), 0, total_texture_size);
    ASSERT_EQ(copy.list->Close(), S_OK);
    MarkWrite(&shared_readback_model.subresources[0], QueueRole::Copy,
              kCopyFenceValue);
    MarkWrite(&texture_readback_model.subresources[0], QueueRole::Copy,
              kCopyFenceValue);
    MarkWrite(&scratch_readback_model.subresources[0], QueueRole::Copy,
              kCopyFenceValue);
    Publish(&copy_clock, QueueRole::Copy, kCopyFenceValue);

    ComPtr<ID3D12Fence> chain_fence;
    ComPtr<ID3D12Fence> compute_gate;
    ASSERT_EQ(context_.device()->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                             IID_PPV_ARGS(chain_fence.put())),
              S_OK);
    ASSERT_EQ(context_.device()->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                             IID_PPV_ARGS(compute_gate.put())),
              S_OK);

    ID3D12CommandList *direct_lists[] = {direct.list.get()};
    direct_queue_->ExecuteCommandLists(1, direct_lists);
    ASSERT_EQ(direct_queue_->Signal(chain_fence.get(), kDirectFenceValue),
              S_OK);

    ASSERT_EQ(compute_queue_->Wait(chain_fence.get(), kDirectFenceValue), S_OK);
    ASSERT_EQ(compute_queue_->Wait(compute_gate.get(), 1), S_OK);
    ASSERT_EQ(compute_gate->Signal(1), S_OK);
    ID3D12CommandList *compute_lists[] = {compute.list.get()};
    compute_queue_->ExecuteCommandLists(1, compute_lists);
    ASSERT_EQ(compute_queue_->Signal(chain_fence.get(), kComputeFenceValue),
              S_OK);

    ASSERT_EQ(copy_queue_->Wait(chain_fence.get(), kComputeFenceValue), S_OK);
    ID3D12CommandList *copy_lists[] = {copy.list.get()};
    copy_queue_->ExecuteCommandLists(1, copy_lists);
    ASSERT_EQ(copy_queue_->Signal(chain_fence.get(), kCopyFenceValue), S_OK);
    ASSERT_EQ(context_.WaitForFence(chain_fence.get(), kCopyFenceValue), S_OK);
    context_.device()->CreateShaderResourceView(
        descriptor_two.get(), &srv,
        context_.CpuDescriptorHandle(descriptor_heap.get(), 0));
    WaitForPublication(&cpu_clock, copy_clock);
    ExpectVisible(shared_readback_model.subresources[0], cpu_clock,
                  "CPU shared-buffer readback");
    ExpectVisible(texture_readback_model.subresources[0], cpu_clock,
                  "CPU texture readback");
    ExpectVisible(scratch_readback_model.subresources[0], cpu_clock,
                  "CPU compute-scratch readback");

    const std::uint32_t expected_shared =
        (plan.initial_shared ^ plan.descriptor_generation_one) + plan.salt;
    const std::uint32_t forbidden_live_generation =
        (plan.initial_shared ^ plan.descriptor_generation_two) + plan.salt;
    ASSERT_NE(expected_shared, forbidden_live_generation);
    mapped = nullptr;
    const D3D12_RANGE shared_read_range = {0, sizeof(std::uint32_t)};
    ASSERT_EQ(shared_readback->Map(0, &shared_read_range, &mapped), S_OK);
    ASSERT_NE(mapped, nullptr);
    const auto actual_shared = *static_cast<const std::uint32_t *>(mapped);
    EXPECT_EQ(actual_shared, expected_shared)
        << "submitted dispatch must retain descriptor generation one";
    EXPECT_NE(actual_shared, forbidden_live_generation)
        << "deferred execution reread descriptor generation two";
    const D3D12_RANGE no_write = {0, 0};
    shared_readback->Unmap(0, &no_write);

    mapped = nullptr;
    const D3D12_RANGE texture_read_range = {
        0, static_cast<SIZE_T>(total_texture_size)};
    ASSERT_EQ(texture_readback->Map(0, &texture_read_range, &mapped), S_OK);
    ASSERT_NE(mapped, nullptr);
    for (UINT mip = 0; mip < kMipCount; ++mip) {
      const UINT width = MipExtent(kTextureWidth, mip);
      for (UINT row = 0; row < row_counts[mip]; ++row) {
        EXPECT_EQ(
            std::memcmp(static_cast<const std::uint8_t *>(mapped) +
                            footprints[mip].Offset +
                            UINT64(row) * footprints[mip].Footprint.RowPitch,
                        plan.mip_pixels[mip].data() + row * width,
                        static_cast<std::size_t>(row_sizes[mip])),
            0)
            << "mip=" << mip << " row=" << row;
      }
    }
    texture_readback->Unmap(0, &no_write);

    mapped = nullptr;
    ASSERT_EQ(compute_scratch_readback->Map(0, &texture_read_range, &mapped),
              S_OK);
    ASSERT_NE(mapped, nullptr);
    const UINT selected_mip = plan.selected_mip;
    const UINT selected_width = MipExtent(kTextureWidth, selected_mip);
    for (UINT row = 0; row < row_counts[selected_mip]; ++row) {
      EXPECT_EQ(
          std::memcmp(
              static_cast<const std::uint8_t *>(mapped) +
                  footprints[selected_mip].Offset +
                  UINT64(row) * footprints[selected_mip].Footprint.RowPitch,
              plan.mip_pixels[selected_mip].data() + row * selected_width,
              static_cast<std::size_t>(row_sizes[selected_mip])),
          0)
          << "compute-consumed mip=" << selected_mip << " row=" << row;
    }
    compute_scratch_readback->Unmap(0, &no_write);

    EXPECT_EQ(shared_model.subresources[0].state,
              D3D12_RESOURCE_STATE_COPY_SOURCE);
    ASSERT_TRUE(shared_model.subresources[0].last_writer.has_value());
    EXPECT_EQ(*shared_model.subresources[0].last_writer, QueueRole::Compute);
    EXPECT_EQ(shared_model.subresources[0].last_writer_fence,
              kComputeFenceValue);
    for (const auto &subresource : texture_model.subresources) {
      ASSERT_TRUE(subresource.last_writer.has_value());
      EXPECT_EQ(*subresource.last_writer, QueueRole::Direct);
      EXPECT_EQ(subresource.last_writer_fence, kDirectFenceValue);
    }
    EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
  }
}

} // namespace
