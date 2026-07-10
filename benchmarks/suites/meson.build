dxmt_benchmark_suites = {
  'placeholder': {
    'sources': files('placeholder_benchmark.cpp'),
    'budget': files('../budgets/placeholder.json'),
  },
}

dxmt_benchmark_targets = []

foreach suite_name, suite_config : dxmt_benchmark_suites
  target_name = 'dxmt-benchmark-' + suite_name
  suite_dependencies = suite_config.get('dependencies', [])
  repetitions = suite_config.get('repetitions', 5)
  min_time = suite_config.get('min_time', '0.05s')
  warmup_time = suite_config.get('warmup_time', '0.01')

  suite_executable = executable(
    target_name,
    suite_config['sources'],
    dependencies: [dxmt_benchmark_dep] + suite_dependencies,
    install: false,
  )
  dxmt_benchmark_targets += [suite_executable]

  benchmark(
    target_name,
    python3,
    args: [
      dxmt_benchmark_runner,
      '--executable', suite_executable,
      '--budget', suite_config['budget'],
      '--repetitions', repetitions.to_string(),
      '--min-time', min_time,
      '--warmup-time', warmup_time,
    ],
    suite: 'performance',
    timeout: suite_config.get('timeout', 120),
  )
endforeach

alias_target('dxmt-benchmarks', dxmt_benchmark_targets)
