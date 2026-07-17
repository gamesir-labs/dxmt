# Build scripts

Product build entry points and dependency bootstrap helpers.

| Path | Role |
|------|------|
| `dxmt-builder` | Build CLI (`bootstrap` / `build` / `test` / `install` / `cache`) |
| `ci-self-hosted.sh` | Host / Wine / LLVM bootstrap backend for the builder (and CI) |
| `prepare-wine-runtime-cache.sh` | Stage Wine runtime dylibs into an install tree |
| `build-apitrace.sh` | Content-addressed apitrace helper (Meson) |
| `apitrace-mingw-toolchain.cmake` | PE toolchain for apitrace |
| `build-wine.sh` | Legacy Meson Wine builder (prefer `bootstrap wine-x64`) |
| `install-gamehub-test-dxmt.sh` | Local GameHub test package refresh |

CI notify helpers live under `.github/scripts/`.

**Tests:** use `scripts/dxmt-builder test --profile <name> ...` only.
There is no separate test script surface.
