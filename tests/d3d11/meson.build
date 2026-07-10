d3d11_test_include_dirs = include_directories('../include')

d3d11_test_main_lib = static_library(
  'dxmt-d3d11-test-main',
  files('../support/win32_main.cpp'),
  dependencies: [gtest_dep],
  include_directories: [d3d11_test_include_dirs],
  install: false,
)

d3d11_test_dep = declare_dependency(
  link_with: [d3d11_test_main_lib],
  dependencies: [gtest_dep],
  include_directories: [d3d11_test_include_dirs],
)

d3d11_device_spec = executable(
  'dxmt-d3d11-device-spec',
  files('device_spec.cpp'),
  dependencies: [d3d11_test_dep, d3d11_dep],
  install: false,
)

test(
  'dxmt-d3d11-device-spec',
  wine_test_launcher,
  args: [d3d11_device_spec],
  suite: ['unit', 'wine', 'd3d11'],
  timeout: 120,
  env: wine_test_env,
  is_parallel: false,
)
