#!/bin/sh

# This file is the single source of truth for DXMT Conventional Commit
# keywords. Keep entries lowercase, unique, and separated by spaces.

DXMT_CONVENTIONAL_COMMIT_TYPES='feat fix docs style refactor perf test build chore revert merge'

# Scopes are stable repository modules or established D3D feature domains.
# Adding a scope requires updating this reviewed list before using it.
DXMT_CONVENTIONAL_COMMIT_SCOPES='airconv allocation apitrace benchmark binding builder cache capability capture ci command component coverage d3d d3d9 d3d10 d3d11 d3d12 deps descriptor device diagnostics dxbc dxgi dxil dxmt feishu format framework gamehub git graphics heap layout llvm log meson metal nativemetal nightly nvapi nvngx object package pipeline query queue repo residency resource root_signature runtime scheduler shader sparse state swapchain sync threading transfer util windows wine winemetal winemetal4'
