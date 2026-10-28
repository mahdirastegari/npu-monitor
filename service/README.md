# npu-monitord — Ascend NPU telemetry daemon

`npu-monitord` samples the DCMI driver interface once per host and serves
cached snapshots to any number of clients over a Unix-domain socket. Clients
never touch the driver — they read instant, consistent snapshots from the
daemon's cache.

- **Two-lane collector** — one thread for the slow aicore utilization sweep
  (~64 ms MCU round-trip per device), one for fast environment queries
  (temperature, power, memory, health). The slow lane never blocks the fast
  lane.
- **Idle/wake power management** — the daemon sleeps when no client is
  connected and wakes instantly when one connects. Each client request keeps
  the daemon awake for a configurable window (default 2 s).
- **Client-count independence** — 1 client or 20 clients, the driver call
  count stays the same. Clients read cached snapshots, never trigger
  hardware queries.
- **Atomic snapshots** — readers always see a complete, consistent snapshot.
  No partial updates, ever.
- **Versioned JSON protocol** — every metric carries a value, unit, source
  function name, timestamp, and validity state ([protocol](../docs/protocol.md)).
- **No link-time DCMI dependency** — the driver library is loaded via
  `dlopen`, so the binary runs even if the SDK isn't installed at build time.

## Requirements

- **Linux** (aarch64 or x86_64)

The daemon version is reported by `npu-monitord --version` (currently
`1.0.0`) and can be set at configure time with
`-DNPU_MONITORD_VERSION=<x.y.z>`.
- **C++20 compiler** (GCC 10+, Clang 12+), **CMake 3.16+**
- **Ascend driver** at runtime (`libdcmi.so`, `/dev/davinci*` devices)

No external libraries — JSON serialization is hand-rolled, DCMI is loaded
dynamically, threading uses the C++ standard library.

## Build

From the repository root:

```bash
./scripts/build.sh
```

Or manually:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

The daemon binary is `build/service/npu-monitord`.

## Install as a systemd service

```bash
sudo ./scripts/install.sh
sudo systemctl enable --now npu-monitord
```

This installs:

| File | Path |
|---|---|
| Daemon binary | `/usr/local/bin/npu-monitord` |
| systemd unit | `/usr/local/lib/systemd/system/npu-monitord.service` |
| Configuration | `/usr/local/etc/npu_monitor/npu-monitor.conf` |

> **Note:** The systemd unit is generated at install time with the correct
> `ExecStart` path. If you prefer installing to `/usr` instead of
> `/usr/local`, run `sudo cmake --install build --prefix /usr`.

Verify and manage:

```bash
systemctl status npu-monitord          # is it running?
journalctl -u npu-monitord -f          # view logs
sudo systemctl restart npu-monitord    # restart after config changes
sudo systemctl disable --now npu-monitord   # remove from startup
```

### Upgrading from a previous install

```bash
sudo ./scripts/uninstall.sh             # remove old binaries, units, configs, sockets
sudo ./scripts/install.sh               # then reinstall and restart
```

## Uninstall

```bash
sudo ./scripts/uninstall.sh
```

Removes the service, binaries, configuration, and sockets. Safe to run
repeatedly. Accepts an optional install prefix (default `/usr/local`):

```bash
sudo ./scripts/uninstall.sh /usr    # if installed with --prefix /usr
```

## Running without systemd

The daemon runs in the foreground — pass it a config file and send `SIGTERM`
or Ctrl-C to shut down:

```bash
sudo ./build/service/npu-monitord /usr/local/etc/npu_monitor/npu-monitor.conf
```

For testing without root, use a config with a socket path in `/tmp`:

```bash
cat > /tmp/npu-monitord-test.conf << 'EOF'
socket_path=/tmp/npu-monitor.sock
idle_timeout_ms=2000
log_level=info
EOF

./build/service/npu-monitord /tmp/npu-monitord-test.conf
```

## Configuration

The config file is simple `key=value` lines. All values have sensible
defaults — the file can be empty. Edit
`/usr/local/etc/npu_monitor/npu-monitor.conf`:

```ini
# --- Collection intervals (milliseconds) ---
utilization_interval_ms=1000    # aicore/vector/npu utilization
env_interval_ms=2000            # temperature, power, memory, health, clocks
process_interval_ms=2000        # per-process device-memory attribution
pcie_interval_ms=0              # 0 = disabled; 5000-10000 to enable

# --- Collector tuning ---
utilization_workers=1           # threads for aicore sweep (1 recommended)
stale_after_ms=5000             # mark readings stale after this
error_threshold=3               # consecutive errors before stale state

# --- Idle/wake power management ---
# Sleep after this many ms with no client activity.
# 0 = always collect. Default: 2000 (2 seconds).
idle_timeout_ms=2000

# --- Socket server ---
socket_path=/var/run/npu-monitor.sock
max_frame_bytes=4194304
max_subscribers=64

# --- Startup ---
ready_timeout_ms=30000          # max wait for first snapshot

# --- Logging ---
log_level=info                  # debug | info | warn | error
log_file=                       # empty = stderr (journald captures it)
```

Restart after changes: `sudo systemctl restart npu-monitord`.

## Idle/wake behavior

The daemon does not collect when nobody is watching:

1. **No client for 2 s** → all lanes sleep on a condition variable. Zero
   driver calls, zero CPU.
2. **Client connects** → the request wakes all lanes instantly. The first
   request gets the last cached snapshot (marked `"daemon_state": "idle"`).
   The lanes collect immediately and subsequent requests get fresh data
   (`"daemon_state": "ready"`).
3. **Each request resets the timer** — as long as clients are actively
   requesting, the daemon stays awake.
4. **Clients stop** → after 2 s of inactivity, the daemon sleeps again.

Set `idle_timeout_ms=0` to disable this feature (daemon always collects).

## Device permissions

The daemon needs read access to DCMI:

- `/dev/davinci*` device files
- `/dev/davinci_manager`
- `/dev/devmm_svm`
- `libdcmi.so` in the driver library path

**Running as root** satisfies all requirements and enables full process
attribution. For a dedicated user, add it to the device group (typically
`HwHiAiUser`):

```bash
sudo usermod -aG HwHiAiUser npu-monitord
```

Then set `User=npu-monitord` in the systemd unit. Process attribution
(`dcmi_get_device_resource_info`) may require root or the `HwHiAiUser` group;
device totals (utilization, temperature, power, memory) work without it.

## Where the data comes from

### Device metrics — DCMI

All device metrics are read through the DCMI driver interface, loaded at
runtime with `dlopen`:

| Metric | DCMI function | Notes |
|---|---|---|
| AICore / Vector / NPU utilization | `dcmi_get_device_multi_utilization_rate` | One call returns all three. ~64 ms per device (MCU round-trip). |
| HBM memory used / total | `dcmi_get_device_hbm_info` | Unsupported (-8255) on some driver versions; reported as `unsupported`, never as zero. |
| Temperature, power, AICore frequency | `dcmi_get_device_temperature`, `dcmi_get_device_power_info`, `dcmi_get_device_frequency` | Fast lane. |
| Health | `dcmi_get_device_health` | Bitmap: 0 = healthy. |
| Device list / model / bus | `dcmi_get_card_list`, `dcmi_get_device_chip_info`, `dcmi_get_device_pci_info` | Enumerated at startup. |

### Process data — DCMI + /proc

DCMI reports **only** the process id and its NPU memory usage — there is no
DCMI API for the process owner or command name. `npu-monitord` enriches
these fields from `/proc`:

| Field | Source | Fallback |
|---|---|---|
| `pid` | DCMI `proc_id` | — |
| `memory_mib` | DCMI `proc_mem_usage` ÷ 1024² (driver reports **bytes**) | — |
| `user` | `/proc/<pid>/status` (`Uid:`) → `/etc/passwd` | empty |
| `command` | `/proc/<pid>/cmdline` (full argv) | `/proc/<pid>/comm` (15-char kernel name) |

### Container-namespace PIDs

For workloads running inside Docker/Kubernetes, DCMI reports the
**container-namespace PID**, which does not exist in the host `/proc`. Those
processes cannot be resolved to a user from the host; the snapshot marks
them `pid_namespace: "container"` (see [protocol](../docs/protocol.md)).

## Testing

Tests use a mock DCMI backend — **no NPU hardware required**:

```bash
./scripts/test.sh
```

| Test | Invariant |
|---|---|
| `client_count` | 5 700 cache reads by 20 clients produce only 48 driver calls — client count ⊥ driver calls |
| `slow_client` | Cache reads stay sub-millisecond during slow collection |
| `consistency` | 200 snapshots read concurrently are all internally consistent |
| `fault_injection` | Failed queries are reported as `error`/`unsupported`, never as zero |
| `stale_retention` | Original timestamps are preserved during failures |
| `idle_wake` | Zero driver calls while idle, resumed instantly on touch |
| `restart` | Collector stops and restarts cleanly |
| `process_access` | Permission-denied on process data produces no fabricated PIDs |

## Architecture

See [docs/design.md](../docs/design.md) for the two-lane architecture and
data contract, [docs/protocol.md](../docs/protocol.md) for the wire format,
and [docs/benchmark.md](../docs/benchmark.md) for measured performance on
real hardware.
