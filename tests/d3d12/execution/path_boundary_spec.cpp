#include <dxmt_d3d12_test_path.hpp>
#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"
#include "shaders/runtime_test_shaders.hpp"

#include <array>
#include <cstring>
#include <vector>

namespace {

using dxmt::test::ColorsMatch;
using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;
using dxmt::test::FullscreenVertexShader;
using dxmt::test::TextureReadback;
using dxmt::d3d12::test::ExecutionPathConfig;
using dxmt::d3d12::test::ExecutionPathMode;
using dxmt::d3d12::test::ExecutionPathSegmentKind;
using dxmt::d3d12::test::ExecutionPathStats;

class ExecutionPathBoundarySpec : public ::testing::Test {
protected:
  enum class DispatchPath {
    Native,
    PredicatedFallback,
    NativeFallbackNative,
    NativeCopyNative,
  };

  enum class ComputeBoundarySequence {
    FallbackNativeFallback,
    DescriptorTableUpdate,
    RootSignatureChange,
  };

  enum class DrawSequence {
    Single,
    BarrierBetweenDraws,
    QueryAroundDraw,
    IndexedAcrossBarrier,
    PredicatedIndexed,
    ClearBetweenDraws,
    TextureCopyBetweenDraws,
    QueryBetweenDraws,
    RenderPassAcrossDraws,
  };

  void
  RunDescriptorDispatch(DispatchPath path, std::vector<std::uint32_t> *values,
                        std::vector<std::uint32_t> *copied_values = nullptr) {
    D3D12TestContext context;
    ASSERT_TRUE(SUCCEEDED(context.Initialize()));

    const auto shader = CompileShader(R"(
      Buffer<uint> input : register(t0);
      RWBuffer<uint> output : register(u0);

      [numthreads(8, 1, 1)]
      void main(uint3 id : SV_DispatchThreadID) {
        output[id.x] += input[id.x];
      }
    )",
                                      "cs_5_0");
    ASSERT_TRUE(SUCCEEDED(shader.result)) << shader.diagnostic_text();

    D3D12_DESCRIPTOR_RANGE ranges[2] = {};
    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[0].NumDescriptors = 1;
    ranges[0].OffsetInDescriptorsFromTableStart = 0;
    ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    ranges[1].NumDescriptors = 1;
    ranges[1].OffsetInDescriptorsFromTableStart = 1;
    D3D12_ROOT_PARAMETER parameter = {};
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameter.DescriptorTable.NumDescriptorRanges = 2;
    parameter.DescriptorTable.pDescriptorRanges = ranges;
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_ROOT_SIGNATURE_DESC root_desc = {};
    root_desc.NumParameters = 1;
    root_desc.pParameters = &parameter;
    auto root_signature = context.CreateRootSignature(root_desc);
    ASSERT_TRUE(root_signature);

    const D3D12_SHADER_BYTECODE bytecode = {shader.bytecode->GetBufferPointer(),
                                            shader.bytecode->GetBufferSize()};
    auto pipeline =
        context.CreateComputePipeline(root_signature.get(), bytecode);
    ASSERT_TRUE(pipeline);

    auto upload = context.CreateUploadBuffer(sizeof(kInput), kInput.data(),
                                             sizeof(kInput));
    auto input = context.CreateBuffer(sizeof(kInput), D3D12_HEAP_TYPE_DEFAULT,
                                      D3D12_RESOURCE_FLAG_NONE,
                                      D3D12_RESOURCE_STATE_COPY_DEST);
    auto output =
        context.CreateBuffer(sizeof(kInput), D3D12_HEAP_TYPE_DEFAULT,
                             D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                             D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    ComPtr<ID3D12Resource> copy;
    if (path == DispatchPath::NativeCopyNative) {
      copy = context.CreateBuffer(sizeof(kInput), D3D12_HEAP_TYPE_DEFAULT,
                                  D3D12_RESOURCE_FLAG_NONE,
                                  D3D12_RESOURCE_STATE_COPY_DEST);
    }
    auto heap = context.CreateDescriptorHeap(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, true);
    ASSERT_TRUE(upload);
    ASSERT_TRUE(input);
    ASSERT_TRUE(output);
    ASSERT_TRUE(path != DispatchPath::NativeCopyNative || copy);
    ASSERT_TRUE(heap);

    D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
    srv.Format = DXGI_FORMAT_R32_UINT;
    srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Buffer.NumElements = kInput.size();
    context.device()->CreateShaderResourceView(
        input.get(), &srv, context.CpuDescriptorHandle(heap.get(), 0));
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
    uav.Format = DXGI_FORMAT_R32_UINT;
    uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uav.Buffer.NumElements = kInput.size();
    context.device()->CreateUnorderedAccessView(
        output.get(), nullptr, &uav,
        context.CpuDescriptorHandle(heap.get(), 1));

    context.list()->CopyBufferRegion(input.get(), 0, upload.get(), 0,
                                     sizeof(kInput));
    ID3D12DescriptorHeap *heaps[] = {heap.get()};
    context.list()->SetDescriptorHeaps(1, heaps);
    const UINT clear[4] = {};
    context.list()->ClearUnorderedAccessViewUint(
        context.GpuDescriptorHandle(heap.get(), 1),
        context.CpuDescriptorHandle(heap.get(), 1), output.get(), clear, 0,
        nullptr);
    context.list()->SetComputeRootSignature(root_signature.get());
    context.list()->SetPipelineState(pipeline.get());
    context.list()->SetComputeRootDescriptorTable(
        0, heap->GetGPUDescriptorHandleForHeapStart());
    D3D12TestContext::Transition(
        context.list(), input.get(), D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    ComPtr<ID3D12Resource> predicate;
    if (path == DispatchPath::PredicatedFallback) {
      constexpr UINT64 kExecute = 0;
      predicate = context.CreateUploadBuffer(sizeof(kExecute), &kExecute,
                                             sizeof(kExecute));
      ASSERT_TRUE(predicate);
      context.list()->SetPredication(predicate.get(), 0,
                                     D3D12_PREDICATION_OP_EQUAL_ZERO);
    }
    context.list()->Dispatch(1, 1, 1);
    if (path == DispatchPath::PredicatedFallback) {
      context.list()->SetPredication(nullptr, 0,
                                     D3D12_PREDICATION_OP_EQUAL_ZERO);
    } else if (path == DispatchPath::NativeFallbackNative) {
      D3D12TestContext::UavBarrier(context.list(), output.get());
      context.list()->Dispatch(1, 1, 1);
    } else if (path == DispatchPath::NativeCopyNative) {
      D3D12TestContext::Transition(context.list(), output.get(),
                                   D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                   D3D12_RESOURCE_STATE_COPY_SOURCE);
      context.list()->CopyBufferRegion(copy.get(), 0, output.get(), 0,
                                       sizeof(kInput));
      D3D12TestContext::Transition(context.list(), output.get(),
                                   D3D12_RESOURCE_STATE_COPY_SOURCE,
                                   D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
      context.list()->Dispatch(1, 1, 1);
    }

    D3D12TestContext::Transition(context.list(), output.get(),
                                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                 D3D12_RESOURCE_STATE_COPY_SOURCE);
    if (copy) {
      ASSERT_NE(copied_values, nullptr);
      D3D12TestContext::Transition(context.list(), copy.get(),
                                   D3D12_RESOURCE_STATE_COPY_DEST,
                                   D3D12_RESOURCE_STATE_COPY_SOURCE);
      std::vector<std::uint8_t> bytes;
      ASSERT_EQ(context.ReadbackBuffer(copy.get(), sizeof(kInput), &bytes),
                S_OK);
      ASSERT_EQ(bytes.size(), sizeof(kInput));
      copied_values->resize(kInput.size());
      std::memcpy(copied_values->data(), bytes.data(), bytes.size());
      ASSERT_EQ(context.ResetCommandList(), S_OK);
    }
    std::vector<std::uint8_t> bytes;
    ASSERT_EQ(context.ReadbackBuffer(output.get(), sizeof(kInput), &bytes),
              S_OK);
    ASSERT_EQ(bytes.size(), sizeof(kInput));
    values->resize(kInput.size());
    std::memcpy(values->data(), bytes.data(), bytes.size());
  }

  void RunAdditiveDraws(DrawSequence sequence, TextureReadback *readback,
                        UINT64 *query_samples = nullptr,
                        TextureReadback *intermediate_readback = nullptr) {
    D3D12TestContext context;
    ASSERT_TRUE(SUCCEEDED(context.Initialize()));

    const auto pixel_shader = CompileShader(R"(
      cbuffer Color : register(b0) { float value; };
      float4 main() : SV_Target { return value.xxxx; }
    )",
                                            "ps_5_0");
    ASSERT_TRUE(SUCCEEDED(pixel_shader.result))
        << pixel_shader.diagnostic_text();

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
    auto root_signature = context.CreateRootSignature(root_desc);
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
    pipeline_desc.PrimitiveTopologyType =
        D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pipeline_desc.NumRenderTargets = 1;
    pipeline_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pipeline_desc.SampleDesc.Count = 1;
    ComPtr<ID3D12PipelineState> pipeline;
    ASSERT_EQ(context.device()->CreateGraphicsPipelineState(
                  &pipeline_desc, IID_PPV_ARGS(pipeline.put())),
              S_OK);

    constexpr UINT kSize = 8;
    auto target =
        context.CreateTexture2D(kSize, kSize, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
                                D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
                                D3D12_RESOURCE_STATE_RENDER_TARGET);
    ComPtr<ID3D12Resource> copy;
    if (sequence == DrawSequence::TextureCopyBetweenDraws) {
      copy = context.CreateTexture2D(kSize, kSize, 1,
                                     DXGI_FORMAT_R8G8B8A8_UNORM,
                                     D3D12_RESOURCE_FLAG_NONE,
                                     D3D12_RESOURCE_STATE_COPY_DEST);
    }
    auto heap =
        context.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
    ASSERT_TRUE(target);
    ASSERT_TRUE(sequence != DrawSequence::TextureCopyBetweenDraws || copy);
    ASSERT_TRUE(heap);
    const auto rtv = heap->GetCPUDescriptorHandleForHeapStart();
    context.device()->CreateRenderTargetView(target.get(), nullptr, rtv);

    ComPtr<ID3D12QueryHeap> query_heap;
    ComPtr<ID3D12Resource> query_result;
    if (sequence == DrawSequence::QueryAroundDraw ||
        sequence == DrawSequence::QueryBetweenDraws) {
      ASSERT_NE(query_samples, nullptr);
      D3D12_QUERY_HEAP_DESC query_desc = {};
      query_desc.Type = D3D12_QUERY_HEAP_TYPE_OCCLUSION;
      query_desc.Count = 1;
      ASSERT_EQ(context.device()->CreateQueryHeap(
                    &query_desc, IID_PPV_ARGS(query_heap.put())),
                S_OK);
      query_result = context.CreateBuffer(
          sizeof(*query_samples), D3D12_HEAP_TYPE_READBACK,
          D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
      ASSERT_TRUE(query_result);
    }

    ComPtr<ID3D12GraphicsCommandList4> list4;
    D3D12_RENDER_PASS_RENDER_TARGET_DESC render_pass_target = {};
    if (sequence == DrawSequence::RenderPassAcrossDraws) {
      ASSERT_EQ(context.list()->QueryInterface(IID_PPV_ARGS(list4.put())),
                S_OK);
      render_pass_target.cpuDescriptor = rtv;
      render_pass_target.BeginningAccess.Type =
          D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_PRESERVE;
      render_pass_target.EndingAccess.Type =
          D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_PRESERVE;
    }

    const FLOAT clear[4] = {};
    context.list()->ClearRenderTargetView(rtv, clear, 0, nullptr);
    if (list4)
      list4->BeginRenderPass(1, &render_pass_target, nullptr,
                             D3D12_RENDER_PASS_FLAG_NONE);
    else
      context.list()->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    context.list()->SetGraphicsRootSignature(root_signature.get());
    context.list()->SetPipelineState(pipeline.get());
    context.list()->SetGraphicsRoot32BitConstant(0, 0x3e800000, 0);
    context.list()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    const D3D12_VIEWPORT viewport = {0.0f, 0.0f, kSize, kSize, 0.0f, 1.0f};
    const D3D12_RECT scissor = {0, 0, kSize, kSize};
    context.list()->RSSetViewports(1, &viewport);
    context.list()->RSSetScissorRects(1, &scissor);

    ComPtr<ID3D12Resource> index_buffer;
    ComPtr<ID3D12Resource> predicate;
    if (sequence == DrawSequence::IndexedAcrossBarrier ||
        sequence == DrawSequence::PredicatedIndexed) {
      constexpr std::array<std::uint16_t, 3> indices = {0, 1, 2};
      index_buffer = context.CreateUploadBuffer(sizeof(indices), indices.data(),
                                                sizeof(indices));
      ASSERT_TRUE(index_buffer);
      const D3D12_INDEX_BUFFER_VIEW view = {
          index_buffer->GetGPUVirtualAddress(), sizeof(indices),
          DXGI_FORMAT_R16_UINT};
      context.list()->IASetIndexBuffer(&view);
    }
    if (sequence == DrawSequence::PredicatedIndexed) {
      constexpr UINT64 execute = 0;
      predicate = context.CreateUploadBuffer(sizeof(execute), &execute,
                                             sizeof(execute));
      ASSERT_TRUE(predicate);
      context.list()->SetPredication(predicate.get(), 0,
                                     D3D12_PREDICATION_OP_EQUAL_ZERO);
    }

    const auto draw = [&]() {
      if (index_buffer)
        context.list()->DrawIndexedInstanced(3, 1, 0, 0, 0);
      else
        context.list()->DrawInstanced(3, 1, 0, 0);
    };

    if (sequence == DrawSequence::QueryAroundDraw) {
      context.list()->BeginQuery(query_heap.get(), D3D12_QUERY_TYPE_OCCLUSION,
                                 0);
    }
    draw();
    if (sequence == DrawSequence::BarrierBetweenDraws ||
        sequence == DrawSequence::IndexedAcrossBarrier) {
      D3D12TestContext::UavBarrier(context.list(), nullptr);
      draw();
    } else if (sequence == DrawSequence::ClearBetweenDraws) {
      constexpr FLOAT kFallbackClear[4] = {0.125f, 0.125f, 0.125f, 0.125f};
      context.list()->ClearRenderTargetView(rtv, kFallbackClear, 0, nullptr);
      draw();
    } else if (sequence == DrawSequence::TextureCopyBetweenDraws) {
      ASSERT_NE(intermediate_readback, nullptr);
      D3D12TestContext::Transition(context.list(), target.get(),
                                   D3D12_RESOURCE_STATE_RENDER_TARGET,
                                   D3D12_RESOURCE_STATE_COPY_SOURCE);
      D3D12_TEXTURE_COPY_LOCATION destination = {};
      destination.pResource = copy.get();
      destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
      destination.SubresourceIndex = 0;
      D3D12_TEXTURE_COPY_LOCATION source = {};
      source.pResource = target.get();
      source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
      source.SubresourceIndex = 0;
      context.list()->CopyTextureRegion(&destination, 0, 0, 0, &source,
                                        nullptr);
      D3D12TestContext::Transition(context.list(), target.get(),
                                   D3D12_RESOURCE_STATE_COPY_SOURCE,
                                   D3D12_RESOURCE_STATE_RENDER_TARGET);
      draw();
    } else if (sequence == DrawSequence::QueryBetweenDraws) {
      context.list()->BeginQuery(query_heap.get(), D3D12_QUERY_TYPE_OCCLUSION,
                                 0);
      draw();
      context.list()->EndQuery(query_heap.get(), D3D12_QUERY_TYPE_OCCLUSION,
                               0);
      context.list()->ResolveQueryData(query_heap.get(),
                                       D3D12_QUERY_TYPE_OCCLUSION, 0, 1,
                                       query_result.get(), 0);
      draw();
    } else if (sequence == DrawSequence::RenderPassAcrossDraws) {
      list4->EndRenderPass();
      list4->BeginRenderPass(1, &render_pass_target, nullptr,
                             D3D12_RENDER_PASS_FLAG_NONE);
      draw();
      list4->EndRenderPass();
    } else if (sequence == DrawSequence::QueryAroundDraw) {
      context.list()->EndQuery(query_heap.get(), D3D12_QUERY_TYPE_OCCLUSION, 0);
      context.list()->ResolveQueryData(query_heap.get(),
                                       D3D12_QUERY_TYPE_OCCLUSION, 0, 1,
                                       query_result.get(), 0);
    }
    if (sequence == DrawSequence::PredicatedIndexed) {
      context.list()->SetPredication(nullptr, 0,
                                     D3D12_PREDICATION_OP_EQUAL_ZERO);
    }
    D3D12TestContext::Transition(context.list(), target.get(),
                                 D3D12_RESOURCE_STATE_RENDER_TARGET,
                                 D3D12_RESOURCE_STATE_COPY_SOURCE);
    if (copy) {
      D3D12TestContext::Transition(context.list(), copy.get(),
                                   D3D12_RESOURCE_STATE_COPY_DEST,
                                   D3D12_RESOURCE_STATE_COPY_SOURCE);
      ASSERT_EQ(context.ReadbackTexture(copy.get(), intermediate_readback),
                S_OK);
      ASSERT_EQ(context.ResetCommandList(), S_OK);
    }
    ASSERT_EQ(context.ReadbackTexture(target.get(), readback), S_OK);

    if (query_result) {
      UINT64 *mapped = nullptr;
      const D3D12_RANGE read_range = {0, sizeof(*query_samples)};
      ASSERT_EQ(
          query_result->Map(0, &read_range, reinterpret_cast<void **>(&mapped)),
          S_OK);
      *query_samples = *mapped;
      const D3D12_RANGE no_write = {0, 0};
      query_result->Unmap(0, &no_write);
    }
  }

  void RunIndirectDispatchBoundary(std::vector<std::uint32_t> *values) {
    D3D12TestContext context;
    ASSERT_TRUE(SUCCEEDED(context.Initialize()));

    const auto shader = CompileShader(R"(
      cbuffer Constants : register(b0) {
        uint output_index;
        uint output_value;
      };
      RWBuffer<uint> output : register(u0);

      [numthreads(1, 1, 1)]
      void main() {
        output[output_index] = output_value;
      }
    )",
                                      "cs_5_0");
    ASSERT_EQ(shader.result, S_OK) << shader.diagnostic_text();

    D3D12_DESCRIPTOR_RANGE range = {};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    range.NumDescriptors = 1;
    D3D12_ROOT_PARAMETER parameters[2] = {};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[0].Constants.ShaderRegister = 0;
    parameters[0].Constants.Num32BitValues = 2;
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[1].DescriptorTable.NumDescriptorRanges = 1;
    parameters[1].DescriptorTable.pDescriptorRanges = &range;
    D3D12_ROOT_SIGNATURE_DESC root_desc = {};
    root_desc.NumParameters = 2;
    root_desc.pParameters = parameters;
    auto root_signature = context.CreateRootSignature(root_desc);
    ASSERT_TRUE(root_signature);
    const D3D12_SHADER_BYTECODE bytecode = {shader.bytecode->GetBufferPointer(),
                                            shader.bytecode->GetBufferSize()};
    auto pipeline =
        context.CreateComputePipeline(root_signature.get(), bytecode);
    ASSERT_TRUE(pipeline);

    constexpr UINT kElementCount = 4;
    constexpr UINT64 kBufferSize = kElementCount * sizeof(UINT);
    auto output =
        context.CreateBuffer(kBufferSize, D3D12_HEAP_TYPE_DEFAULT,
                             D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                             D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    auto heap = context.CreateDescriptorHeap(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true);
    ASSERT_TRUE(output);
    ASSERT_TRUE(heap);
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
    uav.Format = DXGI_FORMAT_R32_UINT;
    uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uav.Buffer.NumElements = kElementCount;
    context.device()->CreateUnorderedAccessView(
        output.get(), nullptr, &uav, heap->GetCPUDescriptorHandleForHeapStart());

    const D3D12_DISPATCH_ARGUMENTS arguments = {1, 1, 1};
    auto argument_buffer = context.CreateUploadBuffer(
        sizeof(arguments), &arguments, sizeof(arguments));
    ASSERT_TRUE(argument_buffer);
    D3D12_INDIRECT_ARGUMENT_DESC argument_desc = {};
    argument_desc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
    D3D12_COMMAND_SIGNATURE_DESC signature_desc = {};
    signature_desc.ByteStride = sizeof(arguments);
    signature_desc.NumArgumentDescs = 1;
    signature_desc.pArgumentDescs = &argument_desc;
    ComPtr<ID3D12CommandSignature> signature;
    ASSERT_EQ(context.device()->CreateCommandSignature(
                  &signature_desc, nullptr,
                  __uuidof(ID3D12CommandSignature),
                  reinterpret_cast<void **>(signature.put())),
              S_OK);

    ID3D12DescriptorHeap *heaps[] = {heap.get()};
    context.list()->SetDescriptorHeaps(1, heaps);
    const UINT clear[4] = {};
    context.list()->ClearUnorderedAccessViewUint(
        heap->GetGPUDescriptorHandleForHeapStart(),
        heap->GetCPUDescriptorHandleForHeapStart(), output.get(), clear, 0,
        nullptr);
    context.list()->SetComputeRootSignature(root_signature.get());
    context.list()->SetPipelineState(pipeline.get());
    context.list()->SetComputeRootDescriptorTable(
        1, heap->GetGPUDescriptorHandleForHeapStart());

    constexpr std::array<UINT, 2> kFirst = {0, 0x11111111};
    constexpr std::array<UINT, 2> kIndirect = {1, 0x22222222};
    constexpr std::array<UINT, 2> kLast = {2, 0x33333333};
    context.list()->SetComputeRoot32BitConstants(0, kFirst.size(),
                                                 kFirst.data(), 0);
    context.list()->Dispatch(1, 1, 1);
    context.list()->SetComputeRoot32BitConstants(0, kIndirect.size(),
                                                 kIndirect.data(), 0);
    context.list()->ExecuteIndirect(signature.get(), 1, argument_buffer.get(),
                                     0, nullptr, 0);
    context.list()->SetComputeRoot32BitConstants(0, kLast.size(), kLast.data(),
                                                 0);
    context.list()->Dispatch(1, 1, 1);

    D3D12TestContext::Transition(context.list(), output.get(),
                                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                 D3D12_RESOURCE_STATE_COPY_SOURCE);
    std::vector<std::uint8_t> bytes;
    ASSERT_EQ(context.ReadbackBuffer(output.get(), kBufferSize, &bytes), S_OK);
    ASSERT_EQ(bytes.size(), kBufferSize);
    values->resize(kElementCount);
    std::memcpy(values->data(), bytes.data(), bytes.size());
  }

  void RunComputeBoundarySequence(ComputeBoundarySequence sequence,
                                  std::vector<std::uint32_t> *values,
                                  ExecutionPathStats *stats = nullptr) {
    D3D12TestContext context;
    ASSERT_EQ(context.Initialize(), S_OK);
    if (stats) {
      ExecutionPathConfig config = {};
      config.mode = ExecutionPathMode::Auto;
      ASSERT_EQ(context.list()->SetPrivateData(
                    dxmt::d3d12::test::kExecutionPathConfigGuid,
                    sizeof(config), &config),
                S_OK);
    }

    const auto shader = CompileShader(R"(
      Buffer<uint> input : register(t0);
      RWBuffer<uint> output : register(u0);

      [numthreads(1, 1, 1)]
      void main() {
        output[input[0]] = input[1];
      }
    )",
                                      "cs_5_0");
    ASSERT_EQ(shader.result, S_OK) << shader.diagnostic_text();

    D3D12_DESCRIPTOR_RANGE ranges[2] = {};
    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[0].NumDescriptors = 1;
    ranges[0].OffsetInDescriptorsFromTableStart = 0;
    ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    ranges[1].NumDescriptors = 1;
    ranges[1].OffsetInDescriptorsFromTableStart = 1;
    D3D12_ROOT_PARAMETER first_parameter = {};
    first_parameter.ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    first_parameter.DescriptorTable.NumDescriptorRanges = 2;
    first_parameter.DescriptorTable.pDescriptorRanges = ranges;
    D3D12_ROOT_SIGNATURE_DESC first_root_desc = {};
    first_root_desc.NumParameters = 1;
    first_root_desc.pParameters = &first_parameter;
    auto first_root = context.CreateRootSignature(first_root_desc);
    ASSERT_TRUE(first_root);

    D3D12_ROOT_PARAMETER second_parameters[2] = {};
    second_parameters[0].ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    second_parameters[0].Constants.ShaderRegister = 7;
    second_parameters[0].Constants.Num32BitValues = 1;
    second_parameters[1] = first_parameter;
    D3D12_ROOT_SIGNATURE_DESC second_root_desc = {};
    second_root_desc.NumParameters = 2;
    second_root_desc.pParameters = second_parameters;
    ComPtr<ID3D12RootSignature> second_root;
    if (sequence == ComputeBoundarySequence::RootSignatureChange) {
      second_root = context.CreateRootSignature(second_root_desc);
      ASSERT_TRUE(second_root);
    }

    const D3D12_SHADER_BYTECODE bytecode = {shader.bytecode->GetBufferPointer(),
                                            shader.bytecode->GetBufferSize()};
    auto first_pipeline =
        context.CreateComputePipeline(first_root.get(), bytecode);
    ASSERT_TRUE(first_pipeline);
    ComPtr<ID3D12PipelineState> second_pipeline;
    if (second_root) {
      second_pipeline =
          context.CreateComputePipeline(second_root.get(), bytecode);
      ASSERT_TRUE(second_pipeline);
    }

    constexpr std::array<std::uint32_t, 2> kFirstInput = {0, 0x11111111};
    constexpr std::array<std::uint32_t, 2> kSecondInput = {1, 0x22222222};
    auto first_input = context.CreateUploadBuffer(
        sizeof(kFirstInput), kFirstInput.data(), sizeof(kFirstInput));
    auto second_input = context.CreateUploadBuffer(
        sizeof(kSecondInput), kSecondInput.data(), sizeof(kSecondInput));
    constexpr UINT kElementCount = 4;
    constexpr UINT64 kBufferSize = kElementCount * sizeof(std::uint32_t);
    auto output =
        context.CreateBuffer(kBufferSize, D3D12_HEAP_TYPE_DEFAULT,
                             D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                             D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    auto heap = context.CreateDescriptorHeap(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 4, true);
    ASSERT_TRUE(first_input);
    ASSERT_TRUE(second_input);
    ASSERT_TRUE(output);
    ASSERT_TRUE(heap);

    D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
    srv.Format = DXGI_FORMAT_R32_UINT;
    srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Buffer.NumElements = kFirstInput.size();
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
    uav.Format = DXGI_FORMAT_R32_UINT;
    uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uav.Buffer.NumElements = kElementCount;
    const std::array<ID3D12Resource *, 2> inputs = {first_input.get(),
                                                   second_input.get()};
    for (UINT table = 0; table < inputs.size(); ++table) {
      const UINT base = table * 2;
      context.device()->CreateShaderResourceView(
          inputs[table], &srv, context.CpuDescriptorHandle(heap.get(), base));
      context.device()->CreateUnorderedAccessView(
          output.get(), nullptr, &uav,
          context.CpuDescriptorHandle(heap.get(), base + 1));
    }

    ComPtr<ID3D12Resource> snapshot;
    if (sequence == ComputeBoundarySequence::FallbackNativeFallback) {
      snapshot = context.CreateBuffer(kBufferSize, D3D12_HEAP_TYPE_DEFAULT,
                                      D3D12_RESOURCE_FLAG_NONE,
                                      D3D12_RESOURCE_STATE_COPY_DEST);
      ASSERT_TRUE(snapshot);
    }

    ID3D12DescriptorHeap *heaps[] = {heap.get()};
    context.list()->SetDescriptorHeaps(1, heaps);
    const UINT clear[4] = {};
    context.list()->ClearUnorderedAccessViewUint(
        context.GpuDescriptorHandle(heap.get(), 1),
        context.CpuDescriptorHandle(heap.get(), 1), output.get(), clear, 0,
        nullptr);
    D3D12TestContext::UavBarrier(context.list(), output.get());
    context.list()->SetComputeRootSignature(first_root.get());
    context.list()->SetPipelineState(first_pipeline.get());
    context.list()->SetComputeRootDescriptorTable(
        0, context.GpuDescriptorHandle(heap.get(), 0));
    context.list()->Dispatch(1, 1, 1);

    ID3D12Resource *read_source = output.get();
    if (sequence == ComputeBoundarySequence::FallbackNativeFallback) {
      D3D12TestContext::Transition(context.list(), output.get(),
                                   D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                   D3D12_RESOURCE_STATE_COPY_SOURCE);
      context.list()->CopyBufferRegion(snapshot.get(), 0, output.get(), 0,
                                       kBufferSize);
      D3D12TestContext::Transition(context.list(), snapshot.get(),
                                   D3D12_RESOURCE_STATE_COPY_DEST,
                                   D3D12_RESOURCE_STATE_COPY_SOURCE);
      read_source = snapshot.get();
    } else if (sequence == ComputeBoundarySequence::DescriptorTableUpdate) {
      context.list()->SetComputeRootDescriptorTable(
          0, context.GpuDescriptorHandle(heap.get(), 2));
      context.list()->Dispatch(1, 1, 1);
      D3D12TestContext::Transition(context.list(), output.get(),
                                   D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                   D3D12_RESOURCE_STATE_COPY_SOURCE);
    } else {
      context.list()->SetComputeRootSignature(second_root.get());
      context.list()->SetPipelineState(second_pipeline.get());
      context.list()->SetComputeRootDescriptorTable(
          1, context.GpuDescriptorHandle(heap.get(), 2));
      context.list()->Dispatch(1, 1, 1);
      D3D12TestContext::Transition(context.list(), output.get(),
                                   D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                   D3D12_RESOURCE_STATE_COPY_SOURCE);
    }

    std::vector<std::uint8_t> bytes;
    ASSERT_EQ(context.ReadbackBuffer(read_source, kBufferSize, &bytes), S_OK);
    if (stats) {
      UINT stats_size = sizeof(*stats);
      ASSERT_EQ(context.list()->GetPrivateData(
                    dxmt::d3d12::test::kExecutionPathStatsGuid, &stats_size,
                    stats),
                S_OK);
      ASSERT_EQ(stats_size, sizeof(*stats));
      ASSERT_EQ(stats->struct_size, sizeof(*stats));
      ASSERT_EQ(stats->segment_count, stats->traced_segment_count)
          << "boundary test exceeded the ordered trace capacity";
    }
    ASSERT_EQ(bytes.size(), kBufferSize);
    values->resize(kElementCount);
    std::memcpy(values->data(), bytes.data(), bytes.size());
  }

  static std::vector<ExecutionPathSegmentKind>
  CompressedSegmentKinds(const ExecutionPathStats &stats) {
    std::vector<ExecutionPathSegmentKind> kinds;
    for (UINT index = 0; index < stats.traced_segment_count; ++index) {
      if (!kinds.empty() && kinds.back() == stats.segment_kinds[index])
        continue;
      kinds.push_back(stats.segment_kinds[index]);
    }
    return kinds;
  }

  void ExpectSolidColor(const TextureReadback &readback,
                        std::uint32_t expected) {
    for (UINT y = 0; y < readback.height; ++y) {
      for (UINT x = 0; x < readback.width; ++x) {
        std::uint32_t pixel = 0;
        std::memcpy(&pixel,
                    readback.data.data() + y * readback.row_pitch +
                        x * sizeof(pixel),
                    sizeof(pixel));
        EXPECT_TRUE(ColorsMatch(pixel, expected, 1))
            << "pixel (" << x << ", " << y << ") was 0x" << std::hex << pixel;
      }
    }
  }

  static constexpr std::array<std::uint32_t, 8> kInput = {1,  3,  7,   15,
                                                          31, 63, 127, 255};
};

class DescriptorParitySpec : public ExecutionPathBoundarySpec {};

TEST_F(DescriptorParitySpec, NativeAndFallbackResultsMatch) {
  std::vector<std::uint32_t> native;
  std::vector<std::uint32_t> fallback;
  ASSERT_NO_FATAL_FAILURE(RunDescriptorDispatch(DispatchPath::Native, &native));
  ASSERT_NO_FATAL_FAILURE(
      RunDescriptorDispatch(DispatchPath::PredicatedFallback, &fallback));

  EXPECT_EQ(native, fallback);
  EXPECT_EQ(native, (std::vector<std::uint32_t>(kInput.begin(), kInput.end())));
}

TEST_F(ExecutionPathBoundarySpec, PreservesDescriptorState) {
  std::vector<std::uint32_t> actual;
  ASSERT_NO_FATAL_FAILURE(
      RunDescriptorDispatch(DispatchPath::NativeFallbackNative, &actual));

  std::vector<std::uint32_t> expected;
  expected.reserve(kInput.size());
  for (const auto value : kInput)
    expected.push_back(value * 2);
  EXPECT_EQ(actual, expected);
}

TEST_F(ExecutionPathBoundarySpec, FlushesPendingBarriers) {
  std::vector<std::uint32_t> actual;
  ASSERT_NO_FATAL_FAILURE(RunDescriptorDispatch(DispatchPath::Native, &actual));

  EXPECT_EQ(actual, (std::vector<std::uint32_t>(kInput.begin(), kInput.end())));
}

TEST_F(ExecutionPathBoundarySpec, DoesNotDropCopy) {
  std::vector<std::uint32_t> actual;
  std::vector<std::uint32_t> copied;
  ASSERT_NO_FATAL_FAILURE(
      RunDescriptorDispatch(DispatchPath::NativeCopyNative, &actual, &copied));

  EXPECT_EQ(copied, (std::vector<std::uint32_t>(kInput.begin(), kInput.end())));
  std::vector<std::uint32_t> expected;
  expected.reserve(kInput.size());
  for (const auto value : kInput)
    expected.push_back(value * 2);
  EXPECT_EQ(actual, expected);
}

TEST_F(ExecutionPathBoundarySpec, PreservesComputeRootState) {
  D3D12TestContext context;
  ASSERT_TRUE(SUCCEEDED(context.Initialize()));

  D3D12_DESCRIPTOR_RANGE range = {};
  range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  range.NumDescriptors = 1;
  D3D12_ROOT_PARAMETER parameters[2] = {};
  parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  parameters[0].Constants.Num32BitValues = 1;
  parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[1].DescriptorTable.NumDescriptorRanges = 1;
  parameters[1].DescriptorTable.pDescriptorRanges = &range;
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  root_desc.NumParameters = 2;
  root_desc.pParameters = parameters;
  auto root_signature = context.CreateRootSignature(root_desc);
  ASSERT_TRUE(root_signature);
  auto pipeline = context.CreateComputePipeline(
      root_signature.get(), dxmt::test::ClearBufferComputeShader());
  ASSERT_TRUE(pipeline);

  constexpr UINT kElementCount = 64;
  constexpr UINT kValue = 0x13579bdf;
  constexpr UINT64 kBufferSize = kElementCount * sizeof(UINT);
  std::array<ComPtr<ID3D12Resource>, 2> outputs;
  auto heap =
      context.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                                   static_cast<UINT>(outputs.size()), true);
  ASSERT_TRUE(heap);
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
  uav.Format = DXGI_FORMAT_R32_UINT;
  uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav.Buffer.NumElements = kElementCount;
  for (UINT index = 0; index < outputs.size(); ++index) {
    outputs[index] =
        context.CreateBuffer(kBufferSize, D3D12_HEAP_TYPE_DEFAULT,
                             D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                             D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    ASSERT_TRUE(outputs[index]);
    context.device()->CreateUnorderedAccessView(
        outputs[index].get(), nullptr, &uav,
        context.CpuDescriptorHandle(heap.get(), index));
  }

  ID3D12DescriptorHeap *heaps[] = {heap.get()};
  context.list()->SetDescriptorHeaps(1, heaps);
  const UINT clear[4] = {};
  for (UINT index = 0; index < outputs.size(); ++index) {
    context.list()->ClearUnorderedAccessViewUint(
        context.GpuDescriptorHandle(heap.get(), index),
        context.CpuDescriptorHandle(heap.get(), index), outputs[index].get(),
        clear, 0, nullptr);
  }
  context.list()->SetComputeRootSignature(root_signature.get());
  context.list()->SetPipelineState(pipeline.get());
  context.list()->SetComputeRoot32BitConstant(0, kValue, 0);
  context.list()->SetComputeRootDescriptorTable(
      1, context.GpuDescriptorHandle(heap.get(), 0));
  context.list()->Dispatch(1, 1, 1);
  D3D12TestContext::UavBarrier(context.list(), outputs[0].get());
  context.list()->SetComputeRootDescriptorTable(
      1, context.GpuDescriptorHandle(heap.get(), 1));
  context.list()->Dispatch(1, 1, 1);

  auto readback = context.CreateBuffer(
      2 * kBufferSize, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(readback);
  for (UINT index = 0; index < outputs.size(); ++index) {
    D3D12TestContext::Transition(context.list(), outputs[index].get(),
                                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                 D3D12_RESOURCE_STATE_COPY_SOURCE);
    context.list()->CopyBufferRegion(readback.get(), index * kBufferSize,
                                     outputs[index].get(), 0, kBufferSize);
  }
  ASSERT_EQ(context.ExecuteAndWait(), S_OK);

  UINT *mapped = nullptr;
  const D3D12_RANGE read_range = {0, 2 * kBufferSize};
  ASSERT_EQ(readback->Map(0, &read_range, reinterpret_cast<void **>(&mapped)),
            S_OK);
  for (UINT index = 0; index < 2 * kElementCount; ++index)
    EXPECT_EQ(mapped[index], kValue) << "element " << index;
  const D3D12_RANGE no_write = {0, 0};
  readback->Unmap(0, &no_write);
}

TEST_F(ExecutionPathBoundarySpec, DoesNotReplayDrawTwice) {
  TextureReadback readback;
  ASSERT_NO_FATAL_FAILURE(RunAdditiveDraws(DrawSequence::Single, &readback));
  ExpectSolidColor(readback, 0x40404040);
}

TEST_F(ExecutionPathBoundarySpec, PreservesGraphicsRootState) {
  TextureReadback readback;
  ASSERT_NO_FATAL_FAILURE(
      RunAdditiveDraws(DrawSequence::BarrierBetweenDraws, &readback));
  ExpectSolidColor(readback, 0x80808080);
}

TEST_F(ExecutionPathBoundarySpec, PreservesQueryState) {
  TextureReadback readback;
  UINT64 samples = 0;
  ASSERT_NO_FATAL_FAILURE(
      RunAdditiveDraws(DrawSequence::QueryAroundDraw, &readback, &samples));

  EXPECT_GT(samples, 0ull);
  ExpectSolidColor(readback, 0x40404040);
}

TEST_F(ExecutionPathBoundarySpec, IndexedDrawPreservesStateAcrossBarrier) {
  TextureReadback readback;
  ASSERT_NO_FATAL_FAILURE(
      RunAdditiveDraws(DrawSequence::IndexedAcrossBarrier, &readback));
  ExpectSolidColor(readback, 0x80808080);
}

TEST_F(ExecutionPathBoundarySpec, PredicatedIndexedDrawMatchesDirectDraw) {
  TextureReadback direct;
  TextureReadback predicated;
  ASSERT_NO_FATAL_FAILURE(RunAdditiveDraws(DrawSequence::Single, &direct));
  ASSERT_NO_FATAL_FAILURE(
      RunAdditiveDraws(DrawSequence::PredicatedIndexed, &predicated));
  EXPECT_EQ(predicated.width, direct.width);
  EXPECT_EQ(predicated.height, direct.height);
  EXPECT_EQ(predicated.row_pitch, direct.row_pitch);
  EXPECT_EQ(predicated.data, direct.data);
  ExpectSolidColor(predicated, 0x40404040);
}

TEST_F(ExecutionPathBoundarySpec, RtvClearPreservesStateBetweenNativeDraws) {
  TextureReadback readback;
  ASSERT_NO_FATAL_FAILURE(
      RunAdditiveDraws(DrawSequence::ClearBetweenDraws, &readback));

  // The first 0.25 draw is replaced by the fallback clear. The second native
  // draw must retain the graphics bindings and add 0.25 to the clear value.
  ExpectSolidColor(readback, 0x60606060);
}

TEST_F(ExecutionPathBoundarySpec,
       TextureCopyPreservesSnapshotAndStateBetweenNativeDraws) {
  TextureReadback final;
  TextureReadback snapshot;
  ASSERT_NO_FATAL_FAILURE(RunAdditiveDraws(
      DrawSequence::TextureCopyBetweenDraws, &final, nullptr, &snapshot));

  ExpectSolidColor(snapshot, 0x40404040);
  ExpectSolidColor(final, 0x80808080);
}

TEST_F(ExecutionPathBoundarySpec,
       ExecuteIndirectPreservesBindingsBetweenNativeDispatches) {
  std::vector<std::uint32_t> values;
  ASSERT_NO_FATAL_FAILURE(RunIndirectDispatchBoundary(&values));

  EXPECT_EQ(values, (std::vector<std::uint32_t>{
                        0x11111111, 0x22222222, 0x33333333, 0x00000000}));
}

TEST_F(ExecutionPathBoundarySpec,
       ClearDispatchCopyExecutesFallbackNativeFallback) {
  std::vector<std::uint32_t> values;
  ASSERT_NO_FATAL_FAILURE(RunComputeBoundarySequence(
      ComputeBoundarySequence::FallbackNativeFallback, &values));

  // ClearUnorderedAccessViewUint is the leading fallback segment, Dispatch is
  // native, and the transition plus CopyBufferRegion form the trailing
  // fallback segment whose snapshot is read back here.
  EXPECT_EQ(values, (std::vector<std::uint32_t>{
                        0x11111111, 0x00000000, 0x00000000, 0x00000000}));
}

TEST_F(ExecutionPathBoundarySpec,
       DescriptorTableOnlyUpdateSelectsNextNativeDispatchInput) {
  std::vector<std::uint32_t> values;
  ASSERT_NO_FATAL_FAILURE(RunComputeBoundarySequence(
      ComputeBoundarySequence::DescriptorTableUpdate, &values));

  // The only state mutation between native dispatches is the fallback
  // SetComputeRootDescriptorTable record selecting the second SRV/UAV table.
  EXPECT_EQ(values, (std::vector<std::uint32_t>{
                        0x11111111, 0x22222222, 0x00000000, 0x00000000}));
}

TEST_F(ExecutionPathBoundarySpec,
       RootSignatureChangeRebuildsBindingsForNextNativeDispatch) {
  std::vector<std::uint32_t> values;
  ASSERT_NO_FATAL_FAILURE(RunComputeBoundarySequence(
      ComputeBoundarySequence::RootSignatureChange, &values));

  // SetComputeRootSignature and SetPipelineState are fallback state records;
  // the following native dispatch uses the table at the new root index.
  EXPECT_EQ(values, (std::vector<std::uint32_t>{
                        0x11111111, 0x22222222, 0x00000000, 0x00000000}));
}

TEST_F(ExecutionPathBoundarySpec,
       OrderedTelemetryProvesBoundarySegmentTopologies) {
  using Kind = ExecutionPathSegmentKind;

  std::vector<std::uint32_t> values;
  ExecutionPathStats fallback_native_fallback = {};
  ASSERT_NO_FATAL_FAILURE(RunComputeBoundarySequence(
      ComputeBoundarySequence::FallbackNativeFallback, &values,
      &fallback_native_fallback));
  EXPECT_EQ(CompressedSegmentKinds(fallback_native_fallback),
            (std::vector<Kind>{Kind::Fallback, Kind::Compute,
                               Kind::Fallback}));
  EXPECT_EQ(fallback_native_fallback.replayed_compute_packets, 1u);
  EXPECT_GT(fallback_native_fallback.replayed_fallback_ranges, 0u);

  ExecutionPathStats descriptor_boundary = {};
  ASSERT_NO_FATAL_FAILURE(RunComputeBoundarySequence(
      ComputeBoundarySequence::DescriptorTableUpdate, &values,
      &descriptor_boundary));
  EXPECT_EQ(CompressedSegmentKinds(descriptor_boundary),
            (std::vector<Kind>{Kind::Fallback, Kind::Compute,
                               Kind::Fallback, Kind::Compute,
                               Kind::Fallback}));
  EXPECT_EQ(descriptor_boundary.replayed_compute_packets, 2u);
  EXPECT_GT(descriptor_boundary.replayed_fallback_ranges, 0u);

  ExecutionPathStats root_signature_boundary = {};
  ASSERT_NO_FATAL_FAILURE(RunComputeBoundarySequence(
      ComputeBoundarySequence::RootSignatureChange, &values,
      &root_signature_boundary));
  EXPECT_EQ(CompressedSegmentKinds(root_signature_boundary),
            (std::vector<Kind>{Kind::Fallback, Kind::Compute,
                               Kind::Fallback, Kind::Compute,
                               Kind::Fallback}));
  EXPECT_EQ(root_signature_boundary.replayed_compute_packets, 2u);
  EXPECT_GT(root_signature_boundary.replayed_fallback_ranges, 0u);
}

TEST_F(ExecutionPathBoundarySpec,
       RenderPassRebindPreservesStateAcrossNativeDraws) {
  TextureReadback readback;
  ASSERT_NO_FATAL_FAILURE(
      RunAdditiveDraws(DrawSequence::RenderPassAcrossDraws, &readback));

  // Each BeginRenderPass emits a fallback RenderTargets record. The native
  // draw on each side must retain the pipeline, roots and additive blend state.
  ExpectSolidColor(readback, 0x80808080);
}

TEST_F(ExecutionPathBoundarySpec,
       QueryBeginEndFallbacksPreserveSurroundingNativeDrawState) {
  TextureReadback readback;
  UINT64 samples = 0;
  ASSERT_NO_FATAL_FAILURE(RunAdditiveDraws(
      DrawSequence::QueryBetweenDraws, &readback, &samples));

  // BeginQuery, EndQuery and ResolveQueryData are fallback records between
  // three native draws. Only the middle draw contributes to the query.
  EXPECT_GT(samples, 0ull);
  ExpectSolidColor(readback, 0xc0c0c0c0);
}

} // namespace
