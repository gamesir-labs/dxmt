dxmt_test_include_dirs = include_directories('include')

dxmt_test_main_lib = static_library(
  'dxmt-wine-test-main',
  files(
    'support/main.cpp',
    'support/scheduler.cpp',
    'support/scheduler_plan.cpp',
  ),
  dependencies: [gtest_dep],
  include_directories: [dxmt_test_include_dirs],
  install: false,
)

dxmt_test_dep = declare_dependency(
  link_with: [dxmt_test_main_lib],
  dependencies: [gtest_dep],
  include_directories: [dxmt_test_include_dirs],
)

subdir('d3d10')
subdir('d3d11')
subdir('d3d12')

dxmt_wine_framework_suites = {
  'framework': {
    'sources': files('wine/framework_spec.cpp'),
  },
  'scheduler': {
    'sources': files('wine/scheduler_spec.cpp'),
  },
  'placeholder': {
    'sources': files('wine/placeholder_spec.cpp'),
  },
  'layout': {
    'sources': files('wine/layout_spec.cpp'),
  },
  'descriptor_revision': {
    'sources': files('wine/descriptor_revision_test.cpp'),
  },
  'lease_range_registry': {
    'sources': files('wine/lease_range_registry_test.cpp'),
  },
  'argument_buffer': {
    'sources': files('wine/argument_buffer_test.cpp'),
  },
  'checked_math': {
    'sources': files('wine/checked_math_test.cpp'),
  },
}

dxmt_wine_framework_sources = []
foreach suite_name, suite_config : dxmt_wine_framework_suites
  dxmt_wine_framework_sources += suite_config['sources']
endforeach

dxmt_wine_framework_tests = executable(
  'dxmt-wine-framework-tests',
  dxmt_wine_framework_sources,
  dependencies: [dxmt_test_dep],
  include_directories: [dxmt_include_path],
  install: false,
)

dxmt_wine_d3d10_tests = executable(
  'dxmt-wine-d3d10-tests',
  d3d10_unit_sources,
  dependencies: [dxmt_test_dep] + d3d10_unit_dependencies,
  install: false,
)

dxmt_wine_d3d11_tests = executable(
  'dxmt-wine-d3d11-tests',
  d3d11_unit_sources,
  dependencies: [dxmt_test_dep] + d3d11_unit_dependencies,
  install: false,
)

dxmt_wine_d3d12_tests = executable(
  'dxmt-wine-d3d12-tests',
  d3d12_unit_sources,
  dependencies: [dxmt_test_dep] + d3d12_unit_dependencies,
  install: false,
)

dxmt_wine_test_scheduler = executable(
  'dxmt-wine-test-scheduler',
  files('support/wine_suite_scheduler.cpp'),
  include_directories: [include_directories('support')],
  install: false,
)

test(
  'dxmt-wine-unit-tests',
  wine_test_launcher,
  args: [dxmt_wine_test_scheduler],
  suite: ['unit', 'wine'],
  timeout: 120,
  env: wine_test_env,
  is_parallel: false,
  workdir: meson.project_source_root(),
)

alias_target(
  'dxmt-wine-tests',
  dxmt_wine_test_scheduler,
  dxmt_wine_framework_tests,
  dxmt_wine_d3d10_tests,
  dxmt_wine_d3d11_tests,
  dxmt_wine_d3d12_tests,
)
