#!/usr/bin/env bash
# clean.sh — remove all build artifacts.
#
# Usage:
#   scripts/clean.sh
#
# License: see LICENSE.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

rm -rf build
echo "clean: removed build/"
