python3 = find_program('python3')

dxmt_benchmark_include_dirs = include_directories('include')
dxmt_benchmark_runner = files('support/benchmark_acceptance.py')

dxmt_benchmark_main_lib = static_library(
  'dxmt-benchmark-main',
  'support/main.cpp',
  dependencies: [google_benchmark_dep],
  include_directories: [dxmt_benchmark_include_dirs],
  install: false,
)

dxmt_benchmark_dep = declare_dependency(
  link_with: [dxmt_benchmark_main_lib],
  dependencies: [google_benchmark_dep],
  include_directories: [dxmt_benchmark_include_dirs],
)

subdir('wine')
