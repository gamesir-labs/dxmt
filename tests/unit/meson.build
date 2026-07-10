dxmt_unit_test_suites = {
  'framework': {
    'sources': files('framework_test.cpp'),
  },
  'placeholder': {
    'sources': files('placeholder_test.cpp'),
  },
  'layout': {
    'sources': files('layout_test.cpp'),
    'workdir': meson.project_source_root(),
  },
}

dxmt_unit_test_targets = []

foreach suite_name, suite_config : dxmt_unit_test_suites
  target_name = 'dxmt-unit-' + suite_name + '-tests'
  suite_dependencies = suite_config.get('dependencies', [])
  is_slow = suite_config.get('slow', false)
  test_suites = is_slow ? ['unit', 'unit-slow'] : ['unit', 'unit-fast']
  test_priority = suite_config.get('priority', is_slow ? 100 : 0)
  test_timeout = suite_config.get('timeout', 30)
  test_workdir = suite_config.get('workdir', meson.project_build_root())

  suite_executable = executable(
    target_name,
    suite_config['sources'],
    dependencies: [dxmt_test_dep] + suite_dependencies,
    install: false,
  )
  dxmt_unit_test_targets += [suite_executable]

  test(
    target_name,
    suite_executable,
    suite: test_suites,
    priority: test_priority,
    timeout: test_timeout,
    workdir: test_workdir,
  )
endforeach

alias_target('dxmt-unit-tests', dxmt_unit_test_targets)
