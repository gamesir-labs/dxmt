#include <dxmt_benchmark.hpp>

#include "airconv_dx12_metal4.h"
#include "shaders/runtime_test_shaders.hpp"
#include "winemetal.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <d3dcompiler.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr unsigned int kParseBatch = 32;
constexpr unsigned int kDxbcAirBatch = 4;
constexpr unsigned int kDxilAirBatch = 2;
constexpr unsigned int kMetalBatch = 4;
constexpr unsigned int kCacheBatch = 64;
constexpr std::uint64_t kShaderCacheVersion = 19;

constexpr const char kComputeHlsl[] = R"hlsl(
RWByteAddressBuffer output_buffer : register(u0);

[numthreads(8, 1, 1)]
void main(uint3 dispatch_id : SV_DispatchThreadID) {
  uint value = dispatch_id.x * 0x9e3779b9u + 0x51a7c0deu;
  output_buffer.Store(dispatch_id.x * 4u, value);
}
)hlsl";

struct AirconvArguments {
  SM50_SHADER_COMMON_DATA common = {};
  DXMT12_MTL4_NATIVE_DESCRIPTOR_ABI_DATA native_abi = {};
  SM50_SHADER_PSO_PIXEL_SHADER_DATA pixel = {};

  AirconvArguments() {
    common.type = SM50_SHADER_COMMON;
    common.metal_version = SM50_SHADER_METAL_320;
    common.flags = static_cast<SM50_SHADER_FLAG>(0);

    native_abi.next = &common;
    native_abi.type = SM50_SHADER_DXMT12_NATIVE_DESCRIPTOR_ABI;
    native_abi.version = DXMT12_MTL4_SHADER_ABI_NATIVE_DESCRIPTOR_TABLE;
    native_abi.enabled = true;

    pixel.next = &native_abi;
    pixel.type = SM50_SHADER_PSO_PIXEL_SHADER;
    pixel.sample_mask = UINT_MAX;
    pixel.dual_source_blending = true;
    pixel.disable_depth_output = true;
    pixel.unorm_output_reg_mask = 0x3u;
  }

  SM50_SHADER_COMPILATION_ARGUMENT_DATA *head(bool pixel_shader) {
    if (pixel_shader) {
      return reinterpret_cast<SM50_SHADER_COMPILATION_ARGUMENT_DATA *>(&pixel);
    }
    return reinterpret_cast<SM50_SHADER_COMPILATION_ARGUMENT_DATA *>(
        &native_abi);
  }
};

struct ParsedShader {
  dxmt12_airconv_shader_t handle = {};
  bool dxil = false;

  ParsedShader() = default;
  ParsedShader(const ParsedShader &) = delete;
  ParsedShader &operator=(const ParsedShader &) = delete;

  ParsedShader(ParsedShader &&other) noexcept
      : handle(other.handle), dxil(other.dxil) {
    other.handle = {};
  }

  ParsedShader &operator=(ParsedShader &&other) noexcept {
    if (this == &other)
      return *this;
    reset();
    handle = other.handle;
    dxil = other.dxil;
    other.handle = {};
    return *this;
  }

  ~ParsedShader() { reset(); }

  void reset() {
    if (!handle)
      return;
    if (dxil)
      DXMT12DXILDestroy(handle);
    else
      DXMT12SM50Destroy(handle);
    handle = {};
  }
};

struct AirBitcode {
  dxmt12_airconv_bitcode_t handle = {};
  std::string function_name;

  AirBitcode() = default;
  AirBitcode(const AirBitcode &) = delete;
  AirBitcode &operator=(const AirBitcode &) = delete;

  AirBitcode(AirBitcode &&other) noexcept
      : handle(other.handle), function_name(std::move(other.function_name)) {
    other.handle = {};
  }

  AirBitcode &operator=(AirBitcode &&other) noexcept {
    if (this == &other)
      return *this;
    reset();
    handle = other.handle;
    function_name = std::move(other.function_name);
    other.handle = {};
    return *this;
  }

  ~AirBitcode() { reset(); }

  void reset() {
    if (handle)
      DXMT12SM50DestroyBitcode(handle);
    handle = {};
  }
};

struct MetalDevice {
  obj_handle_t handle = NULL_OBJECT_HANDLE;

  MetalDevice() = default;
  MetalDevice(const MetalDevice &) = delete;
  MetalDevice &operator=(const MetalDevice &) = delete;
  ~MetalDevice() {
    if (handle)
      NSObject_release(handle);
  }
};

std::string AirconvError(dxmt12_airconv_error_t error) {
  if (!error)
    return "airconv failed without an error object";
  std::string message(512, '\0');
  const auto size =
      DXMT12SM50GetErrorMessage(error, message.data(), message.size());
  message.resize(std::min(size, message.size()));
  DXMT12SM50FreeError(error);
  return message;
}

bool CompileDxbc(std::vector<std::uint8_t> *bytecode, std::string *error) {
  ID3DBlob *shader = nullptr;
  ID3DBlob *diagnostics = nullptr;
  const HRESULT hr = D3DCompile(
      kComputeHlsl, sizeof(kComputeHlsl) - 1, "d3d12_shader_benchmark.hlsl",
      nullptr, nullptr, "main", "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
      &shader, &diagnostics);
  if (FAILED(hr) || !shader) {
    std::ostringstream message;
    message << "D3DCompile failed with HRESULT 0x" << std::hex
            << static_cast<unsigned long>(hr);
    if (diagnostics && diagnostics->GetBufferPointer())
      message << ": "
              << static_cast<const char *>(diagnostics->GetBufferPointer());
    *error = message.str();
    if (diagnostics)
      diagnostics->Release();
    if (shader)
      shader->Release();
    return false;
  }

  const auto *begin =
      static_cast<const std::uint8_t *>(shader->GetBufferPointer());
  bytecode->assign(begin, begin + shader->GetBufferSize());
  shader->Release();
  if (diagnostics)
    diagnostics->Release();
  return true;
}

bool ParseShader(const void *bytes, std::size_t size, bool dxil,
                 ParsedShader *shader, std::string *error,
                 MTL_SHADER_REFLECTION *reflection = nullptr) {
  MTL_SHADER_REFLECTION local_reflection = {};
  dxmt12_airconv_error_t air_error = {};
  shader->dxil = dxil;
  const int result =
      dxil ? DXMT12DXILInitialize(bytes, size, &shader->handle,
                                  reflection ? reflection : &local_reflection,
                                  &air_error)
           : DXMT12SM50Initialize(bytes, size, &shader->handle,
                                  reflection ? reflection : &local_reflection,
                                  &air_error);
  if (!result && shader->handle)
    return true;
  *error = AirconvError(air_error);
  shader->reset();
  return false;
}

bool CompileAir(ParsedShader &shader, const char *function_name,
                AirBitcode *bitcode, std::string *error) {
  AirconvArguments arguments;
  dxmt12_airconv_error_t air_error = {};
  bitcode->function_name = function_name;
  const int result =
      shader.dxil
          ? DXMT12DXILCompile(shader.handle, arguments.head(true),
                              function_name, &bitcode->handle, &air_error)
          : DXMT12SM50Compile(shader.handle, arguments.head(false),
                              function_name, &bitcode->handle, &air_error);
  if (result || !bitcode->handle) {
    *error = AirconvError(air_error);
    bitcode->reset();
    return false;
  }

  SM50_COMPILED_BITCODE compiled = {};
  DXMT12SM50GetCompiledBitcode(bitcode->handle, &compiled);
  if (!compiled.Data || compiled.Size < 16) {
    *error = "airconv returned empty AIR bitcode";
    bitcode->reset();
    return false;
  }
  return true;
}

bool AcquireMetalDevice(MetalDevice *device, std::string *error) {
  const obj_handle_t devices = WMTCopyAllDevices();
  if (!devices) {
    *error = "WMTCopyAllDevices returned no array";
    return false;
  }
  if (NSArray_count(devices) == 0) {
    NSObject_release(devices);
    *error = "Metal reports no devices";
    return false;
  }
  device->handle = NSArray_object(devices, 0);
  if (device->handle)
    NSObject_retain(device->handle);
  NSObject_release(devices);
  if (!device->handle) {
    *error = "Metal device array contains a null device";
    return false;
  }
  return true;
}

bool MaterializeAir(obj_handle_t device, AirBitcode &bitcode,
                    std::string *error) {
  SM50_COMPILED_BITCODE compiled = {};
  DXMT12SM50GetCompiledBitcode(bitcode.handle, &compiled);
  const auto native_pointer = static_cast<std::uint64_t>(
      reinterpret_cast<std::uintptr_t>(compiled.Data));
  const obj_handle_t dispatch_data =
      DispatchData_alloc_init(native_pointer, compiled.Size);
  if (!dispatch_data) {
    *error = "failed to create dispatch data for AIR";
    return false;
  }

  obj_handle_t metal_error = NULL_OBJECT_HANDLE;
  const obj_handle_t library =
      MTLDevice_newLibrary(device, dispatch_data, &metal_error);
  NSObject_release(dispatch_data);
  if (!library) {
    if (metal_error)
      NSObject_release(metal_error);
    *error = "Metal rejected generated AIR bitcode";
    return false;
  }
  const obj_handle_t function =
      MTLLibrary_newFunction(library, bitcode.function_name.c_str());
  if (function)
    NSObject_release(function);
  NSObject_release(library);
  if (metal_error)
    NSObject_release(metal_error);
  if (!function) {
    *error = "generated AIR library does not contain the requested function";
    return false;
  }
  return true;
}

void BI_D3D12DxbcParse(benchmark::State &state) {
  std::vector<std::uint8_t> dxbc;
  std::string error;
  if (!CompileDxbc(&dxbc, &error)) {
    state.SkipWithError(error.c_str());
    return;
  }

  MTL_SHADER_REFLECTION reflection = {};
  ParsedShader precheck;
  if (!ParseShader(dxbc.data(), dxbc.size(), false, &precheck, &error,
                   &reflection) ||
      reflection.ThreadgroupSize[0] != 8 ||
      reflection.ThreadgroupSize[1] != 1 ||
      reflection.ThreadgroupSize[2] != 1) {
    if (error.empty())
      error = "DXBC parse precheck returned incorrect threadgroup reflection";
    state.SkipWithError(error.c_str());
    return;
  }

  for (auto _ : state) {
    for (unsigned int index = 0; index < kParseBatch; ++index) {
      ParsedShader shader;
      if (!ParseShader(dxbc.data(), dxbc.size(), false, &shader, &error)) {
        state.SkipWithError(error.c_str());
        return;
      }
      benchmark::DoNotOptimize(shader.handle);
    }
  }
  state.SetItemsProcessed(state.iterations() * kParseBatch);
  state.counters["bytecode_bytes"] = dxbc.size();
}

void BI_D3D12DxilParse(benchmark::State &state) {
  std::string error;
  ParsedShader precheck;
  if (!ParseShader(dxmt::test::kDualSourcePixelShader,
                   sizeof(dxmt::test::kDualSourcePixelShader), true, &precheck,
                   &error)) {
    state.SkipWithError(error.c_str());
    return;
  }

  for (auto _ : state) {
    for (unsigned int index = 0; index < kParseBatch; ++index) {
      ParsedShader shader;
      if (!ParseShader(dxmt::test::kDualSourcePixelShader,
                       sizeof(dxmt::test::kDualSourcePixelShader), true,
                       &shader, &error)) {
        state.SkipWithError(error.c_str());
        return;
      }
      benchmark::DoNotOptimize(shader.handle);
    }
  }
  state.SetItemsProcessed(state.iterations() * kParseBatch);
  state.counters["bytecode_bytes"] = sizeof(dxmt::test::kDualSourcePixelShader);
}

void RunAirGenerationBenchmark(benchmark::State &state, bool dxil,
                               unsigned int batch_size) {
  std::vector<std::uint8_t> dxbc;
  const void *bytes = dxmt::test::kDualSourcePixelShader;
  std::size_t size = sizeof(dxmt::test::kDualSourcePixelShader);
  std::string error;
  if (!dxil) {
    if (!CompileDxbc(&dxbc, &error)) {
      state.SkipWithError(error.c_str());
      return;
    }
    bytes = dxbc.data();
    size = dxbc.size();
  }

  ParsedShader shader;
  if (!ParseShader(bytes, size, dxil, &shader, &error)) {
    state.SkipWithError(error.c_str());
    return;
  }
  AirBitcode precheck;
  if (!CompileAir(shader, "benchmark_air_precheck", &precheck, &error)) {
    state.SkipWithError(error.c_str());
    return;
  }

  for (auto _ : state) {
    for (unsigned int index = 0; index < batch_size; ++index) {
      const std::string function_name =
          "benchmark_air_" + std::to_string(index);
      AirBitcode bitcode;
      if (!CompileAir(shader, function_name.c_str(), &bitcode, &error)) {
        state.SkipWithError(error.c_str());
        return;
      }
      benchmark::DoNotOptimize(bitcode.handle);
    }
  }
  state.SetItemsProcessed(state.iterations() * batch_size);
  state.counters["bytecode_bytes"] = size;
}

void BI_D3D12DxbcIrAirGeneration(benchmark::State &state) {
  RunAirGenerationBenchmark(state, false, kDxbcAirBatch);
}

void BI_D3D12DxilIrAirGeneration(benchmark::State &state) {
  RunAirGenerationBenchmark(state, true, kDxilAirBatch);
}

void BI_D3D12MetalLibraryCompile(benchmark::State &state) {
  std::vector<std::uint8_t> dxbc;
  std::string error;
  if (!CompileDxbc(&dxbc, &error)) {
    state.SkipWithError(error.c_str());
    return;
  }
  ParsedShader shader;
  if (!ParseShader(dxbc.data(), dxbc.size(), false, &shader, &error)) {
    state.SkipWithError(error.c_str());
    return;
  }
  MetalDevice device;
  if (!AcquireMetalDevice(&device, &error)) {
    state.SkipWithError(error.c_str());
    return;
  }

  std::vector<AirBitcode> bitcodes;
  bitcodes.reserve(kMetalBatch + 1);
  for (unsigned int index = 0; index <= kMetalBatch; ++index) {
    const std::string function_name =
        "benchmark_metal_" + std::to_string(index);
    AirBitcode bitcode;
    if (!CompileAir(shader, function_name.c_str(), &bitcode, &error)) {
      state.SkipWithError(error.c_str());
      return;
    }
    bitcodes.push_back(std::move(bitcode));
  }
  if (!MaterializeAir(device.handle, bitcodes.front(), &error)) {
    state.SkipWithError(error.c_str());
    return;
  }

  for (auto _ : state) {
    for (unsigned int index = 1; index <= kMetalBatch; ++index) {
      if (!MaterializeAir(device.handle, bitcodes[index], &error)) {
        state.SkipWithError(error.c_str());
        return;
      }
    }
  }
  state.SetItemsProcessed(state.iterations() * kMetalBatch);
}

struct CacheFixture {
  std::string unix_path;
  std::string windows_path;
  obj_handle_t writer = NULL_OBJECT_HANDLE;
  obj_handle_t reader = NULL_OBJECT_HANDLE;
  obj_handle_t data = NULL_OBJECT_HANDLE;
  std::array<std::uint8_t, 32> key = {};
  std::array<std::uint8_t, 256> payload = {};

  CacheFixture() {
    std::ostringstream name;
    name << "dxmt-shader-benchmark-" << GetCurrentProcessId() << "-"
         << GetTickCount64() << ".db";
    unix_path = "/tmp/" + name.str();
    windows_path = "Z:\\tmp\\" + name.str();
    for (std::size_t index = 0; index < key.size(); ++index)
      key[index] = static_cast<std::uint8_t>(index * 17u + 3u);
    for (std::size_t index = 0; index < payload.size(); ++index)
      payload[index] = static_cast<std::uint8_t>(index * 29u + 11u);
  }

  CacheFixture(const CacheFixture &) = delete;
  CacheFixture &operator=(const CacheFixture &) = delete;

  ~CacheFixture() {
    if (reader)
      NSObject_release(reader);
    if (writer)
      NSObject_release(writer);
    if (data)
      NSObject_release(data);
    DeleteFileA((windows_path + "-shm").c_str());
    DeleteFileA((windows_path + "-wal").c_str());
    DeleteFileA((windows_path + "-lock").c_str());
    DeleteFileA(windows_path.c_str());
  }

  bool Initialize(bool write_value, std::string *error) {
    writer = WMT4CacheWriter_alloc_init(unix_path.c_str(), kShaderCacheVersion);
    if (!writer) {
      *error = "failed to create persistent AIR cache writer";
      return false;
    }
    data = DispatchData_alloc_init(
        reinterpret_cast<std::uint64_t>(payload.data()), payload.size());
    if (!data) {
      *error = "failed to create persistent AIR cache payload";
      return false;
    }
    if (write_value)
      WMT4CacheWriter_set(writer, key.data(), key.size(), data);
    reader = WMT4CacheReader_alloc_init(unix_path.c_str(), kShaderCacheVersion);
    if (!reader) {
      *error = "failed to create persistent AIR cache reader";
      return false;
    }
    return true;
  }
};

bool ValidateCacheHit(CacheFixture &cache, std::string *error) {
  const obj_handle_t value =
      WMT4CacheReader_get(cache.reader, cache.key.data(), cache.key.size());
  if (!value) {
    *error = "persistent AIR cache precheck missed a populated key";
    return false;
  }
  std::array<std::uint8_t, 256> copied = {};
  const auto length = DispatchData_copy(value, copied.data(), copied.size());
  NSObject_release(value);
  if (length != cache.payload.size() || copied != cache.payload) {
    *error = "persistent AIR cache precheck returned corrupt payload bytes";
    return false;
  }
  return true;
}

void BI_D3D12PersistentAirCacheHit(benchmark::State &state) {
  CacheFixture cache;
  std::string error;
  if (!cache.Initialize(true, &error) || !ValidateCacheHit(cache, &error)) {
    state.SkipWithError(error.c_str());
    return;
  }

  for (auto _ : state) {
    for (unsigned int index = 0; index < kCacheBatch; ++index) {
      obj_handle_t value =
          WMT4CacheReader_get(cache.reader, cache.key.data(), cache.key.size());
      if (!value) {
        state.SkipWithError("persistent AIR cache hit became a miss");
        return;
      }
      benchmark::DoNotOptimize(value);
      NSObject_release(value);
    }
  }
  state.SetItemsProcessed(state.iterations() * kCacheBatch);
  state.counters["payload_bytes"] = cache.payload.size();
}

void BI_D3D12PersistentAirCacheMiss(benchmark::State &state) {
  CacheFixture cache;
  std::string error;
  if (!cache.Initialize(false, &error)) {
    state.SkipWithError(error.c_str());
    return;
  }
  if (WMT4CacheReader_get(cache.reader, cache.key.data(), cache.key.size())) {
    state.SkipWithError("persistent AIR cache precheck unexpectedly hit");
    return;
  }

  for (auto _ : state) {
    for (unsigned int index = 0; index < kCacheBatch; ++index) {
      auto miss_key = cache.key;
      miss_key[0] ^= static_cast<std::uint8_t>(index + 1u);
      obj_handle_t value =
          WMT4CacheReader_get(cache.reader, miss_key.data(), miss_key.size());
      if (value) {
        NSObject_release(value);
        state.SkipWithError("persistent AIR cache miss unexpectedly hit");
        return;
      }
      benchmark::DoNotOptimize(value);
    }
  }
  state.SetItemsProcessed(state.iterations() * kCacheBatch);
}

BENCHMARK(BI_D3D12DxbcParse)->Iterations(1)->UseRealTime();
BENCHMARK(BI_D3D12DxilParse)->Iterations(1)->UseRealTime();
BENCHMARK(BI_D3D12DxbcIrAirGeneration)->Iterations(1)->UseRealTime();
BENCHMARK(BI_D3D12DxilIrAirGeneration)->Iterations(1)->UseRealTime();
BENCHMARK(BI_D3D12MetalLibraryCompile)->Iterations(1)->UseRealTime();
BENCHMARK(BI_D3D12PersistentAirCacheHit)->Iterations(1)->UseRealTime();
BENCHMARK(BI_D3D12PersistentAirCacheMiss)->Iterations(1)->UseRealTime();

} // namespace
