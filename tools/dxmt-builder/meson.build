project(
  'dxmt-builder',
  'cpp',
  version: '1.0.0',
  meson_version: '>=1.3.0',
  default_options: ['cpp_std=c++20', 'warning_level=2', 'b_ndebug=if-release'],
)

builder_inc = include_directories('src')
builder_core = static_library(
  'dxmt-builder-core',
  files('src/builder.cpp', 'src/sha256.cpp'),
  include_directories: builder_inc,
  install: false,
)

builder_core_dep = declare_dependency(
  link_with: builder_core,
  include_directories: builder_inc,
)

executable(
  'dxmt-builder',
  'src/main.cpp',
  dependencies: builder_core_dep,
  install: false,
)

if get_option('tests')
  builder_tests = executable(
    'dxmt-builder-tests',
    'tests/builder_tests.cpp',
    dependencies: builder_core_dep,
    install: false,
  )
  test('dxmt-builder-tests', builder_tests)
endif
