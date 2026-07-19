#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"
#include "shaders/runtime_test_shaders.hpp"
#include "shaders/bindless_dxil_shaders.hpp"

#include <array>
#include <atomic>
#include <barrier>
#include <chrono>
#include <cstring>
#include <limits>
#include <string>
#include <thread>
#include <vector>

namespace {

using dxmt::test::ColorsMatch;
using dxmt::test::ComPtr;
using dxmt::test::CreateIsolatedD3D12Device;
using dxmt::test::D3D12TestContext;
using dxmt::test::DescriptorTablePixelShader;
using dxmt::test::CopyTextureComputeShader;
using dxmt::test::TextureReadback;
using dxmt::test::TextureUavPixelShader;

// Reads a typed buffer SRV at t0 and writes input[0] + 0x12345678 to a typed
// buffer UAV at u0. Keeping the compiled SM5 bytecode in the test avoids
// cross-process D3DCompile cache noise in the parallel Wine test scheduler.
inline constexpr DWORD kNullBufferSrvComputeShader[] = {
    0x43425844, 0x3a97a165, 0x991f089a, 0x7ac9a64e, 0xd12295a5,
    0x00000001, 0x00000264, 0x00000005, 0x00000034, 0x00000044,
    0x00000054, 0x0000011c, 0x000001c8, 0x4e475349, 0x00000008,
    0x00000000, 0x00000008, 0x4e47534f, 0x00000008, 0x00000000,
    0x00000008, 0x46454452, 0x000000c0, 0x00000000, 0x0000009c,
    0x00000002, 0x0000003c, 0x43530500, 0x00000000, 0x0000009c,
    0x31314452, 0x0000003c, 0x00000018, 0x00000020, 0x00000028,
    0x00000024, 0x0000000c, 0x00000000, 0x0000007c, 0x00000002,
    0x00000004, 0x00000001, 0xffffffff, 0x00000000, 0x00000001,
    0x00000001, 0x0000008c, 0x00000004, 0x00000004, 0x00000001,
    0xffffffff, 0x00000000, 0x00000001, 0x00000001, 0x75706e69,
    0x75625f74, 0x72656666, 0xababab00, 0x7074756f, 0x625f7475,
    0x65666675, 0xabab0072, 0x33646b76, 0x68732d64, 0x72656461,
    0x312e3120, 0x57282038, 0x20656e69, 0x646e7562, 0x2964656c,
    0xababab00, 0x52444853, 0x000000a4, 0x00050050, 0x00000029,
    0x0400009b, 0x00000001, 0x00000001, 0x00000001, 0x02000068,
    0x00000001, 0x04000858, 0x00107001, 0x00000000, 0x00004444,
    0x0400089c, 0x0011e001, 0x00000000, 0x00004444, 0x0a00002d,
    0x00100012, 0x00000000, 0x00004002, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00107006, 0x00000000, 0x0700001e,
    0x00100012, 0x00000000, 0x00100006, 0x00000000, 0x00004001,
    0x12345678, 0x070000a4, 0x0011e0f2, 0x00000000, 0x00004001,
    0x00000000, 0x00100006, 0x00000000, 0x0100003e, 0x54415453,
    0x00000094, 0x00000008, 0x00000001, 0x00000000, 0x00000000,
    0x00000000, 0x00000001, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000001, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000001,
};

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

enum class ForeignDescriptorCopyCase {
  SimpleForeignSource,
  RangedForeignSource,
  SimpleForeignDestination,
  RangedForeignDestination,
};

class ForeignDescriptorCopySpec
    : public D3D12DescriptorSpec,
      public ::testing::WithParamInterface<ForeignDescriptorCopyCase> {};

TEST_P(ForeignDescriptorCopySpec,
       LeavesForeignEndpointUnchangedAndAllowsLocalRecovery) {
  auto foreign_device = CreateIsolatedD3D12Device();
  ASSERT_TRUE(foreign_device);
  D3D12TestContext foreign_context;
  ASSERT_EQ(foreign_context.Initialize(foreign_device.get()), S_OK);
  auto local_destination = context_.CreateTexture2D(
      1, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
      D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
      D3D12_RESOURCE_STATE_RENDER_TARGET);
  auto local_source = context_.CreateTexture2D(
      1, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
      D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
      D3D12_RESOURCE_STATE_RENDER_TARGET);
  auto foreign_destination = foreign_context.CreateTexture2D(
      1, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
      D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
      D3D12_RESOURCE_STATE_RENDER_TARGET);
  auto foreign_source = foreign_context.CreateTexture2D(
      1, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
      D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
      D3D12_RESOURCE_STATE_RENDER_TARGET);
  auto local_heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2, false);
  auto foreign_heap = foreign_context.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2, false);
  ASSERT_TRUE(local_destination);
  ASSERT_TRUE(local_source);
  ASSERT_TRUE(foreign_destination);
  ASSERT_TRUE(foreign_source);
  ASSERT_TRUE(local_heap);
  ASSERT_TRUE(foreign_heap);
  const auto local_destination_handle =
      context_.CpuDescriptorHandle(local_heap.get(), 0);
  const auto local_source_handle =
      context_.CpuDescriptorHandle(local_heap.get(), 1);
  const auto foreign_destination_handle =
      foreign_context.CpuDescriptorHandle(foreign_heap.get(), 0);
  const auto foreign_source_handle =
      foreign_context.CpuDescriptorHandle(foreign_heap.get(), 1);
  context_.device()->CreateRenderTargetView(
      local_destination.get(), nullptr, local_destination_handle);
  context_.device()->CreateRenderTargetView(local_source.get(), nullptr,
                                            local_source_handle);
  foreign_device->CreateRenderTargetView(
      foreign_destination.get(), nullptr, foreign_destination_handle);
  foreign_device->CreateRenderTargetView(foreign_source.get(), nullptr,
                                         foreign_source_handle);

  const bool foreign_source_case =
      GetParam() == ForeignDescriptorCopyCase::SimpleForeignSource ||
      GetParam() == ForeignDescriptorCopyCase::RangedForeignSource;
  const bool simple =
      GetParam() == ForeignDescriptorCopyCase::SimpleForeignSource ||
      GetParam() == ForeignDescriptorCopyCase::SimpleForeignDestination;
  const auto destination = foreign_source_case ? local_destination_handle
                                               : foreign_destination_handle;
  const auto source =
      foreign_source_case ? foreign_source_handle : local_source_handle;
  if (simple) {
    context_.device()->CopyDescriptorsSimple(
        1, destination, source, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
  } else {
    constexpr UINT range_size = 1;
    context_.device()->CopyDescriptors(
        1, &destination, &range_size, 1, &source, &range_size,
        D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
  }

  constexpr FLOAT color[4] = {0.25f, 0.5f, 0.75f, 1.0f};
  auto *verification_list =
      foreign_source_case ? context_.list() : foreign_context.list();
  verification_list->ClearRenderTargetView(destination, color, 0, nullptr);
  EXPECT_EQ(verification_list->Close(), S_OK);

  context_.device()->CopyDescriptorsSimple(
      1, local_destination_handle, local_source_handle,
      D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
  ComPtr<ID3D12CommandAllocator> allocator;
  ComPtr<ID3D12GraphicsCommandList> list;
  ASSERT_EQ(context_.device()->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(allocator.put())),
            S_OK);
  ASSERT_EQ(context_.device()->CreateCommandList(
                0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.get(), nullptr,
                IID_PPV_ARGS(list.put())),
            S_OK);
  list->ClearRenderTargetView(local_destination_handle, color, 0, nullptr);
  EXPECT_EQ(list->Close(), S_OK);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
  EXPECT_EQ(foreign_device->GetDeviceRemovedReason(), S_OK);
}

std::string ForeignDescriptorCopyCaseName(
    const ::testing::TestParamInfo<ForeignDescriptorCopyCase> &info) {
  switch (info.param) {
  case ForeignDescriptorCopyCase::SimpleForeignSource:
    return "SimpleForeignSource";
  case ForeignDescriptorCopyCase::RangedForeignSource:
    return "RangedForeignSource";
  case ForeignDescriptorCopyCase::SimpleForeignDestination:
    return "SimpleForeignDestination";
  case ForeignDescriptorCopyCase::RangedForeignDestination:
    return "RangedForeignDestination";
  }
  return "Unknown";
}

INSTANTIATE_TEST_SUITE_P(
    Validation, ForeignDescriptorCopySpec,
    ::testing::Values(ForeignDescriptorCopyCase::SimpleForeignSource,
                      ForeignDescriptorCopyCase::RangedForeignSource,
                      ForeignDescriptorCopyCase::SimpleForeignDestination,
                      ForeignDescriptorCopyCase::RangedForeignDestination),
    ForeignDescriptorCopyCaseName);

struct DescriptorTableDrawOptions {
  bool execute_indirect = false;
  bool test_occlusion_queries = false;
  UINT overwrite_count = 0;
  bool null_cbv = false;
  bool null_other_heap_cbv = false;
  bool write_unused_slots_concurrently = false;
  bool write_other_heap_concurrently = false;
  bool copy_from_released_cpu_heaps = false;
  bool release_bound_heaps_after_submit = false;
  bool use_static_sampler = false;
  bool repeat_graphics_root_signature = false;
  bool set_compute_root_signature = false;
};

ULONG PublicRefCount(IUnknown *object) {
  object->AddRef();
  return object->Release();
}

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
  const UINT resource_tail_parameter = options.use_static_sampler ? 1 : 2;
  if (!options.use_static_sampler) {
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[1].DescriptorTable.NumDescriptorRanges = 1;
    parameters[1].DescriptorTable.pDescriptorRanges = &ranges[1];
    parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  }
  parameters[resource_tail_parameter].ParameterType =
      D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[resource_tail_parameter].DescriptorTable.NumDescriptorRanges = 2;
  parameters[resource_tail_parameter].DescriptorTable.pDescriptorRanges =
      &ranges[2];
  parameters[resource_tail_parameter].ShaderVisibility =
      D3D12_SHADER_VISIBILITY_PIXEL;

  D3D12_STATIC_SAMPLER_DESC static_sampler = {};
  static_sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
  static_sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  static_sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  static_sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  static_sampler.MaxAnisotropy = 1;
  static_sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
  static_sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
  static_sampler.MaxLOD = D3D12_FLOAT32_MAX;
  static_sampler.ShaderRegister = 0;
  static_sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  root_desc.NumParameters = options.use_static_sampler ? 2 : 3;
  root_desc.pParameters = parameters;
  root_desc.NumStaticSamplers = options.use_static_sampler ? 1 : 0;
  root_desc.pStaticSamplers =
      options.use_static_sampler ? &static_sampler : nullptr;
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
        options.null_cbv ? nullptr : &cbv_desc,
        context.CpuDescriptorHandle(resource_write_heap, 5));
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
    if (!options.use_static_sampler)
      context.list()->SetGraphicsRootDescriptorTable(
          1, sampler_heap->GetGPUDescriptorHandleForHeapStart());
    context.list()->SetGraphicsRootDescriptorTable(
        resource_tail_parameter, context.GpuDescriptorHandle(heap, 3));
  };
  bind_resource_heap(resource_heap.get());
  if (options.repeat_graphics_root_signature)
    context.list()->SetGraphicsRootSignature(root_signature.get());
  if (options.set_compute_root_signature)
    context.list()->SetComputeRootSignature(root_signature.get());
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
            D3D12_CONSTANT_BUFFER_VIEW_DESC descriptor, bool null_cbv) {
      publish_barrier.arrive_and_wait();
      for (UINT i = 0; i < textures.size(); ++i) {
        context.device()->CreateShaderResourceView(
            textures[i].get(), nullptr,
            context.CpuDescriptorHandle(heap, i + 1));
      }
      context.device()->CreateConstantBufferView(
          null_cbv ? nullptr : &descriptor,
          context.CpuDescriptorHandle(heap, 5));
    };
    std::thread first_heap_writer(publish_used_descriptors,
                                  resource_heap.get(), cbv_desc, false);
    std::thread second_heap_writer(publish_used_descriptors,
                                   other_resource_heap.get(), other_cbv_desc,
                                   options.null_other_heap_cbv);
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
    const ULONG resource_refs_before = PublicRefCount(resource_heap.get());
    const ULONG sampler_refs_before = PublicRefCount(sampler_heap.get());
    context.queue()->ExecuteCommandLists(1, lists);
    const ULONG resource_refs_after = PublicRefCount(resource_heap.get());
    const ULONG sampler_refs_after = PublicRefCount(sampler_heap.get());
    ASSERT_GE(resource_refs_after, resource_refs_before);
    ASSERT_GE(sampler_refs_after, sampler_refs_before);
    const ULONG resource_ref_delta =
        resource_refs_after - resource_refs_before;
    const ULONG sampler_ref_delta = sampler_refs_after - sampler_refs_before;
    EXPECT_EQ(resource_ref_delta, 2u);
    EXPECT_EQ(sampler_ref_delta, 2u);

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
  if (other_resource_heap) {
    ExpectSplitColor(readback, 0xb2664c19,
                     options.null_other_heap_cbv ? 0x00000000 : 0xff00ff00,
                     2);
  } else {
    ExpectSolidColor(readback, options.null_cbv ? 0x00000000 : 0xb2664c19, 2);
  }

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

TEST_F(D3D12DescriptorSpec, DrawsWithBindlessStaticSampler) {
  RunDescriptorTableDraw(context_, {.use_static_sampler = true});
}

TEST_F(D3D12DescriptorSpec,
       SameGraphicsRootSignaturePreservesGraphicsArguments) {
  RunDescriptorTableDraw(
      context_, {.repeat_graphics_root_signature = true});
}

TEST_F(D3D12DescriptorSpec,
       ComputeRootChangeDoesNotAffectGraphicsRootState) {
  RunDescriptorTableDraw(context_, {.set_compute_root_signature = true});
}

DXMT_GROUP_SERIAL_TESTS("D3D12DescriptorSpec.*", "d3d12-descriptor");
DXMT_SERIAL_TEST_DOMAIN("D3D12DescriptorSpec.*", "descriptor");

DXMT_SERIAL_TEST_F(D3D12DescriptorSpec,
                   DrawsWithResourceBearingDxilBindlessPipeline) {
  D3D12_DESCRIPTOR_RANGE srv_range = {};
  srv_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  srv_range.NumDescriptors = 1;
  srv_range.BaseShaderRegister = 0;
  srv_range.RegisterSpace = 1;
  D3D12_DESCRIPTOR_RANGE sampler_range = {};
  sampler_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
  sampler_range.NumDescriptors = 1;
  sampler_range.BaseShaderRegister = 0;
  sampler_range.RegisterSpace = 2;
  D3D12_ROOT_PARAMETER parameters[3] = {};
  parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  parameters[0].Descriptor.ShaderRegister = 0;
  parameters[0].Descriptor.RegisterSpace = 0;
  parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[1].DescriptorTable.NumDescriptorRanges = 1;
  parameters[1].DescriptorTable.pDescriptorRanges = &srv_range;
  parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[2].DescriptorTable.NumDescriptorRanges = 1;
  parameters[2].DescriptorTable.pDescriptorRanges = &sampler_range;
  parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  root_desc.NumParameters = std::size(parameters);
  root_desc.pParameters = parameters;
  root_desc.Flags =
      D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
  auto root_signature = context_.CreateRootSignature(root_desc);
  ASSERT_TRUE(root_signature);

  D3D12_INPUT_ELEMENT_DESC elements[2] = {};
  elements[0] = {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0,
                 D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0};
  elements[1] = {"TEXCOORD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16,
                 D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0};
  D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {};
  pso_desc.pRootSignature = root_signature.get();
  pso_desc.VS = D3D12_SHADER_BYTECODE{
      dxmt::test::kBindlessDxilPresentVs,
      dxmt::test::kBindlessDxilPresentVs_len};
  pso_desc.PS = D3D12_SHADER_BYTECODE{
      dxmt::test::kBindlessDxilPresentPs,
      dxmt::test::kBindlessDxilPresentPs_len};
  pso_desc.BlendState.RenderTarget[0].RenderTargetWriteMask =
      D3D12_COLOR_WRITE_ENABLE_ALL;
  pso_desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
  pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  pso_desc.SampleMask = UINT_MAX;
  pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  pso_desc.NumRenderTargets = 1;
  pso_desc.RTVFormats[0] = DXGI_FORMAT_R32G32B32A32_FLOAT;
  pso_desc.SampleDesc.Count = 1;
  pso_desc.InputLayout = {elements, std::size(elements)};
  ComPtr<ID3D12PipelineState> pipeline;
  ASSERT_TRUE(SUCCEEDED(context_.device()->CreateGraphicsPipelineState(
      &pso_desc, __uuidof(ID3D12PipelineState),
      reinterpret_cast<void **>(pipeline.put()))));

  auto target = context_.CreateTexture2D(
      32, 32, 1, DXGI_FORMAT_R32G32B32A32_FLOAT,
      D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
      D3D12_RESOURCE_STATE_RENDER_TARGET);
  auto rtv_heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
  ASSERT_TRUE(target);
  ASSERT_TRUE(rtv_heap);
  auto rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
  context_.device()->CreateRenderTargetView(target.get(), nullptr, rtv);

  auto source = context_.CreateTexture2D(
      1, 1, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(source);
  const std::array<float, 4> source_pixel = {0.25f, 0.5f, 0.75f, 1.0f};
  ASSERT_TRUE(SUCCEEDED(context_.UploadTextureAndReset(
      source.get(), source_pixel.data(), sizeof(source_pixel),
      sizeof(source_pixel))));
  D3D12TestContext::Transition(
      context_.list(), source.get(), D3D12_RESOURCE_STATE_COPY_DEST,
      D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

  struct FrameData {
    float projection[16];
    float max_edr;
    float brightness;
    float current_edr_bias;
    float padding;
  } frame = {};
  frame.projection[0] = frame.projection[5] = frame.projection[10] =
      frame.projection[15] = 1.0f;
  frame.max_edr = 1.0f;
  frame.brightness = 1.0f;
  auto frame_buffer = context_.CreateUploadBuffer(
      D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, &frame, sizeof(frame));
  ASSERT_TRUE(frame_buffer);

  struct Vertex {
    float position[4];
    float texcoord[4];
  };
  const Vertex vertices[3] = {
      {{-1.0f, -1.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f, 0.0f}},
      {{-1.0f, 3.0f, 0.0f, 1.0f}, {0.0f, -1.0f, 0.0f, 0.0f}},
      {{3.0f, -1.0f, 0.0f, 1.0f}, {2.0f, 1.0f, 0.0f, 0.0f}},
  };
  auto vertex_buffer = context_.CreateUploadBuffer(
      sizeof(vertices), vertices, sizeof(vertices));
  ASSERT_TRUE(vertex_buffer);

  auto resource_heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true);
  auto sampler_heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 1, true);
  ASSERT_TRUE(resource_heap);
  ASSERT_TRUE(sampler_heap);
  context_.device()->CreateShaderResourceView(
      source.get(), nullptr,
      resource_heap->GetCPUDescriptorHandleForHeapStart());
  D3D12_SAMPLER_DESC sampler = {};
  sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
  sampler.AddressU = sampler.AddressV = sampler.AddressW =
      D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  sampler.MaxLOD = D3D12_FLOAT32_MAX;
  context_.device()->CreateSampler(
      &sampler, sampler_heap->GetCPUDescriptorHandleForHeapStart());

  ID3D12DescriptorHeap *heaps[] = {resource_heap.get(), sampler_heap.get()};
  context_.list()->SetDescriptorHeaps(std::size(heaps), heaps);
  context_.list()->SetGraphicsRootSignature(root_signature.get());
  context_.list()->SetPipelineState(pipeline.get());
  context_.list()->SetGraphicsRootConstantBufferView(
      0, frame_buffer->GetGPUVirtualAddress());
  context_.list()->SetGraphicsRootDescriptorTable(
      1, resource_heap->GetGPUDescriptorHandleForHeapStart());
  context_.list()->SetGraphicsRootDescriptorTable(
      2, sampler_heap->GetGPUDescriptorHandleForHeapStart());
  D3D12_VERTEX_BUFFER_VIEW vertex_view = {};
  vertex_view.BufferLocation = vertex_buffer->GetGPUVirtualAddress();
  vertex_view.SizeInBytes = sizeof(vertices);
  vertex_view.StrideInBytes = sizeof(Vertex);
  context_.list()->IASetVertexBuffers(0, 1, &vertex_view);
  context_.list()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  D3D12_VIEWPORT viewport = {0.0f, 0.0f, 32.0f, 32.0f, 0.0f, 1.0f};
  D3D12_RECT scissor = {0, 0, 32, 32};
  context_.list()->RSSetViewports(1, &viewport);
  context_.list()->RSSetScissorRects(1, &scissor);
  context_.list()->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
  context_.list()->DrawInstanced(3, 1, 0, 0);
  D3D12TestContext::Transition(
      context_.list(), target.get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
      D3D12_RESOURCE_STATE_COPY_SOURCE);

  TextureReadback readback;
  ASSERT_TRUE(SUCCEEDED(context_.ReadbackTexture(target.get(), &readback)));
  std::array<float, 4> center = {};
  std::memcpy(center.data(),
              readback.data.data() + 16 * readback.row_pitch +
                  16 * sizeof(center),
              sizeof(center));
  EXPECT_GT(center[0], 0.01f);
  EXPECT_GT(center[1], 0.01f);
  EXPECT_GT(center[2], 0.01f);
  EXPECT_NEAR(center[3], 1.0f, 0.01f);
}

TEST_F(D3D12DescriptorSpec, DrawsIndirectWithSplitDescriptorTables) {
  RunDescriptorTableDraw(context_, {.execute_indirect = true});
}

TEST(D3D12DescriptorStateSpec, PreservesNullCbvAfterIndirectDraw) {
  D3D12TestContext indirect_context;
  ASSERT_TRUE(SUCCEEDED(indirect_context.Initialize()));
  RunDescriptorTableDraw(indirect_context, {.execute_indirect = true});

  D3D12TestContext null_context;
  ASSERT_TRUE(SUCCEEDED(null_context.Initialize()));
  RunDescriptorTableDraw(null_context, {.null_cbv = true});
}

TEST_F(D3D12DescriptorSpec, ResolvesOcclusionAcrossBatchedDrawBoundaries) {
  RunDescriptorTableDraw(context_, {.test_occlusion_queries = true});
}

TEST_F(D3D12DescriptorSpec, ReusesBackendEntriesWhenDescriptorsAreOverwritten) {
  RunDescriptorTableDraw(context_, {.overwrite_count = 32});
}

TEST_F(D3D12DescriptorSpec,
       NullCbvReadsAsZeroThroughCompiledNativeDescriptorTable) {
  RunDescriptorTableDraw(context_, {.null_cbv = true});
}

DXMT_SERIAL_TEST_F(
    D3D12DescriptorSpec,
    NullBufferSrvReadsAsZeroThroughCompiledNativeDescriptorTable) {
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

  auto root_signature = context_.CreateRootSignature(root_desc);
  ASSERT_TRUE(root_signature);
  const D3D12_SHADER_BYTECODE bytecode = {kNullBufferSrvComputeShader,
                                          sizeof(kNullBufferSrvComputeShader)};
  auto pipeline =
      context_.CreateComputePipeline(root_signature.get(), bytecode);
  ASSERT_TRUE(pipeline);

  auto output = context_.CreateBuffer(
      sizeof(uint32_t), D3D12_HEAP_TYPE_DEFAULT,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  ASSERT_TRUE(output);
  auto heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, true);
  ASSERT_TRUE(heap);

  D3D12_SHADER_RESOURCE_VIEW_DESC null_srv = {};
  null_srv.Format = DXGI_FORMAT_R32_UINT;
  null_srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
  null_srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  null_srv.Buffer.NumElements = 1;
  context_.device()->CreateShaderResourceView(
      nullptr, &null_srv, context_.CpuDescriptorHandle(heap.get(), 0));
  D3D12_UNORDERED_ACCESS_VIEW_DESC output_uav = {};
  output_uav.Format = DXGI_FORMAT_R32_UINT;
  output_uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  output_uav.Buffer.NumElements = 1;
  context_.device()->CreateUnorderedAccessView(
      output.get(), nullptr, &output_uav,
      context_.CpuDescriptorHandle(heap.get(), 1));

  ID3D12DescriptorHeap *heaps[] = {heap.get()};
  context_.list()->SetDescriptorHeaps(1, heaps);
  context_.list()->SetComputeRootSignature(root_signature.get());
  context_.list()->SetPipelineState(pipeline.get());
  context_.list()->SetComputeRootDescriptorTable(
      0, heap->GetGPUDescriptorHandleForHeapStart());
  context_.list()->Dispatch(1, 1, 1);
  D3D12TestContext::Transition(
      context_.list(), output.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_COPY_SOURCE);

  std::vector<uint8_t> readback;
  ASSERT_TRUE(SUCCEEDED(
      context_.ReadbackBuffer(output.get(), sizeof(uint32_t), &readback)));
  ASSERT_EQ(readback.size(), sizeof(uint32_t));
  uint32_t value = 0;
  std::memcpy(&value, readback.data(), sizeof(value));
  EXPECT_EQ(value, 0x12345678u);
}

TEST_F(D3D12DescriptorSpec,
       NullTextureSrvReadsAsZeroThroughCompiledDescriptorTable) {
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

  auto root_signature = context_.CreateRootSignature(root_desc);
  auto pipeline = context_.CreateComputePipeline(
      root_signature.get(), CopyTextureComputeShader());
  auto output = context_.CreateTexture2D(
      4, 4, 1, DXGI_FORMAT_R32G32B32A32_FLOAT,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  auto heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, true);
  ASSERT_TRUE(root_signature);
  ASSERT_TRUE(pipeline);
  ASSERT_TRUE(output);
  ASSERT_TRUE(heap);

  D3D12_SHADER_RESOURCE_VIEW_DESC null_srv = {};
  null_srv.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
  null_srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  null_srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  null_srv.Texture2D.MipLevels = 1;
  context_.device()->CreateShaderResourceView(
      nullptr, &null_srv, context_.CpuDescriptorHandle(heap.get(), 0));
  context_.device()->CreateUnorderedAccessView(
      output.get(), nullptr, nullptr,
      context_.CpuDescriptorHandle(heap.get(), 1));

  ID3D12DescriptorHeap *heaps[] = {heap.get()};
  context_.list()->SetDescriptorHeaps(1, heaps);
  context_.list()->SetComputeRootSignature(root_signature.get());
  context_.list()->SetPipelineState(pipeline.get());
  context_.list()->SetComputeRootDescriptorTable(
      0, heap->GetGPUDescriptorHandleForHeapStart());
  context_.list()->Dispatch(1, 1, 1);
  D3D12TestContext::Transition(
      context_.list(), output.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_COPY_SOURCE);

  TextureReadback readback;
  ASSERT_TRUE(SUCCEEDED(context_.ReadbackTexture(output.get(), &readback)));
  const std::array<float, 4> zero = {};
  for (UINT y = 0; y < 4; ++y) {
    for (UINT x = 0; x < 4; ++x) {
      std::array<float, 4> actual = {};
      std::memcpy(actual.data(),
                  readback.data.data() + y * readback.row_pitch +
                      x * sizeof(actual),
                  sizeof(actual));
      EXPECT_EQ(actual, zero) << "pixel (" << x << ", " << y << ")";
    }
  }
}

TEST_F(D3D12DescriptorSpec,
       CrossDeviceTextureSrvFailsClosedAsNullDescriptor) {
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
  const D3D12_ROOT_SIGNATURE_DESC root_desc = {1, &parameter, 0, nullptr,
                                               D3D12_ROOT_SIGNATURE_FLAG_NONE};
  auto root_signature = context_.CreateRootSignature(root_desc);
  auto pipeline = context_.CreateComputePipeline(
      root_signature.get(), CopyTextureComputeShader());
  auto output = context_.CreateTexture2D(
      4, 4, 1, DXGI_FORMAT_R32G32B32A32_FLOAT,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  auto heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, true);
  ASSERT_TRUE(root_signature);
  ASSERT_TRUE(pipeline);
  ASSERT_TRUE(output);
  ASSERT_TRUE(heap);

  auto foreign_device = dxmt::test::CreateIsolatedD3D12Device();
  ASSERT_TRUE(foreign_device);
  D3D12_HEAP_PROPERTIES foreign_heap_properties = {};
  foreign_heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;
  D3D12_RESOURCE_DESC foreign_desc = {};
  foreign_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  foreign_desc.Width = 4;
  foreign_desc.Height = 4;
  foreign_desc.DepthOrArraySize = 1;
  foreign_desc.MipLevels = 1;
  foreign_desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
  foreign_desc.SampleDesc.Count = 1;
  foreign_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  ComPtr<ID3D12Resource> foreign_texture;
  ASSERT_EQ(foreign_device->CreateCommittedResource(
                &foreign_heap_properties, D3D12_HEAP_FLAG_NONE, &foreign_desc,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr,
                IID_PPV_ARGS(foreign_texture.put())),
            S_OK);

  D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
  srv.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
  srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv.Texture2D.MipLevels = 1;
  context_.device()->CreateShaderResourceView(
      foreign_texture.get(), &srv, context_.CpuDescriptorHandle(heap.get(), 0));
  context_.device()->CreateUnorderedAccessView(
      output.get(), nullptr, nullptr,
      context_.CpuDescriptorHandle(heap.get(), 1));

  ID3D12DescriptorHeap *heaps[] = {heap.get()};
  context_.list()->SetDescriptorHeaps(1, heaps);
  context_.list()->SetComputeRootSignature(root_signature.get());
  context_.list()->SetPipelineState(pipeline.get());
  context_.list()->SetComputeRootDescriptorTable(
      0, heap->GetGPUDescriptorHandleForHeapStart());
  context_.list()->Dispatch(1, 1, 1);
  D3D12TestContext::Transition(
      context_.list(), output.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_COPY_SOURCE);

  TextureReadback readback;
  ASSERT_EQ(context_.ReadbackTexture(output.get(), &readback), S_OK);
  const std::array<float, 4> zero = {};
  for (UINT y = 0; y < 4; ++y) {
    for (UINT x = 0; x < 4; ++x) {
      std::array<float, 4> actual = {};
      std::memcpy(actual.data(),
                  readback.data.data() + y * readback.row_pitch +
                      x * sizeof(actual),
                  sizeof(actual));
      EXPECT_EQ(actual, zero) << "pixel (" << x << ", " << y << ")";
    }
  }
}

TEST_F(D3D12DescriptorSpec,
       AlternatesValidAndNullCbvAcrossCompiledNativeDescriptorHeaps) {
  RunDescriptorTableDraw(
      context_, {.null_other_heap_cbv = true,
                 .write_other_heap_concurrently = true});
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
  RunDescriptorTableDraw(context_, {.release_bound_heaps_after_submit = true,
                                    .use_static_sampler = true});
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
      D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(source);
  ASSERT_TRUE(destination);
  ASSERT_TRUE(SUCCEEDED(context_.UploadTextureAndReset(
      source.get(), input.data(), 4 * sizeof(input[0]), sizeof(input))));
  D3D12TestContext::Transition(
      context_.list(), source.get(), D3D12_RESOURCE_STATE_COPY_DEST,
      D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  D3D12TestContext::Transition(
      context_.list(), destination.get(), D3D12_RESOURCE_STATE_COPY_DEST,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

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

TEST_F(D3D12DescriptorSpec,
       AdaptsArrayResourceToTexture2DForCompiledDescriptorTable) {
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

  auto root_signature = context_.CreateRootSignature(root_desc);
  auto pipeline = context_.CreateComputePipeline(
      root_signature.get(), CopyTextureComputeShader());
  ASSERT_TRUE(root_signature);
  ASSERT_TRUE(pipeline);

  D3D12_HEAP_PROPERTIES heap_properties = {};
  heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;
  D3D12_RESOURCE_DESC source_desc = {};
  source_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  source_desc.Width = 4;
  source_desc.Height = 4;
  source_desc.DepthOrArraySize = 2;
  source_desc.MipLevels = 1;
  source_desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
  source_desc.SampleDesc.Count = 1;
  source_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  ComPtr<ID3D12Resource> source;
  ASSERT_TRUE(SUCCEEDED(context_.device()->CreateCommittedResource(
      &heap_properties, D3D12_HEAP_FLAG_NONE, &source_desc,
      D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(source.put()))));
  auto destination = context_.CreateTexture2D(
      4, 4, 1, DXGI_FORMAT_R32G32B32A32_FLOAT,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  ASSERT_TRUE(source);
  ASSERT_TRUE(destination);

  const std::array<float, 4> expected = {1.0f, 2.0f, 3.0f, 4.0f};
  std::array<std::array<float, 4>, 16> input;
  input.fill(expected);
  ASSERT_TRUE(SUCCEEDED(context_.UploadTextureAndReset(
      source.get(), input.data(), 4 * sizeof(input[0]), sizeof(input), 0)));
  D3D12TestContext::Transition(
      context_.list(), source.get(), D3D12_RESOURCE_STATE_COPY_DEST,
      D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

  auto heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, true);
  ASSERT_TRUE(heap);
  D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
  srv_desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
  srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv_desc.Texture2D.MipLevels = 1;
  context_.device()->CreateShaderResourceView(
      source.get(), &srv_desc, context_.CpuDescriptorHandle(heap.get(), 0));
  context_.device()->CreateUnorderedAccessView(
      destination.get(), nullptr, nullptr,
      context_.CpuDescriptorHandle(heap.get(), 1));

  ID3D12DescriptorHeap *heaps[] = {heap.get()};
  context_.list()->SetDescriptorHeaps(1, heaps);
  context_.list()->SetComputeRootSignature(root_signature.get());
  context_.list()->SetPipelineState(pipeline.get());
  context_.list()->SetComputeRootDescriptorTable(
      0, heap->GetGPUDescriptorHandleForHeapStart());
  context_.list()->Dispatch(1, 1, 1);
  D3D12TestContext::Transition(
      context_.list(), destination.get(),
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_COPY_SOURCE);

  TextureReadback readback;
  ASSERT_TRUE(SUCCEEDED(
      context_.ReadbackTexture(destination.get(), &readback)));
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

TEST_F(D3D12DescriptorSpec,
       PreservesNativeDescriptorDependencyAcrossCommandLists) {
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

  auto root_signature = context_.CreateRootSignature(root_desc);
  ASSERT_TRUE(root_signature);
  const D3D12_SHADER_BYTECODE bytecode = {kNullBufferSrvComputeShader,
                                          sizeof(kNullBufferSrvComputeShader)};
  auto pipeline = context_.CreateComputePipeline(root_signature.get(), bytecode);
  ASSERT_TRUE(pipeline);

  constexpr uint32_t increment = 0x12345678u;
  constexpr uint32_t expected = increment + increment;
  auto intermediate = context_.CreateBuffer(
      sizeof(uint32_t), D3D12_HEAP_TYPE_DEFAULT,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  auto output = context_.CreateBuffer(
      sizeof(uint32_t), D3D12_HEAP_TYPE_DEFAULT,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  ASSERT_TRUE(intermediate);
  ASSERT_TRUE(output);

  auto producer_heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, true);
  auto consumer_heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, true);
  ASSERT_TRUE(producer_heap);
  ASSERT_TRUE(consumer_heap);
  D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
  srv.Format = DXGI_FORMAT_R32_UINT;
  srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
  srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv.Buffer.NumElements = 1;
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
  uav.Format = DXGI_FORMAT_R32_UINT;
  uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav.Buffer.NumElements = 1;
  context_.device()->CreateShaderResourceView(
      nullptr, &srv,
      context_.CpuDescriptorHandle(producer_heap.get(), 0));
  context_.device()->CreateUnorderedAccessView(
      intermediate.get(), nullptr, &uav,
      context_.CpuDescriptorHandle(producer_heap.get(), 1));
  context_.device()->CreateShaderResourceView(
      intermediate.get(), &srv,
      context_.CpuDescriptorHandle(consumer_heap.get(), 0));
  context_.device()->CreateUnorderedAccessView(
      output.get(), nullptr, &uav,
      context_.CpuDescriptorHandle(consumer_heap.get(), 1));

  ComPtr<ID3D12CommandAllocator> producer_allocator;
  ComPtr<ID3D12GraphicsCommandList> producer_list;
  ASSERT_TRUE(SUCCEEDED(context_.device()->CreateCommandAllocator(
      D3D12_COMMAND_LIST_TYPE_DIRECT,
      IID_PPV_ARGS(producer_allocator.put()))));
  ASSERT_TRUE(SUCCEEDED(context_.device()->CreateCommandList(
      0, D3D12_COMMAND_LIST_TYPE_DIRECT, producer_allocator.get(), nullptr,
      IID_PPV_ARGS(producer_list.put()))));
  ID3D12DescriptorHeap *producer_heaps[] = {producer_heap.get()};
  producer_list->SetDescriptorHeaps(1, producer_heaps);
  producer_list->SetComputeRootSignature(root_signature.get());
  producer_list->SetPipelineState(pipeline.get());
  producer_list->SetComputeRootDescriptorTable(
      0, producer_heap->GetGPUDescriptorHandleForHeapStart());
  producer_list->Dispatch(1, 1, 1);
  D3D12TestContext::Transition(
      producer_list.get(), intermediate.get(),
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  ASSERT_TRUE(SUCCEEDED(producer_list->Close()));

  ComPtr<ID3D12CommandAllocator> consumer_allocator;
  ComPtr<ID3D12GraphicsCommandList> consumer_list;
  ASSERT_TRUE(SUCCEEDED(context_.device()->CreateCommandAllocator(
      D3D12_COMMAND_LIST_TYPE_DIRECT,
      IID_PPV_ARGS(consumer_allocator.put()))));
  ASSERT_TRUE(SUCCEEDED(context_.device()->CreateCommandList(
      0, D3D12_COMMAND_LIST_TYPE_DIRECT, consumer_allocator.get(), nullptr,
      IID_PPV_ARGS(consumer_list.put()))));
  ID3D12DescriptorHeap *consumer_heaps[] = {consumer_heap.get()};
  consumer_list->SetDescriptorHeaps(1, consumer_heaps);
  consumer_list->SetComputeRootSignature(root_signature.get());
  consumer_list->SetPipelineState(pipeline.get());
  consumer_list->SetComputeRootDescriptorTable(
      0, consumer_heap->GetGPUDescriptorHandleForHeapStart());
  consumer_list->Dispatch(1, 1, 1);
  D3D12TestContext::Transition(
      consumer_list.get(), output.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_COPY_SOURCE);
  ASSERT_TRUE(SUCCEEDED(consumer_list->Close()));

  ID3D12CommandList *lists[] = {producer_list.get(), consumer_list.get()};
  context_.queue()->ExecuteCommandLists(2, lists);
  ASSERT_TRUE(SUCCEEDED(context_.SignalAndWait()));

  std::vector<uint8_t> readback;
  ASSERT_TRUE(SUCCEEDED(
      context_.ReadbackBuffer(output.get(), sizeof(uint32_t), &readback)));
  ASSERT_EQ(readback.size(), sizeof(uint32_t));
  uint32_t actual = 0;
  std::memcpy(&actual, readback.data(), sizeof(actual));
  EXPECT_EQ(actual, expected);
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
