#!/usr/bin/env bash
# test.sh — run the test suite (mock backend, no NPU hardware required).
#
# Usage:
#   scripts/test.sh
#
# License: see LICENSE.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

if [[ ! -x build/service/tests/test_npu_monitor ]]; then
  echo "test: build/service/tests/test_npu_monitor not found — run scripts/build.sh first" >&2
  exit 1
fi

./build/service/tests/test_npu_monitor
