# GitHub Actions helpers

| Path | Role |
|------|------|
| `ci-feishu.ps1` | PR / issue cards (Windows self-hosted) |
| `ci-feishu.sh` | Aggregated push CI / Component result cards (macOS self-hosted) |
| `commit-types.py` | Classify every commit message in a push event for CI routing |

Product tests: `scripts/dxmt-builder test ...` only.
