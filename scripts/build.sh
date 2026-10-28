#!/usr/bin/env bash
# build.sh — configure and build npu-monitor.
#
# Usage:
#   scripts/build.sh [extra cmake args...]
#   BUILD_TYPE=Debug scripts/build.sh
#
# License: see LICENSE.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

BUILD_TYPE="${BUILD_TYPE:-Release}"

cmake -B build -DCMAKE_BUILD_TYPE="$BUILD_TYPE" "$@"
cmake --build build -j"$(nproc)"

echo
echo "build: OK (${BUILD_TYPE})"
echo "  daemon : build/service/npu-monitord"
echo "  client : build/client/npu-monitor"
echo "  tests  : build/service/tests/test_npu_monitor"
