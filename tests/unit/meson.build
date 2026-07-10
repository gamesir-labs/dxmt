dxmt_unit_test_suites = {
  'framework': {
    'sources': files('framework_test.cpp'),
  },
  'scheduler': {
    'sources': files('scheduler_test.cpp'),
  },
  'placeholder': {
    'sources': files('placeholder_test.cpp'),
  },
  'layout': {
    'sources': files('layout_test.cpp'),
  },
}

dxmt_unit_test_sources = []
dxmt_unit_test_dependencies = []
dxmt_unit_test_timeout = 30

foreach suite_name, suite_config : dxmt_unit_test_suites
  dxmt_unit_test_sources += suite_config['sources']
  dxmt_unit_test_dependencies += suite_config.get('dependencies', [])
  suite_timeout = suite_config.get('timeout', 30)
  if suite_timeout > dxmt_unit_test_timeout
    dxmt_unit_test_timeout = suite_timeout
  endif
endforeach

dxmt_unit_tests = executable(
  'dxmt-unit-tests',
  dxmt_unit_test_sources,
  dependencies: [dxmt_test_dep] + dxmt_unit_test_dependencies,
  install: false,
)

test(
  'dxmt-unit-tests',
  dxmt_unit_tests,
  suite: 'unit',
  is_parallel: false,
  timeout: dxmt_unit_test_timeout,
  workdir: meson.project_source_root(),
)
