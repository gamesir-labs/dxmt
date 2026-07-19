dxmt_test_include_dirs = include_directories('include')
d3d10_test_dep = cpp.find_library('d3d10')
d3d11_test_dep = cpp.find_library('d3d11')
d3d12_test_dep = cpp.find_library('d3d12')
d3dcompiler_test_dep = cpp.find_library('d3dcompiler_47')
dxgi_test_dep = cpp.find_library('dxgi')
python3_native = find_program('python3', native: true)

test(
  'd3d12-coverage-validator-tests',
  python3_native,
  args: [files('coverage/test_check_d3d12_coverage.py')],
  suite: ['unit', 'coverage'],
  timeout: 30,
)

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
  'internal_units': {
    'sources': files(
      'support/logger.cpp',
      'wine/binding_set_test.cpp',
      'wine/command_list_test.cpp',
      'wine/com_object_test.cpp',
      'wine/com_private_data_test.cpp',
      'wine/capture_state_test.cpp',
      'wine/dependency_tracker_test.cpp',
      'wine/descriptor_table_test.cpp',
      'wine/dxbc_parser_test.cpp',
      'wine/dxil_parser_test.cpp',
      'wine/format_test.cpp',
      'wine/names_test.cpp',
      'wine/perf_stats_test.cpp',
      'wine/pipeline_diag_test.cpp',
      'wine/present_source_view_test.cpp',
      'wine/resource_state_test.cpp',
      'wine/resource_subset_test.cpp',
      'wine/ring_bump_allocator_test.cpp',
      'wine/shader_binary_test.cpp',
      'wine/task_scheduler_test.cpp',
      'wine/threading_test.cpp',
      'wine/utility_bit_test.cpp',
      'wine/utility_config_test.cpp',
      'wine/utility_container_test.cpp',
      'wine/utility_math_test.cpp',
      'wine/utility_sha1_test.cpp',
      'wine/utility_string_test.cpp',
      'wine/wsi_platform_test.cpp',
      '../src/dxmt/dxmt_allocation.cpp',
      '../src/dxmt/dxmt_capture.cpp',
      '../src/dxmt/dxmt_deptrack.cpp',
      '../src/dxmt/dxmt_format.cpp',
      '../src/dxmt/dxmt_names.cpp',
      '../src/dxmt/dxmt_perf_stats.cpp',
      '../src/dxmt/dxmt_pipeline_diag.cpp',
      '../src/dxmt/dxmt_subresource.cpp',
    ),
    'dependencies': [util_dep, dxbc_parser_dep, dxil_parser_dep],
  },
}

dxmt_wine_framework_sources = []
dxmt_wine_framework_dependencies = []
foreach suite_name, suite_config : dxmt_wine_framework_suites
  dxmt_wine_framework_sources += suite_config['sources']
  dxmt_wine_framework_dependencies += suite_config.get('dependencies', [])
endforeach

dxmt_wine_framework_tests = executable(
  'dxmt-wine-framework-tests',
  dxmt_wine_framework_sources,
  dependencies: [dxmt_test_dep] + dxmt_wine_framework_dependencies,
  include_directories: [
    dxmt_include_path,
    dxmt_build_include_path,
    include_directories('../src/airconv'),
    include_directories('../src/winemetal'),
  ],
  cpp_args: ['-DWINEMETAL_STATIC'],
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
  args: ['wine-exec', '--', dxmt_wine_test_scheduler],
  suite: ['unit', 'wine'],
  timeout: 120,
  env: wine_test_env,
  is_parallel: false,
  workdir: meson.project_source_root(),
)

airconv_scalar_varying_test = find_program(
  'airconv/scalar-varying-ir-test.sh',
)
test(
  'airconv-scalar-varying-ir',
  airconv_scalar_varying_test,
  args: [
    airconv_cli_darwin,
    files('airconv/scalar-varying-vs.dxbc.hex'),
  ],
  suite: ['unit', 'airconv'],
  timeout: 30,
)

airconv_container_rejection_test = find_program(
  'airconv/container-rejection-test.sh',
)
test(
  'airconv-container-rejection',
  airconv_container_rejection_test,
  args: [
    airconv_cli_darwin,
    files('airconv/scalar-varying-vs.dxbc.hex'),
  ],
  suite: ['unit', 'airconv'],
  timeout: 30,
)

airconv_determinism_test = find_program(
  'airconv/determinism-test.sh',
)
test(
  'airconv-determinism',
  airconv_determinism_test,
  args: [
    airconv_cli_darwin,
    files('airconv/scalar-varying-vs.dxbc.hex'),
    files('airconv/control-flow-ps.dxbc.hex'),
    files('airconv/arithmetic-conversion-ps.dxbc.hex'),
  ],
  suite: ['unit', 'airconv'],
  timeout: 30,
)

airconv_control_flow_test = find_program(
  'airconv/control-flow-ir-test.sh',
)
test(
  'airconv-control-flow-ir',
  airconv_control_flow_test,
  args: [
    airconv_cli_darwin,
    files('airconv/control-flow-ps.dxbc.hex'),
  ],
  suite: ['unit', 'airconv'],
  timeout: 30,
)

airconv_tessellation_phi_test = find_program(
  'airconv/tessellation-phi-ir-test.sh',
)
test(
  'airconv-tessellation-phi-ir',
  airconv_tessellation_phi_test,
  args: [
    airconv_cli_darwin,
    files('airconv/phi-hull.dxbc.hex'),
    files('airconv/phi-domain.dxbc.hex'),
  ],
  suite: ['unit', 'airconv'],
  timeout: 30,
)

airconv_arithmetic_conversion_test = find_program(
  'airconv/arithmetic-conversion-ir-test.sh',
)
test(
  'airconv-arithmetic-conversion-ir',
  airconv_arithmetic_conversion_test,
  args: [
    airconv_cli_darwin,
    files('airconv/arithmetic-conversion-ps.dxbc.hex'),
  ],
  suite: ['unit', 'airconv'],
  timeout: 30,
)

airconv_container_boundary_test = find_program(
  'airconv/container-boundary-test.py',
)
test(
  'airconv-container-boundary',
  airconv_container_boundary_test,
  args: [
    airconv_cli_darwin,
    files('airconv/scalar-varying-vs.dxbc.hex'),
  ],
  suite: ['unit', 'airconv'],
  timeout: 30,
)

airconv_diagnostic_determinism_test = find_program(
  'airconv/diagnostic-determinism-test.py',
)
test(
  'airconv-diagnostic-determinism',
  airconv_diagnostic_determinism_test,
  args: [
    airconv_cli_darwin,
    files('airconv/scalar-varying-vs.dxbc.hex'),
  ],
  suite: ['unit', 'airconv'],
  timeout: 30,
)

alias_target(
  'dxmt-wine-tests',
  dxmt_wine_test_scheduler,
  dxmt_wine_framework_tests,
  dxmt_wine_d3d10_tests,
  dxmt_wine_d3d11_tests,
  dxmt_wine_d3d12_tests,
  d3d12_differential_oracle,
)

alias_target(
  'dxmt-wine-tests-framework',
  dxmt_wine_test_scheduler,
  dxmt_wine_framework_tests,
)
alias_target(
  'dxmt-wine-tests-d3d10',
  dxmt_wine_test_scheduler,
  dxmt_wine_d3d10_tests,
)
alias_target(
  'dxmt-wine-tests-d3d11',
  dxmt_wine_test_scheduler,
  dxmt_wine_d3d11_tests,
)
alias_target(
  'dxmt-wine-tests-d3d12',
  dxmt_wine_test_scheduler,
  dxmt_wine_d3d12_tests,
  d3d12_differential_oracle,
)
