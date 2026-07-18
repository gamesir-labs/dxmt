#include <dxmt_benchmark.hpp>

#include "d3d12_test_context.hpp"
#include "shaders/runtime_test_shaders.hpp"

#include <dxmt_test_shader.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

namespace {

using dxmt::test::ColorsMatch;
using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;
using clock_type = std::chrono::steady_clock;

constexpr UINT64 kBufferSize = 32ull * 1024ull * 1024ull;
constexpr UINT kBufferCopyCount = 8;
constexpr UINT kTextureSize = 2048;
constexpr UINT kTextureCopyCount = 8;
constexpr UINT kClearSize = 2048;
constexpr UINT kClearCount = 8;
constexpr UINT kDispatchCount = 4096;
constexpr UINT kDrawCount = 4096;
constexpr UINT kFillSize = 2048;
constexpr UINT kFillDrawCount = 16;
constexpr UINT kWarmupCommandCount = 8;
constexpr std::uint32_t kCopyPattern = 0x5a5a5a5au;

bool CheckDeviceHealth(benchmark::State &state, D3D12TestContext &context) {
  if (context.device()->GetDeviceRemovedReason() != S_OK) {
    state.SkipWithError("D3D12 device health check failed");
    return false;
  }
  return true;
}

bool ExecuteAndReset(benchmark::State &state, D3D12TestContext &context,
                     const char *error_message) {
  if (FAILED(context.ExecuteAndWait())) {
    state.SkipWithError(error_message);
    return false;
  }
  if (!CheckDeviceHealth(state, context))
    return false;
  if (FAILED(context.ResetCommandList())) {
    state.SkipWithError("command-list reset failed");
    return false;
  }
  return true;
}

bool SubmitClosedLists(benchmark::State &state, D3D12TestContext &context,
                       UINT list_count, ID3D12CommandList *const *lists) {
  context.queue()->ExecuteCommandLists(list_count, lists);
  if (FAILED(context.SignalAndWait())) {
    state.SkipWithError("GPU throughput submission failed");
    return false;
  }
  return CheckDeviceHealth(state, context);
}

bool SubmitClosedList(benchmark::State &state, D3D12TestContext &context) {
  ID3D12CommandList *lists[] = {context.list()};
  return SubmitClosedLists(state, context, 1, lists);
}

bool CloseMeasuredList(benchmark::State &state, D3D12TestContext &context) {
  if (FAILED(context.list()->Close())) {
    state.SkipWithError("GPU throughput command-list close failed");
    return false;
  }
  return true;
}

template <typename T>
bool ReadReadbackValue(benchmark::State &state, ID3D12Resource *resource,
                       UINT64 offset, T *value, const char *error_message) {
  void *mapping = nullptr;
  const D3D12_RANGE read_range = {static_cast<SIZE_T>(offset),
                                  static_cast<SIZE_T>(offset + sizeof(T))};
  if (FAILED(resource->Map(0, &read_range, &mapping)) || !mapping) {
    state.SkipWithError(error_message);
    return false;
  }
  std::memcpy(value, static_cast<const std::uint8_t *>(mapping) + offset,
              sizeof(T));
  const D3D12_RANGE no_write = {0, 0};
  resource->Unmap(0, &no_write);
  return true;
}

class GpuTimestampTimer {
public:
  bool Initialize(benchmark::State &state, D3D12TestContext &context) {
    D3D12_QUERY_HEAP_DESC desc = {};
    desc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    desc.Count = 2;
    if (FAILED(context.device()->CreateQueryHeap(&desc,
                                                 IID_PPV_ARGS(heap_.put())))) {
      state.SkipWithError("timestamp query heap creation failed");
      return false;
    }
    result_ = context.CreateBuffer(2 * sizeof(UINT64), D3D12_HEAP_TYPE_READBACK,
                                   D3D12_RESOURCE_FLAG_NONE,
                                   D3D12_RESOURCE_STATE_COPY_DEST);
    if (!result_ ||
        FAILED(context.queue()->GetTimestampFrequency(&frequency_)) ||
        !frequency_) {
      state.SkipWithError("timestamp query result setup failed");
      return false;
    }
    return true;
  }

  void Begin(ID3D12GraphicsCommandList *list) const {
    list->EndQuery(heap_.get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
  }

  void EndAndResolve(ID3D12GraphicsCommandList *list) const {
    list->EndQuery(heap_.get(), D3D12_QUERY_TYPE_TIMESTAMP, 1);
    list->ResolveQueryData(heap_.get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 2,
                           result_.get(), 0);
  }

  bool ReadSeconds(benchmark::State &state, double *seconds) const {
    std::array<UINT64, 2> timestamps = {};
    void *mapping = nullptr;
    const D3D12_RANGE read_range = {0, sizeof(timestamps)};
    if (FAILED(result_->Map(0, &read_range, &mapping)) || !mapping) {
      state.SkipWithError("timestamp result map failed");
      return false;
    }
    std::memcpy(timestamps.data(), mapping, sizeof(timestamps));
    const D3D12_RANGE no_write = {0, 0};
    result_->Unmap(0, &no_write);
    if (timestamps[1] <= timestamps[0]) {
      state.SkipWithError("GPU timestamps were not strictly increasing");
      return false;
    }
    *seconds = static_cast<double>(timestamps[1] - timestamps[0]) /
               static_cast<double>(frequency_);
    return true;
  }

private:
  ComPtr<ID3D12QueryHeap> heap_;
  ComPtr<ID3D12Resource> result_;
  UINT64 frequency_ = 0;
};

class BufferSampleReadback {
public:
  bool Initialize(benchmark::State &state, D3D12TestContext &context,
                  UINT64 source_size) {
    offsets_ = {0, (source_size / 2) & ~UINT64(3), source_size - 4};
    result_ = context.CreateBuffer(
        offsets_.size() * sizeof(std::uint32_t), D3D12_HEAP_TYPE_READBACK,
        D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
    if (!result_) {
      state.SkipWithError("buffer sample readback creation failed");
      return false;
    }
    return true;
  }

  void Record(ID3D12GraphicsCommandList *list, ID3D12Resource *source) const {
    for (UINT index = 0; index < offsets_.size(); ++index) {
      list->CopyBufferRegion(result_.get(), index * sizeof(std::uint32_t),
                             source, offsets_[index], sizeof(std::uint32_t));
    }
  }

  bool Validate(benchmark::State &state, std::uint32_t expected) const {
    std::array<std::uint32_t, 3> actual = {};
    void *mapping = nullptr;
    const D3D12_RANGE read_range = {0, sizeof(actual)};
    if (FAILED(result_->Map(0, &read_range, &mapping)) || !mapping) {
      state.SkipWithError("buffer sample readback map failed");
      return false;
    }
    std::memcpy(actual.data(), mapping, sizeof(actual));
    const D3D12_RANGE no_write = {0, 0};
    result_->Unmap(0, &no_write);
    if (!std::all_of(
            actual.begin(), actual.end(),
            [expected](std::uint32_t value) { return value == expected; })) {
      state.SkipWithError("buffer copy correctness precheck failed");
      return false;
    }
    return true;
  }

private:
  std::array<UINT64, 3> offsets_ = {};
  ComPtr<ID3D12Resource> result_;
};

class TexturePixelReadback {
public:
  bool Initialize(benchmark::State &state, D3D12TestContext &context,
                  DXGI_FORMAT format) {
    format_ = format;
    result_ = context.CreateBuffer(
        D3D12_TEXTURE_DATA_PITCH_ALIGNMENT, D3D12_HEAP_TYPE_READBACK,
        D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
    if (!result_) {
      state.SkipWithError("texture pixel readback creation failed");
      return false;
    }
    return true;
  }

  void Record(ID3D12GraphicsCommandList *list, ID3D12Resource *source, UINT x,
              UINT y) const {
    D3D12_TEXTURE_COPY_LOCATION destination = {};
    destination.pResource = result_.get();
    destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    destination.PlacedFootprint.Footprint.Format = format_;
    destination.PlacedFootprint.Footprint.Width = 1;
    destination.PlacedFootprint.Footprint.Height = 1;
    destination.PlacedFootprint.Footprint.Depth = 1;
    destination.PlacedFootprint.Footprint.RowPitch =
        D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;
    D3D12_TEXTURE_COPY_LOCATION source_location = {};
    source_location.pResource = source;
    source_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_BOX source_box = {x, y, 0, x + 1, y + 1, 1};
    list->CopyTextureRegion(&destination, 0, 0, 0, &source_location,
                            &source_box);
  }

  bool Validate(benchmark::State &state, std::uint32_t expected,
                UINT tolerance = 0) const {
    std::uint32_t actual = 0;
    if (!ReadReadbackValue(state, result_.get(), 0, &actual,
                           "texture pixel readback map failed"))
      return false;
    if (!ColorsMatch(actual, expected, tolerance)) {
      char message[128] = {};
      std::snprintf(message, sizeof(message),
                    "texture pixel correctness precheck failed: actual "
                    "0x%08x, expected 0x%08x",
                    actual, expected);
      state.SkipWithError(message);
      return false;
    }
    return true;
  }

private:
  ComPtr<ID3D12Resource> result_;
  DXGI_FORMAT format_ = DXGI_FORMAT_UNKNOWN;
};

bool FillUploadBuffer(benchmark::State &state, ID3D12Resource *upload,
                      UINT64 size, std::uint8_t value) {
  void *mapping = nullptr;
  const D3D12_RANGE no_read = {0, 0};
  if (FAILED(upload->Map(0, &no_read, &mapping)) || !mapping) {
    state.SkipWithError("upload buffer map failed");
    return false;
  }
  std::memset(mapping, value, static_cast<std::size_t>(size));
  const D3D12_RANGE written = {0, static_cast<SIZE_T>(size)};
  upload->Unmap(0, &written);
  return true;
}

void RecordBufferCopies(ID3D12GraphicsCommandList *list,
                        ID3D12Resource *destination, ID3D12Resource *source,
                        UINT copy_count, UINT64 size) {
  for (UINT index = 0; index < copy_count; ++index)
    list->CopyBufferRegion(destination, 0, source, 0, size);
}

void RecordBufferCopyBatch(
    ID3D12GraphicsCommandList *list,
    const std::vector<ComPtr<ID3D12Resource>> &destinations,
    ID3D12Resource *source, UINT64 size) {
  for (const auto &destination : destinations)
    list->CopyBufferRegion(destination.get(), 0, source, 0, size);
}

void RecordTextureCopies(ID3D12GraphicsCommandList *list,
                         ID3D12Resource *destination, ID3D12Resource *source,
                         UINT copy_count) {
  D3D12_TEXTURE_COPY_LOCATION source_location = {};
  source_location.pResource = source;
  source_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  D3D12_TEXTURE_COPY_LOCATION destination_location = {};
  destination_location.pResource = destination;
  destination_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  for (UINT index = 0; index < copy_count; ++index) {
    list->CopyTextureRegion(&destination_location, 0, 0, 0, &source_location,
                            nullptr);
  }
}

void RecordTextureCopyBatch(
    ID3D12GraphicsCommandList *list,
    const std::vector<ComPtr<ID3D12Resource>> &destinations,
    ID3D12Resource *source) {
  D3D12_TEXTURE_COPY_LOCATION source_location = {};
  source_location.pResource = source;
  source_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  for (const auto &destination : destinations) {
    D3D12_TEXTURE_COPY_LOCATION destination_location = {};
    destination_location.pResource = destination.get();
    destination_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    list->CopyTextureRegion(&destination_location, 0, 0, 0, &source_location,
                            nullptr);
  }
}

std::array<float, 4> ColorForIndex(UINT index) {
  return {static_cast<float>((index * 37u + 17u) & 0xffu) / 255.0f,
          static_cast<float>((index * 67u + 29u) & 0xffu) / 255.0f,
          static_cast<float>((index * 97u + 43u) & 0xffu) / 255.0f, 1.0f};
}

std::uint32_t PackColor(const std::array<float, 4> &color) {
  std::uint32_t packed = 0;
  for (UINT channel = 0; channel < color.size(); ++channel) {
    const auto value = static_cast<std::uint32_t>(
        std::lround(std::clamp(color[channel], 0.0f, 1.0f) * 255.0f));
    packed |= value << (channel * 8);
  }
  return packed;
}

void BindGraphicsWorkload(ID3D12GraphicsCommandList *list,
                          ID3D12RootSignature *root_signature,
                          ID3D12PipelineState *pipeline,
                          D3D12_CPU_DESCRIPTOR_HANDLE rtv, UINT width,
                          UINT height) {
  const D3D12_VIEWPORT viewport = {
      0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height),
      0.0f, 1.0f};
  const D3D12_RECT scissor = {0, 0, static_cast<LONG>(width),
                              static_cast<LONG>(height)};
  list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
  list->SetGraphicsRootSignature(root_signature);
  list->SetPipelineState(pipeline);
  list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  list->RSSetViewports(1, &viewport);
  list->RSSetScissorRects(1, &scissor);
}

void PublishByteThroughput(benchmark::State &state,
                           std::uint64_t bytes_per_iteration,
                           double measured_seconds) {
  state.SetBytesProcessed(static_cast<std::int64_t>(bytes_per_iteration) *
                          state.iterations());
  state.counters["bytes_per_iteration"] =
      static_cast<double>(bytes_per_iteration);
  state.counters["gpu_ns"] = measured_seconds * 1.0e9;
}

void PublishItemThroughput(benchmark::State &state, const char *counter_name,
                           std::uint64_t items_per_iteration,
                           double measured_seconds) {
  state.SetItemsProcessed(static_cast<std::int64_t>(items_per_iteration) *
                          state.iterations());
  state.counters[counter_name] = static_cast<double>(items_per_iteration);
  state.counters["gpu_ns"] = measured_seconds * 1.0e9;
}

void BI_D3D12BufferCopyBandwidth(benchmark::State &state) {
  D3D12TestContext context;
  if (FAILED(context.Initialize())) {
    state.SkipWithError("D3D12 initialization failed");
    return;
  }

  auto upload = context.CreateUploadBuffer(kBufferSize);
  auto source = context.CreateBuffer(kBufferSize, D3D12_HEAP_TYPE_DEFAULT,
                                     D3D12_RESOURCE_FLAG_NONE,
                                     D3D12_RESOURCE_STATE_COPY_DEST);
  std::vector<ComPtr<ID3D12Resource>> destinations;
  destinations.reserve(kBufferCopyCount);
  for (UINT index = 0; index < kBufferCopyCount; ++index) {
    destinations.push_back(context.CreateBuffer(
        kBufferSize, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
        D3D12_RESOURCE_STATE_COPY_DEST));
  }
  BufferSampleReadback first_samples;
  BufferSampleReadback last_samples;
  const bool destinations_valid =
      std::all_of(destinations.begin(), destinations.end(),
                  [](const auto &resource) { return !!resource; });
  if (!upload || !source || !destinations_valid ||
      !FillUploadBuffer(state, upload.get(), kBufferSize, 0x5a) ||
      !first_samples.Initialize(state, context, kBufferSize) ||
      !last_samples.Initialize(state, context, kBufferSize)) {
    if (!state.skipped())
      state.SkipWithError("buffer copy resource setup failed");
    return;
  }

  context.list()->CopyBufferRegion(source.get(), 0, upload.get(), 0,
                                   kBufferSize);
  D3D12TestContext::Transition(context.list(), source.get(),
                               D3D12_RESOURCE_STATE_COPY_DEST,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
  if (!ExecuteAndReset(state, context, "buffer source initialization failed"))
    return;

  // Representative work is deliberately executed before measurement. It is
  // also the correctness precheck for the source, copy path, and destination.
  RecordBufferCopies(context.list(), destinations.front().get(), source.get(),
                     kWarmupCommandCount, kBufferSize);
  D3D12TestContext::Transition(context.list(), destinations.front().get(),
                               D3D12_RESOURCE_STATE_COPY_DEST,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
  first_samples.Record(context.list(), destinations.front().get());
  D3D12TestContext::Transition(context.list(), destinations.front().get(),
                               D3D12_RESOURCE_STATE_COPY_SOURCE,
                               D3D12_RESOURCE_STATE_COPY_DEST);
  if (!ExecuteAndReset(state, context, "buffer copy warmup failed") ||
      !first_samples.Validate(state, kCopyPattern))
    return;

  GpuTimestampTimer timer;
  if (!timer.Initialize(state, context))
    return;
  timer.Begin(context.list());
  RecordBufferCopyBatch(context.list(), destinations, source.get(),
                        kBufferSize);
  timer.EndAndResolve(context.list());
  D3D12TestContext::Transition(context.list(), destinations.front().get(),
                               D3D12_RESOURCE_STATE_COPY_DEST,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
  first_samples.Record(context.list(), destinations.front().get());
  D3D12TestContext::Transition(context.list(), destinations.front().get(),
                               D3D12_RESOURCE_STATE_COPY_SOURCE,
                               D3D12_RESOURCE_STATE_COPY_DEST);
  D3D12TestContext::Transition(context.list(), destinations.back().get(),
                               D3D12_RESOURCE_STATE_COPY_DEST,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
  last_samples.Record(context.list(), destinations.back().get());
  D3D12TestContext::Transition(context.list(), destinations.back().get(),
                               D3D12_RESOURCE_STATE_COPY_SOURCE,
                               D3D12_RESOURCE_STATE_COPY_DEST);
  if (!CloseMeasuredList(state, context))
    return;

  double measured_seconds = 0.0;
  for (auto _ : state) {
    if (!SubmitClosedList(state, context) ||
        !timer.ReadSeconds(state, &measured_seconds) ||
        !first_samples.Validate(state, kCopyPattern) ||
        !last_samples.Validate(state, kCopyPattern))
      return;
    state.SetIterationTime(measured_seconds);
  }
  PublishByteThroughput(state, kBufferSize * kBufferCopyCount,
                        measured_seconds);
}

void BI_D3D12TextureCopyBandwidth(benchmark::State &state) {
  D3D12TestContext context;
  if (FAILED(context.Initialize())) {
    state.SkipWithError("D3D12 initialization failed");
    return;
  }

  constexpr DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
  constexpr UINT64 texture_bytes =
      UINT64(kTextureSize) * kTextureSize * sizeof(std::uint32_t);
  std::vector<std::uint32_t> source_data(UINT64(kTextureSize) * kTextureSize,
                                         kCopyPattern);
  auto source = context.CreateTexture2D(kTextureSize, kTextureSize, 1, format,
                                        D3D12_RESOURCE_FLAG_NONE,
                                        D3D12_RESOURCE_STATE_COPY_DEST);
  std::vector<ComPtr<ID3D12Resource>> destinations;
  destinations.reserve(kTextureCopyCount);
  for (UINT index = 0; index < kTextureCopyCount; ++index) {
    destinations.push_back(context.CreateTexture2D(
        kTextureSize, kTextureSize, 1, format, D3D12_RESOURCE_FLAG_NONE,
        D3D12_RESOURCE_STATE_COPY_DEST));
  }
  TexturePixelReadback first_pixel;
  TexturePixelReadback last_pixel;
  const bool destinations_valid =
      std::all_of(destinations.begin(), destinations.end(),
                  [](const auto &resource) { return !!resource; });
  if (!source || !destinations_valid ||
      !first_pixel.Initialize(state, context, format) ||
      !last_pixel.Initialize(state, context, format) ||
      FAILED(context.UploadTextureAndReset(
          source.get(), source_data.data(),
          UINT64(kTextureSize) * sizeof(std::uint32_t), texture_bytes))) {
    if (!state.skipped())
      state.SkipWithError("texture copy resource setup failed");
    return;
  }
  D3D12TestContext::Transition(context.list(), source.get(),
                               D3D12_RESOURCE_STATE_COPY_DEST,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
  if (!ExecuteAndReset(state, context, "texture source transition failed"))
    return;

  RecordTextureCopies(context.list(), destinations.front().get(), source.get(),
                      kWarmupCommandCount);
  D3D12TestContext::Transition(context.list(), destinations.front().get(),
                               D3D12_RESOURCE_STATE_COPY_DEST,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
  first_pixel.Record(context.list(), destinations.front().get(),
                     kTextureSize / 2, kTextureSize / 2);
  D3D12TestContext::Transition(context.list(), destinations.front().get(),
                               D3D12_RESOURCE_STATE_COPY_SOURCE,
                               D3D12_RESOURCE_STATE_COPY_DEST);
  if (!ExecuteAndReset(state, context, "texture copy warmup failed") ||
      !first_pixel.Validate(state, kCopyPattern))
    return;

  GpuTimestampTimer timer;
  if (!timer.Initialize(state, context))
    return;
  timer.Begin(context.list());
  RecordTextureCopyBatch(context.list(), destinations, source.get());
  timer.EndAndResolve(context.list());
  D3D12TestContext::Transition(context.list(), destinations.front().get(),
                               D3D12_RESOURCE_STATE_COPY_DEST,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
  first_pixel.Record(context.list(), destinations.front().get(),
                     kTextureSize / 2, kTextureSize / 2);
  D3D12TestContext::Transition(context.list(), destinations.front().get(),
                               D3D12_RESOURCE_STATE_COPY_SOURCE,
                               D3D12_RESOURCE_STATE_COPY_DEST);
  D3D12TestContext::Transition(context.list(), destinations.back().get(),
                               D3D12_RESOURCE_STATE_COPY_DEST,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
  last_pixel.Record(context.list(), destinations.back().get(), kTextureSize / 2,
                    kTextureSize / 2);
  D3D12TestContext::Transition(context.list(), destinations.back().get(),
                               D3D12_RESOURCE_STATE_COPY_SOURCE,
                               D3D12_RESOURCE_STATE_COPY_DEST);
  if (!CloseMeasuredList(state, context))
    return;

  double measured_seconds = 0.0;
  for (auto _ : state) {
    if (!SubmitClosedList(state, context) ||
        !timer.ReadSeconds(state, &measured_seconds) ||
        !first_pixel.Validate(state, kCopyPattern) ||
        !last_pixel.Validate(state, kCopyPattern))
      return;
    state.SetIterationTime(measured_seconds);
  }
  PublishByteThroughput(state, texture_bytes * kTextureCopyCount,
                        measured_seconds);
}

void BI_D3D12ClearRenderTargetThroughput(benchmark::State &state) {
  D3D12TestContext context;
  if (FAILED(context.Initialize())) {
    state.SkipWithError("D3D12 initialization failed");
    return;
  }

  constexpr DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
  std::vector<ComPtr<ID3D12Resource>> targets;
  targets.reserve(kClearCount);
  for (UINT index = 0; index < kClearCount; ++index) {
    targets.push_back(
        context.CreateTexture2D(kClearSize, kClearSize, 1, format,
                                D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
                                D3D12_RESOURCE_STATE_RENDER_TARGET));
  }
  auto rtv_heap = context.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
                                               kClearCount, false);
  TexturePixelReadback first_pixel;
  TexturePixelReadback last_pixel;
  const bool targets_valid =
      std::all_of(targets.begin(), targets.end(),
                  [](const auto &resource) { return !!resource; });
  if (!targets_valid || !rtv_heap ||
      !first_pixel.Initialize(state, context, format) ||
      !last_pixel.Initialize(state, context, format)) {
    if (!state.skipped())
      state.SkipWithError("clear throughput resource setup failed");
    return;
  }
  std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> rtvs(kClearCount);
  for (UINT index = 0; index < kClearCount; ++index) {
    rtvs[index] = context.CpuDescriptorHandle(rtv_heap.get(), index);
    context.device()->CreateRenderTargetView(targets[index].get(), nullptr,
                                             rtvs[index]);
  }

  const auto warmup_color = ColorForIndex(0);
  context.list()->ClearRenderTargetView(rtvs.front(), warmup_color.data(), 0,
                                        nullptr);
  D3D12TestContext::Transition(context.list(), targets.front().get(),
                               D3D12_RESOURCE_STATE_RENDER_TARGET,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
  first_pixel.Record(context.list(), targets.front().get(), kClearSize / 2,
                     kClearSize / 2);
  D3D12TestContext::Transition(context.list(), targets.front().get(),
                               D3D12_RESOURCE_STATE_COPY_SOURCE,
                               D3D12_RESOURCE_STATE_RENDER_TARGET);
  if (!ExecuteAndReset(state, context, "render-target clear warmup failed") ||
      !first_pixel.Validate(state, PackColor(warmup_color), 1))
    return;

  GpuTimestampTimer timer;
  if (!timer.Initialize(state, context))
    return;
  timer.Begin(context.list());
  for (UINT index = 0; index < kClearCount; ++index) {
    const auto color = ColorForIndex(index);
    context.list()->ClearRenderTargetView(rtvs[index], color.data(), 0,
                                          nullptr);
  }
  timer.EndAndResolve(context.list());
  D3D12TestContext::Transition(context.list(), targets.front().get(),
                               D3D12_RESOURCE_STATE_RENDER_TARGET,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
  first_pixel.Record(context.list(), targets.front().get(), kClearSize / 2,
                     kClearSize / 2);
  D3D12TestContext::Transition(context.list(), targets.front().get(),
                               D3D12_RESOURCE_STATE_COPY_SOURCE,
                               D3D12_RESOURCE_STATE_RENDER_TARGET);
  D3D12TestContext::Transition(context.list(), targets.back().get(),
                               D3D12_RESOURCE_STATE_RENDER_TARGET,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
  last_pixel.Record(context.list(), targets.back().get(), kClearSize / 2,
                    kClearSize / 2);
  D3D12TestContext::Transition(context.list(), targets.back().get(),
                               D3D12_RESOURCE_STATE_COPY_SOURCE,
                               D3D12_RESOURCE_STATE_RENDER_TARGET);
  if (!CloseMeasuredList(state, context))
    return;

  const auto first_expected = PackColor(ColorForIndex(0));
  const auto last_expected = PackColor(ColorForIndex(kClearCount - 1));
  double measured_seconds = 0.0;
  for (auto _ : state) {
    if (!SubmitClosedList(state, context) ||
        !timer.ReadSeconds(state, &measured_seconds) ||
        !first_pixel.Validate(state, first_expected, 1) ||
        !last_pixel.Validate(state, last_expected, 1))
      return;
    state.SetIterationTime(measured_seconds);
  }
  const UINT64 pixels = UINT64(kClearSize) * kClearSize * kClearCount;
  PublishItemThroughput(state, "pixels", pixels, measured_seconds);
  state.SetBytesProcessed(static_cast<std::int64_t>(pixels * 4) *
                          state.iterations());
}

void BI_D3D12ComputeDispatchThroughput(benchmark::State &state) {
  D3D12TestContext context;
  if (FAILED(context.Initialize())) {
    state.SkipWithError("D3D12 initialization failed");
    return;
  }

  const auto shader = CompileShader(R"hlsl(
RWStructuredBuffer<uint> counter : register(u0);

[numthreads(1, 1, 1)]
void main() {
  uint previous;
  InterlockedAdd(counter[0], 1, previous);
}
)hlsl",
                                    "cs_5_0");
  if (FAILED(shader.result) || !shader.bytecode) {
    state.SkipWithError("compute throughput shader compilation failed");
    return;
  }

  D3D12_ROOT_PARAMETER parameter = {};
  parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
  parameter.Descriptor.ShaderRegister = 0;
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  root_desc.NumParameters = 1;
  root_desc.pParameters = &parameter;
  auto root_signature = context.CreateRootSignature(root_desc);
  const D3D12_SHADER_BYTECODE bytecode = {shader.bytecode->GetBufferPointer(),
                                          shader.bytecode->GetBufferSize()};
  auto pipeline =
      root_signature
          ? context.CreateComputePipeline(root_signature.get(), bytecode)
          : ComPtr<ID3D12PipelineState>{};
  const std::uint32_t zero = 0;
  auto zero_upload =
      context.CreateUploadBuffer(sizeof(zero), &zero, sizeof(zero));
  auto output =
      context.CreateBuffer(sizeof(std::uint32_t), D3D12_HEAP_TYPE_DEFAULT,
                           D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                           D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  auto readback = context.CreateBuffer(
      sizeof(std::uint32_t), D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  if (!root_signature || !pipeline || !zero_upload || !output || !readback) {
    state.SkipWithError("compute throughput resource setup failed");
    return;
  }

  auto record_reset_and_bind = [&]() {
    D3D12TestContext::Transition(context.list(), output.get(),
                                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                 D3D12_RESOURCE_STATE_COPY_DEST);
    context.list()->CopyBufferRegion(output.get(), 0, zero_upload.get(), 0,
                                     sizeof(zero));
    D3D12TestContext::Transition(context.list(), output.get(),
                                 D3D12_RESOURCE_STATE_COPY_DEST,
                                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    context.list()->SetComputeRootSignature(root_signature.get());
    context.list()->SetPipelineState(pipeline.get());
    context.list()->SetComputeRootUnorderedAccessView(
        0, output->GetGPUVirtualAddress());
  };
  auto record_readback = [&]() {
    D3D12TestContext::Transition(context.list(), output.get(),
                                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                 D3D12_RESOURCE_STATE_COPY_SOURCE);
    context.list()->CopyBufferRegion(readback.get(), 0, output.get(), 0,
                                     sizeof(std::uint32_t));
    D3D12TestContext::Transition(context.list(), output.get(),
                                 D3D12_RESOURCE_STATE_COPY_SOURCE,
                                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  };

  record_reset_and_bind();
  for (UINT index = 0; index < kWarmupCommandCount; ++index)
    context.list()->Dispatch(1, 1, 1);
  record_readback();
  if (!ExecuteAndReset(state, context, "compute dispatch warmup failed"))
    return;
  std::uint32_t actual = 0;
  if (!ReadReadbackValue(state, readback.get(), 0, &actual,
                         "compute dispatch readback map failed") ||
      actual != kWarmupCommandCount) {
    if (!state.skipped())
      state.SkipWithError("compute dispatch correctness precheck failed");
    return;
  }

  GpuTimestampTimer timer;
  if (!timer.Initialize(state, context))
    return;
  record_reset_and_bind();
  timer.Begin(context.list());
  for (UINT index = 0; index < kDispatchCount; ++index)
    context.list()->Dispatch(1, 1, 1);
  timer.EndAndResolve(context.list());
  record_readback();
  if (!CloseMeasuredList(state, context))
    return;

  double measured_seconds = 0.0;
  for (auto _ : state) {
    if (!SubmitClosedList(state, context) ||
        !timer.ReadSeconds(state, &measured_seconds) ||
        !ReadReadbackValue(state, readback.get(), 0, &actual,
                           "compute dispatch readback map failed"))
      return;
    if (actual != kDispatchCount) {
      state.SkipWithError("compute dispatch correctness check failed");
      return;
    }
    state.SetIterationTime(measured_seconds);
  }
  PublishItemThroughput(state, "dispatches", kDispatchCount, measured_seconds);
}

void BI_D3D12DrawCallThroughput(benchmark::State &state) {
  D3D12TestContext context;
  if (FAILED(context.Initialize())) {
    state.SkipWithError("D3D12 initialization failed");
    return;
  }

  const auto pixel_shader = CompileShader(R"hlsl(
RWStructuredBuffer<uint> counter : register(u1);

float4 main() : SV_Target {
  uint previous;
  InterlockedAdd(counter[0], 1, previous);
  return float4(0.25, 0.5, 0.75, 1.0);
}
)hlsl",
                                          "ps_5_0");
  if (FAILED(pixel_shader.result) || !pixel_shader.bytecode) {
    state.SkipWithError("draw throughput shader compilation failed");
    return;
  }

  D3D12_ROOT_PARAMETER parameter = {};
  parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
  parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  // Pixel UAV registers overlap output-merger slots. With RTV slot 0 bound,
  // the first legal UAV register is u1.
  parameter.Descriptor.ShaderRegister = 1;
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  root_desc.NumParameters = 1;
  root_desc.pParameters = &parameter;
  root_desc.Flags =
      D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
  auto root_signature = context.CreateRootSignature(root_desc);
  const D3D12_SHADER_BYTECODE bytecode = {
      pixel_shader.bytecode->GetBufferPointer(),
      pixel_shader.bytecode->GetBufferSize()};
  auto pipeline =
      root_signature
          ? context.CreateGraphicsPipeline(root_signature.get(),
                                           DXGI_FORMAT_R8G8B8A8_UNORM, bytecode)
          : ComPtr<ID3D12PipelineState>{};
  auto target = context.CreateTexture2D(1, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
                                        D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
                                        D3D12_RESOURCE_STATE_RENDER_TARGET);
  auto rtv_heap =
      context.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
  const std::uint32_t zero = 0;
  auto zero_upload =
      context.CreateUploadBuffer(sizeof(zero), &zero, sizeof(zero));
  auto counter =
      context.CreateBuffer(sizeof(std::uint32_t), D3D12_HEAP_TYPE_DEFAULT,
                           D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                           D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  auto readback = context.CreateBuffer(
      sizeof(std::uint32_t), D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  if (!root_signature || !pipeline || !target || !rtv_heap || !zero_upload ||
      !counter || !readback) {
    state.SkipWithError("draw throughput resource setup failed");
    return;
  }
  const auto rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
  context.device()->CreateRenderTargetView(target.get(), nullptr, rtv);

  auto record_reset_and_bind = [&]() {
    D3D12TestContext::Transition(context.list(), counter.get(),
                                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                 D3D12_RESOURCE_STATE_COPY_DEST);
    context.list()->CopyBufferRegion(counter.get(), 0, zero_upload.get(), 0,
                                     sizeof(zero));
    D3D12TestContext::Transition(context.list(), counter.get(),
                                 D3D12_RESOURCE_STATE_COPY_DEST,
                                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    BindGraphicsWorkload(context.list(), root_signature.get(), pipeline.get(),
                         rtv, 1, 1);
    context.list()->SetGraphicsRootUnorderedAccessView(
        0, counter->GetGPUVirtualAddress());
  };
  auto record_readback = [&]() {
    D3D12TestContext::Transition(context.list(), counter.get(),
                                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                 D3D12_RESOURCE_STATE_COPY_SOURCE);
    context.list()->CopyBufferRegion(readback.get(), 0, counter.get(), 0,
                                     sizeof(std::uint32_t));
    D3D12TestContext::Transition(context.list(), counter.get(),
                                 D3D12_RESOURCE_STATE_COPY_SOURCE,
                                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  };

  record_reset_and_bind();
  for (UINT index = 0; index < kWarmupCommandCount; ++index)
    context.list()->DrawInstanced(3, 1, 0, 0);
  record_readback();
  if (!ExecuteAndReset(state, context, "draw-call warmup failed"))
    return;
  std::uint32_t actual = 0;
  if (!ReadReadbackValue(state, readback.get(), 0, &actual,
                         "draw-call readback map failed") ||
      actual != kWarmupCommandCount) {
    if (!state.skipped())
      state.SkipWithError("draw-call correctness precheck failed");
    return;
  }

  GpuTimestampTimer timer;
  if (!timer.Initialize(state, context))
    return;
  record_reset_and_bind();
  timer.Begin(context.list());
  for (UINT index = 0; index < kDrawCount; ++index)
    context.list()->DrawInstanced(3, 1, 0, 0);
  timer.EndAndResolve(context.list());
  record_readback();
  if (!CloseMeasuredList(state, context))
    return;

  double measured_seconds = 0.0;
  for (auto _ : state) {
    if (!SubmitClosedList(state, context) ||
        !timer.ReadSeconds(state, &measured_seconds) ||
        !ReadReadbackValue(state, readback.get(), 0, &actual,
                           "draw-call readback map failed"))
      return;
    if (actual != kDrawCount) {
      state.SkipWithError("draw-call correctness check failed");
      return;
    }
    state.SetIterationTime(measured_seconds);
  }
  PublishItemThroughput(state, "draw_calls", kDrawCount, measured_seconds);
}

void BI_D3D12RenderTargetFill(benchmark::State &state) {
  D3D12TestContext context;
  if (FAILED(context.Initialize())) {
    state.SkipWithError("D3D12 initialization failed");
    return;
  }

  const auto pixel_shader = CompileShader(R"hlsl(
float4 main() : SV_Target {
  return float4(0.25, 0.5, 0.75, 1.0);
}
)hlsl",
                                          "ps_5_0");
  if (FAILED(pixel_shader.result) || !pixel_shader.bytecode) {
    state.SkipWithError("render-target fill shader compilation failed");
    return;
  }

  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  root_desc.Flags =
      D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
  auto root_signature = context.CreateRootSignature(root_desc);
  const D3D12_SHADER_BYTECODE bytecode = {
      pixel_shader.bytecode->GetBufferPointer(),
      pixel_shader.bytecode->GetBufferSize()};
  auto pipeline =
      root_signature
          ? context.CreateGraphicsPipeline(root_signature.get(),
                                           DXGI_FORMAT_R8G8B8A8_UNORM, bytecode)
          : ComPtr<ID3D12PipelineState>{};
  auto target = context.CreateTexture2D(kFillSize, kFillSize, 1,
                                        DXGI_FORMAT_R8G8B8A8_UNORM,
                                        D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
                                        D3D12_RESOURCE_STATE_RENDER_TARGET);
  auto rtv_heap =
      context.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
  TexturePixelReadback pixel;
  if (!root_signature || !pipeline || !target || !rtv_heap ||
      !pixel.Initialize(state, context, DXGI_FORMAT_R8G8B8A8_UNORM)) {
    if (!state.skipped())
      state.SkipWithError("render-target fill resource setup failed");
    return;
  }
  const auto rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
  context.device()->CreateRenderTargetView(target.get(), nullptr, rtv);

  constexpr std::array<float, 4> fill_color = {0.25f, 0.5f, 0.75f, 1.0f};
  constexpr std::array<float, 4> sentinel_color = {1.0f, 0.0f, 0.0f, 1.0f};
  BindGraphicsWorkload(context.list(), root_signature.get(), pipeline.get(),
                       rtv, kFillSize, kFillSize);
  context.list()->ClearRenderTargetView(rtv, sentinel_color.data(), 0, nullptr);
  context.list()->DrawInstanced(3, 1, 0, 0);
  D3D12TestContext::Transition(context.list(), target.get(),
                               D3D12_RESOURCE_STATE_RENDER_TARGET,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
  pixel.Record(context.list(), target.get(), kFillSize / 2, kFillSize / 2);
  D3D12TestContext::Transition(context.list(), target.get(),
                               D3D12_RESOURCE_STATE_COPY_SOURCE,
                               D3D12_RESOURCE_STATE_RENDER_TARGET);
  if (!ExecuteAndReset(state, context, "render-target fill warmup failed") ||
      !pixel.Validate(state, PackColor(fill_color), 1))
    return;

  // Timestamp markers currently suppress the observable color attachment
  // store for an otherwise valid RT-only draw segment. Keep this case honest
  // with end-to-end queue timing until that product limitation is removed.
  context.list()->ClearRenderTargetView(rtv, sentinel_color.data(), 0, nullptr);
  BindGraphicsWorkload(context.list(), root_signature.get(), pipeline.get(),
                       rtv, kFillSize, kFillSize);
  for (UINT index = 0; index < kFillDrawCount; ++index)
    context.list()->DrawInstanced(3, 1, 0, 0);
  D3D12TestContext::Transition(context.list(), target.get(),
                               D3D12_RESOURCE_STATE_RENDER_TARGET,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
  pixel.Record(context.list(), target.get(), kFillSize / 2, kFillSize / 2);
  D3D12TestContext::Transition(context.list(), target.get(),
                               D3D12_RESOURCE_STATE_COPY_SOURCE,
                               D3D12_RESOURCE_STATE_RENDER_TARGET);
  if (!CloseMeasuredList(state, context))
    return;

  const auto expected = PackColor(fill_color);
  double measured_seconds = 0.0;
  for (auto _ : state) {
    const auto begin = clock_type::now();
    if (!SubmitClosedList(state, context))
      return;
    const auto end = clock_type::now();
    measured_seconds = std::chrono::duration<double>(end - begin).count();
    if (!pixel.Validate(state, expected, 1))
      return;
    state.SetIterationTime(measured_seconds);
  }
  const UINT64 pixels = UINT64(kFillSize) * kFillSize * kFillDrawCount;
  state.SetItemsProcessed(static_cast<std::int64_t>(pixels) *
                          state.iterations());
  state.counters["pixels"] = static_cast<double>(pixels);
  state.counters["end_to_end_ns"] = measured_seconds * 1.0e9;
  state.SetBytesProcessed(static_cast<std::int64_t>(pixels * 4) *
                          state.iterations());
}

BENCHMARK(BI_D3D12BufferCopyBandwidth)->Iterations(1)->UseManualTime();
BENCHMARK(BI_D3D12TextureCopyBandwidth)->Iterations(1)->UseManualTime();
BENCHMARK(BI_D3D12ClearRenderTargetThroughput)->Iterations(1)->UseManualTime();
BENCHMARK(BI_D3D12ComputeDispatchThroughput)->Iterations(1)->UseManualTime();
BENCHMARK(BI_D3D12DrawCallThroughput)->Iterations(1)->UseManualTime();
BENCHMARK(BI_D3D12RenderTargetFill)->Iterations(1)->UseManualTime();

} // namespace
