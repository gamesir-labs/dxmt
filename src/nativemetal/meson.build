winemetal_src = [
  '../winemetal/winemetal_thunks.c',
  '../winemetal/unix/winemetal_unix.c',
  '../winemetal/unix/cache.c',
]
winemetal_link_depends = apitrace_metal_native_link_depends

winemetal_ld_args = [
  '-weak_framework', 'CoreFoundation', '-weak_framework', 'Metal',
  '-framework', 'MetalFX', '-weak_framework', 'Foundation',
  '-weak_framework', 'CoreGraphics', '-weak_framework', 'QuartzCore',
  '-weak_framework', 'ColorSync',   '-weak_framework', 'Cocoa',
  '-lsqlite3',
]

winemetal_native_deps = [ airconv_dep_darwin ] + apitrace_metal_native_deps
winemetal_native_include_dirs = [ dxmt_include_path, include_directories('.') ] + apitrace_metal_include_path
winemetal_native_c_args = ['-ObjC', '-DDXMT_NATIVE=1']
if dxmt_apitrace_metal_enabled
  winemetal_native_c_args += ['-DDXMT_APITRACE_METAL=1']
endif

winemetal_dll = shared_library('winemetal', winemetal_src,
  name_prefix         : '',
  dependencies        : winemetal_native_deps,
  include_directories : winemetal_native_include_dirs,
  c_args              : winemetal_native_c_args,
  link_args           : winemetal_ld_args,
  build_rpath         : '@loader_path/:@loader_path/../../',
)

winemetal_dep = declare_dependency(
  link_with           : [ winemetal_dll ],
  include_directories : [ include_directories('../winemetal') ],
)

winemetal4_src = [
  '../winemetal4/airconv_thunks.c',
  '../winemetal4/winemetal_thunks.c',
  '../winemetal4/unix/winemetal_unix.c',
  '../winemetal4/unix/iogpu_diagnostics.c',
  '../winemetal4/unix/metal4_cpp.cpp',
  '../winemetal4/unix/cache.c',
]

winemetal4_ld_args = [
  '-weak_framework', 'CoreFoundation', '-weak_framework', 'Metal',
  '-framework', 'MetalFX', '-weak_framework', 'Foundation',
  '-weak_framework', 'CoreGraphics', '-weak_framework', 'QuartzCore',
  '-weak_framework', 'ColorSync',   '-weak_framework', 'Cocoa',
  '-lsqlite3',
]

winemetal4_native_deps = [ airconv_dx12_metal4_dep_darwin ] + apitrace_metal_native_deps
winemetal4_native_include_dirs = [
  dxmt_include_path,
  include_directories('.'),
  include_directories('../winemetal4'),
  include_directories('../../external/metal-cpp'),
] + apitrace_metal_include_path
winemetal4_native_c_args = ['-ObjC', '-DDXMT_NATIVE=1']
if dxmt_apitrace_metal_enabled
  winemetal4_native_c_args += ['-DDXMT_APITRACE_METAL=1']
endif

winemetal4_dll = shared_library('winemetal4', winemetal4_src,
  name_prefix         : '',
  dependencies        : winemetal4_native_deps,
  include_directories : winemetal4_native_include_dirs,
  c_args              : winemetal4_native_c_args,
  cpp_args            : ['-fblocks'],
  link_args           : winemetal4_ld_args,
  build_rpath         : '@loader_path/:@loader_path/../../',
)

winemetal4_dep = declare_dependency(
  link_with           : [ winemetal4_dll ],
  include_directories : [ include_directories('../winemetal4') ],
)
