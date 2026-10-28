#!/usr/bin/env bash
# install.sh — install binaries, config, and systemd unit.
#
# Requires root (uses sudo when run as a normal user).
#
# Usage:
#   scripts/install.sh            # from the repository root
#   sudo scripts/install.sh       # also fine
#
# License: see LICENSE.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

DAEMON=build/service/npu-monitord
CLIENT=build/client/npu-monitor

if [[ ! -x "$DAEMON" || ! -x "$CLIENT" ]]; then
  echo "install: binaries not found — run scripts/build.sh first" >&2
  echo "  expected: $DAEMON" >&2
  echo "  expected: $CLIENT" >&2
  exit 1
fi

# Locate cmake. Under `sudo`, root's PATH may not include a user-local
# cmake (conda, ~/.local), so prefer the exact cmake that configured the
# build — CMakeCache.txt records its absolute path.
CMAKE="$(grep -m1 '^CMAKE_COMMAND:INTERNAL=' build/CMakeCache.txt 2>/dev/null | cut -d= -f2-)"
if [[ -z "$CMAKE" || ! -x "$CMAKE" ]]; then
  CMAKE="$(command -v cmake || true)"
fi
if [[ -z "$CMAKE" ]]; then
  echo "install: cmake not found — is it installed?" >&2
  exit 1
fi

SUDO=""
if [[ $EUID -ne 0 ]]; then
  SUDO="sudo"
fi

$SUDO "$CMAKE" --install build

# Reload systemd if present (containers may not have it). Do not gate on
# is-system-running: a degraded host still needs the reload.
if command -v systemctl >/dev/null 2>&1; then
  $SUDO systemctl daemon-reload 2>/dev/null || true
  echo "install: OK"
  echo "Start the service:"
  echo "  sudo systemctl enable --now npu-monitor"
else
  echo "install: OK (systemd not detected — run the daemon directly)"
  echo "  sudo $DAEMON"
fi
