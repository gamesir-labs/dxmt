d3d12_test_include_dirs = include_directories('../include', '.')

d3d12_runtime_support_lib = static_library(
  'dxmt-d3d12-runtime-support',
  files('d3d12_test_context.cpp'),
  dependencies: [d3d12_dep],
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
      'device/device_spec.cpp',
    ),
  },
  'cache': {
    'sources': files('cache/cache_spec.cpp'),
  },
  'capability': {
    'sources': files('capability/capability_spec.cpp'),
  },
  'command': {
    'sources': files(
      'command/allocator_lifecycle_spec.cpp',
      'command/list_lifecycle_spec.cpp',
    ),
  },
  'descriptor': {
    'sources': files('descriptor/descriptor_spec.cpp'),
  },
  'pipeline': {
    'sources': files('pipeline/stream_spec.cpp'),
  },
  'queue': {
    'sources': files('queue/queue_spec.cpp'),
  },
  'root-signature': {
    'sources': files(
      'root_signature/layout_validation_spec.cpp',
      'root_signature/runtime_state_spec.cpp',
      'root_signature/serialization_spec.cpp',
    ),
  },
  'resource': {
    'sources': files('resource/resource_spec.cpp'),
  },
  'sparse-resource': {
    'sources': files('resource/sparse_resource_spec.cpp'),
  },
}

d3d12_unit_sources = []
d3d12_unit_dependencies = []
foreach suite_name, suite_config : d3d12_unit_suites
  d3d12_unit_sources += suite_config['sources']
  d3d12_unit_dependencies += suite_config.get('dependencies', [])
endforeach

d3d12_unit_dependencies += [d3d12_runtime_support_dep, dxgi_dep]
