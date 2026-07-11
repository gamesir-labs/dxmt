winemetal_src = [
  'main.c',
  'airconv_thunks.c',
  'winemetal_thunks.c'
]

winemetal_ld_args = []

winemetal_link_depends = []

if wine_build_path != ''
  lib_winecrt0 = wine_build_target[0]
  lib_ntdll = wine_build_target[1]
  lib_dbghelp = wine_build_target[2]
  winebuild = wine_build_target[3]
  winebuild_command = winebuild

  winemetal_ld_args += [
    lib_winecrt0.full_path(),
    lib_ntdll.full_path(),
    lib_dbghelp.full_path(),
  ]
  winemetal_link_depends += [lib_winecrt0, lib_ntdll, lib_dbghelp]
elif wine_install_path != ''
  lib_winecrt0 = cc.find_library('winecrt0', dirs : [ join_paths(wine_install_path,'lib/wine', windows_builtin_install_dir) ])
  lib_ntdll = cc.find_library('ntdll', dirs : [ join_paths(wine_install_path, 'lib/wine', windows_builtin_install_dir)], static: true)
  lib_dbghelp = cc.find_library('dbghelp', dirs : [ join_paths(wine_install_path, 'lib/wine', windows_builtin_install_dir) ])
  winebuild = join_paths(wine_install_path, 'bin/winebuild')
  winebuild_command = find_program(winebuild)
else
  error('One of the options -Dwine_build_path or -Dwine_install_path is required')
endif

winemetal_dll = shared_library('winemetal', winemetal_src,
  name_prefix         : '',
  dependencies        : [ airconv_forward_dep ] + (wine_install_path != '' ? [ lib_winecrt0, lib_ntdll, lib_dbghelp ] : []),
  include_directories : dxmt_include_path,
  install             : true,
  install_dir         : windows_builtin_install_dir,
  install_tag         : 'runtime-metal3',
  link_args           : winemetal_ld_args,
  link_depends        : winemetal_link_depends,
)

winemetal_dep = declare_dependency(
  link_with           : [ winemetal_dll ],
  include_directories : [ include_directories('.') ],
)

winemetal_postprocess = custom_target('postprocess_lib',
  input : winemetal_dll,
  output: 'winemetal.dll.postproc',
  command : [ winebuild_command, '--builtin', '@INPUT@' ],
  depends : [ winemetal_dll ],
  build_by_default : true
)
winemetal_runtime_targets += [winemetal_dll, winemetal_postprocess]

if cpu_family == 'x86_64'
  subdir('unix')
endif
