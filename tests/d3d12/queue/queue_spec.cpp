#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"
#include "shaders/runtime_test_shaders.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using dxmt::test::ColorsMatch;
using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::CreateIsolatedD3D12Device;
using dxmt::test::D3D12TestContext;
using dxmt::test::FullscreenVertexShader;
using dxmt::test::TextureReadback;
using dxmt::test::TextureUavPixelShader;

ComPtr<ID3D12Resource> CreateDefaultTexture2D(
    D3D12TestContext &context, UINT64 width, UINT height, UINT16 array_size,
    DXGI_FORMAT format, UINT sample_count, D3D12_RESOURCE_FLAGS flags,
    D3D12_RESOURCE_STATES state, const D3D12_CLEAR_VALUE *clear_value = nullptr) {
  D3D12_HEAP_PROPERTIES heap = {};
  heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  heap.CreationNodeMask = 1;
  heap.VisibleNodeMask = 1;
  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width = width;
  desc.Height = height;
  desc.DepthOrArraySize = array_size;
  desc.MipLevels = 1;
  desc.Format = format;
  desc.SampleDesc.Count = sample_count;
  desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  desc.Flags = flags;
  ComPtr<ID3D12Resource> resource;
  if (FAILED(context.device()->CreateCommittedResource(
          &heap, D3D12_HEAP_FLAG_NONE, &desc, state, clear_value,
          __uuidof(ID3D12Resource),
          reinterpret_cast<void **>(resource.put()))))
    return {};
  return resource;
}

class LifetimeProbe final : public IUnknown {
public:
  explicit LifetimeProbe(std::shared_ptr<std::atomic_bool> destroyed)
      : destroyed_(std::move(destroyed)) {}

  ~LifetimeProbe() { destroyed_->store(true, std::memory_order_release); }

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **object) override {
    if (!object)
      return E_POINTER;
    *object = nullptr;
    if (iid != __uuidof(IUnknown))
      return E_NOINTERFACE;
    *object = this;
    AddRef();
    return S_OK;
  }

  ULONG STDMETHODCALLTYPE AddRef() override {
    return ref_count_.fetch_add(1, std::memory_order_relaxed) + 1;
  }

  ULONG STDMETHODCALLTYPE Release() override {
    const ULONG refs = ref_count_.fetch_sub(1, std::memory_order_acq_rel) - 1;
    if (!refs)
      delete this;
    return refs;
  }

private:
  std::atomic_ulong ref_count_{1};
  std::shared_ptr<std::atomic_bool> destroyed_;
};

class D3D12QueueSpec : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_TRUE(SUCCEEDED(context_.Initialize())); }

  D3D12TestContext context_;
};

TEST_F(D3D12QueueSpec, CompiledZeroInstanceDrawIsANoOp) {
  auto render_target = context_.CreateTexture2D(
      32, 32, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
      D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
      D3D12_RESOURCE_STATE_RENDER_TARGET);
  auto rtv_heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
  ASSERT_TRUE(render_target);
  ASSERT_TRUE(rtv_heap);
  const auto rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
  context_.device()->CreateRenderTargetView(render_target.get(), nullptr, rtv);

  D3D12_DESCRIPTOR_RANGE uav_range = {};
  uav_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  uav_range.NumDescriptors = 1;
  uav_range.BaseShaderRegister = 1;
  D3D12_ROOT_PARAMETER parameters[2] = {};
  parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[0].DescriptorTable.NumDescriptorRanges = 1;
  parameters[0].DescriptorTable.pDescriptorRanges = &uav_range;
  parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  parameters[1].Constants.ShaderRegister = 0;
  parameters[1].Constants.Num32BitValues = 1;
  parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  root_desc.NumParameters = 2;
  root_desc.pParameters = parameters;
  auto root_signature = context_.CreateRootSignature(root_desc);
  auto pipeline = context_.CreateGraphicsPipeline(
      root_signature.get(), DXGI_FORMAT_R8G8B8A8_UNORM,
      TextureUavPixelShader());
  ASSERT_TRUE(root_signature);
  ASSERT_TRUE(pipeline);

  const float uav_value = 1.0f;
  auto uav_texture = context_.CreateTexture2D(
      1, 1, 1, DXGI_FORMAT_R32_FLOAT,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(uav_texture);
  ASSERT_TRUE(SUCCEEDED(context_.UploadTextureAndReset(
      uav_texture.get(), &uav_value, sizeof(uav_value), sizeof(uav_value))));
  D3D12TestContext::Transition(
      context_.list(), uav_texture.get(), D3D12_RESOURCE_STATE_COPY_DEST,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  auto uav_heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true);
  ASSERT_TRUE(uav_heap);
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
  uav_desc.Format = DXGI_FORMAT_R32_FLOAT;
  uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
  context_.device()->CreateUnorderedAccessView(
      uav_texture.get(), nullptr, &uav_desc,
      uav_heap->GetCPUDescriptorHandleForHeapStart());

  const float clear_color[4] = {};
  context_.list()->ClearRenderTargetView(rtv, clear_color, 0, nullptr);
  context_.list()->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
  context_.list()->SetGraphicsRootSignature(root_signature.get());
  context_.list()->SetPipelineState(pipeline.get());
  ID3D12DescriptorHeap *heaps[] = {uav_heap.get()};
  context_.list()->SetDescriptorHeaps(1, heaps);
  context_.list()->SetGraphicsRootDescriptorTable(
      0, uav_heap->GetGPUDescriptorHandleForHeapStart());
  context_.list()->SetGraphicsRoot32BitConstant(1, 0, 0);
  context_.list()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  const D3D12_VIEWPORT viewport = {0.0f, 0.0f, 32.0f, 32.0f, 0.0f, 1.0f};
  const D3D12_RECT scissor = {0, 0, 32, 32};
  context_.list()->RSSetViewports(1, &viewport);
  context_.list()->RSSetScissorRects(1, &scissor);

  // D3D12 defines this as a no-op. The compiled path must not turn it into an
  // invalid draw, and the following valid draw must still render.
  context_.list()->DrawInstanced(3, 0, 0, 0);
  context_.list()->DrawInstanced(3, 1, 0, 0);
  D3D12TestContext::Transition(
      context_.list(), render_target.get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
      D3D12_RESOURCE_STATE_COPY_SOURCE);

  TextureReadback readback;
  ASSERT_TRUE(SUCCEEDED(
      context_.ReadbackTexture(render_target.get(), &readback)));
  ASSERT_EQ(readback.width, 32u);
  ASSERT_EQ(readback.height, 32u);
  for (UINT y = 0; y < readback.height; ++y) {
    for (UINT x = 0; x < readback.width; ++x) {
      std::uint32_t pixel = 0;
      std::memcpy(&pixel,
                  readback.data.data() + y * readback.row_pitch +
                      x * sizeof(pixel),
                  sizeof(pixel));
      ASSERT_TRUE(ColorsMatch(pixel, 0xffffffff, 0))
          << "pixel (" << x << ", " << y << ") was 0x" << std::hex
          << pixel;
    }
  }
}

TEST_F(D3D12QueueSpec,
       CompiledDrawUsesInBoundsShaderSpanFromOversizedDescriptorRange) {
  auto render_target = context_.CreateTexture2D(
      32, 32, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
      D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
      D3D12_RESOURCE_STATE_RENDER_TARGET);
  auto rtv_heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
  ASSERT_TRUE(render_target);
  ASSERT_TRUE(rtv_heap);
  const auto rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
  context_.device()->CreateRenderTargetView(render_target.get(), nullptr, rtv);

  D3D12_DESCRIPTOR_RANGE uav_range = {};
  uav_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  uav_range.NumDescriptors = 128;
  uav_range.BaseShaderRegister = 1;
  D3D12_ROOT_PARAMETER parameters[2] = {};
  parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[0].DescriptorTable.NumDescriptorRanges = 1;
  parameters[0].DescriptorTable.pDescriptorRanges = &uav_range;
  parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  parameters[1].Constants.ShaderRegister = 0;
  parameters[1].Constants.Num32BitValues = 1;
  parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  root_desc.NumParameters = 2;
  root_desc.pParameters = parameters;
  auto root_signature = context_.CreateRootSignature(root_desc);
  auto pipeline = context_.CreateGraphicsPipeline(
      root_signature.get(), DXGI_FORMAT_R8G8B8A8_UNORM,
      TextureUavPixelShader());
  ASSERT_TRUE(root_signature);
  ASSERT_TRUE(pipeline);

  const float uav_value = 1.0f;
  auto uav_texture = context_.CreateTexture2D(
      1, 1, 1, DXGI_FORMAT_R32_FLOAT,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(uav_texture);
  ASSERT_TRUE(SUCCEEDED(context_.UploadTextureAndReset(
      uav_texture.get(), &uav_value, sizeof(uav_value), sizeof(uav_value))));
  D3D12TestContext::Transition(
      context_.list(), uav_texture.get(), D3D12_RESOURCE_STATE_COPY_DEST,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  auto uav_heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true);
  ASSERT_TRUE(uav_heap);
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
  uav_desc.Format = DXGI_FORMAT_R32_FLOAT;
  uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
  context_.device()->CreateUnorderedAccessView(
      uav_texture.get(), nullptr, &uav_desc,
      uav_heap->GetCPUDescriptorHandleForHeapStart());

  const float clear_color[4] = {};
  context_.list()->ClearRenderTargetView(rtv, clear_color, 0, nullptr);
  context_.list()->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
  context_.list()->SetGraphicsRootSignature(root_signature.get());
  context_.list()->SetPipelineState(pipeline.get());
  ID3D12DescriptorHeap *heaps[] = {uav_heap.get()};
  context_.list()->SetDescriptorHeaps(1, heaps);
  context_.list()->SetGraphicsRootDescriptorTable(
      0, uav_heap->GetGPUDescriptorHandleForHeapStart());
  context_.list()->SetGraphicsRoot32BitConstant(1, 0, 0);
  context_.list()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  const D3D12_VIEWPORT viewport = {0.0f, 0.0f, 32.0f, 32.0f, 0.0f, 1.0f};
  const D3D12_RECT scissor = {0, 0, 32, 32};
  context_.list()->RSSetViewports(1, &viewport);
  context_.list()->RSSetScissorRects(1, &scissor);
  context_.list()->DrawInstanced(3, 1, 0, 0);
  D3D12TestContext::Transition(
      context_.list(), render_target.get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
      D3D12_RESOURCE_STATE_COPY_SOURCE);

  TextureReadback readback;
  ASSERT_TRUE(SUCCEEDED(
      context_.ReadbackTexture(render_target.get(), &readback)));
  ASSERT_EQ(readback.width, 32u);
  ASSERT_EQ(readback.height, 32u);
  for (UINT y = 0; y < readback.height; ++y) {
    for (UINT x = 0; x < readback.width; ++x) {
      std::uint32_t pixel = 0;
      std::memcpy(&pixel,
                  readback.data.data() + y * readback.row_pitch +
                      x * sizeof(pixel),
                  sizeof(pixel));
      ASSERT_TRUE(ColorsMatch(pixel, 0xffffffff, 0))
          << "pixel (" << x << ", " << y << ") was 0x" << std::hex
          << pixel;
    }
  }
}

std::vector<ComPtr<ID3D12GraphicsCommandList>> CreateBarrierOnlyLists(
    D3D12TestContext &context, ID3D12Resource *resource, UINT count,
    std::vector<ComPtr<ID3D12CommandAllocator>> *allocators) {
  std::vector<ComPtr<ID3D12GraphicsCommandList>> lists;
  allocators->reserve(count);
  lists.reserve(count);
  for (UINT index = 0; index < count; ++index) {
    ComPtr<ID3D12CommandAllocator> allocator;
    if (FAILED(context.device()->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            __uuidof(ID3D12CommandAllocator),
            reinterpret_cast<void **>(allocator.put()))))
      return {};
    ComPtr<ID3D12GraphicsCommandList> list;
    if (FAILED(context.device()->CreateCommandList(
            0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.get(), nullptr,
            __uuidof(ID3D12GraphicsCommandList),
            reinterpret_cast<void **>(list.put()))))
      return {};
    D3D12TestContext::UavBarrier(list.get(), resource);
    if (FAILED(list->Close()))
      return {};
    allocators->push_back(std::move(allocator));
    lists.push_back(std::move(list));
  }
  return lists;
}

TEST_F(D3D12QueueSpec, CompletesBufferCopyBeforeFenceSignal) {
  const std::array<std::uint32_t, 16> expected = {
      0x01020304, 0x11121314, 0x21222324, 0x31323334, 0x41424344, 0x51525354,
      0x61626364, 0x71727374, 0x81828384, 0x91929394, 0xa1a2a3a4, 0xb1b2b3b4,
      0xc1c2c3c4, 0xd1d2d3d4, 0xe1e2e3e4, 0xf1f2f3f4,
  };
  ComPtr<ID3D12Resource> upload = context_.CreateUploadBuffer(
      sizeof(expected), expected.data(), sizeof(expected));
  ComPtr<ID3D12Resource> destination = context_.CreateBuffer(
      sizeof(expected), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(upload);
  ASSERT_TRUE(destination);

  context_.list()->CopyBufferRegion(destination.get(), 0, upload.get(), 0,
                                    sizeof(expected));
  ASSERT_TRUE(SUCCEEDED(context_.ExecuteAndWait()));
  ASSERT_TRUE(SUCCEEDED(context_.ResetCommandList()));

  D3D12TestContext::Transition(context_.list(), destination.get(),
                               D3D12_RESOURCE_STATE_COPY_DEST,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
  ASSERT_TRUE(SUCCEEDED(context_.ExecuteAndWait()));
  ASSERT_TRUE(SUCCEEDED(context_.ResetCommandList()));
  std::vector<std::uint8_t> actual;
  ASSERT_TRUE(SUCCEEDED(
      context_.ReadbackBuffer(destination.get(), sizeof(expected), &actual)));
  ASSERT_EQ(actual.size(), sizeof(expected));
  EXPECT_EQ(std::memcmp(actual.data(), expected.data(), sizeof(expected)), 0);
}

TEST_F(D3D12QueueSpec, WritesImmediateValuesOnEveryAdvertisedListType) {
  constexpr UINT kDwordCount = 16;
  constexpr UINT kSentinel = 0xcccccccc;
  const std::array<D3D12_COMMAND_LIST_TYPE, 3> list_types = {
      D3D12_COMMAND_LIST_TYPE_DIRECT,
      D3D12_COMMAND_LIST_TYPE_COMPUTE,
      D3D12_COMMAND_LIST_TYPE_COPY,
  };

  for (const auto type : list_types) {
    std::array<UINT, kDwordCount> initial;
    initial.fill(kSentinel);
    auto upload = context_.CreateUploadBuffer(
        sizeof(initial), initial.data(), sizeof(initial));
    auto destination = context_.CreateBuffer(
        sizeof(initial), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
        D3D12_RESOURCE_STATE_COPY_DEST);
    ASSERT_TRUE(upload);
    ASSERT_TRUE(destination);

    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12GraphicsCommandList2> list2;
    ASSERT_EQ(context_.device()->CreateCommandAllocator(
                  type, __uuidof(ID3D12CommandAllocator),
                  reinterpret_cast<void **>(allocator.put())),
              S_OK);
    ASSERT_EQ(context_.device()->CreateCommandList(
                  0, type, allocator.get(), nullptr,
                  __uuidof(ID3D12GraphicsCommandList),
                  reinterpret_cast<void **>(list.put())),
              S_OK);
    ASSERT_EQ(list->QueryInterface(
                  __uuidof(ID3D12GraphicsCommandList2),
                  reinterpret_cast<void **>(list2.put())),
              S_OK);

    list->CopyBufferRegion(destination.get(), 0, upload.get(), 0,
                           sizeof(initial));
    const auto base = destination->GetGPUVirtualAddress();
    const D3D12_WRITEBUFFERIMMEDIATE_PARAMETER default_write = {
        base, 0x01020304};
    list2->WriteBufferImmediate(1, &default_write, nullptr);
    const std::array<D3D12_WRITEBUFFERIMMEDIATE_PARAMETER, 3> writes = {{
        {base + sizeof(UINT), 0x11121314},
        {base + 5 * sizeof(UINT), 0x51525354},
        {base + (kDwordCount - 1) * sizeof(UINT), 0xf1f2f3f4},
    }};
    const std::array<D3D12_WRITEBUFFERIMMEDIATE_MODE, 3> modes = {
        D3D12_WRITEBUFFERIMMEDIATE_MODE_DEFAULT,
        D3D12_WRITEBUFFERIMMEDIATE_MODE_MARKER_IN,
        D3D12_WRITEBUFFERIMMEDIATE_MODE_MARKER_OUT,
    };
    list2->WriteBufferImmediate(static_cast<UINT>(writes.size()),
                                writes.data(), modes.data());
    ASSERT_EQ(list->Close(), S_OK);

    D3D12_COMMAND_QUEUE_DESC queue_desc = {};
    queue_desc.Type = type;
    ComPtr<ID3D12CommandQueue> queue;
    ASSERT_EQ(context_.device()->CreateCommandQueue(
                  &queue_desc, __uuidof(ID3D12CommandQueue),
                  reinterpret_cast<void **>(queue.put())),
              S_OK);
    ID3D12CommandList *submission[] = {list.get()};
    queue->ExecuteCommandLists(1, submission);
    ComPtr<ID3D12Fence> fence;
    ASSERT_EQ(context_.device()->CreateFence(
                  0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence),
                  reinterpret_cast<void **>(fence.put())),
              S_OK);
    ASSERT_EQ(queue->Signal(fence.get(), 1), S_OK);
    ASSERT_EQ(context_.WaitForFence(fence.get(), 1), S_OK);

    D3D12TestContext::Transition(
        context_.list(), destination.get(), D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_COPY_SOURCE);
    std::vector<std::uint8_t> bytes;
    ASSERT_EQ(context_.ReadbackBuffer(destination.get(), sizeof(initial),
                                      &bytes),
              S_OK);
    ASSERT_EQ(bytes.size(), sizeof(initial));
    std::array<UINT, kDwordCount> actual = {};
    std::memcpy(actual.data(), bytes.data(), bytes.size());
    initial[0] = default_write.Value;
    initial[1] = writes[0].Value;
    initial[5] = writes[1].Value;
    initial.back() = writes[2].Value;
    EXPECT_EQ(actual, initial) << "command list type " << type;
    ASSERT_EQ(context_.ResetCommandList(), S_OK);
  }
}

TEST_F(D3D12QueueSpec, ExecutesWriteBufferImmediateFromBundle) {
  constexpr std::array<UINT, 4> initial = {};
  auto upload = context_.CreateUploadBuffer(
      sizeof(initial), initial.data(), sizeof(initial));
  auto destination = context_.CreateBuffer(
      sizeof(initial), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(upload);
  ASSERT_TRUE(destination);
  context_.list()->CopyBufferRegion(destination.get(), 0, upload.get(), 0,
                                    sizeof(initial));
  ASSERT_EQ(context_.ExecuteAndWait(), S_OK);
  ASSERT_EQ(context_.ResetCommandList(), S_OK);

  ComPtr<ID3D12CommandAllocator> allocator;
  ComPtr<ID3D12GraphicsCommandList> bundle;
  ComPtr<ID3D12GraphicsCommandList2> bundle2;
  ASSERT_EQ(context_.device()->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_BUNDLE,
                __uuidof(ID3D12CommandAllocator),
                reinterpret_cast<void **>(allocator.put())),
            S_OK);
  ASSERT_EQ(context_.device()->CreateCommandList(
                0, D3D12_COMMAND_LIST_TYPE_BUNDLE, allocator.get(), nullptr,
                __uuidof(ID3D12GraphicsCommandList),
                reinterpret_cast<void **>(bundle.put())),
            S_OK);
  ASSERT_EQ(bundle->QueryInterface(
                __uuidof(ID3D12GraphicsCommandList2),
                reinterpret_cast<void **>(bundle2.put())),
            S_OK);
  const D3D12_WRITEBUFFERIMMEDIATE_PARAMETER write = {
      destination->GetGPUVirtualAddress() + sizeof(UINT), 0xdecafbad};
  bundle2->WriteBufferImmediate(1, &write, nullptr);
  ASSERT_EQ(bundle->Close(), S_OK);

  context_.list()->ExecuteBundle(bundle.get());
  context_.list()->ExecuteBundle(bundle.get());
  D3D12TestContext::Transition(
      context_.list(), destination.get(), D3D12_RESOURCE_STATE_COPY_DEST,
      D3D12_RESOURCE_STATE_COPY_SOURCE);
  std::vector<std::uint8_t> bytes;
  ASSERT_EQ(context_.ReadbackBuffer(destination.get(), sizeof(initial),
                                    &bytes),
            S_OK);
  std::array<UINT, initial.size()> actual = {};
  ASSERT_EQ(bytes.size(), sizeof(actual));
  std::memcpy(actual.data(), bytes.data(), bytes.size());
  EXPECT_EQ(actual, (std::array<UINT, 4>{0, write.Value, 0, 0}));
}

TEST_F(D3D12QueueSpec, ReusesBundleWithInheritedAndReturnedGraphicsState) {
  const auto pixel_shader = CompileShader(R"(
    cbuffer Color : register(b0) { float value; };
    float4 main() : SV_Target { return value.xxxx; }
  )",
                                          "ps_5_0");
  ASSERT_EQ(pixel_shader.result, S_OK) << pixel_shader.diagnostic_text();
  D3D12_ROOT_PARAMETER parameter = {};
  parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  parameter.Constants.ShaderRegister = 0;
  parameter.Constants.Num32BitValues = 1;
  parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  root_desc.NumParameters = 1;
  root_desc.pParameters = &parameter;
  root_desc.Flags =
      D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
  auto root_signature = context_.CreateRootSignature(root_desc);
  ASSERT_TRUE(root_signature);

  D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline_desc = {};
  pipeline_desc.pRootSignature = root_signature.get();
  pipeline_desc.VS = FullscreenVertexShader();
  pipeline_desc.PS = {pixel_shader.bytecode->GetBufferPointer(),
                      pixel_shader.bytecode->GetBufferSize()};
  auto &blend = pipeline_desc.BlendState.RenderTarget[0];
  blend.BlendEnable = TRUE;
  blend.SrcBlend = D3D12_BLEND_ONE;
  blend.DestBlend = D3D12_BLEND_ONE;
  blend.BlendOp = D3D12_BLEND_OP_ADD;
  blend.SrcBlendAlpha = D3D12_BLEND_ONE;
  blend.DestBlendAlpha = D3D12_BLEND_ONE;
  blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
  blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
  pipeline_desc.SampleMask = UINT_MAX;
  pipeline_desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
  pipeline_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  pipeline_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  pipeline_desc.NumRenderTargets = 1;
  pipeline_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
  pipeline_desc.SampleDesc.Count = 1;
  ComPtr<ID3D12PipelineState> pipeline;
  ASSERT_EQ(context_.device()->CreateGraphicsPipelineState(
                &pipeline_desc, __uuidof(ID3D12PipelineState),
                reinterpret_cast<void **>(pipeline.put())),
            S_OK);

  auto target = context_.CreateTexture2D(
      8, 8, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
      D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
      D3D12_RESOURCE_STATE_RENDER_TARGET);
  auto rtv_heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
  constexpr std::array<UINT16, 3> indices = {0, 1, 2};
  auto index_buffer = context_.CreateUploadBuffer(
      sizeof(indices), indices.data(), sizeof(indices));
  ASSERT_TRUE(target);
  ASSERT_TRUE(rtv_heap);
  ASSERT_TRUE(index_buffer);
  const auto rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
  context_.device()->CreateRenderTargetView(target.get(), nullptr, rtv);

  ComPtr<ID3D12CommandAllocator> bundle_allocator;
  ComPtr<ID3D12GraphicsCommandList> bundle;
  ASSERT_EQ(context_.device()->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_BUNDLE,
                __uuidof(ID3D12CommandAllocator),
                reinterpret_cast<void **>(bundle_allocator.put())),
            S_OK);
  ASSERT_EQ(context_.device()->CreateCommandList(
                0, D3D12_COMMAND_LIST_TYPE_BUNDLE, bundle_allocator.get(),
                pipeline.get(), __uuidof(ID3D12GraphicsCommandList),
                reinterpret_cast<void **>(bundle.put())),
            S_OK);
  bundle->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  bundle->DrawIndexedInstanced(3, 1, 0, 0, 0);
  ASSERT_EQ(bundle->Close(), S_OK);

  constexpr FLOAT clear[4] = {};
  context_.list()->ClearRenderTargetView(rtv, clear, 0, nullptr);
  context_.list()->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
  context_.list()->SetGraphicsRootSignature(root_signature.get());
  context_.list()->SetGraphicsRoot32BitConstant(
      0, std::bit_cast<UINT>(0.25f), 0);
  const D3D12_INDEX_BUFFER_VIEW index_view = {
      index_buffer->GetGPUVirtualAddress(), sizeof(indices),
      DXGI_FORMAT_R16_UINT};
  context_.list()->IASetIndexBuffer(&index_view);
  const D3D12_VIEWPORT viewport = {0.0f, 0.0f, 8.0f, 8.0f, 0.0f, 1.0f};
  const D3D12_RECT scissor = {0, 0, 8, 8};
  context_.list()->RSSetViewports(1, &viewport);
  context_.list()->RSSetScissorRects(1, &scissor);
  context_.list()->ExecuteBundle(bundle.get());
  context_.list()->ExecuteBundle(bundle.get());
  context_.list()->DrawIndexedInstanced(3, 1, 0, 0, 0);
  bundle.reset();
  bundle_allocator.reset();
  D3D12TestContext::Transition(
      context_.list(), target.get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
      D3D12_RESOURCE_STATE_COPY_SOURCE);

  TextureReadback readback;
  ASSERT_EQ(context_.ReadbackTexture(target.get(), &readback), S_OK);
  for (UINT y = 0; y < readback.height; ++y) {
    for (UINT x = 0; x < readback.width; ++x) {
      UINT pixel = 0;
      std::memcpy(&pixel,
                  readback.data.data() + y * readback.row_pitch +
                      x * sizeof(pixel),
                  sizeof(pixel));
      EXPECT_TRUE(ColorsMatch(pixel, 0xbfbfbfbf, 2));
    }
  }
}

TEST_F(D3D12QueueSpec, ValidatesExecuteBundleContractsOnClose) {
  ComPtr<ID3D12CommandAllocator> open_bundle_allocator;
  ComPtr<ID3D12GraphicsCommandList> open_bundle;
  ASSERT_EQ(context_.device()->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_BUNDLE,
                __uuidof(ID3D12CommandAllocator),
                reinterpret_cast<void **>(open_bundle_allocator.put())),
            S_OK);
  ASSERT_EQ(context_.device()->CreateCommandList(
                0, D3D12_COMMAND_LIST_TYPE_BUNDLE,
                open_bundle_allocator.get(), nullptr,
                __uuidof(ID3D12GraphicsCommandList),
                reinterpret_cast<void **>(open_bundle.put())),
            S_OK);
  context_.list()->ExecuteBundle(open_bundle.get());
  EXPECT_EQ(context_.list()->Close(), E_INVALIDARG);

  ComPtr<ID3D12CommandAllocator> closed_bundle_allocator;
  ComPtr<ID3D12GraphicsCommandList> closed_bundle;
  ASSERT_EQ(context_.device()->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_BUNDLE,
                __uuidof(ID3D12CommandAllocator),
                reinterpret_cast<void **>(closed_bundle_allocator.put())),
            S_OK);
  ASSERT_EQ(context_.device()->CreateCommandList(
                0, D3D12_COMMAND_LIST_TYPE_BUNDLE,
                closed_bundle_allocator.get(), nullptr,
                __uuidof(ID3D12GraphicsCommandList),
                reinterpret_cast<void **>(closed_bundle.put())),
            S_OK);
  ASSERT_EQ(closed_bundle->Close(), S_OK);

  ComPtr<ID3D12CommandAllocator> caller_allocator;
  ComPtr<ID3D12GraphicsCommandList> caller_bundle;
  ASSERT_EQ(context_.device()->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_BUNDLE,
                __uuidof(ID3D12CommandAllocator),
                reinterpret_cast<void **>(caller_allocator.put())),
            S_OK);
  ASSERT_EQ(context_.device()->CreateCommandList(
                0, D3D12_COMMAND_LIST_TYPE_BUNDLE, caller_allocator.get(),
                nullptr, __uuidof(ID3D12GraphicsCommandList),
                reinterpret_cast<void **>(caller_bundle.put())),
            S_OK);
  caller_bundle->ExecuteBundle(closed_bundle.get());
  EXPECT_EQ(caller_bundle->Close(), S_OK);
}

TEST_F(D3D12QueueSpec, DiscardAllowsCompleteAndRectangularOverwrite) {
  constexpr UINT kWidth = 8;
  constexpr UINT kHeight = 6;
  auto texture = context_.CreateTexture2D(
      kWidth, kHeight, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
      D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
      D3D12_RESOURCE_STATE_RENDER_TARGET);
  auto rtv_heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
  ASSERT_TRUE(texture);
  ASSERT_TRUE(rtv_heap);
  const auto rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
  context_.device()->CreateRenderTargetView(texture.get(), nullptr, rtv);

  constexpr float blue[4] = {0.0f, 0.0f, 1.0f, 1.0f};
  constexpr float green[4] = {0.0f, 1.0f, 0.0f, 1.0f};
  context_.list()->DiscardResource(texture.get(), nullptr);
  context_.list()->ClearRenderTargetView(rtv, blue, 0, nullptr);

  const D3D12_RECT rect = {2, 1, 7, 5};
  const D3D12_DISCARD_REGION region = {1, &rect, 0, 1};
  context_.list()->DiscardResource(texture.get(), &region);
  context_.list()->ClearRenderTargetView(rtv, green, 1, &rect);
  D3D12TestContext::Transition(
      context_.list(), texture.get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
      D3D12_RESOURCE_STATE_COPY_SOURCE);

  TextureReadback readback;
  ASSERT_EQ(context_.ReadbackTexture(texture.get(), &readback), S_OK);
  for (UINT y = 0; y < kHeight; ++y) {
    for (UINT x = 0; x < kWidth; ++x) {
      UINT pixel = 0;
      std::memcpy(&pixel,
                  readback.data.data() + y * readback.row_pitch +
                      x * sizeof(pixel),
                  sizeof(pixel));
      const bool overwritten = x >= UINT(rect.left) && x < UINT(rect.right) &&
                               y >= UINT(rect.top) && y < UINT(rect.bottom);
      EXPECT_TRUE(ColorsMatch(pixel, overwritten ? 0xff00ff00 : 0xffff0000,
                              0))
          << "pixel (" << x << ", " << y << ")";
    }
  }
}

TEST_F(D3D12QueueSpec, RejectsForeignDiscardAndAllowsFreshListRecovery) {
  auto foreign_device = CreateIsolatedD3D12Device();
  ASSERT_TRUE(foreign_device);
  D3D12TestContext foreign_context;
  ASSERT_EQ(foreign_context.Initialize(foreign_device.get()), S_OK);
  auto foreign_resource = foreign_context.CreateBuffer(
      16, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COMMON);
  ASSERT_TRUE(foreign_resource);

  context_.list()->DiscardResource(foreign_resource.get(), nullptr);
  EXPECT_EQ(context_.list()->Close(), E_INVALIDARG);

  auto local_resource = context_.CreateBuffer(
      16, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COMMON);
  ASSERT_TRUE(local_resource);
  ComPtr<ID3D12CommandAllocator> allocator;
  ComPtr<ID3D12GraphicsCommandList> list;
  ASSERT_EQ(context_.device()->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(allocator.put())),
            S_OK);
  ASSERT_EQ(context_.device()->CreateCommandList(
                0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.get(), nullptr,
                IID_PPV_ARGS(list.put())),
            S_OK);
  list->DiscardResource(local_resource.get(), nullptr);
  EXPECT_EQ(list->Close(), S_OK);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D12QueueSpec, CompletesFenceSignalsInValueOrder) {
  ComPtr<ID3D12Fence> fence;
  ASSERT_TRUE(SUCCEEDED(context_.device()->CreateFence(
      0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence),
      reinterpret_cast<void **>(fence.put()))));
  ASSERT_TRUE(SUCCEEDED(context_.queue()->Signal(fence.get(), 3)));
  ASSERT_TRUE(SUCCEEDED(context_.queue()->Signal(fence.get(), 7)));
  ASSERT_TRUE(SUCCEEDED(context_.WaitForFence(fence.get(), 3)));
  EXPECT_GE(fence->GetCompletedValue(), 3ull);
  ASSERT_TRUE(SUCCEEDED(context_.WaitForFence(fence.get(), 7)));
  EXPECT_GE(fence->GetCompletedValue(), 7ull);
}

TEST_F(D3D12QueueSpec, SignalsFenceEventsForCompletedAndFutureValues) {
  ComPtr<ID3D12Fence> fence;
  ASSERT_TRUE(SUCCEEDED(context_.device()->CreateFence(
      4, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence),
      reinterpret_cast<void **>(fence.put()))));
  HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  ASSERT_NE(event, nullptr);

  ASSERT_TRUE(SUCCEEDED(fence->SetEventOnCompletion(3, event)));
  EXPECT_EQ(WaitForSingleObject(event, 0), WAIT_OBJECT_0);
  ASSERT_TRUE(SUCCEEDED(fence->SetEventOnCompletion(6, event)));
  EXPECT_EQ(WaitForSingleObject(event, 0), WAIT_TIMEOUT);
  ASSERT_TRUE(SUCCEEDED(context_.queue()->Signal(fence.get(), 6)));
  EXPECT_EQ(WaitForSingleObject(event, 5000), WAIT_OBJECT_0);
  EXPECT_GE(fence->GetCompletedValue(), 6u);

  CloseHandle(event);
}

TEST_F(D3D12QueueSpec, InvalidExecuteBatchesAreAtomicAndRecoverable) {
  constexpr UINT sentinel = 0x13579bdfu;
  constexpr UINT expected = 0xdeadbeefu;
  auto source = context_.CreateUploadBuffer(
      sizeof(expected), &expected, sizeof(expected));
  auto destination = context_.CreateBuffer(
      sizeof(sentinel), D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(source);
  ASSERT_TRUE(destination);

  void *mapped = nullptr;
  D3D12_RANGE empty_read = {0, 0};
  ASSERT_EQ(destination->Map(0, &empty_read, &mapped), S_OK);
  ASSERT_NE(mapped, nullptr);
  std::memcpy(mapped, &sentinel, sizeof(sentinel));
  D3D12_RANGE written = {0, sizeof(sentinel)};
  destination->Unmap(0, &written);

  context_.list()->CopyBufferRegion(destination.get(), 0, source.get(), 0,
                                    sizeof(expected));
  ASSERT_EQ(context_.list()->Close(), S_OK);

  auto foreign_device = CreateIsolatedD3D12Device();
  ASSERT_TRUE(foreign_device);
  D3D12TestContext foreign_context;
  ASSERT_EQ(foreign_context.Initialize(foreign_device.get()), S_OK);
  ASSERT_EQ(foreign_context.list()->Close(), S_OK);

  ComPtr<ID3D12CommandAllocator> compute_allocator;
  ComPtr<ID3D12GraphicsCommandList> compute_list;
  ASSERT_EQ(context_.device()->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_COMPUTE,
                IID_PPV_ARGS(compute_allocator.put())),
            S_OK);
  ASSERT_EQ(context_.device()->CreateCommandList(
                0, D3D12_COMMAND_LIST_TYPE_COMPUTE, compute_allocator.get(),
                nullptr, IID_PPV_ARGS(compute_list.put())),
            S_OK);
  ASSERT_EQ(compute_list->Close(), S_OK);

  ComPtr<ID3D12CommandAllocator> recording_allocator;
  ComPtr<ID3D12GraphicsCommandList> recording_list;
  ASSERT_EQ(context_.device()->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                IID_PPV_ARGS(recording_allocator.put())),
            S_OK);
  ASSERT_EQ(context_.device()->CreateCommandList(
                0, D3D12_COMMAND_LIST_TYPE_DIRECT, recording_allocator.get(),
                nullptr, IID_PPV_ARGS(recording_list.put())),
            S_OK);

  ComPtr<ID3D12Fence> fence;
  ASSERT_EQ(context_.device()->CreateFence(
                0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence.put())),
            S_OK);
  ASSERT_TRUE(fence);
  UINT64 fence_value = 0;
  auto flush_queue = [&] {
    const HRESULT signal =
        context_.queue()->Signal(fence.get(), ++fence_value);
    return FAILED(signal) ? signal
                          : context_.WaitForFence(fence.get(), fence_value);
  };
  auto read_destination = [&] {
    UINT value = 0;
    void *data = nullptr;
    D3D12_RANGE read = {0, sizeof(value)};
    if (FAILED(destination->Map(0, &read, &data)) || !data)
      return UINT_MAX;
    std::memcpy(&value, data, sizeof(value));
    D3D12_RANGE no_write = {0, 0};
    destination->Unmap(0, &no_write);
    return value;
  };

  ID3D12CommandList *with_null[] = {context_.list(), nullptr};
  context_.queue()->ExecuteCommandLists(std::size(with_null), with_null);
  ASSERT_EQ(flush_queue(), S_OK);
  EXPECT_EQ(read_destination(), sentinel);

  ID3D12CommandList *with_foreign[] = {context_.list(),
                                       foreign_context.list()};
  context_.queue()->ExecuteCommandLists(std::size(with_foreign), with_foreign);
  ASSERT_EQ(flush_queue(), S_OK);
  EXPECT_EQ(read_destination(), sentinel);

  ID3D12CommandList *with_wrong_type[] = {context_.list(), compute_list.get()};
  context_.queue()->ExecuteCommandLists(std::size(with_wrong_type),
                                        with_wrong_type);
  ASSERT_EQ(flush_queue(), S_OK);
  EXPECT_EQ(read_destination(), sentinel);

  ID3D12CommandList *with_recording[] = {context_.list(),
                                         recording_list.get()};
  context_.queue()->ExecuteCommandLists(std::size(with_recording),
                                        with_recording);
  ASSERT_EQ(flush_queue(), S_OK);
  EXPECT_EQ(read_destination(), sentinel);

  ID3D12CommandList *valid[] = {context_.list()};
  context_.queue()->ExecuteCommandLists(std::size(valid), valid);
  ASSERT_EQ(flush_queue(), S_OK);
  EXPECT_EQ(read_destination(), expected);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D12QueueSpec, PreservesPartialBufferCopiesAcrossSubmissions) {
  std::array<std::uint8_t, 64> source_data = {};
  std::array<std::uint8_t, 64> expected = {};
  for (std::size_t i = 0; i < source_data.size(); ++i)
    source_data[i] = static_cast<std::uint8_t>(i * 3u + 1u);
  expected.fill(0xcc);

  auto source = context_.CreateUploadBuffer(
      source_data.size(), source_data.data(), source_data.size());
  auto initial = context_.CreateUploadBuffer(
      expected.size(), expected.data(), expected.size());
  auto destination = context_.CreateBuffer(
      expected.size(), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(source);
  ASSERT_TRUE(initial);
  ASSERT_TRUE(destination);

  context_.list()->CopyBufferRegion(destination.get(), 0, initial.get(), 0,
                                    expected.size());
  context_.list()->CopyBufferRegion(destination.get(), 7, source.get(), 5, 9);
  std::copy(source_data.begin() + 5, source_data.begin() + 14,
            expected.begin() + 7);
  ASSERT_TRUE(SUCCEEDED(context_.ExecuteAndWait()));
  ASSERT_TRUE(SUCCEEDED(context_.ResetCommandList()));

  context_.list()->CopyBufferRegion(destination.get(), 40, source.get(), 30,
                                    11);
  std::copy(source_data.begin() + 30, source_data.begin() + 41,
            expected.begin() + 40);
  D3D12TestContext::Transition(
      context_.list(), destination.get(), D3D12_RESOURCE_STATE_COPY_DEST,
      D3D12_RESOURCE_STATE_COPY_SOURCE);
  std::vector<std::uint8_t> actual;
  ASSERT_TRUE(SUCCEEDED(context_.ReadbackBuffer(
      destination.get(), expected.size(), &actual)));
  EXPECT_EQ(actual, (std::vector<std::uint8_t>(expected.begin(),
                                                expected.end())));
}

TEST_F(D3D12QueueSpec, ExecutesDependentCommandListsInArrayOrder) {
  const std::array<std::uint32_t, 8> expected = {
      0x01020304, 0x11121314, 0x21222324, 0x31323334,
      0x41424344, 0x51525354, 0x61626364, 0x71727374,
  };
  auto upload = context_.CreateUploadBuffer(sizeof(expected), expected.data(),
                                             sizeof(expected));
  auto intermediate = context_.CreateBuffer(
      sizeof(expected), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  auto destination = context_.CreateBuffer(
      sizeof(expected), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(upload);
  ASSERT_TRUE(intermediate);
  ASSERT_TRUE(destination);

  std::array<ComPtr<ID3D12CommandAllocator>, 2> allocators;
  std::array<ComPtr<ID3D12GraphicsCommandList>, 2> lists;
  for (UINT index = 0; index < lists.size(); ++index) {
    ASSERT_TRUE(SUCCEEDED(context_.device()->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator),
        reinterpret_cast<void **>(allocators[index].put()))));
    ASSERT_TRUE(SUCCEEDED(context_.device()->CreateCommandList(
        0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocators[index].get(), nullptr,
        __uuidof(ID3D12GraphicsCommandList),
        reinterpret_cast<void **>(lists[index].put()))));
  }

  lists[0]->CopyBufferRegion(intermediate.get(), 0, upload.get(), 0,
                             sizeof(expected));
  D3D12TestContext::Transition(
      lists[0].get(), intermediate.get(), D3D12_RESOURCE_STATE_COPY_DEST,
      D3D12_RESOURCE_STATE_COPY_SOURCE);
  lists[1]->CopyBufferRegion(destination.get(), 0, intermediate.get(), 0,
                             sizeof(expected));
  D3D12TestContext::Transition(
      lists[1].get(), destination.get(), D3D12_RESOURCE_STATE_COPY_DEST,
      D3D12_RESOURCE_STATE_COPY_SOURCE);
  ASSERT_TRUE(SUCCEEDED(lists[0]->Close()));
  ASSERT_TRUE(SUCCEEDED(lists[1]->Close()));

  ID3D12CommandList *submission[] = {lists[0].get(), lists[1].get()};
  context_.queue()->ExecuteCommandLists(ARRAYSIZE(submission), submission);
  ASSERT_TRUE(SUCCEEDED(context_.SignalAndWait()));

  std::vector<std::uint8_t> actual;
  ASSERT_TRUE(SUCCEEDED(context_.ReadbackBuffer(
      destination.get(), sizeof(expected), &actual)));
  ASSERT_EQ(actual.size(), sizeof(expected));
  EXPECT_EQ(std::memcmp(actual.data(), expected.data(), sizeof(expected)), 0);
}

TEST_F(D3D12QueueSpec, CopiesTextureRegionAtNonZeroOffsets) {
  constexpr UINT source_width = 5;
  constexpr UINT source_height = 4;
  constexpr UINT destination_width = 8;
  constexpr UINT destination_height = 6;
  const std::array<std::uint32_t, source_width *source_height> source_data = {
      0xff000001, 0xff000002, 0xff000003, 0xff000004, 0xff000005,
      0xff000011, 0xff000012, 0xff000013, 0xff000014, 0xff000015,
      0xff000021, 0xff000022, 0xff000023, 0xff000024, 0xff000025,
      0xff000031, 0xff000032, 0xff000033, 0xff000034, 0xff000035,
  };
  std::array<std::uint32_t, destination_width * destination_height>
      destination_initial;
  destination_initial.fill(0x7f334455);
  auto expected = destination_initial;
  for (UINT y = 0; y < 2; ++y) {
    for (UINT x = 0; x < 3; ++x) {
      expected[(y + 2) * destination_width + x + 3] =
          source_data[(y + 1) * source_width + x + 1];
    }
  }

  ComPtr<ID3D12Resource> source = context_.CreateTexture2D(
      source_width, source_height, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
      D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
  ComPtr<ID3D12Resource> destination = context_.CreateTexture2D(
      destination_width, destination_height, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
      D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(source);
  ASSERT_TRUE(destination);
  ASSERT_TRUE(SUCCEEDED(context_.UploadTextureAndReset(
      source.get(), source_data.data(), source_width * sizeof(std::uint32_t),
      source_data.size() * sizeof(std::uint32_t))));
  ASSERT_TRUE(SUCCEEDED(context_.UploadTextureAndReset(
      destination.get(), destination_initial.data(),
      destination_width * sizeof(std::uint32_t),
      expected.size() * sizeof(std::uint32_t))));

  D3D12_TEXTURE_COPY_LOCATION source_location = {};
  source_location.pResource = source.get();
  source_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  D3D12_TEXTURE_COPY_LOCATION destination_location = {};
  destination_location.pResource = destination.get();
  destination_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  D3D12_BOX source_box = {1, 1, 0, 4, 3, 1};
  context_.list()->CopyTextureRegion(&destination_location, 3, 2, 0,
                                     &source_location, &source_box);
  D3D12TestContext::Transition(context_.list(), destination.get(),
                               D3D12_RESOURCE_STATE_COPY_DEST,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);

  TextureReadback readback;
  ASSERT_TRUE(
      SUCCEEDED(context_.ReadbackTexture(destination.get(), &readback)));
  ASSERT_EQ(readback.width, destination_width);
  ASSERT_EQ(readback.height, destination_height);
  for (UINT y = 0; y < destination_height; ++y) {
    const auto *row = readback.data.data() + y * readback.row_pitch;
    EXPECT_EQ(std::memcmp(row, expected.data() + y * destination_width,
                          destination_width * sizeof(std::uint32_t)),
              0)
        << "row " << y;
  }
}

TEST_F(D3D12QueueSpec, ReportsTimestampFrequency) {
  UINT64 frequency = 0;
  ASSERT_TRUE(SUCCEEDED(context_.queue()->GetTimestampFrequency(&frequency)));
  EXPECT_GT(frequency, 0ull);
}

TEST_F(D3D12QueueSpec, ClockCalibrationRejectsNullAndIsMonotonic) {
  EXPECT_EQ(context_.queue()->GetTimestampFrequency(nullptr), E_INVALIDARG);

  constexpr UINT64 sentinel = 0xdeadbeefcafef00dull;
  UINT64 gpu_timestamp = sentinel;
  UINT64 cpu_timestamp = sentinel;
  EXPECT_EQ(context_.queue()->GetClockCalibration(nullptr, &cpu_timestamp),
            E_INVALIDARG);
  EXPECT_EQ(cpu_timestamp, sentinel);
  EXPECT_EQ(context_.queue()->GetClockCalibration(&gpu_timestamp, nullptr),
            E_INVALIDARG);
  EXPECT_EQ(gpu_timestamp, sentinel);
  EXPECT_EQ(context_.queue()->GetClockCalibration(nullptr, nullptr),
            E_INVALIDARG);

  UINT64 first_gpu = 0;
  UINT64 first_cpu = 0;
  UINT64 second_gpu = 0;
  UINT64 second_cpu = 0;
  ASSERT_EQ(context_.queue()->GetClockCalibration(&first_gpu, &first_cpu),
            S_OK);
  ASSERT_EQ(context_.queue()->GetClockCalibration(&second_gpu, &second_cpu),
            S_OK);
  EXPECT_GT(first_gpu, 0u);
  EXPECT_GT(first_cpu, 0u);
  EXPECT_GE(second_gpu, first_gpu);
  EXPECT_GE(second_cpu, first_cpu);
}

TEST_F(D3D12QueueSpec, PreservesLongBlitDependencyChainAcrossEncoders) {
  constexpr UINT kLinkCount = 32;
  const std::array<std::uint32_t, 16> expected = {
      0x0badc0de, 0x10203040, 0x50607080, 0x90a0b0c0,
      0xd0e0f001, 0x12345678, 0x89abcdef, 0xfedcba98,
      0x76543210, 0x13579bdf, 0x2468ace0, 0x55aa55aa,
      0xaa55aa55, 0x01010101, 0x7f7f7f7f, 0xffffffff,
  };
  auto upload = context_.CreateUploadBuffer(
      sizeof(expected), expected.data(), sizeof(expected));
  ASSERT_TRUE(upload);

  std::array<ComPtr<ID3D12Resource>, kLinkCount> buffers;
  for (auto &buffer : buffers) {
    buffer = context_.CreateBuffer(
        sizeof(expected), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
        D3D12_RESOURCE_STATE_COPY_DEST);
    ASSERT_TRUE(buffer);
  }

  context_.list()->CopyBufferRegion(buffers[0].get(), 0, upload.get(), 0,
                                    sizeof(expected));
  D3D12TestContext::Transition(
      context_.list(), buffers[0].get(), D3D12_RESOURCE_STATE_COPY_DEST,
      D3D12_RESOURCE_STATE_COPY_SOURCE);
  for (UINT index = 1; index < kLinkCount; ++index) {
    context_.list()->CopyBufferRegion(
        buffers[index].get(), 0, buffers[index - 1].get(), 0,
        sizeof(expected));
    D3D12TestContext::Transition(
        context_.list(), buffers[index].get(), D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_COPY_SOURCE);
  }

  std::vector<std::uint8_t> actual;
  ASSERT_TRUE(SUCCEEDED(context_.ReadbackBuffer(
      buffers.back().get(), sizeof(expected), &actual)));
  ASSERT_EQ(actual.size(), sizeof(expected));
  EXPECT_EQ(std::memcmp(actual.data(), expected.data(), sizeof(expected)), 0);
}

TEST_F(D3D12QueueSpec, ExecutesManyBarrierOnlyCommandLists) {
  constexpr UINT kListCount = 1000;
  auto resource = context_.CreateBuffer(
      4096, D3D12_HEAP_TYPE_DEFAULT,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  ASSERT_TRUE(resource);
  std::vector<ComPtr<ID3D12CommandAllocator>> allocators;
  auto lists = CreateBarrierOnlyLists(context_, resource.get(), kListCount,
                                      &allocators);
  ASSERT_EQ(lists.size(), kListCount);
  std::vector<ID3D12CommandList *> raw_lists;
  for (const auto &list : lists)
    raw_lists.push_back(list.get());
  context_.queue()->ExecuteCommandLists(static_cast<UINT>(raw_lists.size()),
                                        raw_lists.data());
  ASSERT_TRUE(SUCCEEDED(context_.SignalAndWait()));
}

TEST_F(D3D12QueueSpec, ExecutesBarrierOnlyListsAcrossSeparateSubmissions) {
  constexpr UINT kExecuteCount = 64;
  auto resource = context_.CreateBuffer(
      4096, D3D12_HEAP_TYPE_DEFAULT,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  ASSERT_TRUE(resource);
  std::vector<ComPtr<ID3D12CommandAllocator>> allocators;
  auto lists = CreateBarrierOnlyLists(context_, resource.get(), kExecuteCount,
                                      &allocators);
  ASSERT_EQ(lists.size(), kExecuteCount);
  for (const auto &list : lists) {
    ID3D12CommandList *raw_list = list.get();
    context_.queue()->ExecuteCommandLists(1, &raw_list);
  }
  ASSERT_TRUE(SUCCEEDED(context_.SignalAndWait()));
}

void ExpectRenderToCopyDependenciesAcrossSeparateExecutes(
    D3D12TestContext &context, UINT resource_count) {
  constexpr UINT64 kReadbackStride = 512;
  constexpr UINT kRowPitch = 256;
  auto rtv_heap = context.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_RTV, resource_count, false);
  auto readback = context.CreateBuffer(
      resource_count * kReadbackStride, D3D12_HEAP_TYPE_READBACK,
      D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(rtv_heap);
  ASSERT_TRUE(readback);

  std::vector<ComPtr<ID3D12Resource>> textures;
  std::vector<ComPtr<ID3D12CommandAllocator>> allocators;
  std::vector<ComPtr<ID3D12GraphicsCommandList>> lists;
  textures.reserve(resource_count);
  allocators.reserve(resource_count * 2);
  lists.reserve(resource_count * 2);
  constexpr FLOAT clear_color[] = {1.0f, 0.0f, 0.0f, 1.0f};

  for (UINT index = 0; index < resource_count; ++index) {
    auto texture = context.CreateTexture2D(
        1, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
        D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    ASSERT_TRUE(texture);
    const auto rtv = context.CpuDescriptorHandle(rtv_heap.get(), index);
    context.device()->CreateRenderTargetView(texture.get(), nullptr, rtv);

    ComPtr<ID3D12CommandAllocator> producer_allocator;
    ComPtr<ID3D12GraphicsCommandList> producer;
    ASSERT_TRUE(SUCCEEDED(context.device()->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        __uuidof(ID3D12CommandAllocator),
        reinterpret_cast<void **>(producer_allocator.put()))));
    ASSERT_TRUE(SUCCEEDED(context.device()->CreateCommandList(
        0, D3D12_COMMAND_LIST_TYPE_DIRECT, producer_allocator.get(), nullptr,
        __uuidof(ID3D12GraphicsCommandList),
        reinterpret_cast<void **>(producer.put()))));
    producer->ClearRenderTargetView(rtv, clear_color, 0, nullptr);
    D3D12TestContext::Transition(
        producer.get(), texture.get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_COPY_SOURCE);
    ASSERT_TRUE(SUCCEEDED(producer->Close()));

    ComPtr<ID3D12CommandAllocator> consumer_allocator;
    ComPtr<ID3D12GraphicsCommandList> consumer;
    ASSERT_TRUE(SUCCEEDED(context.device()->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        __uuidof(ID3D12CommandAllocator),
        reinterpret_cast<void **>(consumer_allocator.put()))));
    ASSERT_TRUE(SUCCEEDED(context.device()->CreateCommandList(
        0, D3D12_COMMAND_LIST_TYPE_DIRECT, consumer_allocator.get(), nullptr,
        __uuidof(ID3D12GraphicsCommandList),
        reinterpret_cast<void **>(consumer.put()))));
    D3D12_TEXTURE_COPY_LOCATION source = {};
    source.pResource = texture.get();
    source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION destination = {};
    destination.pResource = readback.get();
    destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    destination.PlacedFootprint.Offset = index * kReadbackStride;
    destination.PlacedFootprint.Footprint = {
        DXGI_FORMAT_R8G8B8A8_UNORM, 1, 1, 1, kRowPitch};
    consumer->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
    ASSERT_TRUE(SUCCEEDED(consumer->Close()));

    ID3D12CommandList *producer_list = producer.get();
    context.queue()->ExecuteCommandLists(1, &producer_list);
    ID3D12CommandList *consumer_list = consumer.get();
    context.queue()->ExecuteCommandLists(1, &consumer_list);

    textures.push_back(std::move(texture));
    allocators.push_back(std::move(producer_allocator));
    allocators.push_back(std::move(consumer_allocator));
    lists.push_back(std::move(producer));
    lists.push_back(std::move(consumer));
  }

  ASSERT_TRUE(SUCCEEDED(context.SignalAndWait()));

  void *mapping = nullptr;
  const D3D12_RANGE read_range = {0, resource_count * kReadbackStride};
  ASSERT_TRUE(SUCCEEDED(readback->Map(0, &read_range, &mapping)));
  const auto *bytes = static_cast<const std::uint8_t *>(mapping);
  for (UINT index = 0; index < resource_count; ++index) {
    std::uint32_t pixel = 0;
    std::memcpy(&pixel, bytes + index * kReadbackStride, sizeof(pixel));
    EXPECT_EQ(pixel, 0xff0000ffu) << "resource " << index;
  }
  const D3D12_RANGE written_range = {0, 0};
  readback->Unmap(0, &written_range);
}

DXMT_SERIAL_TEST_F(D3D12QueueSpec,
                   PreservesRenderToCopyDependenciesAcrossSeparateExecutes) {
  ExpectRenderToCopyDependenciesAcrossSeparateExecutes(context_, 64);
}

DXMT_SERIAL_TEST_F(D3D12QueueSpec, RenderToCopySeparateExecuteBoundary31) {
  ExpectRenderToCopyDependenciesAcrossSeparateExecutes(context_, 31);
}

DXMT_SERIAL_TEST_F(D3D12QueueSpec, RenderToCopySeparateExecuteBoundary32) {
  ExpectRenderToCopyDependenciesAcrossSeparateExecutes(context_, 32);
}

DXMT_SERIAL_TEST_F(D3D12QueueSpec, RenderToCopySeparateExecuteBoundary33) {
  ExpectRenderToCopyDependenciesAcrossSeparateExecutes(context_, 33);
}

TEST_F(D3D12QueueSpec, PreservesTextureUploadAcrossSubmissions) {
  const std::array<std::uint32_t, 16> expected = {
      0xff000001, 0xff000002, 0xff000003, 0xff000004, 0xff000011, 0xff000012,
      0xff000013, 0xff000014, 0xff000021, 0xff000022, 0xff000023, 0xff000024,
      0xff000031, 0xff000032, 0xff000033, 0xff000034,
  };
  ComPtr<ID3D12Resource> texture = context_.CreateTexture2D(
      4, 4, 1, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(texture);
  ASSERT_TRUE(SUCCEEDED(context_.UploadTextureAndReset(
      texture.get(), expected.data(), 4 * sizeof(std::uint32_t),
      sizeof(expected))));

  D3D12TestContext::Transition(context_.list(), texture.get(),
                               D3D12_RESOURCE_STATE_COPY_DEST,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
  TextureReadback readback;
  ASSERT_TRUE(SUCCEEDED(context_.ReadbackTexture(texture.get(), &readback)));
  ASSERT_EQ(readback.width, 4u);
  ASSERT_EQ(readback.height, 4u);
  for (UINT y = 0; y < readback.height; ++y) {
    for (UINT x = 0; x < readback.width; ++x) {
      std::uint32_t actual = 0;
      std::memcpy(&actual,
                  readback.data.data() + y * readback.row_pitch +
                      x * sizeof(actual),
                  sizeof(actual));
      EXPECT_EQ(actual, expected[y * readback.width + x]);
    }
  }
}

TEST_F(D3D12QueueSpec, ClearsRenderTargetViewsAcrossMipChain) {
  D3D12_FEATURE_DATA_FORMAT_SUPPORT support = {};
  support.Format = DXGI_FORMAT_R11G11B10_FLOAT;
  if (FAILED(context_.device()->CheckFeatureSupport(
          D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support))) ||
      !(support.Support1 & D3D12_FORMAT_SUPPORT1_RENDER_TARGET))
    GTEST_SKIP() << "R11G11B10_FLOAT render targets are unavailable";

  ComPtr<ID3D12Resource> texture = context_.CreateTexture2D(
      480, 270, 9, DXGI_FORMAT_R11G11B10_FLOAT,
      D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET |
          D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_RENDER_TARGET);
  auto rtv_heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 9, false);
  ASSERT_TRUE(texture);
  ASSERT_TRUE(rtv_heap);

  const FLOAT clear_color[] = {1.0f, 1.0f, 1.0f, 1.0f};
  for (UINT mip = 0; mip < 9; ++mip) {
    D3D12_RENDER_TARGET_VIEW_DESC rtv_desc = {};
    rtv_desc.Format = DXGI_FORMAT_R11G11B10_FLOAT;
    rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    rtv_desc.Texture2D.MipSlice = mip;
    const auto rtv = context_.CpuDescriptorHandle(rtv_heap.get(), mip);
    context_.device()->CreateRenderTargetView(texture.get(), &rtv_desc, rtv);
    context_.list()->ClearRenderTargetView(rtv, clear_color, 0, nullptr);
  }
  D3D12TestContext::Transition(
      context_.list(), texture.get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
      D3D12_RESOURCE_STATE_COPY_SOURCE);

  TextureReadback readback;
  ASSERT_TRUE(SUCCEEDED(
      context_.ReadbackTexture(texture.get(), &readback, 6)));
  ASSERT_EQ(readback.width, 7u);
  ASSERT_EQ(readback.height, 4u);
  constexpr std::uint32_t kPackedWhite = 0x781e03c0;
  for (UINT y = 0; y < readback.height; ++y) {
    for (UINT x = 0; x < readback.width; ++x) {
      std::uint32_t pixel = 0;
      std::memcpy(&pixel,
                  readback.data.data() + y * readback.row_pitch +
                      x * sizeof(pixel),
                  sizeof(pixel));
      EXPECT_EQ(pixel, kPackedWhite)
          << "pixel (" << x << ", " << y << ")";
    }
  }
}

TEST_F(D3D12QueueSpec,
       PreservesMoreThan256IndependentClearToCopyDependencies) {
  constexpr UINT kResourceCount = 300;
  constexpr UINT64 kPlacedFootprintStride = 512;
  constexpr UINT kRowPitch = 256;
  auto rtv_heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_RTV, kResourceCount, false);
  auto readback = context_.CreateBuffer(
      kResourceCount * kPlacedFootprintStride, D3D12_HEAP_TYPE_READBACK,
      D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(rtv_heap);
  ASSERT_TRUE(readback);

  std::vector<ComPtr<ID3D12Resource>> textures(kResourceCount);
  for (UINT i = 0; i < kResourceCount; i++) {
    textures[i] = context_.CreateTexture2D(
        1, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
        D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    ASSERT_TRUE(textures[i]);
    const auto rtv = context_.CpuDescriptorHandle(rtv_heap.get(), i);
    context_.device()->CreateRenderTargetView(textures[i].get(), nullptr, rtv);
  }

  auto record_clear_and_copy = [&](const FLOAT color[4]) {
    constexpr D3D12_RECT rect = {0, 0, 1, 1};
    for (UINT i = 0; i < kResourceCount; i++)
      context_.list()->ClearRenderTargetView(
          context_.CpuDescriptorHandle(rtv_heap.get(), i), color, 1, &rect);

    for (UINT i = 0; i < kResourceCount; i++) {
      D3D12TestContext::Transition(
          context_.list(), textures[i].get(),
          D3D12_RESOURCE_STATE_RENDER_TARGET,
          D3D12_RESOURCE_STATE_COPY_SOURCE);
      D3D12_TEXTURE_COPY_LOCATION source = {};
      source.pResource = textures[i].get();
      source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
      D3D12_TEXTURE_COPY_LOCATION destination = {};
      destination.pResource = readback.get();
      destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
      destination.PlacedFootprint.Offset = i * kPlacedFootprintStride;
      destination.PlacedFootprint.Footprint = {
          DXGI_FORMAT_R8G8B8A8_UNORM, 1, 1, 1, kRowPitch};
      context_.list()->CopyTextureRegion(
          &destination, 0, 0, 0, &source, nullptr);
    }
  };

  constexpr FLOAT first_color[] = {1.0f, 0.0f, 0.0f, 1.0f};
  record_clear_and_copy(first_color);
  ASSERT_TRUE(SUCCEEDED(context_.ExecuteAndWait()));

  ASSERT_TRUE(SUCCEEDED(context_.ResetCommandList()));
  for (const auto &texture : textures)
    D3D12TestContext::Transition(
        context_.list(), texture.get(), D3D12_RESOURCE_STATE_COPY_SOURCE,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
  constexpr FLOAT second_color[] = {0.0f, 1.0f, 0.0f, 1.0f};
  record_clear_and_copy(second_color);
  ASSERT_TRUE(SUCCEEDED(context_.ExecuteAndWait()));

  void *mapping = nullptr;
  const D3D12_RANGE read_range = {
      0, kResourceCount * kPlacedFootprintStride};
  ASSERT_TRUE(SUCCEEDED(readback->Map(0, &read_range, &mapping)));
  const auto *bytes = static_cast<const std::uint8_t *>(mapping);
  for (UINT i : {0u, kResourceCount / 2, kResourceCount - 1}) {
    const auto *pixel = bytes + i * kPlacedFootprintStride;
    EXPECT_EQ(pixel[0], 0u);
    EXPECT_EQ(pixel[1], 255u);
    EXPECT_EQ(pixel[2], 0u);
    EXPECT_EQ(pixel[3], 255u);
  }
  const D3D12_RANGE written_range = {0, 0};
  readback->Unmap(0, &written_range);
}

TEST_F(D3D12QueueSpec,
       LargeClearSequenceProducesExpectedPixels) {
  constexpr UINT kResourceCount = 64;
  constexpr UINT kClearCount = 600;
  auto rtv_heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_RTV, kResourceCount, false);
  ASSERT_TRUE(rtv_heap);

  std::array<ComPtr<ID3D12Resource>, kResourceCount> textures;
  for (UINT i = 0; i < kResourceCount; i++) {
    textures[i] = context_.CreateTexture2D(
        64, 64, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
        D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    ASSERT_TRUE(textures[i]);
    context_.device()->CreateRenderTargetView(
        textures[i].get(), nullptr,
        context_.CpuDescriptorHandle(rtv_heap.get(), i));
  }

  constexpr FLOAT kClearColor[] = {0.25f, 0.5f, 0.75f, 1.0f};
  for (UINT i = 0; i < kClearCount; i++) {
    context_.list()->ClearRenderTargetView(
        context_.CpuDescriptorHandle(rtv_heap.get(), i % kResourceCount),
        kClearColor, 0, nullptr);
  }

  ASSERT_TRUE(SUCCEEDED(context_.ExecuteAndWait()));

  ASSERT_TRUE(SUCCEEDED(context_.ResetCommandList()));
  D3D12TestContext::Transition(
      context_.list(), textures[0].get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
      D3D12_RESOURCE_STATE_COPY_SOURCE);
  TextureReadback readback;
  ASSERT_TRUE(
      SUCCEEDED(context_.ReadbackTexture(textures[0].get(), &readback)));
  ASSERT_EQ(readback.width, 64u);
  ASSERT_EQ(readback.height, 64u);

  const std::array<std::uint8_t, 4> expected = {64, 128, 191, 255};
  for (UINT y = 0; y < readback.height; y++) {
    for (UINT x = 0; x < readback.width; x++) {
      const auto *pixel = readback.data.data() + y * readback.row_pitch +
                          x * expected.size();
      EXPECT_TRUE(std::equal(expected.begin(), expected.end(), pixel))
          << "pixel (" << x << ", " << y << ")";
    }
  }
}

TEST_F(D3D12QueueSpec, ClearsNonzeroMipPlacedRenderTargetView) {
  D3D12_FEATURE_DATA_FORMAT_SUPPORT support = {};
  support.Format = DXGI_FORMAT_R11G11B10_FLOAT;
  if (FAILED(context_.device()->CheckFeatureSupport(
          D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support))) ||
      !(support.Support1 & D3D12_FORMAT_SUPPORT1_RENDER_TARGET))
    GTEST_SKIP() << "R11G11B10_FLOAT render targets are unavailable";

  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width = 480;
  desc.Height = 270;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 9;
  desc.Format = DXGI_FORMAT_R11G11B10_FLOAT;
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET |
               D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

  const auto allocation =
      context_.device()->GetResourceAllocationInfo(0, 1, &desc);
  ASSERT_NE(allocation.SizeInBytes, UINT64_MAX);
  ASSERT_NE(allocation.SizeInBytes, 0u);
  D3D12_HEAP_DESC heap_desc = {};
  heap_desc.SizeInBytes = allocation.SizeInBytes;
  heap_desc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
  heap_desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
  heap_desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES;
  ComPtr<ID3D12Heap> heap;
  ASSERT_TRUE(SUCCEEDED(context_.device()->CreateHeap(
      &heap_desc, __uuidof(ID3D12Heap),
      reinterpret_cast<void **>(heap.put()))));

  ComPtr<ID3D12Resource> texture;
  ASSERT_TRUE(SUCCEEDED(context_.device()->CreatePlacedResource(
      heap.get(), 0, &desc, D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr,
      __uuidof(ID3D12Resource), reinterpret_cast<void **>(texture.put()))));
  auto rtv_heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
  ASSERT_TRUE(rtv_heap);

  D3D12_RENDER_TARGET_VIEW_DESC rtv_desc = {};
  rtv_desc.Format = DXGI_FORMAT_R11G11B10_FLOAT;
  rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
  rtv_desc.Texture2D.MipSlice = 3;
  const auto rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
  context_.device()->CreateRenderTargetView(texture.get(), &rtv_desc, rtv);

  const FLOAT clear_color[] = {1.0f, 1.0f, 1.0f, 1.0f};
  context_.list()->ClearRenderTargetView(rtv, clear_color, 0, nullptr);
  D3D12TestContext::Transition(
      context_.list(), texture.get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
      D3D12_RESOURCE_STATE_COPY_SOURCE);

  TextureReadback readback;
  ASSERT_TRUE(SUCCEEDED(
      context_.ReadbackTexture(texture.get(), &readback, 3)));
  ASSERT_EQ(readback.width, 60u);
  ASSERT_EQ(readback.height, 33u);
  constexpr std::uint32_t kPackedWhite = 0x781e03c0;
  for (UINT y = 0; y < readback.height; ++y) {
    for (UINT x = 0; x < readback.width; ++x) {
      std::uint32_t pixel = 0;
      std::memcpy(&pixel,
                  readback.data.data() + y * readback.row_pitch +
                      x * sizeof(pixel),
                  sizeof(pixel));
      EXPECT_EQ(pixel, kPackedWhite)
          << "pixel (" << x << ", " << y << ")";
    }
  }
}

TEST_F(D3D12QueueSpec, ResolvesSourceRegionAtNonZeroDestination) {
  constexpr UINT kSampleCount = 4;
  D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS quality = {};
  quality.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  quality.SampleCount = kSampleCount;
  if (FAILED(context_.device()->CheckFeatureSupport(
          D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &quality,
          sizeof(quality))) ||
      !quality.NumQualityLevels)
    GTEST_SKIP() << "4x MSAA is unavailable";

  D3D12_HEAP_PROPERTIES heap = {};
  heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  heap.CreationNodeMask = 1;
  heap.VisibleNodeMask = 1;

  D3D12_RESOURCE_DESC source_desc = {};
  source_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  source_desc.Width = 4;
  source_desc.Height = 4;
  source_desc.DepthOrArraySize = 1;
  source_desc.MipLevels = 1;
  source_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  source_desc.SampleDesc.Count = kSampleCount;
  source_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  source_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

  D3D12_CLEAR_VALUE clear_value = {};
  clear_value.Format = source_desc.Format;
  clear_value.Color[0] = 0.25f;
  clear_value.Color[1] = 0.5f;
  clear_value.Color[2] = 0.75f;
  clear_value.Color[3] = 1.0f;

  ComPtr<ID3D12Resource> source;
  ASSERT_TRUE(SUCCEEDED(context_.device()->CreateCommittedResource(
      &heap, D3D12_HEAP_FLAG_NONE, &source_desc,
      D3D12_RESOURCE_STATE_RENDER_TARGET, &clear_value,
      __uuidof(ID3D12Resource), reinterpret_cast<void **>(source.put()))));

  auto destination_desc = source_desc;
  destination_desc.Width = 8;
  destination_desc.Height = 8;
  destination_desc.SampleDesc.Count = 1;
  destination_desc.Flags = D3D12_RESOURCE_FLAG_NONE;
  ComPtr<ID3D12Resource> destination;
  ASSERT_TRUE(SUCCEEDED(context_.device()->CreateCommittedResource(
      &heap, D3D12_HEAP_FLAG_NONE, &destination_desc,
      D3D12_RESOURCE_STATE_RESOLVE_DEST, nullptr, __uuidof(ID3D12Resource),
      reinterpret_cast<void **>(destination.put()))));

  auto rtv_heap =
      context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
  ASSERT_TRUE(rtv_heap);
  const auto rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
  context_.device()->CreateRenderTargetView(source.get(), nullptr, rtv);
  context_.list()->ClearRenderTargetView(rtv, clear_value.Color, 0, nullptr);
  D3D12TestContext::Transition(context_.list(), source.get(),
                               D3D12_RESOURCE_STATE_RENDER_TARGET,
                               D3D12_RESOURCE_STATE_RESOLVE_SOURCE);

  ComPtr<ID3D12GraphicsCommandList1> list1;
  ASSERT_TRUE(SUCCEEDED(
      context_.list()->QueryInterface(__uuidof(ID3D12GraphicsCommandList1),
                                      reinterpret_cast<void **>(list1.put()))));
  D3D12_RECT source_rect = {1, 1, 4, 4};
  list1->ResolveSubresourceRegion(destination.get(), 0, 2, 3, source.get(), 0,
                                  &source_rect, DXGI_FORMAT_R8G8B8A8_UNORM,
                                  D3D12_RESOLVE_MODE_AVERAGE);
  D3D12TestContext::Transition(context_.list(), destination.get(),
                               D3D12_RESOURCE_STATE_RESOLVE_DEST,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);

  TextureReadback readback;
  ASSERT_TRUE(
      SUCCEEDED(context_.ReadbackTexture(destination.get(), &readback)));
  ASSERT_EQ(readback.width, 8u);
  ASSERT_EQ(readback.height, 8u);
  for (UINT y = 3; y < 6; ++y) {
    for (UINT x = 2; x < 5; ++x) {
      std::uint32_t pixel = 0;
      std::memcpy(&pixel,
                  readback.data.data() + y * readback.row_pitch +
                      x * sizeof(pixel),
                  sizeof(pixel));
      EXPECT_TRUE(ColorsMatch(pixel, 0xffbf8040, 2));
    }
  }
}

TEST_F(D3D12QueueSpec, ResolvesFullSubresourceForAdvertisedSampleCounts) {
  constexpr DXGI_FORMAT kFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
  constexpr FLOAT kColor[4] = {0.125f, 0.375f, 0.625f, 1.0f};
  constexpr UINT kExpected = 0xff9f6020;

  for (const UINT sample_count : {2u, 4u, 8u}) {
    SCOPED_TRACE(::testing::Message() << "sample_count=" << sample_count);
    D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS quality = {};
    quality.Format = kFormat;
    quality.SampleCount = sample_count;
    ASSERT_EQ(context_.device()->CheckFeatureSupport(
                  D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &quality,
                  sizeof(quality)),
              S_OK);
    if (!quality.NumQualityLevels)
      continue;

    D3D12_CLEAR_VALUE clear_value = {};
    clear_value.Format = kFormat;
    std::copy(std::begin(kColor), std::end(kColor), clear_value.Color);
    auto source = CreateDefaultTexture2D(
        context_, 7, 5, 1, kFormat, sample_count,
        D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
        D3D12_RESOURCE_STATE_RENDER_TARGET, &clear_value);
    auto destination = CreateDefaultTexture2D(
        context_, 7, 5, 1, kFormat, 1, D3D12_RESOURCE_FLAG_NONE,
        D3D12_RESOURCE_STATE_RESOLVE_DEST);
    auto rtv_heap = context_.CreateDescriptorHeap(
        D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
    ASSERT_TRUE(source);
    ASSERT_TRUE(destination);
    ASSERT_TRUE(rtv_heap);
    const auto rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
    context_.device()->CreateRenderTargetView(source.get(), nullptr, rtv);
    context_.list()->ClearRenderTargetView(rtv, kColor, 0, nullptr);
    D3D12TestContext::Transition(
        context_.list(), source.get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
    context_.list()->ResolveSubresource(destination.get(), 0, source.get(), 0,
                                        kFormat);
    D3D12TestContext::Transition(
        context_.list(), destination.get(), D3D12_RESOURCE_STATE_RESOLVE_DEST,
        D3D12_RESOURCE_STATE_COPY_SOURCE);

    TextureReadback readback;
    ASSERT_EQ(context_.ReadbackTexture(destination.get(), &readback), S_OK);
    for (const auto [x, y] :
         {std::pair{0u, 0u}, std::pair{3u, 2u}, std::pair{6u, 4u}}) {
      UINT pixel = 0;
      std::memcpy(&pixel,
                  readback.data.data() + y * readback.row_pitch +
                      x * sizeof(pixel),
                  sizeof(pixel));
      EXPECT_TRUE(ColorsMatch(pixel, kExpected, 2));
    }
    ASSERT_EQ(context_.ResetCommandList(), S_OK);
  }
}

TEST_F(D3D12QueueSpec, ResolvesNonZeroMultisampleArraySlice) {
  constexpr DXGI_FORMAT kFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
  constexpr FLOAT kColor[4] = {0.75f, 0.25f, 0.5f, 1.0f};
  D3D12_CLEAR_VALUE clear_value = {};
  clear_value.Format = kFormat;
  std::copy(std::begin(kColor), std::end(kColor), clear_value.Color);
  auto source = CreateDefaultTexture2D(
      context_, 5, 3, 2, kFormat, 4,
      D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
      D3D12_RESOURCE_STATE_RENDER_TARGET, &clear_value);
  auto destination = CreateDefaultTexture2D(
      context_, 5, 3, 2, kFormat, 1, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_RESOLVE_DEST);
  auto rtv_heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
  ASSERT_TRUE(source);
  ASSERT_TRUE(destination);
  ASSERT_TRUE(rtv_heap);
  D3D12_RENDER_TARGET_VIEW_DESC rtv_desc = {};
  rtv_desc.Format = kFormat;
  rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY;
  rtv_desc.Texture2DMSArray.FirstArraySlice = 1;
  rtv_desc.Texture2DMSArray.ArraySize = 1;
  const auto rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
  context_.device()->CreateRenderTargetView(source.get(), &rtv_desc, rtv);
  context_.list()->ClearRenderTargetView(rtv, kColor, 0, nullptr);
  D3D12TestContext::Transition(
      context_.list(), source.get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
      D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
  context_.list()->ResolveSubresource(destination.get(), 1, source.get(), 1,
                                      kFormat);
  D3D12TestContext::Transition(
      context_.list(), destination.get(), D3D12_RESOURCE_STATE_RESOLVE_DEST,
      D3D12_RESOURCE_STATE_COPY_SOURCE);

  TextureReadback readback;
  ASSERT_EQ(context_.ReadbackTexture(destination.get(), &readback, 1), S_OK);
  for (UINT y = 0; y < readback.height; ++y) {
    for (UINT x = 0; x < readback.width; ++x) {
      UINT pixel = 0;
      std::memcpy(&pixel,
                  readback.data.data() + y * readback.row_pitch +
                      x * sizeof(pixel),
                  sizeof(pixel));
      EXPECT_TRUE(ColorsMatch(pixel, 0xff8040bf, 2));
    }
  }
}

TEST_F(D3D12QueueSpec, ResolvesFloatAndTypelessColorFormats) {
  struct ResolveFormatCase {
    DXGI_FORMAT resource_format;
    DXGI_FORMAT view_format;
    UINT bytes_per_pixel;
  };
  constexpr std::array cases = {
      ResolveFormatCase{DXGI_FORMAT_R32G32B32A32_FLOAT,
                        DXGI_FORMAT_R32G32B32A32_FLOAT, 16},
      ResolveFormatCase{DXGI_FORMAT_R8G8B8A8_TYPELESS,
                        DXGI_FORMAT_R8G8B8A8_UNORM, 4},
  };
  constexpr FLOAT kColor[4] = {0.125f, 0.375f, 0.625f, 1.0f};

  for (const auto &test_case : cases) {
    SCOPED_TRACE(::testing::Message()
                 << "resource_format=" << UINT(test_case.resource_format));
    D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS quality = {};
    quality.Format = test_case.view_format;
    quality.SampleCount = 4;
    ASSERT_EQ(context_.device()->CheckFeatureSupport(
                  D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &quality,
                  sizeof(quality)),
              S_OK);
    if (!quality.NumQualityLevels)
      continue;

    D3D12_CLEAR_VALUE clear_value = {};
    clear_value.Format = test_case.view_format;
    std::copy(std::begin(kColor), std::end(kColor), clear_value.Color);
    auto source = CreateDefaultTexture2D(
        context_, 3, 2, 1, test_case.resource_format, 4,
        D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
        D3D12_RESOURCE_STATE_RENDER_TARGET, &clear_value);
    auto destination = CreateDefaultTexture2D(
        context_, 3, 2, 1, test_case.resource_format, 1,
        D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_RESOLVE_DEST);
    auto rtv_heap = context_.CreateDescriptorHeap(
        D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
    ASSERT_TRUE(source);
    ASSERT_TRUE(destination);
    ASSERT_TRUE(rtv_heap);
    D3D12_RENDER_TARGET_VIEW_DESC rtv_desc = {};
    rtv_desc.Format = test_case.view_format;
    rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMS;
    const auto rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
    context_.device()->CreateRenderTargetView(source.get(), &rtv_desc, rtv);
    context_.list()->ClearRenderTargetView(rtv, kColor, 0, nullptr);
    D3D12TestContext::Transition(
        context_.list(), source.get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
    context_.list()->ResolveSubresource(destination.get(), 0, source.get(), 0,
                                        test_case.view_format);
    D3D12TestContext::Transition(
        context_.list(), destination.get(), D3D12_RESOURCE_STATE_RESOLVE_DEST,
        D3D12_RESOURCE_STATE_COPY_SOURCE);

    TextureReadback readback;
    ASSERT_EQ(context_.ReadbackTexture(destination.get(), &readback), S_OK);
    if (test_case.bytes_per_pixel == sizeof(kColor)) {
      std::array<FLOAT, 4> actual = {};
      std::memcpy(actual.data(), readback.data.data(), sizeof(actual));
      for (UINT channel = 0; channel < actual.size(); ++channel)
        EXPECT_NEAR(actual[channel], kColor[channel], 0.0001f);
    } else {
      UINT pixel = 0;
      std::memcpy(&pixel, readback.data.data(), sizeof(pixel));
      EXPECT_TRUE(ColorsMatch(pixel, 0xff9f6020, 2));
    }
    ASSERT_EQ(context_.ResetCommandList(), S_OK);
  }
}

TEST_F(D3D12QueueSpec, ResolvesPerSampleValuesWithAverageMinAndMax) {
  const auto pixel_shader = CompileShader(R"(
    float4 main(uint sample_index : SV_SampleIndex) : SV_Target {
      return float4(0.2f * (sample_index + 1), 0.0f, 0.0f, 1.0f);
    })",
                                          "ps_5_0");
  ASSERT_EQ(pixel_shader.result, S_OK) << pixel_shader.diagnostic_text();
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  root_desc.Flags =
      D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
  auto root_signature = context_.CreateRootSignature(root_desc);
  ASSERT_TRUE(root_signature);

  D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline_desc = {};
  pipeline_desc.pRootSignature = root_signature.get();
  pipeline_desc.VS = FullscreenVertexShader();
  pipeline_desc.PS = {pixel_shader.bytecode->GetBufferPointer(),
                      pixel_shader.bytecode->GetBufferSize()};
  pipeline_desc.BlendState.RenderTarget[0].RenderTargetWriteMask =
      D3D12_COLOR_WRITE_ENABLE_ALL;
  pipeline_desc.SampleMask = UINT_MAX;
  pipeline_desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
  pipeline_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  pipeline_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  pipeline_desc.NumRenderTargets = 1;
  pipeline_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
  pipeline_desc.SampleDesc.Count = 4;
  ComPtr<ID3D12PipelineState> pipeline;
  ASSERT_EQ(context_.device()->CreateGraphicsPipelineState(
                &pipeline_desc, __uuidof(ID3D12PipelineState),
                reinterpret_cast<void **>(pipeline.put())),
            S_OK);

  constexpr UINT kSize = 4;
  auto source = CreateDefaultTexture2D(
      context_, kSize, kSize, 1, DXGI_FORMAT_R8G8B8A8_UNORM, 4,
      D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
      D3D12_RESOURCE_STATE_RENDER_TARGET);
  std::array<ComPtr<ID3D12Resource>, 3> destinations;
  for (auto &destination : destinations) {
    destination = CreateDefaultTexture2D(
        context_, kSize, kSize, 1, DXGI_FORMAT_R8G8B8A8_UNORM, 1,
        D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_RESOLVE_DEST);
    ASSERT_TRUE(destination);
  }
  auto rtv_heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
  ASSERT_TRUE(source);
  ASSERT_TRUE(rtv_heap);
  const auto rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
  context_.device()->CreateRenderTargetView(source.get(), nullptr, rtv);

  context_.list()->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
  context_.list()->SetGraphicsRootSignature(root_signature.get());
  context_.list()->SetPipelineState(pipeline.get());
  context_.list()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  const D3D12_VIEWPORT viewport = {0.0f, 0.0f, float(kSize), float(kSize),
                                   0.0f, 1.0f};
  const D3D12_RECT scissor = {0, 0, kSize, kSize};
  context_.list()->RSSetViewports(1, &viewport);
  context_.list()->RSSetScissorRects(1, &scissor);
  context_.list()->DrawInstanced(3, 1, 0, 0);
  D3D12TestContext::Transition(
      context_.list(), source.get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
      D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
  ComPtr<ID3D12GraphicsCommandList1> list1;
  ASSERT_EQ(context_.list()->QueryInterface(
                __uuidof(ID3D12GraphicsCommandList1),
                reinterpret_cast<void **>(list1.put())),
            S_OK);
  const std::array modes = {D3D12_RESOLVE_MODE_AVERAGE,
                            D3D12_RESOLVE_MODE_MIN,
                            D3D12_RESOLVE_MODE_MAX};
  for (UINT index = 0; index < destinations.size(); ++index) {
    list1->ResolveSubresourceRegion(
        destinations[index].get(), 0, 0, 0, source.get(), 0, nullptr,
        DXGI_FORMAT_R8G8B8A8_UNORM, modes[index]);
    D3D12TestContext::Transition(
        context_.list(), destinations[index].get(),
        D3D12_RESOURCE_STATE_RESOLVE_DEST,
        D3D12_RESOURCE_STATE_COPY_SOURCE);
  }
  ASSERT_EQ(context_.ExecuteAndWait(), S_OK);
  ASSERT_EQ(context_.ResetCommandList(), S_OK);

  constexpr std::array<UINT, 3> expected = {
      0xff000080, 0xff000033, 0xff0000cc};
  for (UINT index = 0; index < destinations.size(); ++index) {
    TextureReadback readback;
    ASSERT_EQ(context_.ReadbackTexture(destinations[index].get(), &readback),
              S_OK);
    UINT pixel = 0;
    std::memcpy(&pixel, readback.data.data(), sizeof(pixel));
    EXPECT_TRUE(ColorsMatch(pixel, expected[index], 2))
        << "resolve mode " << modes[index];
    if (index + 1 < destinations.size()) {
      ASSERT_EQ(context_.ResetCommandList(), S_OK);
    }
  }
}

TEST_F(D3D12QueueSpec, FailsCloseForUnadvertisedCommandFeatures) {
  struct RecordingList {
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12GraphicsCommandList1> list1;
  };
  auto create_list = [&] {
    RecordingList result;
    if (FAILED(context_.device()->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            __uuidof(ID3D12CommandAllocator),
            reinterpret_cast<void **>(result.allocator.put()))))
      return result;
    if (FAILED(context_.device()->CreateCommandList(
            0, D3D12_COMMAND_LIST_TYPE_DIRECT, result.allocator.get(),
            nullptr, __uuidof(ID3D12GraphicsCommandList),
            reinterpret_cast<void **>(result.list.put()))))
      return result;
    result.list->QueryInterface(
        __uuidof(ID3D12GraphicsCommandList1),
        reinterpret_cast<void **>(result.list1.put()));
    return result;
  };

  std::array<std::uint32_t, 64> atomic_data = {};
  auto atomic_source = context_.CreateUploadBuffer(
      sizeof(atomic_data), atomic_data.data(), sizeof(atomic_data));
  auto atomic_destination =
      context_.CreateBuffer(sizeof(atomic_data), D3D12_HEAP_TYPE_DEFAULT,
                            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                            D3D12_RESOURCE_STATE_COPY_DEST);
  auto atomic_dependent =
      context_.CreateBuffer(sizeof(atomic_data), D3D12_HEAP_TYPE_DEFAULT,
                            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                            D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(atomic_source);
  ASSERT_TRUE(atomic_destination);
  ASSERT_TRUE(atomic_dependent);
  ID3D12Resource *dependent_resources[] = {atomic_dependent.get()};
  D3D12_SUBRESOURCE_RANGE_UINT64 dependent_ranges[] = {{0, {0, sizeof(UINT)}}};
  auto atomic_list = create_list();
  ASSERT_TRUE(atomic_list.list1);
  atomic_list.list1->AtomicCopyBufferUINT(
      atomic_destination.get(), 0, atomic_source.get(), 0, 1,
      dependent_resources, dependent_ranges);
  EXPECT_EQ(atomic_list.list->Close(), E_NOTIMPL);

  D3D12_STREAM_OUTPUT_BUFFER_VIEW stream_output = {};
  stream_output.BufferLocation = atomic_destination->GetGPUVirtualAddress();
  stream_output.SizeInBytes = sizeof(atomic_data);
  stream_output.BufferFilledSizeLocation =
      atomic_dependent->GetGPUVirtualAddress();
  auto stream_output_list = create_list();
  ASSERT_TRUE(stream_output_list.list);
  stream_output_list.list->SOSetTargets(0, 1, &stream_output);
  EXPECT_EQ(stream_output_list.list->Close(), E_NOTIMPL);

  auto depth_bounds_list = create_list();
  ASSERT_TRUE(depth_bounds_list.list1);
  depth_bounds_list.list1->OMSetDepthBounds(0.25f, 0.75f);
  EXPECT_EQ(depth_bounds_list.list->Close(), E_NOTIMPL);

  auto decompress_source =
      context_.CreateTexture2D(16, 16, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
                               D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
                               D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
  auto decompress_destination = context_.CreateTexture2D(
      16, 16, 1, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_RESOLVE_DEST);
  ASSERT_TRUE(decompress_source);
  ASSERT_TRUE(decompress_destination);
  auto resolve_list = create_list();
  ASSERT_TRUE(resolve_list.list1);
  resolve_list.list1->ResolveSubresourceRegion(
      decompress_destination.get(), 0, 0, 0, decompress_source.get(), 0,
      nullptr, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOLVE_MODE_DECOMPRESS);
  EXPECT_EQ(resolve_list.list->Close(), E_NOTIMPL);

  // The default depth-bounds state remains a valid no-op when the feature is
  // not advertised.
  auto default_depth_bounds_list = create_list();
  ASSERT_TRUE(default_depth_bounds_list.list1);
  default_depth_bounds_list.list1->OMSetDepthBounds(0.0f, 1.0f);
  EXPECT_EQ(default_depth_bounds_list.list->Close(), S_OK);
}

TEST_F(D3D12QueueSpec, ResolveQueryRecordDoesNotRetainItsCommandList) {
  ComPtr<ID3D12CommandAllocator> allocator;
  ASSERT_TRUE(SUCCEEDED(context_.device()->CreateCommandAllocator(
      D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator),
      reinterpret_cast<void **>(allocator.put()))));
  ComPtr<ID3D12GraphicsCommandList> list;
  ASSERT_TRUE(SUCCEEDED(context_.device()->CreateCommandList(
      0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.get(), nullptr,
      __uuidof(ID3D12GraphicsCommandList),
      reinterpret_cast<void **>(list.put()))));
  D3D12_QUERY_HEAP_DESC query_desc = {};
  query_desc.Type = D3D12_QUERY_HEAP_TYPE_OCCLUSION;
  query_desc.Count = 1;
  ComPtr<ID3D12QueryHeap> query_heap;
  ASSERT_TRUE(SUCCEEDED(context_.device()->CreateQueryHeap(
      &query_desc, __uuidof(ID3D12QueryHeap),
      reinterpret_cast<void **>(query_heap.put()))));
  ComPtr<ID3D12Resource> result = context_.CreateBuffer(
      sizeof(UINT64), D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(result);

  auto destroyed = std::make_shared<std::atomic_bool>(false);
  auto *probe = new LifetimeProbe(destroyed);
  ASSERT_TRUE(
      SUCCEEDED(list->SetPrivateDataInterface(__uuidof(IUnknown), probe)));
  probe->Release();
  list->ResolveQueryData(query_heap.get(), D3D12_QUERY_TYPE_OCCLUSION, 0, 1,
                         result.get(), 0);
  ASSERT_TRUE(SUCCEEDED(list->Close()));
  list.reset();
  EXPECT_TRUE(destroyed->load(std::memory_order_acquire));
}

TEST_F(D3D12QueueSpec, EndsAndResolvesTimestampQuery) {
  D3D12_QUERY_HEAP_DESC query_desc = {};
  query_desc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
  query_desc.Count = 2;
  ComPtr<ID3D12QueryHeap> query_heap;
  ASSERT_TRUE(SUCCEEDED(context_.device()->CreateQueryHeap(
      &query_desc, __uuidof(ID3D12QueryHeap),
      reinterpret_cast<void **>(query_heap.put()))));

  ComPtr<ID3D12Resource> result = context_.CreateBuffer(
      sizeof(UINT64), D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(result);

  context_.list()->EndQuery(query_heap.get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
  context_.list()->ResolveQueryData(query_heap.get(),
                                    D3D12_QUERY_TYPE_TIMESTAMP, 0, 1,
                                    result.get(), 0);
  EXPECT_TRUE(SUCCEEDED(context_.ExecuteAndWait()));

  UINT64 *timestamp = nullptr;
  D3D12_RANGE read_range = {0, sizeof(*timestamp)};
  ASSERT_TRUE(SUCCEEDED(result->Map(
      0, &read_range, reinterpret_cast<void **>(&timestamp))));
  ASSERT_NE(timestamp, nullptr);
  EXPECT_NE(*timestamp, ~UINT64{0});
  D3D12_RANGE written_range = {};
  result->Unmap(0, &written_range);
}

TEST_F(D3D12QueueSpec, ReleasingQueueCancelsAnUnresolvedFenceWait) {
  D3D12_COMMAND_QUEUE_DESC queue_desc = {};
  queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  ComPtr<ID3D12CommandQueue> queue;
  ASSERT_TRUE(SUCCEEDED(context_.device()->CreateCommandQueue(
      &queue_desc, __uuidof(ID3D12CommandQueue),
      reinterpret_cast<void **>(queue.put()))));
  ComPtr<ID3D12Fence> fence;
  ASSERT_TRUE(SUCCEEDED(context_.device()->CreateFence(
      0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence),
      reinterpret_cast<void **>(fence.put()))));

  auto destroyed = std::make_shared<std::atomic_bool>(false);
  auto *probe = new LifetimeProbe(destroyed);
  ASSERT_TRUE(
      SUCCEEDED(queue->SetPrivateDataInterface(__uuidof(IUnknown), probe)));
  probe->Release();
  ASSERT_TRUE(SUCCEEDED(queue->Wait(fence.get(), 1)));
  Sleep(50);
  queue.reset();
  EXPECT_TRUE(destroyed->load(std::memory_order_acquire));
}

TEST_F(D3D12QueueSpec, QueueDestructionCanRaceFenceWaitCallbackArming) {
  ComPtr<ID3D12Fence> fence;
  ASSERT_TRUE(SUCCEEDED(context_.device()->CreateFence(
      0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence),
      reinterpret_cast<void **>(fence.put()))));

  // Immediate destruction deliberately races the submission worker between
  // observing the queued Wait and registering its CPU completion callback.
  // Repetition makes the narrow lifetime window a practical ASan/TSan stress
  // scenario without relying on implementation sleeps.
  for (UINT iteration = 0; iteration < 64; ++iteration) {
    D3D12_COMMAND_QUEUE_DESC queue_desc = {};
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ComPtr<ID3D12CommandQueue> queue;
    ASSERT_TRUE(SUCCEEDED(context_.device()->CreateCommandQueue(
        &queue_desc, __uuidof(ID3D12CommandQueue),
        reinterpret_cast<void **>(queue.put()))));
    ASSERT_TRUE(SUCCEEDED(queue->Wait(fence.get(), iteration + 1)));
    queue.reset();
  }
}

} // namespace
