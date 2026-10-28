# NPU Telemetry — Ascend NPU Monitoring as a Service

A lightweight C++20 monitoring stack for Ascend NPUs: `npu-monitord` samples
the DCMI driver interface once and serves cached snapshots to any number of
clients over a Unix-domain socket. Clients never touch the driver — they read
instant, consistent snapshots from the daemon's cache.

**Why?** When multiple users monitor the same host, a shared daemon replaces
per-client driver access: 128–160 driver calls per sample become a single
collection cycle. When nobody is watching, the daemon **sleeps entirely** —
zero driver calls.

| Component | Version | Description |
|---|---|---|
| [`service/`](service/) | `1.0.0` | `npu-monitord` — the telemetry daemon |
| [`client/`](client/) | `1.0.0` | `npu-monitor` — terminal dashboard client |
| [`docs/`](docs/) | — | Wire protocol, design notes, benchmarks |

## Features

- **Two-lane collector** — one thread for the slow aicore utilization sweep
  (~64 ms MCU round-trip per device), one for fast environment queries
  (temperature, power, memory, health). The slow lane never blocks the fast
  lane.
- **Idle/wake power management** — the daemon sleeps when no client is
  connected and wakes instantly when one connects.
- **Client-count independence** — 1 client or 20 clients, the driver call
  count stays the same.
- **Atomic snapshots** — readers always see a complete, consistent snapshot.
  No partial updates, ever.
- **Versioned JSON protocol** — every metric carries a value, unit, source
  function name, timestamp, and validity state (`ok`, `stale`, `unsupported`,
  `error`, `pending`).
- **Standalone client** — the client depends only on the socket protocol;
  build and run it anywhere, no Ascend SDK needed.
- **No external libraries** — JSON is hand-rolled, DCMI is loaded
  dynamically with `dlopen`, threading uses the C++ standard library.

## Install

### Requirements

- **Linux** (aarch64 or x86_64)
- **C++20 compiler** (GCC 10+, Clang 12+) and **CMake 3.16+**
- **git** — the build fetches [FTXUI](https://github.com/ArthurSonzogni/FTXUI)
  for the dashboard at configure time
- **Ascend driver** at runtime only (`libdcmi.so`, `/dev/davinci*` devices).
  Building does not require the SDK.

On Debian/Ubuntu:

```bash
sudo apt-get install -y build-essential cmake git
```

On openEuler/RHEL:

```bash
sudo dnf install -y gcc-c++ cmake git
```

### 1. Clone

```bash
git clone https://github.com/mahdirastegari/npu-monitor.git
cd npu-monitor
```

### 2. Build

```bash
./scripts/build.sh
```

This configures and builds a Release build in `build/`. To build with a
different configuration:

```bash
BUILD_TYPE=Debug ./scripts/build.sh     # debug build with symbols
./scripts/build.sh -DNPU_MONITOR_BUILD_TESTS=OFF   # skip tests
```

Optional: run the test suite (uses a mock backend, no NPU required):

```bash
./scripts/test.sh
```

### 3. Install and run

```bash
sudo ./scripts/install.sh
sudo systemctl enable --now npu-monitor
```

`install.sh` installs both binaries to `/usr/local/bin`, the configuration
to `/usr/local/etc/npu_monitor/`, and the systemd unit. Then connect:

```bash
npu-monitor                        # live dashboard (default command)
npu-monitor snapshot               # one-shot JSON
```

To run without systemd (foreground, Ctrl-C to stop):

```bash
sudo /usr/local/bin/npu-monitord /usr/local/etc/npu_monitor/npu-monitor.conf
```

To remove everything: `sudo ./scripts/uninstall.sh`.

Binaries produced by the build:

| Binary | Description |
|---|---|
| `build/service/npu-monitord` | The daemon (runs in foreground, designed for systemd) |
| `build/client/npu-monitor` | The CLI client |
| `build/service/tests/test_npu_monitor` | Test suite (no hardware required) |

### Scripts

| Script | Purpose |
|---|---|
| `scripts/build.sh` | Configure + parallel build (`BUILD_TYPE=Debug` to override) |
| `scripts/test.sh` | Run the test suite (mock backend, no NPU needed) |
| `scripts/install.sh` | Install binaries, config, and systemd unit |
| `scripts/uninstall.sh` | Remove binaries, config, systemd unit, and sockets |
| `scripts/clean.sh` | Remove all build artifacts |

### Build options

| Option | Default | Description |
|---|---|---|
| `CMAKE_BUILD_TYPE` | `Release` | `Debug` for tests, `Release` for deployment |
| `NPU_MONITORD_VERSION` | `1.0.0` | Daemon version reported by `npu-monitord --version` |
| `NPU_MONITOR_VERSION` | `1.0.0` | Client version reported by `npu-monitor --version` |
| `NPU_MONITOR_BUILD_TESTS` | `ON` | Set to `OFF` to skip building tests |

### Versions

The service and the client are versioned independently and both start at
`1.0.0`. Check the version of an installed binary:

```bash
npu-monitord --version
npu-monitor --version
```

The service version is bumped on any wire-protocol change (see
[docs/protocol.md](docs/protocol.md)); the client version tracks the
dashboard and CLI.

## Performance

Measured on aarch64 openEuler 22.03, 8× Ascend910 (16 chips), driver 26.0.rc1:

| Metric | Per-client polling (Python, 1:1) | npu-monitord (daemon, 1:N) |
|---|---|---|
| Driver calls per sample | ~128–160 per user | ~24/s shared (all users) |
| Client latency | ~1.3 s (full sample) | 0.74 ms (cached read) |
| Memory per client | ~50 MB (Python) | 181 KB (`npu-monitor`) |
| Daemon memory | N/A | 8 MB RSS |

The 64 ms per-device aicore utilization MCU round-trip is hardware-bound and
cannot be improved. The daemon overlaps fast queries with it and shares the
result across all clients.

## Documentation

| Doc | Contents |
|---|---|
| [docs/protocol.md](docs/protocol.md) | **The wire protocol** — socket, requests, snapshot JSON schema |
| [service/README.md](service/README.md) | Daemon: install, systemd, configuration, permissions, data sources |
| [client/README.md](client/README.md) | Client: usage, dashboard, writing your own client |
| [docs/design.md](docs/design.md) | Architecture and data contract |
| [docs/benchmark.md](docs/benchmark.md) | Measured performance on real hardware |

## License

[Apache License 2.0](LICENSE).

The license permits commercial use, modification, and redistribution, but
requires [NOTICE](NOTICE) attribution to be preserved and modified files to
carry a statement of change — so anyone who builds on this project keeps the
credit in place.

### Building your own version

This project is meant to be extended. If you build your own service or
client on top of it — a GUI, a web dashboard, a different collector, a
smaller daemon — you are welcome to, and the goal is that you keep it
compatible where it makes sense:

- **Service side** — keep the wire protocol documented in
  [docs/protocol.md](docs/protocol.md) (or publish a new protocol version) so
  existing clients keep working.
- **Client side** — the client is deliberately standalone; write yours
  against the protocol and it will work against any conforming daemon.

Open an issue or a pull request if you want a feature merged here instead.
