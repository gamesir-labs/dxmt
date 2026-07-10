# GoogleTest

This directory contains the GoogleTest sources required by DXMT's native unit
tests. The files are vendored directly into the repository and are not a Git
submodule or a Meson WrapDB dependency.

- Upstream version: `1.17.0`
- Source archive: `https://github.com/google/googletest/archive/refs/tags/v1.17.0.tar.gz`
- Source SHA-256: `65fab701d9829d38cb77c14acdc431d2108bfdbf8979e40eb8ae567edf10b27c`
- License: BSD 3-Clause; see `LICENSE`

Only the `googletest/include` and `googletest/src` trees are retained.
GoogleMock and upstream build-system files are intentionally omitted. Updating
GoogleTest requires an explicit replacement of this snapshot and a review of
the resulting source diff.
