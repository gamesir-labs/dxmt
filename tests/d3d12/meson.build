d3d12_test_include_dirs = include_directories('../include', '.')

d3d12_test_main_lib = static_library(
  'dxmt-d3d12-test-main',
  files('../support/win32_main.cpp'),
  dependencies: [gtest_dep],
  include_directories: [d3d12_test_include_dirs],
  install: false,
)

d3d12_test_dep = declare_dependency(
  link_with: [d3d12_test_main_lib],
  dependencies: [gtest_dep],
  include_directories: [d3d12_test_include_dirs],
)

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
    'sources': files('device_spec.cpp'),
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

d3d12_unit_tests = executable(
  'dxmt-d3d12-unit-tests',
  d3d12_unit_sources,
  dependencies: [d3d12_test_dep, d3d12_runtime_support_dep] + d3d12_unit_dependencies,
  install: false,
)

test(
  'dxmt-d3d12-unit-tests',
  wine_test_launcher,
  args: [d3d12_unit_tests],
  suite: ['unit', 'wine', 'd3d12'],
  timeout: 120,
  env: wine_test_env,
  is_parallel: false,
)

alias_target('dxmt-d3d12-tests', d3d12_unit_tests)
