# The d3d9 suite drives two corpora, both as separate PEs it spawns rather than
# linked-in code, so the suite itself carries no d3d9 link dependency:
#   - the vendored vkd3d shader_runner over the in-tree numeric corpus
#     (see tests/dx9/shader-oracle), and
#   - Wine's own d3d9 conformance modules, built below.

# Wine's d3d9 tests are the mature reference for this API and the bar this
# frontend is measured against. They are not vendored: the repository already
# pins Wine as the external/wine submodule, which is what wine_source_path
# points at, so the corpus is fetched rather than copied and tracks that pin.
#
# Two trees are needed and they are not interchangeable. The Wine SOURCE tree
# carries include/wine/test.h and the test sources; the Wine INSTALL carries the
# PE SDK headers under include/wine/windows. A checkout without the submodule
# produces no targets, and the suite then reports the modules as unconfigured
# rather than passing on nothing.
#
# STANDALONE makes test.h emit main() and derive the test list from START_TEST,
# so no generated testlist is required. __WINESRC__ is deliberately NOT defined:
# it turns on lints meant for Wine's own sources, including a winbase.h macro
# that redefines strncpy to a deliberate error, which some toolchains then trip
# over through their own CRT headers. Neither the test sources nor test.h read
# it.

wine_conformance_tests = []
wine_tests_source_root = get_option('wine_source_path')

if dxmt_crossbuild and wine_tests_source_root != ''
  if not fs.is_absolute(wine_tests_source_root)
    wine_tests_source_root = meson.project_source_root() / wine_tests_source_root
  endif

  wine_tests_dir = wine_tests_source_root / 'dlls/d3d9/tests'
  wine_tests_headers = wine_tests_source_root / 'include'
  wine_tests_sdk = get_option('wine_install_path') / 'include/wine/windows'

  if not fs.is_file(wine_tests_headers / 'wine/test.h')
    message('wine_source_path has no include/wine/test.h; the Wine d3d9 conformance modules are disabled')
  elif not fs.is_dir(wine_tests_sdk)
    message('wine_install_path has no include/wine/windows; the Wine d3d9 conformance modules are disabled')
  else
    # Wine's headers must precede the toolchain's so its windows.h wins, while
    # the CRT headers stay reachable because the D3D headers include them.
    wine_tests_c_args = cc.get_supported_arguments([
      '-DSTANDALONE',
      # The tree defines NOMINMAX project-wide to keep windows.h's min and max
      # macros out of the C++ frontend, but these tests call them. Undefine it
      # for just these translation units, exactly as the vendored shader runner
      # does, rather than editing the upstream corpus.
      '-UNOMINMAX',
      '-I' + wine_tests_sdk,
      '-I' + wine_tests_headers,
      '-I' + wine_tests_dir,
      # The corpus is upstream Wine's and is kept unmodified, so its warnings
      # are not ours to fix and would otherwise bury a real diagnostic.
      '-w',
    ])

    # stateblock is the smallest and runs clean; device and visual carry real
    # failures and known crashes, which the baseline and the missing-summary
    # rule are there to express.
    #
    # Two modules are absent for build reasons, each needing its own answer
    # rather than a flag applied to all of them:
    #   d3d9ex includes wine/test.h before d3d9.h, so test.h's console calls are
    #     compiled before windows.h has been seen. Force-including a header
    #     ahead of it disturbs the order the others rely on.
    #   device both references and defines a large set of interface GUIDs on the
    #     build runner's toolchain, so it collides with the GUID library that
    #     the same references require. It needs a GUID resolution of its own.
    #   visual builds and runs, but crashes partway through, so a whole-module
    #     run reports only that it did not finish and says nothing about the
    #     assertions before or after. It becomes usable once each test function
    #     runs in its own process, which is what the isolation transform is for.
    foreach module : [ 'stateblock' ]
      wine_conformance_tests += executable('dx9-wine-' + module + '-test',
        [ wine_tests_dir / (module + '.c'), files('wine_debug_stubs.c') ],
        c_args: wine_tests_c_args,
        dependencies: [
          cc.find_library('d3d9'),
          # The modules reference the D3D9 interface GUIDs without defining
          # them, so the GUID library has to be linked in.
          cc.find_library('dxguid'),
          cc.find_library('user32'),
          cc.find_library('gdi32'),
          cc.find_library('ole32'),
        ],
        install: false)
    endforeach
  endif
endif

d3d9_unit_sources = files(
  'shader_oracle_spec.cpp',
  'wine_conformance_spec.cpp',
)
d3d9_unit_dependencies = []

d3d9_unit_cpp_args = []
if wine_conformance_tests.length() > 0
  d3d9_unit_cpp_args += [ '-DDXMT_D3D9_WINE_TESTS_BUILT' ]
endif

# The shader-oracle corpus lives in the source tree, so the spec cannot find it
# relative to the test image the way it finds the runner. Compile the source
# location in as a last-resort fallback so a plain build-tree run needs no
# environment. The oracle build never wants it: that drop is copied to a machine
# with no source tree, where the copies beside the image are the only correct
# answer.
if not get_option('windows_oracle_only')
  d3d9_unit_cpp_args += [
    '-DDXMT_D3D9_ORACLE_CORPUS_DIR="'
    + (meson.current_source_dir() / '..' / 'dx9' / 'shader-oracle' / 'corpus')
    + '"',
  ]
endif
