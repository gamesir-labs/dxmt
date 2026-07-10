# DXMT tests

DXMT unit tests use GoogleTest and run as native macOS executables. Keeping the
unit-test runner native avoids adding Wine process startup to test scheduling
and lets Meson parallelize test executables directly.

## Layout

```text
tests/
  include/dxmt_test.hpp  Stable include for all tests
  support/main.cpp       Shared DXMT test runner entry point
  unit/meson.build       Unit-test suite manifest
  unit/placeholder_test.cpp  Minimal copyable test example
  unit/layout_test.cpp   Test-infrastructure layout contract
  unit/*_test.cpp        Unit-test sources grouped by subsystem
```

`tests/meson.build` owns the common runner and dependency wiring. Individual
test suites must not create their own runner or repeat GoogleTest dependencies.
`layout_test.cpp` intentionally fails if core test-infrastructure files are
moved without updating the contract.

## Add a test

Add the test source under `tests/unit/`, include the DXMT test header, and use
GoogleTest normally:

```cpp
#include <dxmt_test.hpp>

TEST(ExampleTest, DoesSomething) {
  EXPECT_EQ(1 + 1, 2);
}
```

Then add the file to an existing entry in `tests/unit/meson.build`:

```meson
'sources': files(
  'existing_test.cpp',
  'new_test.cpp',
),
```

To create an independently schedulable suite, add one dictionary entry. The
shared loop creates both the executable and Meson test registration:

```meson
'util': {
  'sources': files('util_test.cpp'),
  'dependencies': [util_dep],
},
```

## Mark slow tests

`slow` is a relative scheduling hint within the unit-test set. A slow unit test
might take hundreds of milliseconds instead of a few milliseconds, but it is
still a correctness test and is always included in the normal `unit` run.

Keep relatively slow cases in a separate source/suite and mark the suite with
`'slow': true`. Slow suites receive priority 100 and are started before normal
suites, which reduces parallel scheduling tail latency. The marker does not
change the default 30-second timeout and does not make the test optional.

```meson
'pipeline-cache': {
  'sources': files('pipeline_cache_test.cpp'),
  'dependencies': [dxmt_dep],
  'slow': true,
},
```

`unit-fast` and `unit-slow` labels are available for diagnostics, but the
standard command runs the shared `unit` label so both classes always execute.
Do not mix relatively slow cases into a fast suite: keeping them in a separate
executable gives Meson a useful scheduling boundary.

Performance benchmarks are not unit tests and must not be registered in this
manifest. They have a separate dependency, runner, directory, Meson
abstraction, and acceptance policy under `benchmarks/`; see
`benchmarks/README.md`.

## Build and run

Configure, build, and run all unit-test suites with:

```sh
meson setup \
  -Dnative_llvm_path=/usr/local/opt/llvm@15 \
  -Denable_tests=true \
  build-tests \
  --buildtype release
meson compile -C build-tests dxmt-unit-tests
# Runs every correctness test, including relatively slow suites.
meson test -C build-tests --suite unit --print-errorlogs
```

Use a separate cross-build directory for Wine-facing DLLs. GoogleTest unit
tests intentionally reject cross-build configuration so the test runner and
its scheduler never pay Wine process startup overhead.

GoogleTest is compiled from the pinned source snapshot in
`external/googletest`.
