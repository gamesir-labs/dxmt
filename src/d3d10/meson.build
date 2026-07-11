d3d10core_res = wrc_generator.process('version.rc')

d3d10core_src = [
  'd3d10core.cpp',
]

d3d10_ld_args      = ['-L' + windows_native_install_dir] # a nice trick to force relink
d3d10_link_depends = []

d3d10core_dll = shared_library('d3d10core', d3d10core_src, d3d10core_res,
  name_prefix         : '',
  dependencies        : [ d3d11_dep, dxgi_dep, util_dep ],
  include_directories : [ dxmt_include_path ],
  install             : true,
  install_dir         : windows_native_install_dir,
  install_tag         : 'runtime-metal3',
  vs_module_defs      : 'd3d10core.def',
  link_args           : d3d10_ld_args,
  link_depends        : [ d3d10_link_depends ],
)

d3d10core_dep = declare_dependency(
  link_with           : [ d3d10core_dll ],
  include_directories : [ dxmt_include_path ],
)

if wine_builtin_dll
d3d10_postprocess = custom_target('postprocess_libd3d10core',
  input : d3d10core_dll,
  output: 'd3d10core.dll.postproc',
  command : [ winebuild_command, '--builtin', '@INPUT@' ],
  depends : d3d10core_dll,
  build_by_default : true
)
d3d10_postprocess_targets += [d3d10_postprocess]
endif
