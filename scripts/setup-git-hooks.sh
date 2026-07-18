#!/bin/sh
set -eu

repo_root=$(git rev-parse --show-toplevel)
git -C "$repo_root" config core.hooksPath .githooks
printf '%s\n' 'DXMT Git hooks enabled via core.hooksPath=.githooks'
