gtest_include_dirs = include_directories('.', 'include', is_system: true)
gtest_threads_dep = dependency('threads')

gtest_lib = static_library(
  'dxmt-gtest',
  'src/gtest-all.cc',
  dependencies: [gtest_threads_dep],
  include_directories: [gtest_include_dirs],
  install: false,
)

gtest_dep = declare_dependency(
  link_with: [gtest_lib],
  dependencies: [gtest_threads_dep],
  include_directories: [gtest_include_dirs],
)

gtest_main_lib = static_library(
  'dxmt-gtest-main',
  'src/gtest_main.cc',
  include_directories: [gtest_include_dirs],
  install: false,
)

gtest_main_dep = declare_dependency(
  link_with: [gtest_main_lib],
  dependencies: [gtest_dep],
  include_directories: [gtest_include_dirs],
)
