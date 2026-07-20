#pragma once

#include <d3d12.h>

#include <cstdint>

namespace dxmt::d3d12::test {

// Test-only command-list controls carried through ID3D12Object private data.
// These controls are deliberately per-list so path tests do not depend on
// process-global environment variables or leak state into unrelated tests.
enum class ExecutionPathMode : std::uint32_t {
  // Normal compiler selection. Runtime packet fallbacks remain observable in
  // ExecutionPathStats.
  Auto = 0,
  // Every compilable Draw/DrawIndexed/Dispatch record must select a compiled
  // packet at Close. Non-packet records (copy, barrier, query, and similar)
  // retain their ordinary range replay, and a queue-time packet fallback is
  // reported by native_requirement_satisfied == 0 after completion.
  NativeCompiled = 1,
  // Bypass packet construction, native descriptor materialization, and
  // payload finalization; replay the complete record stream as fallback.
  Fallback = 2,
};

enum ExecutionPathFlags : std::uint32_t {
  ExecutionPathFlagNone = 0,
  ExecutionPathFlagInjectEmptyNativeSegment = 1u << 0,
  ExecutionPathFlagInjectEmptyFallbackSegment = 1u << 1,
};

// Ordered test-only view of the segments selected by the compiled command
// builder.  Keep this independent from the implementation enum so tests can
// inspect a stable ABI without including private command-list headers.
enum class ExecutionPathSegmentKind : std::uint32_t {
  Graphics = 0,
  Compute = 1,
  Fallback = 2,
};

inline constexpr std::uint32_t kExecutionPathMaxTracedSegments = 64;

struct ExecutionPathConfig {
  std::uint32_t struct_size = sizeof(ExecutionPathConfig);
  ExecutionPathMode mode = ExecutionPathMode::Auto;
  std::uint32_t flags = ExecutionPathFlagNone;
};

struct ExecutionPathStats {
  std::uint32_t struct_size = sizeof(ExecutionPathStats);
  ExecutionPathMode mode = ExecutionPathMode::Auto;
  std::uint32_t record_count = 0;
  std::uint32_t work_record_count = 0;
  std::uint32_t compiled_work_record_count = 0;
  std::uint32_t native_requirement_satisfied = 0;
  std::uint32_t graphics_segments = 0;
  std::uint32_t compute_segments = 0;
  std::uint32_t fallback_segments = 0;
  std::uint32_t empty_native_segments = 0;
  std::uint32_t empty_fallback_segments = 0;
  std::uint32_t selected_graphics_packets = 0;
  std::uint32_t selected_compute_packets = 0;
  std::uint32_t retained_graphics_packets = 0;
  std::uint32_t retained_compute_packets = 0;
  std::uint32_t has_native_root_base_buffer = 0;
  std::uint32_t replayed_graphics_packets = 0;
  std::uint32_t replayed_compute_packets = 0;
  std::uint32_t replayed_fallback_ranges = 0;
  std::uint32_t replayed_fallback_records = 0;
  std::uint32_t replayed_compiled_packet_fallbacks = 0;
  std::uint32_t replayed_empty_native_segments = 0;
  std::uint32_t replayed_empty_fallback_segments = 0;
  // segment_count always reports the complete count. traced_segment_count is
  // capped at kExecutionPathMaxTracedSegments; the parallel arrays preserve
  // builder order and make N/F boundary topology directly testable.
  std::uint32_t segment_count = 0;
  std::uint32_t traced_segment_count = 0;
  ExecutionPathSegmentKind
      segment_kinds[kExecutionPathMaxTracedSegments] = {};
  std::uint32_t
      segment_first_record_indices[kExecutionPathMaxTracedSegments] = {};
  std::uint32_t segment_record_counts[kExecutionPathMaxTracedSegments] = {};
};

// Marks an already-published shader-visible descriptor slot as requiring one
// encode-time mirror repair. This lets the residency tests exercise the exact
// deferred typed-buffer path without relying on scheduling races.
struct DescriptorHeapSlotRepairConfig {
  std::uint32_t struct_size = sizeof(DescriptorHeapSlotRepairConfig);
  std::uint32_t slot = 0;
};

// Read-only queue residency accounting exposed through ID3D12Device private
// data. The values are sampled under the queue's residency lock.
struct PersistentResidencyStats {
  std::uint32_t struct_size = sizeof(PersistentResidencyStats);
  std::uint32_t entry_count = 0;
  std::uint64_t total_ref_count = 0;
};

// Read-only identity of the immutable native artifact backing a public PSO.
// Public ID3D12PipelineState objects remain distinct even when this value is
// shared. The address is diagnostic only and must never be dereferenced.
struct PipelineNativeArtifactIdentity {
  std::uint32_t struct_size = sizeof(PipelineNativeArtifactIdentity);
  std::uintptr_t artifact = 0;
};

struct PipelineNativeArtifactCacheStats {
  std::uint32_t struct_size = sizeof(PipelineNativeArtifactCacheStats);
  std::uint64_t hits = 0;
  std::uint64_t misses = 0;
  std::uint64_t waits = 0;
  std::uint64_t compiles = 0;
  std::uint64_t compile_failures = 0;
};

inline constexpr GUID kExecutionPathConfigGuid = {
    0x6ca960e8,
    0xe87f,
    0x45a8,
    {0x9f, 0x5e, 0x2a, 0x42, 0x2e, 0xf9, 0xc3, 0x8d}};

inline constexpr GUID kExecutionPathStatsGuid = {
    0x52cbcfab,
    0xf833,
    0x412e,
    {0x89, 0xcb, 0xd0, 0xa7, 0x8f, 0x35, 0xef, 0x1e}};

inline constexpr GUID kDescriptorHeapSlotRepairGuid = {
    0x6389fa37,
    0x0efe,
    0x4725,
    {0xab, 0xd4, 0xe5, 0xc7, 0xad, 0x78, 0x0e, 0xe8}};

inline constexpr GUID kPersistentResidencyStatsGuid = {
    0x16d2bca8,
    0x0347,
    0x407f,
    {0xaa, 0x72, 0x04, 0x5f, 0x65, 0xed, 0xb7, 0x90}};

inline constexpr GUID kPipelineNativeArtifactIdentityGuid = {
    0x5e486ca2,
    0x0362,
    0x4af9,
    {0x92, 0x94, 0x2d, 0x8c, 0x39, 0xe2, 0x25, 0x67}};

inline constexpr GUID kPipelineNativeArtifactCacheStatsGuid = {
    0x870fe3ef,
    0xe4c0,
    0x44e6,
    {0xa6, 0xdc, 0xf9, 0x71, 0x83, 0xb1, 0x48, 0x31}};

} // namespace dxmt::d3d12::test
