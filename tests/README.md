# DXMT tests

DXMT unit tests use GoogleTest and run as native macOS executables. Keeping the
unit-test runner native avoids adding Wine process startup to test scheduling
and lets Meson parallelize test executables directly.

Configure, build, and run the tests with:

```sh
meson setup \
  -Dnative_llvm_path=/usr/local/opt/llvm@15 \
  -Denable_tests=true \
  build-tests \
  --buildtype release
meson compile -C build-tests dxmt-unit-tests
meson test -C build-tests --suite unit --print-errorlogs
```

Use a separate cross-build directory for Wine-facing DLLs. GoogleTest unit
tests intentionally reject cross-build configuration so the test runner and
its scheduler never pay Wine process startup overhead.

GoogleTest is compiled from the pinned source snapshot in
`external/googletest`.
