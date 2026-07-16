#include "d3d12_binding_hotspot_scenarios.hpp"

#include "d3d12_test_context.hpp"
#include "shaders/bindless_dxil_shaders.hpp"

#include <d3dcompiler.h>

#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

namespace dxmt::test {
namespace {

BindingHotspotError HResultError(const char *operation, HRESULT hr) {
  if (SUCCEEDED(hr))
    return std::nullopt;

  std::ostringstream message;
  message << operation << " failed with HRESULT 0x" << std::hex
          << static_cast<unsigned long>(hr);
  return message.str();
}

BindingHotspotError CompileAveragingPixelShader(std::uint32_t input_count,
                                                bool typed_buffers,
                                                ComPtr<ID3DBlob> *shader) {
  std::ostringstream source;
  for (std::uint32_t input = 0; input < input_count; ++input) {
    source << (typed_buffers ? "Buffer<float4> input"
                             : "StructuredBuffer<float4> input")
           << input << " : register(t" << input << ");\n";
  }
  source << "float4 main(float4 position : SV_Position) : SV_Target { return (";
  for (std::uint32_t input = 0; input < input_count; ++input) {
    if (input)
      source << " + ";
    source << "input" << input << "[0]";
  }
  source << ") / " << input_count << ".0; }\n";

  const auto text = source.str();
  ComPtr<ID3DBlob> errors;
  const HRESULT hr = D3DCompile(
      text.data(), text.size(), "binding_hotspot_pixel.hlsl", nullptr, nullptr,
      "main", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, shader->put(),
      errors.put());
  if (SUCCEEDED(hr))
    return std::nullopt;

  std::string message = "failed to compile binding-hotspot pixel shader";
  if (errors && errors->GetBufferPointer()) {
    message += ": ";
    message.append(static_cast<const char *>(errors->GetBufferPointer()),
                   errors->GetBufferSize());
  }
  return message;
}

using InputValue = std::array<float, 4>;

ComPtr<ID3D12Resource> CreateInputBuffer(D3D12TestContext &context,
                                         const InputValue &value) {
  return context.CreateUploadBuffer(sizeof(value), value.data(), sizeof(value));
}

void WriteStructuredBufferSrv(D3D12TestContext &context,
                              ID3D12Resource *resource,
                              D3D12_CPU_DESCRIPTOR_HANDLE handle) {
  D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
  desc.Format = DXGI_FORMAT_UNKNOWN;
  desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
  desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  desc.Buffer.FirstElement = 0;
  desc.Buffer.NumElements = 1;
  desc.Buffer.StructureByteStride = sizeof(InputValue);
  context.device()->CreateShaderResourceView(resource, &desc, handle);
}

void WriteTypedBufferSrv(D3D12TestContext &context,
                         ID3D12Resource *resource,
                         D3D12_CPU_DESCRIPTOR_HANDLE handle) {
  D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
  desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
  desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
  desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  desc.Buffer.FirstElement = 0;
  desc.Buffer.NumElements = 1;
  context.device()->CreateShaderResourceView(resource, &desc, handle);
}

struct GraphicsScenarioResources {
  ComPtr<ID3D12RootSignature> root_signature;
  ComPtr<ID3D12PipelineState> pipeline;
  ComPtr<ID3D12Resource> render_target;
  ComPtr<ID3D12DescriptorHeap> rtv_heap;
  D3D12_CPU_DESCRIPTOR_HANDLE rtv = {};
};

BindingHotspotError CreateGraphicsScenarioResources(
    D3D12TestContext &context,
    const std::vector<D3D12_ROOT_PARAMETER> &parameters,
    std::uint32_t input_count, GraphicsScenarioResources *resources,
    bool typed_buffers = false) {
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  root_desc.NumParameters = static_cast<UINT>(parameters.size());
  root_desc.pParameters = parameters.data();
  resources->root_signature = context.CreateRootSignature(root_desc);
  if (!resources->root_signature)
    return "failed to create binding-hotspot root signature";

  ComPtr<ID3DBlob> shader;
  if (auto error =
          CompileAveragingPixelShader(input_count, typed_buffers, &shader))
    return error;
  const D3D12_SHADER_BYTECODE bytecode = {shader->GetBufferPointer(),
                                          shader->GetBufferSize()};
  resources->pipeline = context.CreateGraphicsPipeline(
      resources->root_signature.get(), DXGI_FORMAT_R8G8B8A8_UNORM, bytecode);
  if (!resources->pipeline)
    return "failed to create binding-hotspot graphics pipeline";

  resources->render_target =
      context.CreateTexture2D(4, 4, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
                              D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
                              D3D12_RESOURCE_STATE_RENDER_TARGET);
  resources->rtv_heap =
      context.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
  if (!resources->render_target || !resources->rtv_heap)
    return "failed to create binding-hotspot render target";
  resources->rtv = resources->rtv_heap->GetCPUDescriptorHandleForHeapStart();
  context.device()->CreateRenderTargetView(resources->render_target.get(),
                                           nullptr, resources->rtv);
  return std::nullopt;
}

void RecordGraphicsState(D3D12TestContext &context,
                         const GraphicsScenarioResources &resources,
                         ID3D12DescriptorHeap *heap,
                         ID3D12GraphicsCommandList *list = nullptr) {
  if (!list)
    list = context.list();
  const D3D12_VIEWPORT viewport = {0.0f, 0.0f, 4.0f, 4.0f, 0.0f, 1.0f};
  const D3D12_RECT scissor = {0, 0, 4, 4};
  const float clear_color[4] = {};
  list->ClearRenderTargetView(resources.rtv, clear_color, 0, nullptr);
  list->OMSetRenderTargets(1, &resources.rtv, FALSE, nullptr);
  list->SetGraphicsRootSignature(resources.root_signature.get());
  list->SetPipelineState(resources.pipeline.get());
  if (heap) {
    ID3D12DescriptorHeap *heaps[] = {heap};
    list->SetDescriptorHeaps(1, heaps);
  }
  list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  list->RSSetViewports(1, &viewport);
  list->RSSetScissorRects(1, &scissor);
}

void RecordSingleTableDraw(D3D12TestContext &context,
                           const GraphicsScenarioResources &resources,
                           ID3D12DescriptorHeap *heap) {
  RecordGraphicsState(context, resources, heap);
  context.list()->SetGraphicsRootDescriptorTable(
      0, heap->GetGPUDescriptorHandleForHeapStart());
  context.list()->DrawInstanced(3, 1, 0, 0);
  D3D12TestContext::Transition(context.list(), resources.render_target.get(),
                               D3D12_RESOURCE_STATE_RENDER_TARGET,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
}

BindingHotspotError ExecuteAndValidate(
    D3D12TestContext &context, const GraphicsScenarioResources &resources,
    std::uint32_t expected, BindingHotspotMeasurement *measurement) {
  TextureReadback readback;
  if (auto error = HResultError(
          "binding-hotspot readback",
          context.ReadbackTexture(resources.render_target.get(), &readback)))
    return error;
  if (readback.data.size() < sizeof(expected))
    return "binding-hotspot readback has an unexpected size";

  std::uint32_t actual = 0;
  std::memcpy(&actual, readback.data.data(), sizeof(actual));
  if (measurement) {
    measurement->expected = expected;
    measurement->actual = actual;
  }
  if (actual == expected)
    return std::nullopt;

  std::ostringstream message;
  message << "binding-hotspot readback was " << actual << ", expected "
          << expected;
  return message.str();
}

BindingHotspotError ValidateDimensions(const char *scenario,
                                       std::uint32_t first,
                                       std::uint32_t second) {
  if (first && second)
    return std::nullopt;
  std::ostringstream message;
  message << scenario << " requires non-zero dimensions";
  return message.str();
}

} // namespace

BindingHotspotError
RunArgumentTableUpdateScenario(std::uint32_t descriptor_count,
                               std::uint32_t overwrite_rounds,
                               BindingHotspotMeasurement *measurement) {
  if (auto error = ValidateDimensions("argument-table update", descriptor_count,
                                      overwrite_rounds))
    return error;

  D3D12TestContext context;
  if (auto error =
          HResultError("D3D12 context initialization", context.Initialize()))
    return error;

  D3D12_DESCRIPTOR_RANGE range = {};
  range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  range.NumDescriptors = descriptor_count;
  range.BaseShaderRegister = 0;
  range.OffsetInDescriptorsFromTableStart = 0;
  std::array<D3D12_ROOT_PARAMETER, 1> parameters = {};
  parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[0].DescriptorTable.NumDescriptorRanges = 1;
  parameters[0].DescriptorTable.pDescriptorRanges = &range;
  parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

  const std::vector<D3D12_ROOT_PARAMETER> parameter_vector(parameters.begin(),
                                                           parameters.end());
  GraphicsScenarioResources resources;
  if (auto error = CreateGraphicsScenarioResources(
          context, parameter_vector, descriptor_count, &resources))
    return error;

  constexpr InputValue kValueA = {1.0f, 0.0f, 0.0f, 1.0f};
  constexpr InputValue kValueB = {0.0f, 1.0f, 0.0f, 1.0f};
  constexpr std::uint32_t kPixelA = 0xff0000ffu;
  constexpr std::uint32_t kPixelB = 0xff00ff00u;
  auto input_a = CreateInputBuffer(context, kValueA);
  auto input_b = CreateInputBuffer(context, kValueB);
  auto heap = context.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, descriptor_count, true);
  if (!input_a || !input_b || !heap)
    return "failed to create argument-table update resources";

  for (std::uint32_t descriptor = 0; descriptor < descriptor_count;
       ++descriptor) {
    WriteStructuredBufferSrv(
        context, input_a.get(),
        context.CpuDescriptorHandle(heap.get(), descriptor));
  }

  const auto begin = std::chrono::steady_clock::now();
  for (std::uint32_t round = 0; round < overwrite_rounds; ++round) {
    auto *resource = (round & 1u) ? input_b.get() : input_a.get();
    for (std::uint32_t descriptor = 0; descriptor < descriptor_count;
         ++descriptor) {
      WriteStructuredBufferSrv(
          context, resource,
          context.CpuDescriptorHandle(heap.get(), descriptor));
    }
  }
  const auto end = std::chrono::steady_clock::now();
  if (measurement) {
    measurement->measured_ms =
        std::chrono::duration<double, std::milli>(end - begin).count();
    measurement->operations =
        std::uint64_t(descriptor_count) * overwrite_rounds;
  }

  RecordSingleTableDraw(context, resources, heap.get());
  const std::uint32_t expected =
      ((overwrite_rounds - 1) & 1u) ? kPixelB : kPixelA;
  return ExecuteAndValidate(context, resources, expected, measurement);
}

BindingHotspotError
RunRootTableMaterializationScenario(std::uint32_t draw_count,
                                    std::uint32_t root_table_count,
                                    BindingHotspotMeasurement *measurement) {
  if (auto error = ValidateDimensions("root-table materialization", draw_count,
                                      root_table_count))
    return error;
  if (root_table_count > 16)
    return "root-table materialization exceeds the test root-table limit";

  D3D12TestContext context;
  if (auto error =
          HResultError("D3D12 context initialization", context.Initialize()))
    return error;

  std::vector<D3D12_DESCRIPTOR_RANGE> ranges(root_table_count);
  std::vector<D3D12_ROOT_PARAMETER> parameters(root_table_count);
  for (std::uint32_t table = 0; table < root_table_count; ++table) {
    ranges[table].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[table].NumDescriptors = 1;
    ranges[table].BaseShaderRegister = table;
    ranges[table].OffsetInDescriptorsFromTableStart = 0;
    parameters[table].ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[table].DescriptorTable.NumDescriptorRanges = 1;
    parameters[table].DescriptorTable.pDescriptorRanges = &ranges[table];
    parameters[table].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  }

  GraphicsScenarioResources resources;
  if (auto error = CreateGraphicsScenarioResources(
          context, parameters, root_table_count, &resources))
    return error;

  auto heap = context.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, root_table_count, true);
  std::vector<ComPtr<ID3D12Resource>> inputs;
  inputs.reserve(root_table_count);
  constexpr InputValue kValue = {0.0f, 0.0f, 1.0f, 1.0f};
  constexpr std::uint32_t kExpectedPixel = 0xffff0000u;
  for (std::uint32_t table = 0; table < root_table_count; ++table) {
    auto input = CreateInputBuffer(context, kValue);
    if (!input || !heap)
      return "failed to create root-table materialization resources";
    WriteStructuredBufferSrv(context, input.get(),
                             context.CpuDescriptorHandle(heap.get(), table));
    inputs.push_back(std::move(input));
  }

  auto record_draws = [&](std::uint32_t count, bool copy_source) {
    RecordGraphicsState(context, resources, heap.get());
    for (std::uint32_t draw = 0; draw < count; ++draw) {
      for (std::uint32_t table = 0; table < root_table_count; ++table) {
        context.list()->SetGraphicsRootDescriptorTable(
            table, context.GpuDescriptorHandle(heap.get(), table));
      }
      context.list()->DrawInstanced(3, 1, 0, 0);
    }
    if (copy_source) {
      D3D12TestContext::Transition(
          context.list(), resources.render_target.get(),
          D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    }
  };

  record_draws(1, false);
  if (auto error =
          HResultError("root-table warm-up close", context.list()->Close()))
    return error;
  ID3D12CommandList *warmup_lists[] = {context.list()};
  context.queue()->ExecuteCommandLists(1, warmup_lists);
  if (auto error = HResultError("root-table warm-up completion",
                                context.SignalAndWait()))
    return error;
  if (auto error = HResultError("root-table measured-list reset",
                                context.ResetCommandList()))
    return error;

  record_draws(draw_count, true);
  const auto begin = std::chrono::steady_clock::now();
  const HRESULT close_hr = context.list()->Close();
  const auto end = std::chrono::steady_clock::now();
  if (auto error = HResultError("root-table measured close", close_hr))
    return error;
  if (measurement) {
    measurement->measured_ms =
        std::chrono::duration<double, std::milli>(end - begin).count();
    measurement->operations = std::uint64_t(draw_count) * root_table_count;
  }

  ID3D12CommandList *lists[] = {context.list()};
  context.queue()->ExecuteCommandLists(1, lists);
  if (auto error = HResultError("root-table measured completion",
                                context.SignalAndWait()))
    return error;
  if (auto error = HResultError("root-table readback-list reset",
                                context.ResetCommandList()))
    return error;
  return ExecuteAndValidate(context, resources, kExpectedPixel, measurement);
}

BindingHotspotError
RunDescriptorMirrorMutationScenario(std::uint32_t descriptor_count,
                                    std::uint32_t copy_rounds,
                                    BindingHotspotMeasurement *measurement) {
  if (auto error = ValidateDimensions("descriptor mirror mutation",
                                      descriptor_count, copy_rounds))
    return error;

  D3D12TestContext context;
  if (auto error =
          HResultError("D3D12 context initialization", context.Initialize()))
    return error;

  D3D12_DESCRIPTOR_RANGE range = {};
  range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  range.NumDescriptors = descriptor_count;
  range.BaseShaderRegister = 0;
  range.OffsetInDescriptorsFromTableStart = 0;
  std::array<D3D12_ROOT_PARAMETER, 1> parameters = {};
  parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[0].DescriptorTable.NumDescriptorRanges = 1;
  parameters[0].DescriptorTable.pDescriptorRanges = &range;
  parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

  const std::vector<D3D12_ROOT_PARAMETER> parameter_vector(parameters.begin(),
                                                           parameters.end());
  GraphicsScenarioResources resources;
  if (auto error = CreateGraphicsScenarioResources(
          context, parameter_vector, descriptor_count, &resources))
    return error;

  constexpr InputValue kValueA = {1.0f, 0.0f, 0.0f, 1.0f};
  constexpr InputValue kValueB = {0.0f, 1.0f, 0.0f, 1.0f};
  constexpr std::uint32_t kPixelA = 0xff0000ffu;
  constexpr std::uint32_t kPixelB = 0xff00ff00u;
  auto input_a = CreateInputBuffer(context, kValueA);
  auto input_b = CreateInputBuffer(context, kValueB);
  auto source_a = context.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, descriptor_count, false);
  auto source_b = context.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, descriptor_count, false);
  auto destination = context.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, descriptor_count, true);
  if (!input_a || !input_b || !source_a || !source_b || !destination)
    return "failed to create descriptor mirror mutation resources";

  for (std::uint32_t descriptor = 0; descriptor < descriptor_count;
       ++descriptor) {
    WriteStructuredBufferSrv(
        context, input_a.get(),
        context.CpuDescriptorHandle(source_a.get(), descriptor));
    WriteStructuredBufferSrv(
        context, input_b.get(),
        context.CpuDescriptorHandle(source_b.get(), descriptor));
  }
  const auto destination_start =
      destination->GetCPUDescriptorHandleForHeapStart();
  context.device()->CopyDescriptorsSimple(
      descriptor_count, destination_start,
      source_a->GetCPUDescriptorHandleForHeapStart(),
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

  const auto begin = std::chrono::steady_clock::now();
  for (std::uint32_t round = 0; round < copy_rounds; ++round) {
    auto *source = (round & 1u) ? source_b.get() : source_a.get();
    context.device()->CopyDescriptorsSimple(
        descriptor_count, destination_start,
        source->GetCPUDescriptorHandleForHeapStart(),
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  }
  const auto end = std::chrono::steady_clock::now();
  if (measurement) {
    measurement->measured_ms =
        std::chrono::duration<double, std::milli>(end - begin).count();
    measurement->operations = std::uint64_t(descriptor_count) * copy_rounds;
  }

  RecordSingleTableDraw(context, resources, destination.get());
  const std::uint32_t expected = ((copy_rounds - 1) & 1u) ? kPixelB : kPixelA;
  return ExecuteAndValidate(context, resources, expected, measurement);
}

BindingHotspotError
RunTypedBufferDescriptorScenario(BindingHotspotMeasurement *measurement) {
  D3D12TestContext context;
  if (auto error =
          HResultError("D3D12 context initialization", context.Initialize()))
    return error;

  D3D12_DESCRIPTOR_RANGE range = {};
  range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  range.NumDescriptors = 1;
  std::array<D3D12_ROOT_PARAMETER, 1> parameters = {};
  parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[0].DescriptorTable.NumDescriptorRanges = 1;
  parameters[0].DescriptorTable.pDescriptorRanges = &range;
  parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

  GraphicsScenarioResources resources;
  if (auto error = CreateGraphicsScenarioResources(
          context,
          std::vector<D3D12_ROOT_PARAMETER>(parameters.begin(),
                                            parameters.end()),
          1, &resources, true))
    return error;

  constexpr InputValue kValue = {1.0f, 0.0f, 0.0f, 1.0f};
  constexpr std::uint32_t kExpectedPixel = 0xff0000ffu;
  auto input = CreateInputBuffer(context, kValue);
  auto heap = context.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true);
  if (!input || !heap)
    return "failed to create typed-buffer descriptor resources";
  WriteTypedBufferSrv(context, input.get(),
                      heap->GetCPUDescriptorHandleForHeapStart());

  RecordSingleTableDraw(context, resources, heap.get());
  return ExecuteAndValidate(context, resources, kExpectedPixel, measurement);
}

BindingHotspotError
RunSharedDescriptorResidencyScenario(BindingHotspotMeasurement *measurement) {
  D3D12TestContext context;
  if (auto error =
          HResultError("D3D12 context initialization", context.Initialize()))
    return error;

  D3D12_DESCRIPTOR_RANGE range = {};
  range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  range.NumDescriptors = 2;
  std::array<D3D12_ROOT_PARAMETER, 1> parameters = {};
  parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[0].DescriptorTable.NumDescriptorRanges = 1;
  parameters[0].DescriptorTable.pDescriptorRanges = &range;
  parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

  GraphicsScenarioResources resources;
  if (auto error = CreateGraphicsScenarioResources(
          context,
          std::vector<D3D12_ROOT_PARAMETER>(parameters.begin(),
                                            parameters.end()),
          2, &resources))
    return error;

  constexpr InputValue kValueA = {1.0f, 0.0f, 0.0f, 1.0f};
  constexpr InputValue kValueB = {0.0f, 1.0f, 0.0f, 1.0f};
  constexpr std::uint32_t kExpectedPixel = 0xff007f7fu;
  auto input_a = CreateInputBuffer(context, kValueA);
  auto input_b = CreateInputBuffer(context, kValueB);
  auto heap = context.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, true);
  if (!input_a || !input_b || !heap)
    return "failed to create shared-residency descriptor resources";

  WriteStructuredBufferSrv(context, input_a.get(),
                           context.CpuDescriptorHandle(heap.get(), 0));
  WriteStructuredBufferSrv(context, input_a.get(),
                           context.CpuDescriptorHandle(heap.get(), 1));
  WriteStructuredBufferSrv(context, input_b.get(),
                           context.CpuDescriptorHandle(heap.get(), 0));

  RecordSingleTableDraw(context, resources, heap.get());
  return ExecuteAndValidate(context, resources, kExpectedPixel, measurement);
}

BindingHotspotError
RunRootTableInvalidationScenario(BindingHotspotMeasurement *measurement) {
  D3D12TestContext context;
  if (auto error =
          HResultError("D3D12 context initialization", context.Initialize()))
    return error;

  D3D12_DESCRIPTOR_RANGE range = {};
  range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  range.NumDescriptors = 1;
  std::array<D3D12_ROOT_PARAMETER, 1> parameters = {};
  parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[0].DescriptorTable.NumDescriptorRanges = 1;
  parameters[0].DescriptorTable.pDescriptorRanges = &range;
  parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

  GraphicsScenarioResources resources;
  if (auto error = CreateGraphicsScenarioResources(
          context,
          std::vector<D3D12_ROOT_PARAMETER>(parameters.begin(),
                                            parameters.end()),
          1, &resources))
    return error;

  constexpr InputValue kValueA = {1.0f, 0.0f, 0.0f, 1.0f};
  constexpr InputValue kValueB = {0.0f, 1.0f, 0.0f, 1.0f};
  constexpr std::uint32_t kExpectedPixel = 0xff00ff00u;
  auto input_a = CreateInputBuffer(context, kValueA);
  auto input_b = CreateInputBuffer(context, kValueB);
  auto heap = context.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, true);
  if (!input_a || !input_b || !heap)
    return "failed to create root-table invalidation resources";
  WriteStructuredBufferSrv(context, input_a.get(),
                           context.CpuDescriptorHandle(heap.get(), 0));
  WriteStructuredBufferSrv(context, input_b.get(),
                           context.CpuDescriptorHandle(heap.get(), 1));

  RecordGraphicsState(context, resources, heap.get());
  context.list()->SetGraphicsRootDescriptorTable(
      0, context.GpuDescriptorHandle(heap.get(), 0));
  context.list()->DrawInstanced(3, 1, 0, 0);
  context.list()->SetGraphicsRootDescriptorTable(
      0, context.GpuDescriptorHandle(heap.get(), 1));
  context.list()->DrawInstanced(3, 1, 0, 0);
  D3D12TestContext::Transition(context.list(), resources.render_target.get(),
                               D3D12_RESOURCE_STATE_RENDER_TARGET,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
  return ExecuteAndValidate(context, resources, kExpectedPixel, measurement);
}

BindingHotspotError
RunCompiledRootDescriptorResidencyScenario(
    std::uint32_t draw_count, BindingHotspotMeasurement *measurement) {
  if (!draw_count)
    return "compiled root-descriptor residency requires at least one draw";

  D3D12TestContext context;
  if (auto error =
          HResultError("D3D12 context initialization", context.Initialize()))
    return error;

  std::array<D3D12_ROOT_PARAMETER, 1> parameters = {};
  parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
  parameters[0].Descriptor.ShaderRegister = 0;
  parameters[0].Descriptor.RegisterSpace = 0;
  parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

  GraphicsScenarioResources resources;
  if (auto error = CreateGraphicsScenarioResources(
          context,
          std::vector<D3D12_ROOT_PARAMETER>(parameters.begin(),
                                            parameters.end()),
          1, &resources))
    return error;

  constexpr InputValue kValueA = {1.0f, 0.0f, 0.0f, 1.0f};
  constexpr InputValue kValueB = {0.0f, 1.0f, 0.0f, 1.0f};
  constexpr std::uint32_t kPixelA = 0xff0000ffu;
  constexpr std::uint32_t kPixelB = 0xff00ff00u;
  auto input_a = CreateInputBuffer(context, kValueA);
  auto input_b = CreateInputBuffer(context, kValueB);
  if (!input_a || !input_b)
    return "failed to create compiled root-descriptor resources";

  ComPtr<ID3D12CommandAllocator> submission_allocator;
  if (auto error = HResultError(
          "compiled root-descriptor allocator creation",
          context.device()->CreateCommandAllocator(
              D3D12_COMMAND_LIST_TYPE_DIRECT,
              __uuidof(ID3D12CommandAllocator),
              reinterpret_cast<void **>(submission_allocator.put()))))
    return error;
  ComPtr<ID3D12GraphicsCommandList> submission_list;
  if (auto error = HResultError(
          "compiled root-descriptor command-list creation",
          context.device()->CreateCommandList(
              0, D3D12_COMMAND_LIST_TYPE_DIRECT, submission_allocator.get(),
              nullptr, __uuidof(ID3D12GraphicsCommandList),
              reinterpret_cast<void **>(submission_list.put()))))
    return error;

  RecordGraphicsState(context, resources, nullptr, submission_list.get());
  const auto begin = std::chrono::steady_clock::now();
  for (std::uint32_t draw = 0; draw < draw_count; ++draw) {
    auto *input = (draw & 1u) ? input_b.get() : input_a.get();
    submission_list->SetGraphicsRootShaderResourceView(
        0, input->GetGPUVirtualAddress());
    submission_list->DrawInstanced(3, 1, 0, 0);
  }
  D3D12TestContext::Transition(submission_list.get(),
                               resources.render_target.get(),
                               D3D12_RESOURCE_STATE_RENDER_TARGET,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
  if (auto error = HResultError("compiled root-descriptor close",
                                submission_list->Close()))
    return error;
  ID3D12CommandList *submission_lists[] = {submission_list.get()};
  context.queue()->ExecuteCommandLists(1, submission_lists);

  // ExecuteCommandLists is asynchronous. Destroy every recording-side owner
  // before the submission and Metal encode workers finish; the queued packet
  // must retain the PSO/root signature and the direct buffer allocations.
  submission_list.reset();
  submission_allocator.reset();
  resources.pipeline.reset();
  resources.root_signature.reset();
  if (auto error = HResultError("compiled root-descriptor completion",
                                context.SignalAndWait()))
    return error;
  const auto end = std::chrono::steady_clock::now();
  if (measurement) {
    measurement->measured_ms =
        std::chrono::duration<double, std::milli>(end - begin).count();
    measurement->operations = draw_count;
  }

  const auto expected = (draw_count & 1u) ? kPixelA : kPixelB;
  return ExecuteAndValidate(context, resources, expected, measurement);
}

BindingHotspotError RunCompiledDescriptorSubmissionSnapshotScenario(
    BindingHotspotMeasurement *measurement) {
  D3D12TestContext context;
  if (auto error =
          HResultError("D3D12 context initialization", context.Initialize()))
    return error;

  D3D12_DESCRIPTOR_RANGE srv_range = {};
  srv_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  srv_range.NumDescriptors = 1;
  srv_range.RegisterSpace = 1;
  D3D12_DESCRIPTOR_RANGE sampler_range = {};
  sampler_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
  sampler_range.NumDescriptors = 1;
  sampler_range.RegisterSpace = 2;
  std::array<D3D12_ROOT_PARAMETER, 3> parameters = {};
  parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
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
  root_desc.NumParameters = parameters.size();
  root_desc.pParameters = parameters.data();
  auto root_signature = context.CreateRootSignature(root_desc);
  if (!root_signature)
    return "failed to create compiled descriptor snapshot root signature";

  D3D12_INPUT_ELEMENT_DESC elements[2] = {};
  elements[0] = {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0,
                 D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0};
  elements[1] = {"TEXCOORD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16,
                 D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0};
  D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {};
  pso_desc.pRootSignature = root_signature.get();
  pso_desc.VS = {kBindlessDxilPresentVs, kBindlessDxilPresentVs_len};
  pso_desc.PS = {kBindlessDxilPresentPs, kBindlessDxilPresentPs_len};
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
  if (auto error = HResultError(
          "compiled descriptor snapshot pipeline creation",
          context.device()->CreateGraphicsPipelineState(
              &pso_desc, __uuidof(ID3D12PipelineState),
              reinterpret_cast<void **>(pipeline.put()))))
    return error;

  auto target = context.CreateTexture2D(
      4, 4, 1, DXGI_FORMAT_R32G32B32A32_FLOAT,
      D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
      D3D12_RESOURCE_STATE_RENDER_TARGET);
  auto source = context.CreateTexture2D(
      1, 1, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  auto rtv_heap = context.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
  auto resource_heap = context.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true);
  auto sampler_heap = context.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 1, true);
  constexpr InputValue kReplacement = {1.0f, 0.0f, 0.0f, 1.0f};
  auto replacement_buffer = CreateInputBuffer(context, kReplacement);
  if (!target || !source || !rtv_heap || !resource_heap || !sampler_heap ||
      !replacement_buffer)
    return "failed to create compiled descriptor snapshot resources";

  const InputValue source_pixel = {0.25f, 0.5f, 0.75f, 1.0f};
  if (auto error = HResultError(
          "compiled descriptor snapshot texture upload",
          context.UploadTextureAndReset(source.get(), source_pixel.data(),
                                        sizeof(source_pixel),
                                        sizeof(source_pixel))))
    return error;
  const auto rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
  context.device()->CreateRenderTargetView(target.get(), nullptr, rtv);
  const auto resource_slot =
      resource_heap->GetCPUDescriptorHandleForHeapStart();
  context.device()->CreateShaderResourceView(source.get(), nullptr,
                                             resource_slot);
  D3D12_SAMPLER_DESC sampler = {};
  sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
  sampler.AddressU = sampler.AddressV = sampler.AddressW =
      D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  sampler.MaxLOD = D3D12_FLOAT32_MAX;
  context.device()->CreateSampler(
      &sampler, sampler_heap->GetCPUDescriptorHandleForHeapStart());

  struct FrameData {
    float projection[16];
    float max_edr;
    float brightness;
    float current_edr_bias;
    float padding;
  } frame = {};
  frame.projection[0] = frame.projection[5] = frame.projection[10] =
      frame.projection[15] = 1.0f;
  frame.max_edr = frame.brightness = 1.0f;
  auto frame_buffer = context.CreateUploadBuffer(
      D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, &frame, sizeof(frame));
  struct Vertex {
    float position[4];
    float texcoord[4];
  };
  const Vertex vertices[3] = {
      {{-1.0f, -1.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f, 0.0f}},
      {{-1.0f, 3.0f, 0.0f, 1.0f}, {0.0f, -1.0f, 0.0f, 0.0f}},
      {{3.0f, -1.0f, 0.0f, 1.0f}, {2.0f, 1.0f, 0.0f, 0.0f}},
  };
  auto vertex_buffer = context.CreateUploadBuffer(
      sizeof(vertices), vertices, sizeof(vertices));
  if (!frame_buffer || !vertex_buffer)
    return "failed to create compiled descriptor snapshot draw inputs";

  ComPtr<ID3D12CommandAllocator> submission_allocator;
  if (auto error = HResultError(
          "compiled descriptor snapshot allocator creation",
          context.device()->CreateCommandAllocator(
              D3D12_COMMAND_LIST_TYPE_DIRECT,
              __uuidof(ID3D12CommandAllocator),
              reinterpret_cast<void **>(submission_allocator.put()))))
    return error;
  ComPtr<ID3D12GraphicsCommandList> submission_list;
  if (auto error = HResultError(
          "compiled descriptor snapshot command-list creation",
          context.device()->CreateCommandList(
              0, D3D12_COMMAND_LIST_TYPE_DIRECT, submission_allocator.get(),
              nullptr, __uuidof(ID3D12GraphicsCommandList),
              reinterpret_cast<void **>(submission_list.put()))))
    return error;

  D3D12TestContext::Transition(submission_list.get(), source.get(),
                               D3D12_RESOURCE_STATE_COPY_DEST,
                               D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  ID3D12DescriptorHeap *heaps[] = {resource_heap.get(), sampler_heap.get()};
  submission_list->SetDescriptorHeaps(std::size(heaps), heaps);
  submission_list->SetGraphicsRootSignature(root_signature.get());
  submission_list->SetPipelineState(pipeline.get());
  submission_list->SetGraphicsRootConstantBufferView(
      0, frame_buffer->GetGPUVirtualAddress());
  submission_list->SetGraphicsRootDescriptorTable(
      1, resource_heap->GetGPUDescriptorHandleForHeapStart());
  submission_list->SetGraphicsRootDescriptorTable(
      2, sampler_heap->GetGPUDescriptorHandleForHeapStart());
  D3D12_VERTEX_BUFFER_VIEW vertex_view = {};
  vertex_view.BufferLocation = vertex_buffer->GetGPUVirtualAddress();
  vertex_view.SizeInBytes = sizeof(vertices);
  vertex_view.StrideInBytes = sizeof(Vertex);
  submission_list->IASetVertexBuffers(0, 1, &vertex_view);
  submission_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  const D3D12_VIEWPORT viewport = {0.0f, 0.0f, 4.0f, 4.0f, 0.0f, 1.0f};
  const D3D12_RECT scissor = {0, 0, 4, 4};
  submission_list->RSSetViewports(1, &viewport);
  submission_list->RSSetScissorRects(1, &scissor);
  submission_list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
  submission_list->DrawInstanced(3, 1, 0, 0);
  D3D12TestContext::Transition(submission_list.get(),
                               target.get(),
                               D3D12_RESOURCE_STATE_RENDER_TARGET,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
  if (auto error = HResultError("compiled descriptor snapshot close",
                                submission_list->Close()))
    return error;

  ComPtr<ID3D12Fence> gate;
  if (auto error = HResultError(
          "compiled descriptor snapshot gate creation",
          context.device()->CreateFence(
              0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence),
              reinterpret_cast<void **>(gate.put()))))
    return error;
  if (auto error = HResultError("compiled descriptor snapshot queue wait",
                                context.queue()->Wait(gate.get(), 1)))
    return error;

  ID3D12CommandList *submission_lists[] = {submission_list.get()};
  context.queue()->ExecuteCommandLists(1, submission_lists);

  // Keep replay deterministically behind the queue wait while replacing the
  // live heap slot with a different resource class. The submitted bindless
  // packet must retain the Texture2D descriptor generation observed at
  // Execute time; deferred Metal encoding must not reread this typed buffer.
  WriteTypedBufferSrv(context, replacement_buffer.get(), resource_slot);
  if (auto error = HResultError("compiled descriptor snapshot gate signal",
                                gate->Signal(1)))
    return error;
  if (auto error = HResultError("compiled descriptor snapshot completion",
                                context.SignalAndWait()))
    return error;

  TextureReadback readback;
  if (auto error = HResultError(
          "compiled descriptor snapshot readback",
          context.ReadbackTexture(target.get(), &readback)))
    return error;
  InputValue center = {};
  std::memcpy(center.data(),
              readback.data.data() + 2 * readback.row_pitch +
                  2 * sizeof(center),
              sizeof(center));
  // The present shader applies its display transfer function to RGB.
  constexpr InputValue kExpectedOutput = {0.19988f, 0.37252f, 0.50728f,
                                          1.0f};
  const bool matched =
      std::abs(center[0] - kExpectedOutput[0]) < 0.01f &&
      std::abs(center[1] - kExpectedOutput[1]) < 0.01f &&
      std::abs(center[2] - kExpectedOutput[2]) < 0.01f &&
      std::abs(center[3] - kExpectedOutput[3]) < 0.01f;
  if (measurement) {
    measurement->operations = 1;
    measurement->expected = 1;
    measurement->actual = matched ? 1 : 0;
  }
  if (matched)
    return std::nullopt;
  std::ostringstream message;
  message << "compiled descriptor snapshot readback was " << center[0] << ","
          << center[1] << "," << center[2] << "," << center[3]
          << ", expected 0.19988,0.37252,0.50728,1";
  return message.str();
}

} // namespace dxmt::test
