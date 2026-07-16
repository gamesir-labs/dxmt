#pragma once

#include "d3d12_command_allocator.hpp"
#include "d3d12_descriptor_heap.hpp"
#include "d3d12_interfaces.hpp"
#include "d3d12_pipeline.hpp"
#include "com/com_pointer.hpp"
#include "dxmt_statistics.hpp"
#include <d3d12.h>
#include <array>
#include <memory>
#include <optional>
#include <unordered_map>
#include <variant>
#include <vector>

namespace dxmt::d3d12 {

struct StoredTextureCopyLocation {
  Com<ID3D12Resource> resource;
  D3D12_TEXTURE_COPY_TYPE type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT placed_footprint = {};
  UINT subresource_index = 0;
};

struct StoredResourceBarrier {
  D3D12_RESOURCE_BARRIER barrier = {};
  Com<ID3D12Resource> resource;
  Com<ID3D12Resource> resource_before;
  Com<ID3D12Resource> resource_after;
};

struct DrawInstancedRecord {
  UINT vertex_count_per_instance = 0;
  UINT instance_count = 0;
  UINT start_vertex_location = 0;
  UINT start_instance_location = 0;
};

struct DrawIndexedInstancedRecord {
  UINT index_count_per_instance = 0;
  UINT instance_count = 0;
  UINT start_index_location = 0;
  INT base_vertex_location = 0;
  UINT start_instance_location = 0;
};

struct DispatchRecord {
  UINT x = 0;
  UINT y = 0;
  UINT z = 0;
};

struct PipelineStateRecord {
  Com<ID3D12PipelineState> pipeline_state;
};

struct ClearStateRecord {
  Com<ID3D12PipelineState> pipeline_state;
};

struct CopyBufferRegionRecord {
  Com<ID3D12Resource> dst;
  UINT64 dst_offset = 0;
  Com<ID3D12Resource> src;
  UINT64 src_offset = 0;
  UINT64 byte_count = 0;
};

struct CopyTextureRegionRecord {
  StoredTextureCopyLocation dst;
  UINT dst_x = 0;
  UINT dst_y = 0;
  UINT dst_z = 0;
  StoredTextureCopyLocation src;
  std::optional<D3D12_BOX> src_box;
};

struct CopyResourceRecord {
  Com<ID3D12Resource> dst;
  Com<ID3D12Resource> src;
};

struct CopyTilesRecord {
  Com<ID3D12Resource> tiled_resource;
  D3D12_TILED_RESOURCE_COORDINATE start = {};
  D3D12_TILE_REGION_SIZE size = {};
  Com<ID3D12Resource> buffer;
  UINT64 buffer_offset = 0;
  D3D12_TILE_COPY_FLAGS flags = D3D12_TILE_COPY_FLAG_NONE;
};

struct ResolveSubresourceRecord {
  Com<ID3D12Resource> dst;
  UINT dst_subresource = 0;
  UINT dst_x = 0;
  UINT dst_y = 0;
  Com<ID3D12Resource> src;
  UINT src_subresource = 0;
  std::optional<D3D12_RECT> src_rect;
  DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
  D3D12_RESOLVE_MODE mode = D3D12_RESOLVE_MODE_AVERAGE;
};

struct PendingRenderPassResolve {
  Com<ID3D12Resource> src;
  Com<ID3D12Resource> dst;
  UINT src_subresource = 0;
  UINT dst_subresource = 0;
  UINT dst_x = 0;
  UINT dst_y = 0;
  std::optional<D3D12_RECT> src_rect;
  DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
  D3D12_RESOLVE_MODE mode = D3D12_RESOLVE_MODE_AVERAGE;
};

struct WriteBufferImmediateRecord {
  std::vector<D3D12_WRITEBUFFERIMMEDIATE_PARAMETER> parameters;
  std::vector<D3D12_WRITEBUFFERIMMEDIATE_MODE> modes;
};

struct ResourceBarrierRecord {
  std::vector<StoredResourceBarrier> barriers;
};

struct ClearRenderTargetRecord {
  DescriptorRecord descriptor;
  std::array<FLOAT, 4> color = {};
  std::vector<D3D12_RECT> rects;
};

struct ClearDepthStencilRecord {
  DescriptorRecord descriptor;
  D3D12_CLEAR_FLAGS flags = D3D12_CLEAR_FLAG_DEPTH;
  FLOAT depth = 1.0f;
  UINT8 stencil = 0;
  std::vector<D3D12_RECT> rects;
};

struct ClearUnorderedAccessRecord {
  DescriptorRecord descriptor;
  Com<ID3D12Resource> resource;
  std::array<UINT, 4> uint_values = {};
  std::array<FLOAT, 4> float_values = {};
  bool integer = false;
  std::vector<D3D12_RECT> rects;
};

struct DiscardResourceRecord {
  Com<ID3D12Resource> resource;
  std::vector<D3D12_RECT> rects;
  UINT first_subresource = 0;
  UINT subresource_count = 0;
};

struct ViewportRecord {
  std::vector<D3D12_VIEWPORT> viewports;
};

struct ScissorRecord {
  std::vector<D3D12_RECT> rects;
};

struct BlendFactorRecord {
  std::array<FLOAT, 4> blend_factor = {1.0f, 1.0f, 1.0f, 1.0f};
};

struct StencilRefRecord {
  UINT stencil_ref = 0;
};

struct PrimitiveTopologyRecord {
  D3D12_PRIMITIVE_TOPOLOGY topology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
};

struct RenderTargetsRecord {
  std::vector<DescriptorRecord> render_targets;
  std::optional<DescriptorRecord> depth_stencil;
};

struct VertexBuffersRecord {
  UINT start_slot = 0;
  UINT view_count = 0;
  std::vector<D3D12_VERTEX_BUFFER_VIEW> views;
};

struct IndexBufferRecord {
  std::optional<D3D12_INDEX_BUFFER_VIEW> view;
};

struct RootSignatureRecord {
  bool compute = false;
  Com<ID3D12RootSignature> root_signature;
};

struct DescriptorHeapsRecord {
  std::vector<Com<ID3D12DescriptorHeap>> heaps;
};

struct RootDescriptorTableRecord {
  bool compute = false;
  UINT root_parameter_index = 0;
  D3D12_GPU_DESCRIPTOR_HANDLE base_descriptor = {};
};

struct RootConstantsRecord {
  bool compute = false;
  UINT root_parameter_index = 0;
  UINT dst_offset = 0;
  std::vector<UINT> values;
};

struct RootDescriptorRecord {
  bool compute = false;
  D3D12_ROOT_PARAMETER_TYPE parameter_type = D3D12_ROOT_PARAMETER_TYPE_CBV;
  UINT root_parameter_index = 0;
  D3D12_GPU_VIRTUAL_ADDRESS address = 0;
};

struct BeginQueryRecord {
  Com<ID3D12QueryHeap> heap;
  D3D12_QUERY_TYPE type = D3D12_QUERY_TYPE_OCCLUSION;
  UINT index = 0;
};

struct EndQueryRecord {
  Com<ID3D12QueryHeap> heap;
  D3D12_QUERY_TYPE type = D3D12_QUERY_TYPE_OCCLUSION;
  UINT index = 0;
};

struct ResolveQueryDataRecord {
  uintptr_t command_list_identity = 0;
  Com<ID3D12QueryHeap> heap;
  D3D12_QUERY_TYPE type = D3D12_QUERY_TYPE_OCCLUSION;
  UINT start_index = 0;
  UINT query_count = 0;
  Com<ID3D12Resource> dst_buffer;
  UINT64 dst_buffer_offset = 0;
};

struct PredicationRecord {
  Com<ID3D12Resource> buffer;
  UINT64 buffer_offset = 0;
  D3D12_PREDICATION_OP operation = D3D12_PREDICATION_OP_EQUAL_ZERO;
};

struct ExecuteIndirectRecord {
  Com<ID3D12CommandSignature> command_signature;
  UINT max_command_count = 0;
  Com<ID3D12Resource> arg_buffer;
  UINT64 arg_buffer_offset = 0;
  Com<ID3D12Resource> count_buffer;
  UINT64 count_buffer_offset = 0;
};

struct TemporalUpscaleRecord {
  UINT input_content_width = 0;
  UINT input_content_height = 0;
  UINT motion_vector_width = 0;
  UINT motion_vector_height = 0;
  BOOL auto_exposure = FALSE;
  BOOL in_reset = FALSE;
  BOOL depth_reversed = FALSE;
  BOOL motion_vector_in_display_res = FALSE;
  Com<ID3D12Resource> color;
  Com<ID3D12Resource> depth;
  Com<ID3D12Resource> motion_vector;
  Com<ID3D12Resource> output;
  FLOAT motion_vector_scale_x = 1.0f;
  FLOAT motion_vector_scale_y = 1.0f;
  FLOAT pre_exposure = 0.0f;
  Com<ID3D12Resource> exposure_texture;
  FLOAT jitter_offset_x = 0.0f;
  FLOAT jitter_offset_y = 0.0f;
};

using CommandRecordPayload = std::variant<
    DrawInstancedRecord, DrawIndexedInstancedRecord, DispatchRecord,
    PipelineStateRecord, ClearStateRecord, CopyBufferRegionRecord,
    CopyTextureRegionRecord, CopyResourceRecord, CopyTilesRecord,
    ResolveSubresourceRecord,
    ResourceBarrierRecord, ClearRenderTargetRecord, ClearDepthStencilRecord,
    ClearUnorderedAccessRecord, DiscardResourceRecord, ViewportRecord,
    ScissorRecord, BlendFactorRecord, StencilRefRecord, PrimitiveTopologyRecord,
    RenderTargetsRecord, VertexBuffersRecord, IndexBufferRecord,
    RootSignatureRecord, DescriptorHeapsRecord, RootDescriptorTableRecord,
    RootConstantsRecord, RootDescriptorRecord, BeginQueryRecord,
    EndQueryRecord, ResolveQueryDataRecord, PredicationRecord,
    WriteBufferImmediateRecord,
    ExecuteIndirectRecord, TemporalUpscaleRecord>;

struct CommandRecord {
  std::uint64_t d3d_sequence = 0;
  CommandRecordPayload payload;
};

enum class CompiledCommandSegmentKind {
  Graphics,
  Compute,
  Fallback,
};

enum class CompiledCommandFallbackReason {
  None,
  ConservativeCompiler,
  LegacyPipelineState,
  NonBindlessPipelineState,
  GeometryPipeline,
  TessellationPipeline,
  MissingPipelineState,
  MissingRootSignature,
  ExecuteIndirect,
  UnsupportedBarrier,
  CopyOrResolve,
  ClearOrDiscard,
  QueryOrPredication,
  SnapshotDependent,
  TemporalUpscale,
  NativeUnsupportedRootSignature,
  NativeUnsupportedDescriptorRange,
  NativeUnsupportedRootDescriptor,
  NativeUnsupportedGeometryPipeline,
  NativeUnsupportedTessellationPipeline,
  NativeUnsupportedExecuteIndirect,
  NativeUnsupportedDynamicResource,
  NativeMissingDescriptorBackend,
  NativeShaderAbiMismatch,
  NativeResidencyUnsupported,
  UnsupportedRootSignature,
  UnsupportedDescriptorTable,
  UnsupportedRootDescriptor,
  UnsupportedRootConstants,
  UnsupportedVertexIndexState,
  UnsupportedRenderTargetState,
  UnsupportedArgumentTable,
  UnsupportedCommand,
};

struct CompiledCommandDescriptorHeaps {
  Com<ID3D12DescriptorHeap> cbv_srv_uav;
  Com<ID3D12DescriptorHeap> sampler;
  std::vector<Com<ID3D12DescriptorHeap>> all;
};

struct CompiledCommandPipelineMetadata {
  PipelineState *pipeline = nullptr;
  const PipelineMetalGraphicsState *metal_graphics = nullptr;
  const PipelineMetalComputeState *metal_compute = nullptr;
  PipelineStateType type = PipelineStateType::Graphics;
  DXMT12_MTL4_SHADER_ABI_VERSION shader_abi_version =
      DXMT12_MTL4_SHADER_ABI_BINDLESS_MIRROR;
  NativeShaderAbiEligibilityReason native_eligibility_reason =
      NativeShaderAbiEligibilityReason::UnsupportedRootSignature;
  dxmt::CompiledFallbackReason perf_fallback_reason =
      dxmt::CompiledFallbackReason::Unknown;
  bool has_pipeline_state = false;
  bool has_dxmt_pipeline = false;
  bool type_matches = false;
  bool has_root_signature = false;
  bool uses_bindless_mirror = false;
  bool uses_bindless_mirror_abi = false;
  bool uses_native_descriptor_table_abi = false;
  bool uses_geometry = false;
  bool uses_tessellation = false;
  bool ordinary_native = false;
  bool ordinary_bindless = false;
  bool ordinary_compiled = false;
  bool metal_pso_ready = false;
};

struct CompiledCommandPipelineBinding {
  Com<ID3D12PipelineState> pipeline_state;
  Com<ID3D12RootSignature> root_signature;
  CompiledCommandPipelineMetadata metadata;
  bool pipeline_state_pending = true;
  bool root_signature_pending = true;
  bool bindless_candidate = false;
};

struct CompiledCommandRootDescriptorTable {
  UINT root_parameter_index = 0;
  D3D12_DESCRIPTOR_HEAP_TYPE heap_type = D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES;
  D3D12_GPU_DESCRIPTOR_HANDLE base_descriptor = {};
  UINT heap_index = 0;
  UINT descriptor_count = 0;
  UINT heap_count = 0;
  UINT table_offset = 0;
  UINT table_entry_stride = sizeof(uint64_t) * 3;
  UINT root_table_base_descriptor_index = 0;
  uint64_t descriptor_table_gpu_address = 0;
  uint64_t descriptor_table_entry_gpu_address = 0;
  uint64_t buffer_descriptor_record_gpu_address = 0;
  uint64_t buffer_resource_table_gpu_address = 0;
  uint64_t buffer_resource_table_generation = 0;
  // Keeps the mirror-owned descriptor backend buffers alive while compiled
  // packets are in flight.
  Com<ID3D12DescriptorHeap> owning_heap;
  DescriptorHeapMirror *mirror = nullptr;
  bool resolved = false;
  bool descriptor_table_backend_ready = false;
  bool native_descriptor_record_storage_ready = false;
  bool native_buffer_resource_table_ready = false;
  bool native_root_table_base_ready = false;
};

struct CompiledCommandRootConstants {
  UINT root_parameter_index = 0;
  UINT dst_offset = 0;
  std::vector<UINT> values;
};

struct CompiledCommandRootDescriptor {
  D3D12_ROOT_PARAMETER_TYPE parameter_type = D3D12_ROOT_PARAMETER_TYPE_CBV;
  UINT root_parameter_index = 0;
  D3D12_GPU_VIRTUAL_ADDRESS address = 0;
};

struct CompiledCommandVertexBuffer {
  UINT slot = 0;
  D3D12_VERTEX_BUFFER_VIEW view = {};
};

struct CompiledCommandInputAssemblerState {
  std::vector<CompiledCommandVertexBuffer> vertex_buffers;
  std::optional<D3D12_INDEX_BUFFER_VIEW> index_buffer;
  std::uint64_t vertex_buffer_dirty_mask = 0;
  bool index_buffer_dirty = false;
};

struct CompiledCommandRenderState {
  std::vector<DescriptorRecord> render_targets;
  std::optional<DescriptorRecord> depth_stencil;
  std::vector<D3D12_VIEWPORT> viewports;
  std::vector<D3D12_RECT> scissors;
  std::array<FLOAT, 4> blend_factor = {1.0f, 1.0f, 1.0f, 1.0f};
  UINT stencil_ref = 0;
  D3D12_PRIMITIVE_TOPOLOGY topology = D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
};

struct CompiledNativeStageBinding {
  uint64_t cbuffer_root_base_offset = 0;
  uint64_t resource_root_base_offset = 0;
  uint32_t cbuffer_root_base_count = 0;
  uint32_t resource_root_base_count = 0;
  // CPU copies are retained for bounded diagnostics. The GPU consumes the
  // packed native_root_base_buffer; keeping the source words lets a hang log
  // identify the exact descriptor-table bases without a synchronizing GPU
  // readback that would perturb command scheduling.
  std::vector<uint32_t> cbuffer_root_bases;
  std::vector<uint32_t> resource_root_bases;
  bool ready = false;
};

struct CompiledGraphicsPacket {
  UINT record_index = 0;
  std::uint64_t d3d_sequence = 0;
  CompiledCommandPipelineBinding pipeline;
  CompiledCommandDescriptorHeaps descriptor_heaps;
  std::vector<CompiledCommandRootDescriptorTable> root_tables;
  std::vector<CompiledCommandRootConstants> root_constants;
  std::vector<CompiledCommandRootDescriptor> root_descriptors;
  CompiledCommandInputAssemblerState input_assembler;
  CompiledCommandRenderState render_state;
  CompiledNativeStageBinding native_vertex;
  CompiledNativeStageBinding native_pixel;
  std::optional<DrawInstancedRecord> draw;
  std::optional<DrawIndexedInstancedRecord> draw_indexed;
};

struct CompiledComputePacket {
  UINT record_index = 0;
  std::uint64_t d3d_sequence = 0;
  CompiledCommandPipelineBinding pipeline;
  CompiledCommandDescriptorHeaps descriptor_heaps;
  std::vector<CompiledCommandRootDescriptorTable> root_tables;
  std::vector<CompiledCommandRootConstants> root_constants;
  std::vector<CompiledCommandRootDescriptor> root_descriptors;
  CompiledNativeStageBinding native_compute;
  DispatchRecord dispatch;
};

struct CompiledCommandSegment {
  CompiledCommandSegmentKind kind = CompiledCommandSegmentKind::Fallback;
  UINT first_record_index = 0;
  UINT record_count = 0;
  UINT first_graphics_packet = 0;
  UINT graphics_packet_count = 0;
  UINT first_compute_packet = 0;
  UINT compute_packet_count = 0;
  CompiledCommandFallbackReason fallback_reason =
      CompiledCommandFallbackReason::None;
  dxmt::CompiledFallbackReason perf_fallback_reason =
      dxmt::CompiledFallbackReason::Unknown;
};

struct CompiledCommandList {
  UINT record_count = 0;
  WMT::Reference<WMT::Buffer> native_root_base_buffer;
  std::vector<CompiledCommandSegment> segments;
  std::vector<CompiledGraphicsPacket> graphics_packets;
  std::vector<CompiledComputePacket> compute_packets;
};

const char *CompiledCommandSegmentKindName(CompiledCommandSegmentKind kind);
const char *CompiledCommandFallbackReasonName(
    CompiledCommandFallbackReason reason);
dxmt::CompiledFallbackReason
CompiledCommandFallbackReasonToPerf(CompiledCommandFallbackReason reason);

struct SubmittedCommandAllocatorUse {
  Com<CommandAllocatorObject, false> allocator;
  UINT64 serial = 0;
};

class GraphicsCommandList {
public:
  virtual ~GraphicsCommandList() = default;

  virtual IMTLD3D12Device *GetParentDevice() const = 0;
  virtual bool IsClosed() const = 0;
  virtual D3D12_COMMAND_LIST_TYPE GetCommandListType() const = 0;
  virtual const std::vector<CommandRecord> &GetCommandRecords() const = 0;
  virtual std::shared_ptr<const CompiledCommandList> GetCompiledCommands()
      const = 0;
  virtual void SetApitraceLifecycleRecordingEnabled(bool enabled) = 0;
  virtual HRESULT MarkSubmittedToQueue(
      D3D12_COMMAND_LIST_TYPE queue_type,
      std::vector<SubmittedCommandAllocatorUse> &allocator_uses) = 0;
};

class CommandSignature {
public:
  virtual ~CommandSignature() = default;
  virtual const D3D12_COMMAND_SIGNATURE_DESC &GetDesc() const = 0;
  virtual const std::vector<D3D12_INDIRECT_ARGUMENT_DESC> &GetArguments() const = 0;
  virtual ID3D12RootSignature *GetRootSignature() const = 0;
};

Com<ID3D12GraphicsCommandList>
CreateGraphicsCommandList(IMTLD3D12Device *device, UINT node_mask,
                          D3D12_COMMAND_LIST_TYPE type,
                          ID3D12CommandAllocator *command_allocator,
                          ID3D12PipelineState *initial_pipeline_state,
                          HRESULT *status = nullptr);

Com<ID3D12CommandSignature>
CreateCommandSignature(IMTLD3D12Device *device,
                       const D3D12_COMMAND_SIGNATURE_DESC *desc,
                       ID3D12RootSignature *root_signature);

} // namespace dxmt::d3d12
