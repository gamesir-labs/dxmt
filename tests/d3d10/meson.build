d3d10_test_include_dirs = include_directories('../include')

d3d10_test_main_lib = static_library(
  'dxmt-d3d10-test-main',
  files('../support/win32_main.cpp'),
  dependencies: [gtest_dep],
  include_directories: [d3d10_test_include_dirs],
  install: false,
)

d3d10_test_dep = declare_dependency(
  link_with: [d3d10_test_main_lib],
  dependencies: [gtest_dep],
  include_directories: [d3d10_test_include_dirs],
)

d3d10_device_spec = executable(
  'dxmt-d3d10-device-spec',
  files('device_spec.cpp'),
  dependencies: [d3d10_test_dep, d3d10core_dep, dxgi_dep],
  install: false,
)

test(
  'dxmt-d3d10-device-spec',
  d3d10_device_spec,
  suite: ['d3d10'],
  timeout: 30,
)
