ue_d3d11_initialization = executable(
  'dxmt-ue-d3d11-initialization',
  files('ue_d3d11_initialization.cpp'),
  dependencies: [d3d11_dep, dxgi_dep],
  include_directories: [include_directories('../../tests/include')],
  install: false,
)

ue_d3d12_initialization = executable(
  'dxmt-ue-d3d12-initialization',
  files('ue_d3d12_initialization.cpp'),
  dependencies: [d3d12_dep, dxgi_dep],
  include_directories: [include_directories('../../tests/include')],
  install: false,
)

d3dcompiler_47_dep = cpp.find_library('d3dcompiler_47')

wine_benchmark_suites = {
  'd3d12-integration': {
    'sources': files('d3d12_integration_benchmark.cpp'),
    'dependencies': [d3d12_runtime_support_dep, d3dcompiler_47_dep],
    'mode': 'integration',
    'suite': ['wine', 'integration', 'd3d12'],
    'timeout': 600,
  },
  'd3d12-pipeline-creation': {
    'sources': files('d3d12_pipeline_creation_benchmark.cpp'),
    'dependencies': [d3d12_dep, d3dcompiler_47_dep],
    'include_directories': [include_directories('../../tests/include')],
    'mode': 'integration',
    'suite': ['wine', 'integration', 'd3d12', 'pipeline'],
    'timeout': 600,
  },
  'd3d12-binding-hotspots': {
    'sources': files('d3d12_binding_hotspot_benchmark.cpp'),
    'dependencies': [d3d12_runtime_support_dep],
    'include_directories': [include_directories('../../tests/d3d12')],
    'mode': 'integration',
    'suite': ['wine', 'integration', 'd3d12', 'binding'],
    'timeout': 600,
  },
  'ue-rhi-initialization': {
    'sources': files('ue_initialization_benchmark.cpp'),
    'include_directories': [include_directories('../../tests/support')],
    'runtime_targets': [
      ue_d3d11_initialization,
      ue_d3d12_initialization,
    ],
    'mode': 'integration',
    'suite': ['wine', 'integration', 'ue', 'd3d11', 'd3d12'],
    'timeout': 600,
  },
}

wine_benchmark_targets = [
  ue_d3d11_initialization,
  ue_d3d12_initialization,
]

foreach suite_name, suite_config : wine_benchmark_suites
  target_name = 'dxmt-wine-benchmark-' + suite_name
  mode = suite_config.get('mode', 'performance')
  suite_executable = executable(
    target_name,
    suite_config['sources'],
    dependencies: [dxmt_benchmark_dep] + suite_config.get('dependencies', []),
    include_directories: suite_config.get('include_directories', []),
    install: false,
  )
  wine_benchmark_targets += [suite_executable]

  runner_args = [
    dxmt_benchmark_runner,
    '--mode', mode,
    '--executable', suite_executable,
    '--launcher', wine_test_launcher.full_path(),
  ]
  if mode == 'performance'
    runner_args += [
      '--budget', suite_config['budget'],
      '--repetitions', suite_config.get('repetitions', 5).to_string(),
      '--min-time', suite_config.get('min_time', '0.05s'),
      '--warmup-time', suite_config.get('warmup_time', '0.01'),
    ]
  endif

  benchmark(
    target_name,
    python3,
    args: runner_args,
    suite: suite_config.get('suite', ['wine']),
    timeout: suite_config.get('timeout', 120),
    env: wine_test_env,
    depends: suite_config.get('runtime_targets', []),
  )
endforeach

alias_target('dxmt-wine-benchmarks', wine_benchmark_targets)
alias_target('dxmt-benchmarks', wine_benchmark_targets)
