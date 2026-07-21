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
using dxmt::test::TextureReadback;

class ExecuteIndirectDrawSpec : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_TRUE(SUCCEEDED(context_.Initialize())); }

  void ExpectDrawIndexed(
      const UINT *count_value = nullptr,
      UINT command_stride = sizeof(D3D12_DRAW_INDEXED_ARGUMENTS)) {
    constexpr UINT width = 16;
    constexpr UINT height = 16;
    constexpr UINT64 count_offset = sizeof(UINT);
    auto render_target =
        context_.CreateTexture2D(width, height, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
                                 D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
                                 D3D12_RESOURCE_STATE_RENDER_TARGET);
    auto rtv_heap =
        context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
    ASSERT_TRUE(render_target);
    ASSERT_TRUE(rtv_heap);
    const auto rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
    context_.device()->CreateRenderTargetView(render_target.get(), nullptr,
                                              rtv);

    D3D12_ROOT_SIGNATURE_DESC root_desc = {};
    root_desc.Flags =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    auto root_signature = context_.CreateRootSignature(root_desc);
    const auto pixel = CompileShader(
        "float4 main() : SV_Target { return float4(1, 0, 0, 1); }",
        "ps_5_0");
    ASSERT_EQ(pixel.result, S_OK) << pixel.diagnostic_text();
    const D3D12_SHADER_BYTECODE pixel_bytecode = {
        pixel.bytecode->GetBufferPointer(), pixel.bytecode->GetBufferSize()};
    auto pipeline = context_.CreateGraphicsPipeline(root_signature.get(),
                                                    DXGI_FORMAT_R8G8B8A8_UNORM,
                                                    pixel_bytecode);
    ASSERT_TRUE(root_signature);
    ASSERT_TRUE(pipeline);

    const std::array<std::uint16_t, 3> indices = {0, 1, 2};
    auto index_buffer = context_.CreateUploadBuffer(
        sizeof(indices), indices.data(), sizeof(indices));
    const D3D12_DRAW_INDEXED_ARGUMENTS arguments = {3, 1, 0, 0, 0};
    ASSERT_GE(command_stride, sizeof(arguments));
    std::vector<std::uint8_t> command(command_stride, 0xcd);
    std::memcpy(command.data(), &arguments, sizeof(arguments));
    auto argument_buffer = context_.CreateUploadBuffer(
        command.size(), command.data(), command.size());
    ComPtr<ID3D12Resource> count_buffer;
    ComPtr<ID3D12Resource> count_upload;
    if (count_value) {
      const std::array<UINT, 2> count_data = {0xdeadbeef, *count_value};
      count_upload = context_.CreateUploadBuffer(
          sizeof(count_data), count_data.data(), sizeof(count_data));
      count_buffer = context_.CreateBuffer(
          sizeof(count_data), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
          D3D12_RESOURCE_STATE_COPY_DEST);
      if (count_upload && count_buffer) {
        context_.list()->CopyBufferRegion(count_buffer.get(), count_offset,
                                          count_upload.get(), count_offset,
                                          sizeof(*count_value));
        D3D12TestContext::Transition(context_.list(), count_buffer.get(),
                                     D3D12_RESOURCE_STATE_COPY_DEST,
                                     D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
      }
    }
    ASSERT_TRUE(index_buffer);
    ASSERT_TRUE(argument_buffer);
    ASSERT_TRUE(!count_value || count_buffer);
    ASSERT_TRUE(!count_value || count_upload);

    D3D12_INDIRECT_ARGUMENT_DESC argument_desc = {};
    argument_desc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;
    D3D12_COMMAND_SIGNATURE_DESC signature_desc = {};
    signature_desc.ByteStride = command_stride;
    signature_desc.NumArgumentDescs = 1;
    signature_desc.pArgumentDescs = &argument_desc;
    ComPtr<ID3D12CommandSignature> signature;
    ASSERT_TRUE(SUCCEEDED(context_.device()->CreateCommandSignature(
        &signature_desc, nullptr, IID_PPV_ARGS(signature.put()))));

    const float clear_color[4] = {};
    context_.list()->ClearRenderTargetView(rtv, clear_color, 0, nullptr);
    context_.list()->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    context_.list()->SetGraphicsRootSignature(root_signature.get());
    context_.list()->SetPipelineState(pipeline.get());
    context_.list()->IASetPrimitiveTopology(
        D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    const D3D12_INDEX_BUFFER_VIEW index_view = {
        index_buffer->GetGPUVirtualAddress(), sizeof(indices),
        DXGI_FORMAT_R16_UINT};
    context_.list()->IASetIndexBuffer(&index_view);
    const D3D12_VIEWPORT viewport = {
        0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height),
        0.0f, 1.0f};
    const D3D12_RECT scissor = {0, 0, width, height};
    context_.list()->RSSetViewports(1, &viewport);
    context_.list()->RSSetScissorRects(1, &scissor);
    context_.list()->ExecuteIndirect(signature.get(), 1, argument_buffer.get(),
                                     0, count_buffer.get(),
                                     count_buffer ? count_offset : 0);
    D3D12TestContext::Transition(context_.list(), render_target.get(),
                                 D3D12_RESOURCE_STATE_RENDER_TARGET,
                                 D3D12_RESOURCE_STATE_COPY_SOURCE);

    TextureReadback readback;
    ASSERT_TRUE(
        SUCCEEDED(context_.ReadbackTexture(render_target.get(), &readback)));
    ASSERT_EQ(readback.width, width);
    ASSERT_EQ(readback.height, height);
    for (UINT y = 0; y < height; ++y) {
      for (UINT x = 0; x < width; ++x) {
        std::uint32_t pixel = 0;
        std::memcpy(&pixel,
                    readback.data.data() + y * readback.row_pitch +
                        x * sizeof(pixel),
                    sizeof(pixel));
        const std::uint32_t expected =
            !count_value || *count_value ? 0xff0000ff : 0;
        EXPECT_TRUE(ColorsMatch(pixel, expected, 1))
            << "pixel (" << x << ", " << y << ")";
      }
    }
  }

  D3D12TestContext context_;
};

TEST_F(ExecuteIndirectDrawSpec, DrawIndexed) { ExpectDrawIndexed(); }

TEST_F(ExecuteIndirectDrawSpec, PaddedCommandStrideDrawsIndexed) {
  ExpectDrawIndexed(nullptr, sizeof(D3D12_DRAW_INDEXED_ARGUMENTS) + 16);
}

TEST_F(ExecuteIndirectDrawSpec, CountBufferZeroSkipsDrawIndexed) {
  const UINT count = 0;
  ExpectDrawIndexed(&count);
}

TEST_F(ExecuteIndirectDrawSpec, CountBufferOneDrawsIndexed) {
  const UINT count = 1;
  ExpectDrawIndexed(&count);
}

TEST_F(ExecuteIndirectDrawSpec, RejectsStrideSmallerThanDrawArguments) {
  D3D12_INDIRECT_ARGUMENT_DESC argument = {};
  argument.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
  D3D12_COMMAND_SIGNATURE_DESC desc = {};
  desc.ByteStride = sizeof(D3D12_DRAW_ARGUMENTS) - sizeof(UINT);
  desc.NumArgumentDescs = 1;
  desc.pArgumentDescs = &argument;
  ComPtr<ID3D12CommandSignature> signature;

  EXPECT_EQ(context_.device()->CreateCommandSignature(
                &desc, nullptr, IID_PPV_ARGS(signature.put())),
            E_INVALIDARG);
  EXPECT_FALSE(signature);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(ExecuteIndirectDrawSpec, RejectsSignatureWithoutTerminalOperation) {
  D3D12_INDIRECT_ARGUMENT_DESC argument = {};
  argument.Type = D3D12_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW;
  argument.VertexBuffer.Slot = 0;
  D3D12_COMMAND_SIGNATURE_DESC desc = {};
  desc.ByteStride = sizeof(D3D12_VERTEX_BUFFER_VIEW);
  desc.NumArgumentDescs = 1;
  desc.pArgumentDescs = &argument;
  ComPtr<ID3D12CommandSignature> signature;

  EXPECT_EQ(context_.device()->CreateCommandSignature(
                &desc, nullptr, IID_PPV_ARGS(signature.put())),
            E_INVALIDARG);
  EXPECT_FALSE(signature);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
