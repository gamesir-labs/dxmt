#include <dxmt_benchmark.hpp>
#include <dxmt_test_com.hpp>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <d3d12.h>
#include <d3dcompiler.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

namespace {

using dxmt::test::ComPtr;

constexpr unsigned int kPipelineRequestCount = 96;
constexpr unsigned int kUnsupportedRequestStride = 8;

constexpr const char kPipelineStressHlsl[] = R"hlsl(
struct PatchPoint {
  float3 position : POSITION;
  float2 uv : TEXCOORD0;
};

struct RasterPoint {
  float4 position : SV_Position;
  float2 uv : TEXCOORD0;
};

float4 StressTransform(float4 value, float seed) {
  [unroll]
  for (uint i = 0; i < 16; ++i) {
    float phase = seed + float(i) * 0.03125;
    value = value.zwxy * (0.875 + phase) + value.yxwz * 0.0625;
    value = frac(abs(value)) * 2.0 - 1.0;
  }
  return value;
}

RasterPoint StandardVS(uint vertex_id : SV_VertexID) {
  static const float2 positions[3] = {
      float2(-1.0, -1.0), float2(-1.0, 3.0), float2(3.0, -1.0)};
  RasterPoint output;
  float2 position = positions[vertex_id % 3];
  output.position = float4(position, 0.0, 1.0);
  output.uv = position * 0.5 + 0.5;
  return output;
}

PatchPoint PatchVS(uint vertex_id : SV_VertexID) {
  static const float3 positions[3] = {
      float3(-1.0, -1.0, 0.0),
      float3(0.0, 1.0, 0.0),
      float3(1.0, -1.0, 0.0)};
  PatchPoint output;
  output.position = positions[vertex_id % 3];
  output.uv = output.position.xy * 0.5 + 0.5;
  return output;
}

struct PatchConstants {
  float edges[3] : SV_TessFactor;
  float inside : SV_InsideTessFactor;
};

PatchConstants PatchConstantsMain(InputPatch<PatchPoint, 3> patch) {
  PatchConstants output;
  float4 stress = StressTransform(
      float4(patch[0].position, patch[1].position.x), patch[2].uv.x);
  output.edges[0] = 2.0 + abs(stress.x);
  output.edges[1] = 2.0 + abs(stress.y);
  output.edges[2] = 2.0 + abs(stress.z);
  output.inside = 2.0 + abs(stress.w);
  return output;
}

[domain("tri")]
[partitioning("fractional_odd")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(3)]
[patchconstantfunc("PatchConstantsMain")]
PatchPoint HullMain(InputPatch<PatchPoint, 3> patch,
                    uint point_id : SV_OutputControlPointID) {
  PatchPoint output = patch[point_id];
  float4 stress = StressTransform(
      float4(output.position, output.uv.x), output.uv.y + float(point_id));
  output.position += stress.xyz * 0.0001;
  return output;
}

[domain("tri")]
RasterPoint DomainMain(PatchConstants constants,
                       const OutputPatch<PatchPoint, 3> patch,
                       float3 barycentric : SV_DomainLocation) {
  RasterPoint output;
  float3 position = patch[0].position * barycentric.x +
                    patch[1].position * barycentric.y +
                    patch[2].position * barycentric.z;
  float2 uv = patch[0].uv * barycentric.x +
              patch[1].uv * barycentric.y +
              patch[2].uv * barycentric.z;
  float4 stress = StressTransform(
      float4(position, constants.inside), uv.x + uv.y);
  output.position = float4(position + stress.xyz * 0.0001, 1.0);
  output.uv = uv;
  return output;
}

float4 PixelMain(RasterPoint input) : SV_Target {
  float4 stress = StressTransform(
      float4(input.uv, input.position.zw), input.position.x * 0.001);
  return float4(abs(stress.xyz), 1.0);
}
)hlsl";

struct ShaderSet {
  ComPtr<ID3DBlob> standard_vertex;
  ComPtr<ID3DBlob> patch_vertex;
  ComPtr<ID3DBlob> hull;
  ComPtr<ID3DBlob> domain;
  ComPtr<ID3DBlob> pixel;
};

struct PipelineRequest {
  D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
  bool expect_success = true;
  bool tessellation = false;
};

struct BurstResult {
  std::vector<double> latency_us;
  std::uint64_t successful = 0;
  std::uint64_t expected_rejections = 0;
  std::uint64_t tessellation = 0;
  double wall_us = 0.0;
  std::string error;
};

std::string HResultMessage(const char *operation, HRESULT hr) {
  std::ostringstream message;
  message << operation << " failed with HRESULT 0x" << std::hex
          << static_cast<unsigned long>(hr);
  return message.str();
}

std::optional<std::string> CompileShader(const char *entry_point,
                                         const char *target,
                                         ComPtr<ID3DBlob> *shader) {
  ComPtr<ID3DBlob> errors;
  const HRESULT hr = D3DCompile(
      kPipelineStressHlsl, sizeof(kPipelineStressHlsl) - 1,
      "d3d12_pipeline_creation_benchmark.hlsl", nullptr, nullptr, entry_point,
      target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, shader->put(), errors.put());
  if (SUCCEEDED(hr))
    return std::nullopt;

  std::ostringstream message;
  message << HResultMessage(entry_point, hr);
  if (errors && errors->GetBufferPointer() && errors->GetBufferSize()) {
    message << ": "
            << std::string(
                   static_cast<const char *>(errors->GetBufferPointer()),
                   errors->GetBufferSize());
  }
  return message.str();
}

std::optional<std::string> CompileShaders(ShaderSet *shaders) {
  for (const auto &[entry_point, target, output] :
       std::array<std::tuple<const char *, const char *, ComPtr<ID3DBlob> *>,
                  5>{{{"StandardVS", "vs_5_0", &shaders->standard_vertex},
                      {"PatchVS", "vs_5_0", &shaders->patch_vertex},
                      {"HullMain", "hs_5_0", &shaders->hull},
                      {"DomainMain", "ds_5_0", &shaders->domain},
                      {"PixelMain", "ps_5_0", &shaders->pixel}}}) {
    if (auto error = CompileShader(entry_point, target, output))
      return error;
  }
  return std::nullopt;
}

D3D12_SHADER_BYTECODE ShaderBytecode(ID3DBlob *shader) {
  return {shader->GetBufferPointer(), shader->GetBufferSize()};
}

std::optional<std::string>
CreateDeviceAndRootSignature(ComPtr<ID3D12Device> *device,
                             ComPtr<ID3D12RootSignature> *root_signature) {
  HRESULT hr =
      D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device),
                        reinterpret_cast<void **>(device->put()));
  if (FAILED(hr))
    return HResultMessage("D3D12CreateDevice", hr);

  D3D12_ROOT_SIGNATURE_DESC desc = {};
  desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
  ComPtr<ID3DBlob> blob;
  ComPtr<ID3DBlob> errors;
  hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1_0,
                                   blob.put(), errors.put());
  if (FAILED(hr))
    return HResultMessage("D3D12SerializeRootSignature", hr);

  hr = (*device)->CreateRootSignature(
      0, blob->GetBufferPointer(), blob->GetBufferSize(),
      __uuidof(ID3D12RootSignature),
      reinterpret_cast<void **>(root_signature->put()));
  return FAILED(hr) ? std::optional<std::string>(HResultMessage(
                          "ID3D12Device::CreateRootSignature", hr))
                    : std::nullopt;
}

bool SupportsSampleCount(ID3D12Device *device, UINT sample_count) {
  D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS support = {};
  support.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  support.SampleCount = sample_count;
  support.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
  return SUCCEEDED(device->CheckFeatureSupport(
             D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &support,
             sizeof(support))) &&
         support.NumQualityLevels > 0;
}

std::vector<PipelineRequest>
BuildPipelineRequests(ID3D12Device *device, ID3D12RootSignature *root_signature,
                      const ShaderSet &shaders, unsigned int run_nonce) {
  std::vector<UINT> supported_sample_counts;
  std::optional<UINT> unsupported_sample_count;
  for (const UINT count : {1u, 2u, 4u, 8u, 16u}) {
    if (SupportsSampleCount(device, count))
      supported_sample_counts.push_back(count);
    else if (!unsupported_sample_count)
      unsupported_sample_count = count;
  }
  if (supported_sample_counts.empty())
    supported_sample_counts.push_back(1);

  std::vector<PipelineRequest> requests;
  requests.reserve(kPipelineRequestCount);
  for (unsigned int index = 0; index < kPipelineRequestCount; ++index) {
    PipelineRequest request;
    request.tessellation = index % 5 < 2;
    request.expect_success =
        !unsupported_sample_count || index % kUnsupportedRequestStride != 0;

    auto &desc = request.desc;
    desc.pRootSignature = root_signature;
    desc.VS =
        ShaderBytecode(request.tessellation ? shaders.patch_vertex.get()
                                            : shaders.standard_vertex.get());
    desc.PS = ShaderBytecode(shaders.pixel.get());
    if (request.tessellation) {
      desc.HS = ShaderBytecode(shaders.hull.get());
      desc.DS = ShaderBytecode(shaders.domain.get());
    }

    desc.BlendState.RenderTarget[0].RenderTargetWriteMask =
        D3D12_COLOR_WRITE_ENABLE_ALL;
    if (index % 3 == 1) {
      auto &blend = desc.BlendState.RenderTarget[0];
      blend.BlendEnable = TRUE;
      blend.SrcBlend = D3D12_BLEND_SRC_ALPHA;
      blend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
      blend.BlendOp = D3D12_BLEND_OP_ADD;
      blend.SrcBlendAlpha = D3D12_BLEND_ONE;
      blend.DestBlendAlpha = D3D12_BLEND_ZERO;
      blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    }

    desc.SampleMask = 0xffff0000u | ((run_nonce + index + 1u) & 0x0000ffffu);
    desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    desc.RasterizerState.CullMode =
        index % 3 == 0
            ? D3D12_CULL_MODE_NONE
            : (index % 3 == 1 ? D3D12_CULL_MODE_FRONT : D3D12_CULL_MODE_BACK);
    desc.RasterizerState.FrontCounterClockwise = index % 2;
    desc.RasterizerState.DepthBias =
        static_cast<INT>((run_nonce + index) % 31u) - 15;
    desc.RasterizerState.DepthClipEnable = TRUE;
    desc.PrimitiveTopologyType = request.tessellation
                                     ? D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH
                                     : D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count =
        request.expect_success
            ? supported_sample_counts[index % supported_sample_counts.size()]
            : *unsupported_sample_count;
    requests.push_back(request);
  }
  return requests;
}

double Percentile(const std::vector<double> &sorted, double percentile) {
  if (sorted.empty())
    return 0.0;
  const auto index = static_cast<std::size_t>(std::ceil(
                         percentile * static_cast<double>(sorted.size()))) -
                     1;
  return sorted[std::min(index, sorted.size() - 1)];
}

BurstResult
RunPipelineCreationBurst(ID3D12Device *device,
                         const std::vector<PipelineRequest> &requests,
                         unsigned int worker_count) {
  struct WorkerResult {
    std::vector<double> latency_us;
    std::uint64_t successful = 0;
    std::uint64_t expected_rejections = 0;
    std::uint64_t tessellation = 0;
    std::string error;
  };

  BurstResult result;
  HANDLE start_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!start_event) {
    result.error = "failed to create pipeline burst start event";
    return result;
  }

  std::vector<WorkerResult> worker_results(worker_count);
  std::vector<std::thread> workers;
  workers.reserve(worker_count);
  for (unsigned int worker = 0; worker < worker_count; ++worker) {
    workers.emplace_back([&, worker] {
      WorkerResult &local = worker_results[worker];
      if (WaitForSingleObject(start_event, INFINITE) != WAIT_OBJECT_0) {
        local.error = "pipeline worker failed to wait for start event";
        return;
      }

      for (std::size_t index = worker; index < requests.size();
           index += worker_count) {
        const PipelineRequest &request = requests[index];
        const auto begin = std::chrono::steady_clock::now();
        ComPtr<ID3D12PipelineState> pipeline;
        const HRESULT hr = device->CreateGraphicsPipelineState(
            &request.desc, __uuidof(ID3D12PipelineState),
            reinterpret_cast<void **>(pipeline.put()));
        const auto elapsed = std::chrono::duration<double, std::micro>(
                                 std::chrono::steady_clock::now() - begin)
                                 .count();
        local.latency_us.push_back(elapsed);

        if (request.expect_success) {
          if (FAILED(hr) || !pipeline) {
            local.error =
                HResultMessage("supported CreateGraphicsPipelineState", hr);
            return;
          }
          ++local.successful;
          if (request.tessellation)
            ++local.tessellation;
        } else {
          if (SUCCEEDED(hr) || pipeline) {
            local.error =
                "unsupported sample-count pipeline was unexpectedly accepted";
            return;
          }
          ++local.expected_rejections;
        }
      }
    });
  }

  const auto wall_begin = std::chrono::steady_clock::now();
  SetEvent(start_event);
  for (auto &worker : workers)
    worker.join();
  result.wall_us = std::chrono::duration<double, std::micro>(
                       std::chrono::steady_clock::now() - wall_begin)
                       .count();
  CloseHandle(start_event);

  for (WorkerResult &worker : worker_results) {
    result.successful += worker.successful;
    result.expected_rejections += worker.expected_rejections;
    result.tessellation += worker.tessellation;
    result.latency_us.insert(result.latency_us.end(), worker.latency_us.begin(),
                             worker.latency_us.end());
    if (result.error.empty() && !worker.error.empty())
      result.error = std::move(worker.error);
  }
  return result;
}

void BI_D3D12GraphicsPipelineCreationBurst(benchmark::State &state) {
  ShaderSet shaders;
  ComPtr<ID3D12Device> device;
  ComPtr<ID3D12RootSignature> root_signature;
  const auto worker_count = static_cast<unsigned int>(state.range(0));
  std::vector<PipelineRequest> requests;
  BurstResult result;
  for (auto _ : state) {
    state.PauseTiming();
    SetEnvironmentVariableA("DXMT_SHADER_CACHE", "0");
    if (auto error = CompileShaders(&shaders)) {
      state.SkipWithError(error->c_str());
      return;
    }
    if (auto error = CreateDeviceAndRootSignature(&device, &root_signature)) {
      state.SkipWithError(error->c_str());
      return;
    }
    requests = BuildPipelineRequests(device.get(), root_signature.get(),
                                     shaders, worker_count * 10'000u);
    state.ResumeTiming();
    result = RunPipelineCreationBurst(device.get(), requests, worker_count);
    if (!result.error.empty()) {
      state.SkipWithError(result.error.c_str());
      return;
    }
  }

  std::sort(result.latency_us.begin(), result.latency_us.end());
  double latency_sum_us = 0.0;
  for (const double latency : result.latency_us)
    latency_sum_us += latency;

  state.counters["workers"] = worker_count;
  state.counters["requests"] = result.latency_us.size();
  state.counters["successful"] = result.successful;
  state.counters["rejected"] = result.expected_rejections;
  state.counters["tessellation"] = result.tessellation;
  state.counters["mean_us"] = result.latency_us.empty()
                                  ? 0.0
                                  : latency_sum_us / result.latency_us.size();
  state.counters["p50_us"] = Percentile(result.latency_us, 0.50);
  state.counters["p95_us"] = Percentile(result.latency_us, 0.95);
  state.counters["max_us"] =
      result.latency_us.empty() ? 0.0 : result.latency_us.back();
  state.counters["sum_to_wall"] =
      result.wall_us > 0.0 ? latency_sum_us / result.wall_us : 0.0;
  state.SetItemsProcessed(state.iterations() * result.latency_us.size());
}

BENCHMARK(BI_D3D12GraphicsPipelineCreationBurst)
    ->ArgName("workers")
    ->Arg(1)
    ->Arg(4)
    ->Iterations(1)
    ->UseRealTime();

} // namespace
