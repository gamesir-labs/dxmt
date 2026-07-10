dxmt_unit_tests = executable(
  'dxmt-unit-tests',
  'framework_test.cpp',
  dependencies: [gtest_main_dep],
  install: false,
)

test(
  'dxmt-unit-tests',
  dxmt_unit_tests,
  args: ['--gtest_brief=1'],
  suite: 'unit',
)
