dxmt_test_include_dirs = include_directories('include')

dxmt_test_main_lib = static_library(
  'dxmt-test-main',
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

subdir('unit')
