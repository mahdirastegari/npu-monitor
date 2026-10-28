#!/usr/bin/env bash
# uninstall.sh — remove npu-monitor from this machine.
#
# Stops and disables the service, removes binaries, the systemd unit,
# configuration files, and sockets. Safe to run repeatedly.
#
# Usage:
#   scripts/uninstall.sh [prefix]    # default prefix: /usr/local
#
# License: see LICENSE.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

PREFIX="${1:-/usr/local}"

if [[ -z "$PREFIX" || "$PREFIX" == "/" ]]; then
  echo "uninstall: refusing unsafe prefix: '$PREFIX'" >&2
  exit 1
fi

SUDO=""
if [[ $EUID -ne 0 ]]; then
  SUDO="sudo"
fi

count=0

remove() {
  local path="$1"
  if [[ -e "$path" || -L "$path" ]]; then
    $SUDO rm -rf "$path"
    echo "  removed: $path"
    count=$((count + 1))
  fi
}

echo "Stopping and disabling services..."
for svc in npu-monitor; do
  $SUDO systemctl stop "$svc.service" 2>/dev/null || true
  $SUDO systemctl disable "$svc.service" 2>/dev/null || true
  $SUDO systemctl reset-failed "$svc.service" 2>/dev/null || true
done

echo "Removing binaries..."
for bin_dir in "$PREFIX/bin" "/usr/bin"; do
  for bin in npu-monitord npu-monitor; do
    remove "$bin_dir/$bin"
  done
done

echo "Removing systemd units..."
for unit_dir in "$PREFIX/lib/systemd/system" "/lib/systemd/system" "/usr/lib/systemd/system" "/etc/systemd/system"; do
  for unit in npu-monitor.service; do
    remove "$unit_dir/$unit"
    remove "$unit_dir/multi-user.target.wants/$unit"
  done
done

echo "Removing configuration..."
for conf_dir in "$PREFIX/etc" "/etc"; do
  for conf in npu_monitor; do
    remove "$conf_dir/$conf"
  done
done

echo "Removing sockets..."
for run_dir in /var/run /run; do
  for sock in npu-monitor.sock; do
    remove "$run_dir/$sock"
  done
done

echo "Reloading systemd..."
$SUDO systemctl daemon-reload 2>/dev/null || true

echo
if [[ $count -eq 0 ]]; then
  echo "uninstall: nothing to remove — system is clean"
else
  echo "uninstall: removed $count item(s)"
fi
