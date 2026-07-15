#include "d3d12_binding_hotspot_scenarios.hpp"

#include "d3d12_test_context.hpp"

#include <d3dcompiler.h>

#include <array>
#include <chrono>
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

} // namespace dxmt::test
