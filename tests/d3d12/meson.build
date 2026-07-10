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
      'device_spec.cpp',
      'capability_spec.cpp',
    ),
  },
  'descriptor': {
    'sources': files(
      'descriptor_spec.cpp',
    ),
  },
  'queue': {
    'sources': files(
      'queue_spec.cpp',
    ),
  },
  'sparse-resource': {
    'sources': files(
      'sparse_resource_spec.cpp',
    ),
  },
}

d3d12_unit_sources = []
d3d12_unit_dependencies = []
foreach suite_name, suite_config : d3d12_unit_suites
  d3d12_unit_sources += suite_config['sources']
  d3d12_unit_dependencies += suite_config.get('dependencies', [])
endforeach

d3d12_unit_dependencies += [d3d12_runtime_support_dep, dxgi_dep]
