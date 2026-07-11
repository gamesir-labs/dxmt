dxgi_res = wrc_generator.process('version.rc')

dxgi_src = [
  'dxgi_adapter.cpp',
  'dxgi_backend.cpp',
  'dxgi_factory.cpp',
  'dxgi_output.cpp',
  'dxgi_options.cpp',
  'dxgi.cpp',
]

dxgi_ld_args      = ['-L' + windows_native_install_dir] # a nice trick to force relink
dxgi_link_depends = []

dxgi_dll = shared_library('dxgi', dxgi_src, dxgi_res,
  name_prefix         : '',
  dependencies        : [ util_dep ],
  include_directories : [ dxmt_include_path ],
  install             : true,
  install_dir         : windows_native_install_dir,
  install_tag         : 'runtime-common',
  vs_module_defs      : 'dxgi.def',
  link_args           : dxgi_ld_args,
  link_depends        : [ dxgi_link_depends ],
)

dxgi_dep = declare_dependency(
  link_with           : [ dxgi_dll ],
  include_directories : [ dxmt_include_path, include_directories('.') ],
)

if wine_builtin_dll
dxgi_postprocess = custom_target('postprocess_libdxgi',
  input : dxgi_dll,
  output: 'dxgi.dll.postproc',
  command : [ winebuild_command, '--builtin', '@INPUT@' ],
  depends : dxgi_dll,
  build_by_default : true
)
dxgi_postprocess_targets += [dxgi_postprocess]
endif
