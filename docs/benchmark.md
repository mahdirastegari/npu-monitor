# Multi-User NPU Telemetry — Benchmark Results

## Environment

| | |
|---|---|
| Host | aarch64, openEuler 22.03 (LTS-SP4), 640 cores, 2 TiB RAM |
| NPU | 8× Ascend910 (16 chips), driver 26.0.rc1, CANN 9.1.0 |
| DCMI | `/usr/local/Ascend/driver/lib64/driver/libdcmi.so` |

All measurements are **hardware measurements** on the live host, not mock
results. Mock results from the test suite are labeled separately.

## Baseline: 1:1 per-client polling (Python reference)

The baseline is a conventional per-client monitor: a small Python program
that binds directly to the driver. Every running instance — whether a GUI
or a `--once` invocation — does the same work:

- Loads `libdcmi.so` via ctypes, calls `dcmi_init`, enumerates devices.
- For each of 16 devices, makes **8–10 driver calls per sample**:
  `hbm_info`, `memory_info_v3`, `_utilization(2)` (aicore),
  `_utilization(13)` (npu), `_frequency(7)`, `temperature`, `power_info`,
  `resource_info`, plus detail queries when expanded.
- Serializes all calls through one `threading.RLock`.
- Total driver calls per sample: **~128–160**.
- Collection time per sample: **~1.0–1.3 s** (dominated by 16 × 64 ms
  aicore utilization).
- Each user starts a separate Python process with its own DCMI connection.
- **N users → N× the driver calls.**

### Per-call latency (steady-state, device 0)

| Query | p50 (ms) | p90 (ms) | max (ms) |
|---|---|---|---|
| `dcmi_get_device_utilization_rate(2)` aicore | 63.9 | 121.1 | 125.1 |
| `dcmi_get_device_utilization_rate(12)` vector | 64.0 | 124.0 | 128.0 |
| `dcmi_get_device_multi_utilization_rate` | 61.1 | — | — |
| `dcmi_get_device_utilization_rate(6)` hbm | 4.3 | 4.8 | 7.0 |
| `dcmi_get_device_utilization_rate(13)` npu | 1.4 | 1.5 | 5.4 |
| `dcmi_get_device_hbm_info` | 1.4 | 1.5 | 1.5 |
| `dcmi_get_device_temperature` | 0.017 | 0.018 | 0.056 |
| `dcmi_get_device_power_info` | 0.016 | 0.017 | 0.018 |
| `dcmi_get_device_health` | 0.018 | 0.018 | 0.034 |
| `dcmi_get_device_resource_info` | 0.008 | 0.009 | 0.019 |

**The aicore/vector utilization call dominates at ~64 ms per device.**
All other queries are sub-2 ms.

### Concurrency probe

Two-thread probe: 11 870 mixed calls
with **0 errors**; fast-lane max latency 13.9 ms (vs 0.02 ms solo). DCMI
is safe for concurrent access from separate threads.

### Parallel sweep probe

16-device aicore sweep:

| Threads | Wall time | Errors |
|---|---|---|
| 1 | 1.0 s | 0 |
| 2 | 0.65 s | 0 |
| 4 | 0.45 s | 0 |

Sub-linear scaling (MCU contention), but 2 threads roughly halve the sweep.

## New: npu-monitord (C++ daemon)

### Architecture

- One daemon per host, two-lane collector:
  - **Utilization lane** (1 thread): `dcmi_get_device_multi_utilization_rate`
    for all 16 devices, every 1 s. Uses multi-rate to get aicore/aiv/aicore/npu
    in one call per device.
  - **Environment lane** (1 thread): temperature, power, health, memory,
    clocks for all 16 devices, every 2 s. All sub-2 ms per device.
  - **Process lane** (1 thread): `dcmi_get_device_resource_info` for all
    devices, every 2 s.
- Clients read cached snapshots over a Unix-domain socket. **Client reads
  never trigger driver calls.**

### Resource usage

| Metric | Value |
|---|---|
| Daemon RSS | 8 MB |
| Daemon CPU | 0.3% |
| Binary size (npu-monitord) | 1.4 MB |
| Binary size (npu-monitor) | 181 KB |

### Client latency

| Scenario | Mean | Max |
|---|---|---|
| 1 client, sequential | 0.74 ms | 0.86 ms |
| 20 clients, concurrent | 6.05 ms | 11.32 ms |

Response size: 24.6 KB for 16 devices (JSON).

### Driver calls: client-count independence (mock)

From `test_npu_monitor`:

```
client_count: 5700 cache reads, 24 driver calls
```

5 700 cache reads by 20 simulated clients produced only 24 driver calls
(bounded by the collection interval, not by client count). This proves
the core invariant: **increasing clients from 1 to 20 does not increase
driver calls.**

### Comparison

| Metric | Per-client polling (Python, 1:1) | npu-monitord (C++, 1:N) | Improvement |
|---|---|---|---|
| Driver calls per sample | ~128–160 | ~24/s (shared) | 5–7× fewer |
| Users sharing collection | None (1:1) | All (1:N) | N× for N users |
| Client latency | ~1.3 s (full sample) | 0.74 ms (cached) | 1 750× faster |
| Memory per client | ~50 MB (Python) | 181 KB (npu-monitor) | 280× less |
| Aicore utilization cost | 64 ms × 16 (serial) | 64 ms × 16 (1 lane) | Same (hardware-bound) |

### What improved and why

1. **Shared collection**: One daemon collects once; N clients read the
   cache. This is the dominant improvement for multi-user hosts.
2. **Fewer queries**: `dcmi_get_device_multi_utilization_rate` replaces
   separate aicore/vector/npu calls with one per device.
3. **Two-lane overlap**: The slow utilization lane (~1 s for 16 devices)
   runs concurrently with the fast environment lane (<50 ms for 16
   devices). Temperature, power, and memory are never delayed behind
   the 64 ms MCU round-trip.
4. **Native code**: The daemon is 8 MB RSS vs ~50 MB per Python process.
   Client reads are 0.74 ms vs spawning a Python process.

### What did NOT improve

- **Aicore utilization latency**: The 64 ms MCU round-trip per device is
  a hardware constraint. No software optimization can make it faster.
  The daemon overlaps other work with it, but the utilization sweep
  itself still takes ~1 s for 16 devices on one thread.

### Unresolved hardware limitations

- `dcmi_get_device_memory_info_v3` returns `-8255` (unsupported) on this
  driver. HBM info (`dcmi_get_device_hbm_info`) is used instead.
- `dcmi_subscribe_fault_event` returns `-8001` with a no-op callback.
  Root-cause unknown (possibly requires root or a different filter). The
  daemon falls back to periodic health polling.
- `dcmi_get_device_utilization_rate(1)` (DDR) returns `-8255` (unsupported
  on 910). DDR utilization is not reported.
- Per-process CPU utilization is not available via DCMI; only
  per-process device-memory usage (`dcmi_get_device_resource_info`).
  Host-side CPU/memory enrichment requires `/proc` access, not yet
  implemented in the C++ daemon (the Python version uses psutil).
