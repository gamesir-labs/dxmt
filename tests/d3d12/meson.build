d3d12_test_include_dirs = include_directories('../include', '.')

d3d12_runtime_support_lib = static_library(
  'dxmt-d3d12-runtime-support',
  files(
    'd3d12_test_context.cpp',
    'd3d12_binding_hotspot_scenarios.cpp',
  ),
  dependencies: [d3d12_dep, d3dcompiler_test_dep],
  include_directories: [d3d12_test_include_dirs],
  install: false,
)

d3d12_runtime_support_dep = declare_dependency(
  link_with: [d3d12_runtime_support_lib],
  dependencies: [d3d12_dep],
  include_directories: [d3d12_test_include_dirs],
)

d3d12_unit_suites = {
  'device': {
    'sources': files(
      'device/agility_core_spec.cpp',
      'device/device_spec.cpp',
    ),
  },
  'object': {
    'sources': files(
      'object/com_identity_spec.cpp',
      'object/private_data_spec.cpp',
      'object/shared_handle_spec.cpp',
    ),
  },
  'cache': {
    'sources': files('cache/cache_spec.cpp'),
  },
  'capability': {
    'sources': files(
      'capability/capability_spec.cpp',
      'capability/feature_query_spec.cpp',
      'capability/format_execution_matrix_spec.cpp',
      'capability/format_msaa_execution_spec.cpp',
      'capability/format_operation_spec.cpp',
      'capability/format_uav_execution_spec.cpp',
      'capability/optional_feature_gate_spec.cpp',
    ),
  },
  'command': {
    'sources': files(
      'command/allocator_lifecycle_spec.cpp',
      'command/bundle_legality_spec.cpp',
      'command/list_lifecycle_spec.cpp',
      'command/list_state_spec.cpp',
      'command/state_machine_spec.cpp',
      'command/type_legality_spec.cpp',
    ),
  },
  'compute': {
    'sources': files('compute/dispatch_spec.cpp'),
  },
  'descriptor': {
    'sources': files(
      'descriptor/bindless_graphics_stage_spec.cpp',
      'descriptor/binding_hotspot_spec.cpp',
      'descriptor/buffer_view_spec.cpp',
      'descriptor/descriptor_spec.cpp',
      'descriptor/heap_contract_spec.cpp',
      'descriptor/sampler_matrix_spec.cpp',
      'descriptor/sampler_spec.cpp',
      'descriptor/table_binding_spec.cpp',
    ),
  },
  'execution': {
    'sources': files(
      'execution/path_boundary_spec.cpp',
      'execution/path_empty_segment_spec.cpp',
      'execution/path_root_descriptor_spec.cpp',
    ),
  },
  'graphics': {
    'sources': files(
      'graphics/blend_spec.cpp',
      'graphics/depth_stencil_spec.cpp',
      'graphics/graphics_edge_semantics_spec.cpp',
      'graphics/input_assembler_spec.cpp',
      'graphics/output_merger_spec.cpp',
      'graphics/rasterization_execution_spec.cpp',
      'graphics/render_pass_execution_spec.cpp',
      'graphics/render_pass_mrt_semantics_spec.cpp',
      'graphics/render_pass_validation_spec.cpp',
      'graphics/viewport_scissor_spec.cpp',
    ),
  },
  'integration': {
    'sources': files(
      'integration/batched_matrix_spec.cpp',
      'integration/frame_spec.cpp',
      'integration/swapchain_spec.cpp',
    ),
  },
  'pipeline': {
    'sources': files(
      'pipeline/scalar_varying_spec.cpp',
      'pipeline/graphics_creation_spec.cpp',
      'pipeline/stream_spec.cpp',
    ),
    'dependencies': [d3dcompiler_test_dep],
  },
  'query': {
    'sources': files(
      'query/indirect_draw_spec.cpp',
      'query/indirect_spec.cpp',
      'query/occlusion_spec.cpp',
      'query/predication_spec.cpp',
      'query/query_heap_spec.cpp',
      'query/timestamp_spec.cpp',
      'query/validation_spec.cpp',
    ),
  },
  'queue': {
    'sources': files('queue/queue_spec.cpp'),
  },
  'root-signature': {
    'sources': files(
      'root_signature/layout_validation_spec.cpp',
      'root_signature/contract_matrix_spec.cpp',
      'root_signature/root_descriptor_spec.cpp',
      'root_signature/runtime_state_spec.cpp',
      'root_signature/serialization_spec.cpp',
      'root_signature/volatility_spec.cpp',
    ),
  },
  'robustness': {
    'sources': files(
      'robustness/descriptor_sequence_fuzz_spec.cpp',
      'robustness/fault_injection_spec.cpp',
      'robustness/fuzz_spec.cpp',
      'robustness/internal_fault_injection_spec.cpp',
      'robustness/resource_desc_fuzz_spec.cpp',
      'robustness/shader_container_fuzz_spec.cpp',
      'robustness/state_model_fuzz_spec.cpp',
    ),
  },
  'sync': {
    'sources': files(
      'sync/aliasing_barrier_spec.cpp',
      'sync/barrier_matrix_spec.cpp',
      'sync/cross_queue_spec.cpp',
      'sync/enhanced_barrier_spec.cpp',
      'sync/fence_spec.cpp',
      'sync/legacy_transition_spec.cpp',
      'sync/promotion_decay_spec.cpp',
      'sync/queue_wait_spec.cpp',
      'sync/split_transition_spec.cpp',
      'sync/transition_sequence_spec.cpp',
      'sync/uav_barrier_spec.cpp',
    ),
  },
  'transfer': {
    'sources': files(
      'transfer/clear_dsv_spec.cpp',
      'transfer/clear_rtv_spec.cpp',
      'transfer/clear_uav_spec.cpp',
      'transfer/copy_footprint_spec.cpp',
      'transfer/copy_edge_spec.cpp',
      'transfer/copy_resource_spec.cpp',
      'transfer/resolve_validation_spec.cpp',
      'transfer/copy_texture_spec.cpp',
    ),
  },
  'resource': {
    'sources': files(
      'resource/planar_format_spec.cpp',
      'resource/residency_spec.cpp',
      'resource/resource_spec.cpp',
      'resource/validation_memory_spec.cpp',
    ),
  },
  'shader': {
    'sources': files(
      'shader/container_corruption_spec.cpp',
      'shader/geometry_tessellation_spec.cpp',
      'shader/resource_semantic_spec.cpp',
      'shader/semantic_matrix_spec.cpp',
      'shader/wave_spec.cpp',
      'shader/wave_semantic_matrix_spec.cpp',
    ),
  },
  'sparse-resource': {
    'sources': files(
      'resource/sparse_mapping_matrix_spec.cpp',
      'resource/sparse_resource_spec.cpp',
    ),
  },
}

d3d12_unit_sources = []
d3d12_unit_dependencies = []
foreach suite_name, suite_config : d3d12_unit_suites
  d3d12_unit_sources += suite_config['sources']
  d3d12_unit_dependencies += suite_config.get('dependencies', [])
endforeach

d3d12_unit_dependencies += [
  d3d12_runtime_support_dep,
  d3dcompiler_test_dep,
  dxgi_dep,
]

d3d12_differential_oracle = executable(
  'dxmt-d3d12-differential-oracle',
  files('differential/oracle.cpp'),
  dependencies: [d3d12_dep, d3dcompiler_test_dep, dxgi_dep],
  include_directories: [d3d12_test_include_dirs],
  install: false,
)
