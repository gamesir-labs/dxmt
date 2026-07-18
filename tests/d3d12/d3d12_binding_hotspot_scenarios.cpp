#include "d3d12_binding_hotspot_scenarios.hpp"

#include <dxmt_d3d12_test_path.hpp>

#include "d3d12_test_context.hpp"
#include "shaders/bindless_dxil_shaders.hpp"

#include <d3dcompiler.h>

#include <algorithm>
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

BindingHotspotError CompileBindingHotspotShader(const char *source,
                                                const char *source_name,
                                                const char *target,
                                                ComPtr<ID3DBlob> *shader) {
  ComPtr<ID3DBlob> errors;
  const HRESULT hr = D3DCompile(
      source, std::strlen(source), source_name, nullptr, nullptr, "main", target,
      D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, shader->put(), errors.put());
  if (SUCCEEDED(hr))
    return std::nullopt;

  std::string message = "failed to compile ";
  message += source_name;
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
  std::array<D3D12_ROOT_PARAMETER, 2> parameters = {};
  parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[0].DescriptorTable.NumDescriptorRanges = 1;
  parameters[0].DescriptorTable.pDescriptorRanges = &range;
  parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  // Any root payload selects the bindless-mirror ABI. The constant itself is
  // intentionally unused; it makes this scenario exercise deferred typed
  // buffer mirror repair instead of the direct native descriptor-table ABI.
  parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  parameters[1].Constants.ShaderRegister = 0;
  parameters[1].Constants.Num32BitValues = 1;
  parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

  GraphicsScenarioResources resources;
  if (auto error = CreateGraphicsScenarioResources(
          context,
          std::vector<D3D12_ROOT_PARAMETER>(parameters.begin(),
                                            parameters.end()),
          2, &resources, true))
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

  WriteTypedBufferSrv(context, input_a.get(),
                      context.CpuDescriptorHandle(heap.get(), 0));
  WriteTypedBufferSrv(context, input_a.get(),
                      context.CpuDescriptorHandle(heap.get(), 1));
  WriteTypedBufferSrv(context, input_b.get(),
                      context.CpuDescriptorHandle(heap.get(), 0));

  dxmt::d3d12::test::DescriptorHeapSlotRepairConfig repair = {};
  repair.slot = 1;
  if (auto error = HResultError(
          "mark shared descriptor slot for encode-time repair",
          heap->SetPrivateData(
              dxmt::d3d12::test::kDescriptorHeapSlotRepairGuid,
              sizeof(repair), &repair)))
    return error;

  dxmt::d3d12::test::PersistentResidencyStats before = {};
  UINT before_size = sizeof(before);
  if (auto error = HResultError(
          "read persistent residency stats before draw",
          context.device()->GetPrivateData(
              dxmt::d3d12::test::kPersistentResidencyStatsGuid,
              &before_size, &before)))
    return error;

  RecordSingleTableDraw(context, resources, heap.get());
  if (auto error = ExecuteAndValidate(context, resources, kExpectedPixel,
                                      measurement))
    return error;

  dxmt::d3d12::test::PersistentResidencyStats after = {};
  UINT after_size = sizeof(after);
  if (auto error = HResultError(
          "read persistent residency stats after draw",
          context.device()->GetPrivateData(
              dxmt::d3d12::test::kPersistentResidencyStatsGuid,
              &after_size, &after)))
    return error;
  if (after.entry_count == before.entry_count + 1 &&
      after.total_ref_count == before.total_ref_count + 1)
    return std::nullopt;

  std::ostringstream message;
  message << "encode-time descriptor repair changed persistent residency "
             "entries/ref-count from "
          << before.entry_count << "/" << before.total_ref_count << " to "
          << after.entry_count << "/" << after.total_ref_count
          << ", expected exactly one added allocation";
  return message.str();
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

BindingHotspotError RunFh4MultiTableSubmissionSnapshotScenario(
    BindingHotspotMeasurement *measurement) {
  D3D12TestContext context;
  if (auto error =
          HResultError("D3D12 context initialization", context.Initialize()))
    return error;

  // Reproduce the version-1.1 root signature serialized in the FH4 UI trace:
  // root CBVs b0(VS), b0/b1/b2/b3(PS), sampler tables s0/s1, SRV tables
  // t0/t1/t2..t4(PS), t0..t1(VS), t0(GS), and a final u1..u2(PS) table.
  // In particular, b0/b3 are root descriptors rather than descriptor-table
  // entries; this distinction exercises the compiled bindless CBV fallback.
  std::array<D3D12_DESCRIPTOR_RANGE1, 8> ranges = {};
  ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
  ranges[0].NumDescriptors = 1;
  ranges[0].BaseShaderRegister = 0;
  ranges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
  ranges[1] = ranges[0];
  ranges[1].BaseShaderRegister = 1;
  ranges[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  ranges[2].NumDescriptors = 1;
  ranges[2].BaseShaderRegister = 0;
  ranges[2].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
  ranges[3] = ranges[2];
  ranges[3].BaseShaderRegister = 1;
  ranges[4] = ranges[2];
  ranges[4].NumDescriptors = 3;
  ranges[4].BaseShaderRegister = 2;
  ranges[5] = ranges[2];
  ranges[5].NumDescriptors = 2;
  ranges[6] = ranges[2];
  ranges[7].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  ranges[7].NumDescriptors = 2;
  ranges[7].BaseShaderRegister = 1;
  ranges[7].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

  std::array<D3D12_ROOT_PARAMETER1, 13> parameters = {};
  constexpr std::array<UINT, 5> kRootCbvRegisters = {0, 0, 1, 2, 3};
  constexpr std::array<D3D12_SHADER_VISIBILITY, 5> kRootCbvVisibility = {
      D3D12_SHADER_VISIBILITY_VERTEX, D3D12_SHADER_VISIBILITY_PIXEL,
      D3D12_SHADER_VISIBILITY_PIXEL, D3D12_SHADER_VISIBILITY_PIXEL,
      D3D12_SHADER_VISIBILITY_PIXEL};
  for (UINT index = 0; index < kRootCbvRegisters.size(); ++index) {
    parameters[index].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameters[index].Descriptor.ShaderRegister = kRootCbvRegisters[index];
    parameters[index].Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_NONE;
    parameters[index].ShaderVisibility = kRootCbvVisibility[index];
  }
  const auto set_table = [&](UINT root, const D3D12_DESCRIPTOR_RANGE1 *table,
                             D3D12_SHADER_VISIBILITY visibility) {
    parameters[root].ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[root].DescriptorTable.NumDescriptorRanges = 1;
    parameters[root].DescriptorTable.pDescriptorRanges = table;
    parameters[root].ShaderVisibility = visibility;
  };
  set_table(5, &ranges[0], D3D12_SHADER_VISIBILITY_PIXEL);
  set_table(6, &ranges[1], D3D12_SHADER_VISIBILITY_PIXEL);
  set_table(7, &ranges[2], D3D12_SHADER_VISIBILITY_PIXEL);
  set_table(8, &ranges[3], D3D12_SHADER_VISIBILITY_PIXEL);
  set_table(9, &ranges[4], D3D12_SHADER_VISIBILITY_PIXEL);
  set_table(10, &ranges[5], D3D12_SHADER_VISIBILITY_VERTEX);
  set_table(11, &ranges[6], D3D12_SHADER_VISIBILITY_GEOMETRY);
  set_table(12, &ranges[7], D3D12_SHADER_VISIBILITY_PIXEL);

  D3D12_VERSIONED_ROOT_SIGNATURE_DESC root_desc = {};
  root_desc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
  root_desc.Desc_1_1.NumParameters = parameters.size();
  root_desc.Desc_1_1.pParameters = parameters.data();
  root_desc.Desc_1_1.Flags =
      D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
  ComPtr<ID3DBlob> root_blob;
  ComPtr<ID3DBlob> root_error;
  if (auto error = HResultError(
          "FH4-shaped root signature serialization",
          D3D12SerializeVersionedRootSignature(
              &root_desc, root_blob.put(), root_error.put())))
    return error;
  ComPtr<ID3D12RootSignature> root_signature;
  if (auto error = HResultError(
          "FH4-shaped root signature creation",
          context.device()->CreateRootSignature(
              0, root_blob->GetBufferPointer(), root_blob->GetBufferSize(),
              __uuidof(ID3D12RootSignature),
              reinterpret_cast<void **>(root_signature.put()))))
    return error;
  if (!root_signature)
    return "failed to create FH4-shaped multi-table root signature";

  constexpr const char *kVertexSource = R"(
    cbuffer Root0 : register(b0) { float4 root0; };
    struct VertexInput {
      float4 position : POSITION;
      float4 color : NORMAL;
      float2 texcoord : TEXCOORD0;
    };
    struct VertexOutput {
      float4 position : SV_Position;
      float2 texcoord : TEXCOORD0;
      float4 color : COLOR0;
    };
    VertexOutput main(VertexInput input) {
      VertexOutput output;
      output.position = input.position;
      output.position.xy += root0.xy * 0.001;
      output.texcoord = input.texcoord;
      output.color = input.color;
      return output;
    })";
  constexpr const char *kPixelSource = R"(
    Texture2D<float4> texture0 : register(t0);
    Texture2D<float4> texture1 : register(t1);
    SamplerState sampler0 : register(s0);
    SamplerState sampler1 : register(s1);
    cbuffer Weight0 : register(b0) {
      float4 weight0_unused;
      float4 weight0;
    };
    cbuffer Weight1 : register(b3) {
      float4 weight1_unused;
      float4 weight1;
      float4 overlay_color;
      float4 texture_scale;
    };
    float4 main(float4 position : SV_Position,
                float2 texcoord : TEXCOORD0,
                float4 color : COLOR0) : SV_Target {
      float4 first = texture0.SampleLevel(
          sampler0, float2(-0.25, 0.5) + texcoord * 0.001, 0.0);
      float4 second = texture1.SampleLevel(
          sampler1, float2(1.25, 0.5) + texcoord * 0.001, 0.0);
      return (first * weight0 + second * weight1) * texture_scale * color;
    })";
  constexpr const char *kOverlayPixelSource = R"(
    cbuffer Overlay : register(b3) {
      float4 overlay_unused0;
      float4 overlay_unused1;
      float4 overlay_color;
    };
    float4 main(float4 position : SV_Position,
                float2 texcoord : TEXCOORD0,
                float4 color : COLOR0) : SV_Target {
      return overlay_color * color;
    })";
  ComPtr<ID3DBlob> vertex_shader;
  ComPtr<ID3DBlob> pixel_shader;
  ComPtr<ID3DBlob> overlay_pixel_shader;
  if (auto error = CompileBindingHotspotShader(
          kVertexSource, "fh4_multi_table_vertex.hlsl", "vs_5_0",
          &vertex_shader))
    return error;
  if (auto error = CompileBindingHotspotShader(
          kPixelSource, "fh4_multi_table_pixel.hlsl", "ps_5_0",
          &pixel_shader))
    return error;
  if (auto error = CompileBindingHotspotShader(
          kOverlayPixelSource, "fh4_overlay_pixel.hlsl", "ps_5_0",
          &overlay_pixel_shader))
    return error;

  D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {};
  pso_desc.pRootSignature = root_signature.get();
  pso_desc.VS = {vertex_shader->GetBufferPointer(),
                 vertex_shader->GetBufferSize()};
  pso_desc.PS = {pixel_shader->GetBufferPointer(),
                 pixel_shader->GetBufferSize()};
  auto &blend = pso_desc.BlendState.RenderTarget[0];
  blend.BlendEnable = TRUE;
  blend.SrcBlend = D3D12_BLEND_ONE;
  blend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
  blend.BlendOp = D3D12_BLEND_OP_ADD;
  blend.SrcBlendAlpha = D3D12_BLEND_ONE;
  blend.DestBlendAlpha = D3D12_BLEND_ZERO;
  blend.BlendOpAlpha = D3D12_BLEND_OP_MAX;
  blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
  pso_desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
  pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  pso_desc.SampleMask = UINT_MAX;
  pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  pso_desc.NumRenderTargets = 1;
  pso_desc.RTVFormats[0] = DXGI_FORMAT_R10G10B10A2_UNORM;
  pso_desc.SampleDesc.Count = 1;
  const D3D12_INPUT_ELEMENT_DESC input_elements[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0,
       D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
      {"NORMAL", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16,
       D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
      {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 32,
       D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
  };
  pso_desc.InputLayout = {input_elements, std::size(input_elements)};
  ComPtr<ID3D12PipelineState> pipeline;
  if (auto error = HResultError(
          "FH4-shaped multi-table pipeline creation",
          context.device()->CreateGraphicsPipelineState(
              &pso_desc, __uuidof(ID3D12PipelineState),
              reinterpret_cast<void **>(pipeline.put()))))
    return error;
  D3D12_GRAPHICS_PIPELINE_STATE_DESC overlay_pso_desc = pso_desc;
  overlay_pso_desc.PS = {overlay_pixel_shader->GetBufferPointer(),
                         overlay_pixel_shader->GetBufferSize()};
  ComPtr<ID3D12PipelineState> overlay_pipeline;
  if (auto error = HResultError(
          "FH4-shaped overlay pipeline creation",
          context.device()->CreateGraphicsPipelineState(
              &overlay_pso_desc, __uuidof(ID3D12PipelineState),
              reinterpret_cast<void **>(overlay_pipeline.put()))))
    return error;

  constexpr UINT kDrawCount = 8;
  auto target = context.CreateTexture2D(
      kDrawCount * 2, 2, 1, DXGI_FORMAT_R10G10B10A2_UNORM,
      D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
      D3D12_RESOURCE_STATE_RENDER_TARGET);
  auto source0 = context.CreateTexture2D(
      2, 1, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  auto source1 = context.CreateTexture2D(
      2, 1, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  std::array<ComPtr<ID3D12Resource>, kDrawCount> replacement0;
  for (auto &texture : replacement0) {
    texture = context.CreateTexture2D(
        2, 1, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, D3D12_RESOURCE_FLAG_NONE,
        D3D12_RESOURCE_STATE_COPY_DEST);
  }
  auto replacement1 = context.CreateTexture2D(
      2, 1, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  auto rtv_heap = context.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
  constexpr UINT kResourceHeapSize = 1'000'000;
  constexpr UINT kSamplerHeapSize = 2'048;
  constexpr std::array<UINT, kDrawCount> kUavTableBases = {
      115'509, 115'558, 113'695, 113'744,
      113'793, 113'842, 113'700, 113'750};
  constexpr std::array<UINT, kDrawCount> kSrv0Slots = {
      115'511, 115'560, 113'698, 113'748,
      113'797, 113'838, 113'703, 113'753};
  constexpr UINT kSrv1Slot = 115'566;
  constexpr UINT kUnusedSrvTableBase = 115'600;
  constexpr UINT kSampler0Slot = 756;
  constexpr UINT kSampler1Slot = 755;
  constexpr UINT kCpuSrv0Slot = 0;
  constexpr UINT kCpuSrv1Slot = 1;
  constexpr UINT kCpuUav1Slot = 2;
  constexpr UINT kCpuUav2Slot = 3;
  constexpr UINT kCpuSampler0Slot = 0;
  constexpr UINT kCpuSampler1Slot = 1;
  auto resource_heap = context.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kResourceHeapSize, true);
  auto sampler_heap = context.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, kSamplerHeapSize, true);
  auto cpu_resource_heap = context.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 8, false);
  auto cpu_sampler_heap = context.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 2, false);
  auto unused_uav1 = context.CreateBuffer(
      256, D3D12_HEAP_TYPE_DEFAULT,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  auto unused_uav2 = context.CreateBuffer(
      256, D3D12_HEAP_TYPE_DEFAULT,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  const bool missing_replacement =
      std::any_of(replacement0.begin(), replacement0.end(),
                  [](const auto &texture) { return !texture; });
  if (!target || !source0 || !source1 || missing_replacement ||
      !replacement1 || !rtv_heap || !resource_heap || !sampler_heap ||
      !cpu_resource_heap || !cpu_sampler_heap || !unused_uav1 ||
      !unused_uav2)
    return "failed to create FH4-shaped multi-table resources";

  constexpr std::array<InputValue, 2> kSource0 = {
      InputValue{0.2f, 0.4f, 0.6f, 0.8f},
      InputValue{0.9f, 0.9f, 0.9f, 0.9f}};
  constexpr std::array<InputValue, 2> kSource1 = {
      InputValue{0.8f, 0.8f, 0.8f, 0.8f},
      InputValue{0.7f, 0.1f, 0.3f, 0.5f}};
  constexpr std::array<InputValue, kDrawCount> kReplacementColors = {
      InputValue{1.0f, 0.0f, 0.0f, 1.0f},
      InputValue{0.0f, 1.0f, 0.0f, 1.0f},
      InputValue{0.0f, 0.0f, 1.0f, 1.0f},
      InputValue{1.0f, 1.0f, 0.0f, 1.0f},
      InputValue{1.0f, 0.0f, 1.0f, 1.0f},
      InputValue{0.0f, 1.0f, 1.0f, 1.0f},
      InputValue{0.25f, 0.5f, 0.75f, 1.0f},
      InputValue{0.75f, 0.5f, 0.25f, 1.0f}};
  constexpr std::array<InputValue, 2> kReplacement1 = {
      InputValue{0.0f, 1.0f, 0.0f, 1.0f},
      InputValue{0.0f, 1.0f, 0.0f, 1.0f}};
  const auto upload = [&](const char *name, ID3D12Resource *texture,
                          const auto &pixels) -> BindingHotspotError {
    return HResultError(
        name, context.UploadTextureAndReset(
                  texture, pixels.data(), sizeof(InputValue) * pixels.size(),
                  sizeof(InputValue) * pixels.size()));
  };
  if (auto error = upload("FH4 source texture 0 upload", source0.get(),
                          kSource0))
    return error;
  if (auto error = upload("FH4 source texture 1 upload", source1.get(),
                          kSource1))
    return error;
  for (UINT draw = 0; draw < kDrawCount; ++draw) {
    const std::array pixels = {kReplacementColors[draw],
                               kReplacementColors[draw]};
    if (auto error = upload("FH4 replacement texture 0 upload",
                            replacement0[draw].get(), pixels))
      return error;
  }
  if (auto error = upload("FH4 replacement texture 1 upload",
                          replacement1.get(), kReplacement1))
    return error;

  constexpr UINT kRootCbvStride =
      D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
  constexpr UINT kRootCbvRingBytes = 64 * 1024;
  constexpr std::array<UINT, kDrawCount> kRootCbvBaseSlots = {
      244, 247, 250, 253, 0, 3, 6, 9};
  std::array<std::uint8_t, kRootCbvRingBytes> root_cbv_data = {};
  std::array<InputValue, kDrawCount> expected_colors = {};
  std::array<InputValue, kDrawCount> expected_overlays = {};
  for (UINT draw = 0; draw < kDrawCount; ++draw) {
    const float second_weight = float(draw + 1) / float(kDrawCount + 2);
    const InputValue first_weight = {
        1.0f - second_weight, 1.0f - second_weight,
        1.0f - second_weight, 1.0f - second_weight};
    const InputValue second_weight_value = {
        second_weight, second_weight, second_weight, second_weight};
    const InputValue texture_scale = {1.0f, 1.0f, 1.0f, 1.0f};
    expected_overlays[draw] = {
        float(draw + 1) / 16.0f, float(kDrawCount - draw) / 16.0f,
        float((draw % 3) + 1) / 8.0f, 1.0f};
    const UINT base = kRootCbvBaseSlots[draw] * kRootCbvStride;
    std::memcpy(root_cbv_data.data() + base + kRootCbvStride +
                    sizeof(InputValue),
                first_weight.data(), sizeof(first_weight));
    std::memcpy(root_cbv_data.data() + base + 2 * kRootCbvStride +
                    sizeof(InputValue),
                second_weight_value.data(), sizeof(second_weight_value));
    std::memcpy(root_cbv_data.data() + base + 2 * kRootCbvStride +
                    2 * sizeof(InputValue),
                expected_overlays[draw].data(), sizeof(InputValue));
    std::memcpy(root_cbv_data.data() + base + 2 * kRootCbvStride +
                    3 * sizeof(InputValue),
                texture_scale.data(), sizeof(InputValue));
    for (UINT component = 0; component < expected_colors[draw].size();
         ++component) {
      expected_colors[draw][component] =
          kReplacementColors[draw][component] * first_weight[component] +
          kReplacement1[0][component] * second_weight_value[component];
    }
  }
  auto root_cbv_buffer = context.CreateUploadBuffer(
      sizeof(root_cbv_data), root_cbv_data.data(), sizeof(root_cbv_data));
  if (!root_cbv_buffer)
    return "failed to create FH4-shaped root descriptor ring";
  struct Vertex {
    float position[4];
    float color[4];
    float texcoord[2];
  };
  constexpr std::array<Vertex, 4> kVertices = {
      Vertex{{-1.0f, -1.0f, 0.0f, 1.0f},
             {1.0f, 1.0f, 1.0f, 1.0f},
             {0.0f, 1.0f}},
      Vertex{{-1.0f, 1.0f, 0.0f, 1.0f},
             {1.0f, 1.0f, 1.0f, 1.0f},
             {0.0f, 0.0f}},
      Vertex{{1.0f, -1.0f, 0.0f, 1.0f},
             {1.0f, 1.0f, 1.0f, 1.0f},
             {1.0f, 1.0f}},
      Vertex{{1.0f, 1.0f, 0.0f, 1.0f},
             {1.0f, 1.0f, 1.0f, 1.0f},
             {1.0f, 0.0f}},
  };
  constexpr std::array<std::uint16_t, 30> kIndices = {0, 1, 2, 2, 1, 3};
  auto vertex_buffer = context.CreateUploadBuffer(
      sizeof(kVertices), kVertices.data(), sizeof(kVertices));
  auto index_buffer = context.CreateUploadBuffer(
      sizeof(kIndices), kIndices.data(), sizeof(kIndices));
  if (!vertex_buffer || !index_buffer)
    return "failed to create FH4-shaped indexed draw inputs";

  const auto rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
  context.device()->CreateRenderTargetView(target.get(), nullptr, rtv);
  constexpr InputValue kDestination = {0.1f, 0.2f, 0.3f, 0.0f};
  context.list()->ClearRenderTargetView(rtv, kDestination.data(), 0, nullptr);
  if (auto error = HResultError("FH4 background pass completion",
                                context.ExecuteAndWait()))
    return error;
  if (auto error = HResultError("FH4 background command-list reset",
                                context.ResetCommandList()))
    return error;
  const auto copy_resource_descriptor = [&](UINT destination, UINT source) {
    context.device()->CopyDescriptorsSimple(
        1, context.CpuDescriptorHandle(resource_heap.get(), destination),
        context.CpuDescriptorHandle(cpu_resource_heap.get(), source),
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  };
  const auto publish_resource_generation =
      [&](ID3D12Resource *texture0, ID3D12Resource *texture1,
          UINT srv0_slot) {
        context.device()->CreateShaderResourceView(
            texture0, nullptr,
            context.CpuDescriptorHandle(cpu_resource_heap.get(),
                                        kCpuSrv0Slot));
        context.device()->CreateShaderResourceView(
            texture1, nullptr,
            context.CpuDescriptorHandle(cpu_resource_heap.get(),
                                        kCpuSrv1Slot));
        copy_resource_descriptor(srv0_slot, kCpuSrv0Slot);
        copy_resource_descriptor(kSrv1Slot, kCpuSrv1Slot);
      };
  for (UINT draw = 0; draw < kDrawCount; ++draw) {
    publish_resource_generation(source0.get(), source1.get(),
                                kSrv0Slots[draw]);
  }
  for (UINT descriptor = 0; descriptor < 3; ++descriptor)
    copy_resource_descriptor(kUnusedSrvTableBase + descriptor, kCpuSrv1Slot);
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
  uav_desc.Format = DXGI_FORMAT_R32_UINT;
  uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav_desc.Buffer.NumElements = 64;
  context.device()->CreateUnorderedAccessView(
      unused_uav1.get(), nullptr, &uav_desc,
      context.CpuDescriptorHandle(cpu_resource_heap.get(), kCpuUav1Slot));
  context.device()->CreateUnorderedAccessView(
      unused_uav2.get(), nullptr, &uav_desc,
      context.CpuDescriptorHandle(cpu_resource_heap.get(), kCpuUav2Slot));
  for (UINT draw = 0; draw < kDrawCount; ++draw) {
    copy_resource_descriptor(kUavTableBases[draw], kCpuUav1Slot);
    copy_resource_descriptor(kUavTableBases[draw] + 1, kCpuUav2Slot);
  }
  const auto publish_sampler_generation = [&](D3D12_SAMPLER_DESC desc) {
    context.device()->CreateSampler(
        &desc, context.CpuDescriptorHandle(cpu_sampler_heap.get(),
                                           kCpuSampler0Slot));
    context.device()->CreateSampler(
        &desc, context.CpuDescriptorHandle(cpu_sampler_heap.get(),
                                           kCpuSampler1Slot));
    context.device()->CopyDescriptorsSimple(
        1, context.CpuDescriptorHandle(sampler_heap.get(), kSampler0Slot),
        context.CpuDescriptorHandle(cpu_sampler_heap.get(), kCpuSampler0Slot),
        D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    context.device()->CopyDescriptorsSimple(
        1, context.CpuDescriptorHandle(sampler_heap.get(), kSampler1Slot),
        context.CpuDescriptorHandle(cpu_sampler_heap.get(), kCpuSampler1Slot),
        D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
  };
  D3D12_SAMPLER_DESC sampler = {};
  sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
  sampler.AddressU = sampler.AddressV = sampler.AddressW =
      D3D12_TEXTURE_ADDRESS_MODE_BORDER;
  sampler.BorderColor[0] = sampler.BorderColor[1] =
      sampler.BorderColor[2] = sampler.BorderColor[3] = 0.0f;
  sampler.MaxLOD = D3D12_FLOAT32_MAX;
  publish_sampler_generation(sampler);

  ComPtr<ID3D12CommandAllocator> submission_allocator;
  if (auto error = HResultError(
          "FH4-shaped multi-table allocator creation",
          context.device()->CreateCommandAllocator(
              D3D12_COMMAND_LIST_TYPE_DIRECT,
              __uuidof(ID3D12CommandAllocator),
              reinterpret_cast<void **>(submission_allocator.put()))))
    return error;
  ComPtr<ID3D12GraphicsCommandList> submission_list;
  if (auto error = HResultError(
          "FH4-shaped multi-table command-list creation",
          context.device()->CreateCommandList(
              0, D3D12_COMMAND_LIST_TYPE_DIRECT, submission_allocator.get(),
              pipeline.get(), __uuidof(ID3D12GraphicsCommandList),
              reinterpret_cast<void **>(submission_list.put()))))
    return error;

  for (auto *texture : {source0.get(), source1.get(), replacement1.get()}) {
    D3D12TestContext::Transition(submission_list.get(), texture,
                                 D3D12_RESOURCE_STATE_COPY_DEST,
                                 D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  }
  for (const auto &texture : replacement0) {
    D3D12TestContext::Transition(
        submission_list.get(), texture.get(), D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  }
  ID3D12DescriptorHeap *heaps[] = {resource_heap.get(), sampler_heap.get()};
  submission_list->SetDescriptorHeaps(std::size(heaps), heaps);
  submission_list->SetGraphicsRootSignature(root_signature.get());
  submission_list->SetGraphicsRootConstantBufferView(
      2, root_cbv_buffer->GetGPUVirtualAddress() +
             (kRootCbvBaseSlots[0] + 1) * kRootCbvStride);
  submission_list->SetGraphicsRootConstantBufferView(
      3, root_cbv_buffer->GetGPUVirtualAddress() +
             (kRootCbvBaseSlots[0] + 1) * kRootCbvStride);
  submission_list->SetGraphicsRootDescriptorTable(
      5, context.GpuDescriptorHandle(sampler_heap.get(), kSampler0Slot));
  submission_list->SetGraphicsRootDescriptorTable(
      6, context.GpuDescriptorHandle(sampler_heap.get(), kSampler1Slot));
  submission_list->SetGraphicsRootDescriptorTable(
      8, context.GpuDescriptorHandle(resource_heap.get(), kSrv1Slot));
  submission_list->SetGraphicsRootDescriptorTable(
      9, context.GpuDescriptorHandle(resource_heap.get(),
                                     kUnusedSrvTableBase));
  submission_list->SetGraphicsRootDescriptorTable(
      10, context.GpuDescriptorHandle(resource_heap.get(),
                                      kUnusedSrvTableBase));
  submission_list->SetGraphicsRootDescriptorTable(
      11, context.GpuDescriptorHandle(resource_heap.get(),
                                      kUnusedSrvTableBase));
  D3D12_VERTEX_BUFFER_VIEW vertex_view = {};
  vertex_view.BufferLocation = vertex_buffer->GetGPUVirtualAddress();
  vertex_view.SizeInBytes = sizeof(kVertices);
  vertex_view.StrideInBytes = sizeof(Vertex);
  D3D12_INDEX_BUFFER_VIEW index_view = {};
  index_view.BufferLocation = index_buffer->GetGPUVirtualAddress();
  index_view.SizeInBytes = sizeof(kIndices);
  index_view.Format = DXGI_FORMAT_R16_UINT;
  submission_list->IASetVertexBuffers(0, 1, &vertex_view);
  submission_list->IASetIndexBuffer(&index_view);
  submission_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  const D3D12_VIEWPORT viewport = {0.0f, 0.0f, float(kDrawCount * 2), 2.0f,
                                   0.0f, 1.0f};
  submission_list->RSSetViewports(1, &viewport);
  submission_list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
  for (UINT draw = 0; draw < kDrawCount; ++draw) {
    const auto cbv_base = root_cbv_buffer->GetGPUVirtualAddress() +
                          kRootCbvBaseSlots[draw] * kRootCbvStride;
    submission_list->SetGraphicsRootConstantBufferView(0, cbv_base);
    submission_list->SetGraphicsRootConstantBufferView(
        1, cbv_base + kRootCbvStride);
    submission_list->SetGraphicsRootConstantBufferView(
        4, cbv_base + 2 * kRootCbvStride);
    const D3D12_RECT scissor = {LONG(draw * 2), 0, LONG(draw * 2 + 2), 2};
    submission_list->RSSetScissorRects(1, &scissor);
    submission_list->SetGraphicsRootDescriptorTable(
        7,
        context.GpuDescriptorHandle(resource_heap.get(), kSrv0Slots[draw]));
    submission_list->SetGraphicsRootDescriptorTable(
        12, context.GpuDescriptorHandle(resource_heap.get(),
                                        kUavTableBases[draw]));
    submission_list->SetPipelineState(pipeline.get());
    submission_list->DrawIndexedInstanced(kIndices.size(), 1, 0, 0, 0);
    const D3D12_RECT overlay_scissor = {
        LONG(draw * 2), 0, LONG(draw * 2 + 2), 1};
    submission_list->RSSetScissorRects(1, &overlay_scissor);
    submission_list->SetPipelineState(overlay_pipeline.get());
    submission_list->DrawIndexedInstanced(kIndices.size(), 1, 0, 0, 0);
  }
  D3D12TestContext::Transition(submission_list.get(), target.get(),
                               D3D12_RESOURCE_STATE_RENDER_TARGET,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
  if (auto error = HResultError("FH4-shaped multi-table close",
                                submission_list->Close()))
    return error;

  // Generation A was visible while recording. Publish generation B after
  // Close but before ExecuteCommandLists; this is the D3D12 submission-time
  // descriptor state that the command list must consume.
  for (UINT draw = 0; draw < kDrawCount; ++draw) {
    publish_resource_generation(replacement0[draw].get(), replacement1.get(),
                                kSrv0Slots[draw]);
  }
  sampler.AddressU = sampler.AddressV = sampler.AddressW =
      D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  publish_sampler_generation(sampler);

  ComPtr<ID3D12Fence> gate;
  ComPtr<ID3D12Fence> completion;
  if (auto error = HResultError(
          "FH4-shaped multi-table gate creation",
          context.device()->CreateFence(
              0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence),
              reinterpret_cast<void **>(gate.put()))))
    return error;
  if (auto error = HResultError(
          "FH4-shaped completion fence creation",
          context.device()->CreateFence(
              0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence),
              reinterpret_cast<void **>(completion.put()))))
    return error;
  if (auto error = HResultError("FH4-shaped multi-table queue wait",
                                context.queue()->Wait(gate.get(), 1)))
    return error;
  ID3D12CommandList *submission_lists[] = {submission_list.get()};
  context.queue()->ExecuteCommandLists(1, submission_lists);
  if (auto error = HResultError(
          "FH4-shaped completion fence signal",
          context.queue()->Signal(completion.get(), 1)))
    return error;
  Sleep(25);
  if (completion->GetCompletedValue() >= 1)
    return "FH4-shaped completion fence bypassed the preceding queue wait";

  if (auto error = HResultError("FH4-shaped multi-table gate signal",
                                gate->Signal(1)))
    return error;
  if (auto error = HResultError(
          "FH4-shaped completion fence wait",
          context.WaitForFence(completion.get(), 1)))
    return error;

  // Root-signature 1.1 marks these descriptor ranges static. Reuse them only
  // after the application-visible completion fence, matching FH4's upload and
  // descriptor-ring contract. If DXMT exposes completion before the compiled
  // draw has consumed its bindings, this post-fence poison changes the result.
  for (UINT draw = 0; draw < kDrawCount; ++draw)
    publish_resource_generation(source0.get(), source1.get(),
                                kSrv0Slots[draw]);
  constexpr InputValue kCrossTypeValue = {0.9f, 0.1f, 0.7f, 1.0f};
  auto cross_type_srv0 = CreateInputBuffer(context, kCrossTypeValue);
  if (!cross_type_srv0)
    return "failed to create FH4-shaped cross-type overwrite buffers";
  WriteTypedBufferSrv(
      context, cross_type_srv0.get(),
      context.CpuDescriptorHandle(cpu_resource_heap.get(), kCpuSrv0Slot));
  for (UINT draw = 0; draw < kDrawCount; ++draw) {
    copy_resource_descriptor(kSrv0Slots[draw], kCpuSrv0Slot);
    copy_resource_descriptor(kUavTableBases[draw], kCpuSrv0Slot);
    copy_resource_descriptor(kUavTableBases[draw] + 1, kCpuSrv0Slot);
  }
  sampler.AddressU = sampler.AddressV = sampler.AddressW =
      D3D12_TEXTURE_ADDRESS_MODE_BORDER;
  sampler.BorderColor[0] = sampler.BorderColor[1] =
      sampler.BorderColor[2] = sampler.BorderColor[3] = 0.0f;
  publish_sampler_generation(sampler);

  void *root_cbv_mapped = nullptr;
  D3D12_RANGE root_cbv_read_range = {0, 0};
  if (auto error = HResultError(
          "FH4-shaped root descriptor ring remap",
          root_cbv_buffer->Map(0, &root_cbv_read_range, &root_cbv_mapped)))
    return error;
  std::memset(root_cbv_mapped, 0xcd, root_cbv_data.size());
  D3D12_RANGE root_cbv_written_range = {0, root_cbv_data.size()};
  root_cbv_buffer->Unmap(0, &root_cbv_written_range);

  TextureReadback readback;
  if (auto error = HResultError(
          "FH4-shaped multi-table readback",
          context.ReadbackTexture(target.get(), &readback)))
    return error;
  UINT matched = 0;
  InputValue first_mismatch = {};
  UINT first_mismatch_draw = UINT_MAX;
  for (UINT draw = 0; draw < kDrawCount; ++draw) {
    std::uint32_t packed = 0;
    std::memcpy(&packed,
                readback.data.data() + readback.row_pitch +
                    (draw * 2 + 1) * sizeof(packed),
                sizeof(packed));
    const InputValue actual = {
        float(packed & 0x3ffu) / 1023.0f,
        float((packed >> 10) & 0x3ffu) / 1023.0f,
        float((packed >> 20) & 0x3ffu) / 1023.0f,
        float((packed >> 30) & 0x3u) / 3.0f};
    bool draw_matched = true;
    for (UINT component = 0; component < actual.size(); ++component) {
      draw_matched &= std::abs(actual[component] -
                               expected_colors[draw][component]) < 0.01f;
    }
    if (draw_matched) {
      ++matched;
    } else if (first_mismatch_draw == UINT_MAX) {
      first_mismatch_draw = draw;
      first_mismatch = actual;
    }

    std::memcpy(&packed,
                readback.data.data() +
                    (draw * 2 + 1) * sizeof(packed),
                sizeof(packed));
    const InputValue overlay_actual = {
        float(packed & 0x3ffu) / 1023.0f,
        float((packed >> 10) & 0x3ffu) / 1023.0f,
        float((packed >> 20) & 0x3ffu) / 1023.0f,
        float((packed >> 30) & 0x3u) / 3.0f};
    bool overlay_matched = true;
    for (UINT component = 0; component < overlay_actual.size(); ++component) {
      overlay_matched &=
          std::abs(overlay_actual[component] -
                   expected_overlays[draw][component]) < 0.01f;
    }
    if (overlay_matched) {
      ++matched;
    } else if (first_mismatch_draw == UINT_MAX) {
      first_mismatch_draw = draw;
      first_mismatch = overlay_actual;
    }
  }
  if (measurement) {
    measurement->operations = kDrawCount * 2;
    measurement->expected = kDrawCount * 2;
    measurement->actual = matched;
  }
  if (matched == kDrawCount * 2)
    return std::nullopt;

  std::ostringstream message;
  message << "FH4-shaped draw " << first_mismatch_draw << " readback was "
          << first_mismatch[0] << "," << first_mismatch[1] << ","
          << first_mismatch[2] << "," << first_mismatch[3] << ", expected "
          << expected_colors[first_mismatch_draw][0] << ","
          << expected_colors[first_mismatch_draw][1] << ","
          << expected_colors[first_mismatch_draw][2] << ","
          << expected_colors[first_mismatch_draw][3] << "; matched "
          << matched << "/" << kDrawCount * 2;
  return message.str();
}

BindingHotspotError RunCompiledDescriptorBacklogScenario(
    std::uint32_t submission_count, BindingHotspotMeasurement *measurement) {
  if (!submission_count)
    return "descriptor backlog requires at least one submission";

  D3D12TestContext context;
  if (auto error =
          HResultError("D3D12 context initialization", context.Initialize()))
    return error;

  D3D12_DESCRIPTOR_RANGE srv_range = {};
  srv_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  srv_range.NumDescriptors = 1;
  srv_range.BaseShaderRegister = 0;
  std::array<D3D12_ROOT_PARAMETER, 2> parameters = {};
  parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[0].DescriptorTable.NumDescriptorRanges = 1;
  parameters[0].DescriptorTable.pDescriptorRanges = &srv_range;
  parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
  parameters[1].Descriptor.ShaderRegister = 0;
  parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  root_desc.NumParameters = parameters.size();
  root_desc.pParameters = parameters.data();
  auto root_signature = context.CreateRootSignature(root_desc);
  if (!root_signature)
    return "failed to create descriptor backlog root signature";

  constexpr const char *kComputeSource = R"(
    Buffer<uint> input : register(t0);
    RWByteAddressBuffer output : register(u0);
    [numthreads(1, 1, 1)]
    void main() { output.Store(0, input[0]); })";
  ComPtr<ID3DBlob> compute_shader;
  if (auto error = CompileBindingHotspotShader(
          kComputeSource, "descriptor_backlog_compute.hlsl", "cs_5_0",
          &compute_shader))
    return error;
  const D3D12_SHADER_BYTECODE bytecode = {compute_shader->GetBufferPointer(),
                                          compute_shader->GetBufferSize()};
  auto pipeline = context.CreateComputePipeline(root_signature.get(), bytecode);
  if (!pipeline)
    return "failed to create descriptor backlog pipeline";

  constexpr UINT kHeapSize = 1'000'000;
  constexpr UINT kDescriptorSlot = 115'511;
  auto heap = context.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kHeapSize, true);
  if (!heap)
    return "failed to create descriptor backlog heap";

  struct Submission {
    std::uint32_t expected = 0;
    ComPtr<ID3D12Resource> input;
    ComPtr<ID3D12Resource> output;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
  };
  std::vector<Submission> submissions(submission_count);
  for (std::uint32_t index = 0; index < submission_count; ++index) {
    auto &submission = submissions[index];
    submission.expected = 0x51a70000u + index;
    submission.input = context.CreateUploadBuffer(
        sizeof(submission.expected), &submission.expected,
        sizeof(submission.expected));
    submission.output = context.CreateBuffer(
        sizeof(submission.expected), D3D12_HEAP_TYPE_DEFAULT,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    if (!submission.input || !submission.output)
      return "failed to create descriptor backlog buffers";
    if (auto error = HResultError(
            "descriptor backlog allocator creation",
            context.device()->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                __uuidof(ID3D12CommandAllocator),
                reinterpret_cast<void **>(submission.allocator.put()))))
      return error;
    if (auto error = HResultError(
            "descriptor backlog command-list creation",
            context.device()->CreateCommandList(
                0, D3D12_COMMAND_LIST_TYPE_DIRECT, submission.allocator.get(),
                pipeline.get(), __uuidof(ID3D12GraphicsCommandList),
                reinterpret_cast<void **>(submission.list.put()))))
      return error;

    ID3D12DescriptorHeap *heaps[] = {heap.get()};
    submission.list->SetDescriptorHeaps(1, heaps);
    submission.list->SetComputeRootSignature(root_signature.get());
    submission.list->SetComputeRootDescriptorTable(
        0, context.GpuDescriptorHandle(heap.get(), kDescriptorSlot));
    submission.list->SetComputeRootUnorderedAccessView(
        1, submission.output->GetGPUVirtualAddress());
    submission.list->Dispatch(1, 1, 1);
    D3D12TestContext::Transition(
        submission.list.get(), submission.output.get(),
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_SOURCE);
    if (auto error = HResultError("descriptor backlog close",
                                  submission.list->Close()))
      return error;
  }

  ComPtr<ID3D12Fence> gate;
  if (auto error = HResultError(
          "descriptor backlog gate creation",
          context.device()->CreateFence(
              0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence),
              reinterpret_cast<void **>(gate.put()))))
    return error;
  if (auto error = HResultError("descriptor backlog queue wait",
                                context.queue()->Wait(gate.get(), 1)))
    return error;

  const auto write_srv = [&](ID3D12Resource *resource) {
    D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
    desc.Format = DXGI_FORMAT_R32_UINT;
    desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    desc.Buffer.NumElements = 1;
    context.device()->CreateShaderResourceView(
        resource, &desc, context.CpuDescriptorHandle(heap.get(),
                                                      kDescriptorSlot));
  };
  for (auto &submission : submissions) {
    write_srv(submission.input.get());
    ID3D12CommandList *lists[] = {submission.list.get()};
    context.queue()->ExecuteCommandLists(1, lists);
  }

  constexpr std::uint32_t kPoison = 0xdeadc0deu;
  auto poison = context.CreateUploadBuffer(sizeof(kPoison), &kPoison,
                                            sizeof(kPoison));
  if (!poison)
    return "failed to create descriptor backlog poison buffer";
  write_srv(poison.get());

  if (auto error = HResultError("descriptor backlog gate signal",
                                gate->Signal(1)))
    return error;
  if (auto error = HResultError("descriptor backlog completion",
                                context.SignalAndWait()))
    return error;

  std::uint32_t matched = 0;
  for (std::uint32_t index = 0; index < submissions.size(); ++index) {
    std::vector<std::uint8_t> data;
    if (auto error = HResultError(
            "descriptor backlog readback",
            context.ReadbackBuffer(submissions[index].output.get(),
                                   sizeof(std::uint32_t), &data)))
      return error;
    std::uint32_t actual = 0;
    std::memcpy(&actual, data.data(), sizeof(actual));
    if (actual != submissions[index].expected) {
      std::ostringstream message;
      message << "descriptor backlog submission " << index << " produced 0x"
              << std::hex << actual << ", expected 0x"
              << submissions[index].expected;
      return message.str();
    }
    matched++;
    if (index + 1 < submissions.size()) {
      if (auto error = HResultError("descriptor backlog readback reset",
                                    context.ResetCommandList()))
        return error;
    }
  }
  if (measurement) {
    measurement->operations = submission_count;
    measurement->expected = submission_count;
    measurement->actual = matched;
  }
  return std::nullopt;
}

BindingHotspotError
RunRootCbvFenceReuseScenario(BindingHotspotMeasurement *measurement) {
  D3D12TestContext context;
  if (auto error =
          HResultError("D3D12 context initialization", context.Initialize()))
    return error;

  std::array<D3D12_ROOT_PARAMETER, 2> parameters = {};
  parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  parameters[0].Descriptor.ShaderRegister = 0;
  parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
  parameters[1].Descriptor.ShaderRegister = 0;
  parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  root_desc.NumParameters = parameters.size();
  root_desc.pParameters = parameters.data();
  auto root_signature = context.CreateRootSignature(root_desc);
  if (!root_signature)
    return "failed to create root-CBV fence root signature";

  constexpr const char *kComputeSource = R"(
    cbuffer Input : register(b0) { uint value; };
    RWByteAddressBuffer output : register(u0);
    [numthreads(1, 1, 1)]
    void main() { output.Store(0, value); })";
  ComPtr<ID3DBlob> compute_shader;
  if (auto error = CompileBindingHotspotShader(
          kComputeSource, "root_cbv_fence_compute.hlsl", "cs_5_0",
          &compute_shader))
    return error;
  const D3D12_SHADER_BYTECODE bytecode = {compute_shader->GetBufferPointer(),
                                          compute_shader->GetBufferSize()};
  auto pipeline = context.CreateComputePipeline(root_signature.get(), bytecode);
  if (!pipeline)
    return "failed to create root-CBV fence pipeline";

  constexpr std::uint32_t kExpected = 0x13579bdfu;
  constexpr std::uint32_t kPoison = 0xdeadc0deu;
  std::array<std::uint32_t, 64> constants_data = {};
  constants_data[0] = kExpected;
  auto constants = context.CreateUploadBuffer(
      sizeof(constants_data), constants_data.data(), sizeof(constants_data));
  auto output = context.CreateBuffer(
      sizeof(std::uint32_t), D3D12_HEAP_TYPE_DEFAULT,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  if (!constants || !output)
    return "failed to create root-CBV fence resources";

  ComPtr<ID3D12CommandAllocator> allocator;
  if (auto error = HResultError(
          "root-CBV fence allocator creation",
          context.device()->CreateCommandAllocator(
              D3D12_COMMAND_LIST_TYPE_DIRECT,
              __uuidof(ID3D12CommandAllocator),
              reinterpret_cast<void **>(allocator.put()))))
    return error;
  ComPtr<ID3D12GraphicsCommandList> list;
  if (auto error = HResultError(
          "root-CBV fence command-list creation",
          context.device()->CreateCommandList(
              0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.get(),
              pipeline.get(), __uuidof(ID3D12GraphicsCommandList),
              reinterpret_cast<void **>(list.put()))))
    return error;
  list->SetComputeRootSignature(root_signature.get());
  list->SetComputeRootConstantBufferView(
      0, constants->GetGPUVirtualAddress());
  list->SetComputeRootUnorderedAccessView(1, output->GetGPUVirtualAddress());
  list->Dispatch(1, 1, 1);
  D3D12TestContext::Transition(
      list.get(), output.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_COPY_SOURCE);
  if (auto error = HResultError("root-CBV fence close", list->Close()))
    return error;

  ComPtr<ID3D12Fence> gate;
  ComPtr<ID3D12Fence> application_fence;
  if (auto error = HResultError(
          "root-CBV fence gate creation",
          context.device()->CreateFence(
              0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence),
              reinterpret_cast<void **>(gate.put()))))
    return error;
  if (auto error = HResultError(
          "root-CBV application fence creation",
          context.device()->CreateFence(
              0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence),
              reinterpret_cast<void **>(application_fence.put()))))
    return error;
  if (auto error = HResultError("root-CBV queue wait",
                                context.queue()->Wait(gate.get(), 1)))
    return error;
  ID3D12CommandList *lists[] = {list.get()};
  context.queue()->ExecuteCommandLists(1, lists);
  if (auto error = HResultError(
          "root-CBV application fence signal",
          context.queue()->Signal(application_fence.get(), 1)))
    return error;

  // The queued wait must keep the application's completion fence pending.
  // Retrace frame export naturally creates this kind of drain; live rendering
  // relies on the fence itself before recycling mapped upload-ring storage.
  Sleep(50);
  const bool signaled_early = application_fence->GetCompletedValue() >= 1;
  if (auto error = HResultError("root-CBV gate signal", gate->Signal(1)))
    return error;
  if (auto error = HResultError(
          "root-CBV application fence completion",
          context.WaitForFence(application_fence.get(), 1)))
    return error;
  if (signaled_early)
    return "queue fence completed while preceding queue wait was unsatisfied";

  void *mapped = nullptr;
  D3D12_RANGE read_range = {0, 0};
  if (auto error = HResultError("root-CBV remap",
                                constants->Map(0, &read_range, &mapped)))
    return error;
  std::memcpy(mapped, &kPoison, sizeof(kPoison));
  D3D12_RANGE written_range = {0, sizeof(kPoison)};
  constants->Unmap(0, &written_range);

  std::vector<std::uint8_t> data;
  if (auto error = HResultError(
          "root-CBV fence readback",
          context.ReadbackBuffer(output.get(), sizeof(kExpected), &data)))
    return error;
  std::uint32_t actual = 0;
  std::memcpy(&actual, data.data(), sizeof(actual));
  if (measurement) {
    measurement->operations = 1;
    measurement->expected = kExpected;
    measurement->actual = actual;
  }
  if (actual == kExpected)
    return std::nullopt;

  std::ostringstream message;
  message << "root-CBV fence readback was 0x" << std::hex << actual
          << ", expected 0x" << kExpected;
  return message.str();
}

BindingHotspotError RunQueuedUiLayerCompositionScenario(
    std::uint32_t frame_count, BindingHotspotMeasurement *measurement) {
  if (!frame_count)
    return "queued UI composition requires at least one frame";

  D3D12TestContext context;
  if (auto error =
          HResultError("D3D12 context initialization", context.Initialize()))
    return error;

  std::array<D3D12_ROOT_PARAMETER, 1> parameters = {};
  parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  parameters[0].Descriptor.ShaderRegister = 0;
  parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  root_desc.NumParameters = parameters.size();
  root_desc.pParameters = parameters.data();
  auto root_signature = context.CreateRootSignature(root_desc);
  if (!root_signature)
    return "failed to create queued UI composition root signature";

  constexpr const char *kVertexSource = R"(
    float4 main(uint vertex_id : SV_VertexID) : SV_Position {
      float2 position = vertex_id == 0 ? float2(-1.0, -1.0) :
                        vertex_id == 1 ? float2(-1.0,  3.0) :
                                         float2( 3.0, -1.0);
      return float4(position, 0.0, 1.0);
    })";
  constexpr const char *kPixelSource = R"(
    cbuffer Layer : register(b0) { float4 color; };
    float4 main() : SV_Target { return color; })";
  ComPtr<ID3DBlob> vertex_shader;
  ComPtr<ID3DBlob> pixel_shader;
  if (auto error = CompileBindingHotspotShader(
          kVertexSource, "queued_ui_layer_vertex.hlsl", "vs_5_0",
          &vertex_shader))
    return error;
  if (auto error = CompileBindingHotspotShader(
          kPixelSource, "queued_ui_layer_pixel.hlsl", "ps_5_0",
          &pixel_shader))
    return error;

  D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {};
  pso_desc.pRootSignature = root_signature.get();
  pso_desc.VS = {vertex_shader->GetBufferPointer(),
                 vertex_shader->GetBufferSize()};
  pso_desc.PS = {pixel_shader->GetBufferPointer(),
                 pixel_shader->GetBufferSize()};
  auto &blend = pso_desc.BlendState.RenderTarget[0];
  blend.BlendEnable = TRUE;
  blend.SrcBlend = D3D12_BLEND_ONE;
  blend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
  blend.BlendOp = D3D12_BLEND_OP_ADD;
  blend.SrcBlendAlpha = D3D12_BLEND_ONE;
  blend.DestBlendAlpha = D3D12_BLEND_ZERO;
  blend.BlendOpAlpha = D3D12_BLEND_OP_MAX;
  blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
  pso_desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
  pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  pso_desc.SampleMask = UINT_MAX;
  pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  pso_desc.NumRenderTargets = 1;
  pso_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
  pso_desc.SampleDesc.Count = 1;
  ComPtr<ID3D12PipelineState> pipeline;
  if (auto error = HResultError(
          "queued UI composition pipeline creation",
          context.device()->CreateGraphicsPipelineState(
              &pso_desc, __uuidof(ID3D12PipelineState),
              reinterpret_cast<void **>(pipeline.put()))))
    return error;

  constexpr UINT kBackbufferCount = 3;
  auto rtv_heap = context.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_RTV, kBackbufferCount, false);
  std::array<ComPtr<ID3D12Resource>, kBackbufferCount> backbuffers;
  for (UINT index = 0; index < kBackbufferCount; ++index) {
    backbuffers[index] = context.CreateTexture2D(
        4, 4, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
        D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    if (!backbuffers[index] || !rtv_heap)
      return "failed to create queued UI composition backbuffers";
    context.device()->CreateRenderTargetView(
        backbuffers[index].get(), nullptr,
        context.CpuDescriptorHandle(rtv_heap.get(), index));
  }

  std::vector<ComPtr<ID3D12Resource>> snapshots(frame_count);
  for (auto &snapshot : snapshots) {
    snapshot = context.CreateTexture2D(
        4, 4, 1, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_NONE,
        D3D12_RESOURCE_STATE_COPY_DEST);
    if (!snapshot)
      return "failed to create queued UI composition snapshots";
  }

  constexpr UINT64 kConstantStride =
      D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
  std::vector<InputValue> panels(frame_count);
  std::vector<InputValue> foregrounds(frame_count);
  for (std::uint32_t frame = 0; frame < frame_count; ++frame) {
    panels[frame] = {
        float(8 + frame % 5) / 64.0f,
        float(4 + (frame / 5) % 4) / 64.0f,
        float(2 + frame % 3) / 64.0f,
        0.5f};
    foregrounds[frame] = {
        float(2 + frame % 3) / 64.0f,
        float(6 + frame % 5) / 64.0f,
        float(1 + (frame / 3) % 4) / 64.0f,
        0.25f};
  }

  // Match the live renderer's upload-ring shape instead of allocating unique
  // constants for every queued frame. The slots are only recycled after the
  // application fence covering their previous use has completed.
  std::vector<std::uint8_t> constant_data(
      kBackbufferCount * 2 * kConstantStride, 0);
  const auto initial_frame_count =
      std::min(frame_count, static_cast<std::uint32_t>(kBackbufferCount));
  for (std::uint32_t frame = 0; frame < initial_frame_count; ++frame) {
    std::memcpy(constant_data.data() + (frame * 2) * kConstantStride,
                panels[frame].data(), sizeof(InputValue));
    std::memcpy(constant_data.data() + (frame * 2 + 1) * kConstantStride,
                foregrounds[frame].data(), sizeof(InputValue));
  }
  auto constants = context.CreateUploadBuffer(
      constant_data.size(), constant_data.data(), constant_data.size());
  if (!constants)
    return "failed to create queued UI composition constants";

  const auto write_constant_slot = [&](std::uint32_t frame)
      -> BindingHotspotError {
    const UINT slot = frame % kBackbufferCount;
    void *mapped = nullptr;
    D3D12_RANGE read_range = {0, 0};
    if (auto error = HResultError("queued UI constants map",
                                  constants->Map(0, &read_range, &mapped)))
      return error;
    const auto panel_offset = (slot * 2) * kConstantStride;
    const auto foreground_offset = (slot * 2 + 1) * kConstantStride;
    std::memcpy(static_cast<std::uint8_t *>(mapped) + panel_offset,
                panels[frame].data(), sizeof(InputValue));
    std::memcpy(static_cast<std::uint8_t *>(mapped) + foreground_offset,
                foregrounds[frame].data(), sizeof(InputValue));
    D3D12_RANGE written_range = {
        static_cast<SIZE_T>(panel_offset),
        static_cast<SIZE_T>(foreground_offset + sizeof(InputValue))};
    constants->Unmap(0, &written_range);
    return std::nullopt;
  };

  struct Submission {
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
  };
  std::vector<Submission> submissions;
  submissions.reserve(frame_count * 4);
  const auto create_submission = [&](ID3D12PipelineState *initial_pipeline,
                                     Submission **submission)
      -> BindingHotspotError {
    submissions.emplace_back();
    auto &result = submissions.back();
    if (auto error = HResultError(
            "queued UI composition allocator creation",
            context.device()->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                __uuidof(ID3D12CommandAllocator),
                reinterpret_cast<void **>(result.allocator.put()))))
      return error;
    if (auto error = HResultError(
            "queued UI composition command-list creation",
            context.device()->CreateCommandList(
                0, D3D12_COMMAND_LIST_TYPE_DIRECT, result.allocator.get(),
                initial_pipeline, __uuidof(ID3D12GraphicsCommandList),
                reinterpret_cast<void **>(result.list.put()))))
      return error;
    *submission = &result;
    return std::nullopt;
  };
  const auto close_and_execute = [&](Submission &submission)
      -> BindingHotspotError {
    if (auto error = HResultError("queued UI composition close",
                                  submission.list->Close()))
      return error;
    ID3D12CommandList *lists[] = {submission.list.get()};
    context.queue()->ExecuteCommandLists(1, lists);
    return std::nullopt;
  };

  ComPtr<ID3D12Fence> gate;
  ComPtr<ID3D12Fence> application_fence;
  if (auto error = HResultError(
          "queued UI composition gate creation",
          context.device()->CreateFence(
              0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence),
              reinterpret_cast<void **>(gate.put()))))
    return error;
  if (auto error = HResultError(
          "queued UI composition application fence creation",
          context.device()->CreateFence(
              0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence),
              reinterpret_cast<void **>(application_fence.put()))))
    return error;
  if (auto error = HResultError("queued UI composition queue wait",
                                context.queue()->Wait(gate.get(), 1)))
    return error;

  const D3D12_VIEWPORT viewport = {0.0f, 0.0f, 4.0f, 4.0f, 0.0f, 1.0f};
  const D3D12_RECT panel_scissor = {1, 1, 4, 4};
  const D3D12_RECT foreground_scissor = {2, 2, 4, 3};
  std::vector<InputValue> backgrounds(frame_count);
  for (std::uint32_t frame = 0; frame < frame_count; ++frame) {
    const UINT backbuffer_index = frame % kBackbufferCount;
    if (frame >= kBackbufferCount) {
      const UINT64 reusable_fence_value = frame - kBackbufferCount + 1;
      if (auto error = HResultError(
              "queued UI reusable-slot fence completion",
              context.WaitForFence(application_fence.get(),
                                   reusable_fence_value)))
        return error;
      if (auto error = write_constant_slot(frame))
        return error;
    }
    const auto rtv =
        context.CpuDescriptorHandle(rtv_heap.get(), backbuffer_index);
    backgrounds[frame] = {
        float((frame % 4) + 1) / 16.0f,
        float(((frame / 4) % 4) + 1) / 16.0f,
        float((frame % 3) + 1) / 16.0f,
        0.0f};

    Submission *background = nullptr;
    if (auto error = create_submission(nullptr, &background))
      return error;
    background->list->ClearRenderTargetView(
        rtv, backgrounds[frame].data(), 0, nullptr);
    if (auto error = close_and_execute(*background))
      return error;

    const auto record_layer = [&](const D3D12_RECT &scissor,
                                  UINT64 constant_offset)
        -> BindingHotspotError {
      Submission *layer = nullptr;
      if (auto error = create_submission(pipeline.get(), &layer))
        return error;
      layer->list->SetGraphicsRootSignature(root_signature.get());
      layer->list->SetGraphicsRootConstantBufferView(
          0, constants->GetGPUVirtualAddress() + constant_offset);
      layer->list->IASetPrimitiveTopology(
          D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
      layer->list->RSSetViewports(1, &viewport);
      layer->list->RSSetScissorRects(1, &scissor);
      layer->list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
      layer->list->DrawInstanced(3, 1, 0, 0);
      return close_and_execute(*layer);
    };
    if (auto error = record_layer(panel_scissor,
                                  (backbuffer_index * 2) * kConstantStride))
      return error;
    if (auto error = record_layer(foreground_scissor,
                                  (backbuffer_index * 2 + 1) *
                                      kConstantStride))
      return error;

    Submission *present_snapshot = nullptr;
    if (auto error = create_submission(nullptr, &present_snapshot))
      return error;
    D3D12TestContext::Transition(
        present_snapshot->list.get(), backbuffers[backbuffer_index].get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_COPY_SOURCE);
    present_snapshot->list->CopyResource(
        snapshots[frame].get(), backbuffers[backbuffer_index].get());
    D3D12TestContext::Transition(
        present_snapshot->list.get(), snapshots[frame].get(),
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12TestContext::Transition(
        present_snapshot->list.get(), backbuffers[backbuffer_index].get(),
        D3D12_RESOURCE_STATE_COPY_SOURCE,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    if (auto error = close_and_execute(*present_snapshot))
      return error;

    const UINT64 frame_fence_value = frame + 1;
    if (auto error = HResultError(
            "queued UI composition application fence signal",
            context.queue()->Signal(application_fence.get(),
                                    frame_fence_value)))
      return error;
    if (frame + 1 == initial_frame_count) {
      Sleep(50);
      const bool signaled_early =
          application_fence->GetCompletedValue() != 0;
      if (auto error = HResultError("queued UI composition gate signal",
                                    gate->Signal(1)))
        return error;
      if (signaled_early)
        return "queued UI fence completed while the queue gate was unsatisfied";
    }
  }

  if (auto error = HResultError(
          "queued UI composition final fence completion",
          context.WaitForFence(application_fence.get(), frame_count)))
    return error;

  // Mirror legal upload-ring recycling: contents may only be overwritten once
  // the application fence covering every queued frame has completed.
  void *mapped = nullptr;
  D3D12_RANGE read_range = {0, 0};
  if (auto error = HResultError("queued UI constants remap",
                                constants->Map(0, &read_range, &mapped)))
    return error;
  std::memset(mapped, 0xcd, constant_data.size());
  D3D12_RANGE written_range = {0, constant_data.size()};
  constants->Unmap(0, &written_range);

  const auto blend_over = [](const InputValue &source,
                             const InputValue &destination) {
    InputValue result = {};
    for (UINT channel = 0; channel < 3; ++channel)
      result[channel] =
          source[channel] + destination[channel] * (1.0f - source[3]);
    result[3] = std::max(source[3], destination[3]);
    return result;
  };
  const auto pack_unorm = [](const InputValue &color) {
    std::uint32_t packed = 0;
    for (UINT channel = 0; channel < color.size(); ++channel) {
      const auto clamped = std::max(0.0f, std::min(1.0f, color[channel]));
      const auto byte = static_cast<std::uint32_t>(
          std::lround(clamped * 255.0f));
      packed |= byte << (channel * 8);
    }
    return packed;
  };

  std::uint32_t matched = 0;
  for (std::uint32_t frame = 0; frame < frame_count; ++frame) {
    TextureReadback readback;
    if (auto error = HResultError(
            "queued UI composition readback",
            context.ReadbackTexture(snapshots[frame].get(), &readback)))
      return error;
    const auto panel = blend_over(panels[frame], backgrounds[frame]);
    const auto foreground = blend_over(foregrounds[frame], panel);
    const std::array<std::pair<const char *, std::pair<UINT, UINT>>, 3>
        sample_points = {{{"background", {0, 0}},
                          {"panel", {1, 1}},
                          {"foreground", {2, 2}}}};
    const std::array<std::uint32_t, 3> expected = {
        pack_unorm(backgrounds[frame]), pack_unorm(panel),
        pack_unorm(foreground)};
    for (UINT sample = 0; sample < sample_points.size(); ++sample) {
      const auto [x, y] = sample_points[sample].second;
      std::uint32_t actual = 0;
      std::memcpy(&actual,
                  readback.data.data() + y * readback.row_pitch +
                      x * sizeof(actual),
                  sizeof(actual));
      if (!ColorsMatch(actual, expected[sample], 2)) {
        std::ostringstream message;
        message << "queued UI frame " << frame << " "
                << sample_points[sample].first << " pixel was 0x" << std::hex
                << actual << ", expected 0x" << expected[sample];
        return message.str();
      }
      matched++;
    }
    if (frame + 1 < frame_count) {
      if (auto error = HResultError("queued UI readback reset",
                                    context.ResetCommandList()))
        return error;
    }
  }

  if (measurement) {
    measurement->operations = frame_count * 3;
    measurement->expected = frame_count * 3;
    measurement->actual = matched;
  }
  return std::nullopt;
}
} // namespace dxmt::test
