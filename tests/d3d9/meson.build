# The d3d9 suite drives the vendored vkd3d shader_runner over the in-tree
# corpus. The runner is a separate PE (see tests/dx9/shader-oracle) rather than
# linked-in code, so this suite carries no d3d9 link dependency of its own.

d3d9_unit_sources = files(
  'shader_oracle_spec.cpp',
)
d3d9_unit_dependencies = []

# The corpus lives in the source tree, so the spec cannot find it relative to
# the test image the way it finds the runner. Compile the source location in as
# a last-resort fallback so a plain build-tree run needs no environment. The
# oracle build never wants it: that drop is copied to a machine with no source
# tree, where the copies beside the image are the only correct answer.
d3d9_unit_cpp_args = []
if not get_option('windows_oracle_only')
  d3d9_unit_cpp_args += [
    '-DDXMT_D3D9_ORACLE_CORPUS_DIR="'
    + (meson.current_source_dir() / '..' / 'dx9' / 'shader-oracle' / 'corpus')
    + '"',
  ]
endif
