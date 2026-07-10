# Google Benchmark

This directory contains the Google Benchmark sources required by DXMT's native
performance benchmarks. The files are vendored directly into the repository
and are not a Git submodule or package-manager dependency.

- Upstream version: `1.9.5`
- Source archive: `https://github.com/google/benchmark/archive/refs/tags/v1.9.5.tar.gz`
- Source SHA-256: `9631341c82bac4a288bef951f8b26b41f69021794184ece969f8473977eaa340`
- License: Apache 2.0; see `LICENSE`

Only the upstream `include` and `src` trees are retained. Updating Google
Benchmark requires an explicit replacement of this snapshot and a review of
the resulting source diff.
