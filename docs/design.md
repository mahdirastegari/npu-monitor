# npu-monitord — Design Notes

## What this is

`npu-monitord` is a single-host NPU telemetry daemon. It samples Ascend NPUs once per
host via the DCMI interface and serves cached snapshots to any number of
lightweight clients over a Unix-domain socket. Clients never touch the driver.

## Baseline findings (this host)

Measured on aarch64 openEuler 22.03, 8× Ascend910 (16 chips), driver 26.0.rc1:

| Query | p50 (ms) | p90 (ms) | max (ms) |
|---|---|---|---|
| `dcmi_get_device_utilization_rate(2)` aicore | 63.9 | 121.1 | 125.1 |
| `dcmi_get_device_utilization_rate(12)` vector | 64.0 | 124.0 | 128.0 |
| `dcmi_get_device_multi_utilization_rate` | 61.1 | — | — |
| `dcmi_get_device_utilization_rate(6)` hbm | 4.3 | 4.8 | 7.0 |
| `dcmi_get_device_utilization_rate(13)` npu | 1.4 | 1.5 | 5.4 |
| `dcmi_get_device_utilization_rate(10)` hbm_bw | 1.4 | 1.4 | 4.1 |
| `dcmi_get_device_utilization_rate(3)` aicpu | 0.008 | 0.009 | 0.03 |
| `dcmi_get_device_hbm_info` | 1.4 | 1.5 | 1.5 |
| `dcmi_get_device_temperature` | 0.017 | 0.018 | 0.056 |
| `dcmi_get_device_power_info` | 0.016 | 0.017 | 0.018 |
| `dcmi_get_device_health` | 0.018 | 0.018 | 0.034 |
| `dcmi_get_device_memory_info_v3` | unsupported (-8255) | — | — |
| `dcmi_get_device_resource_info` | 0.008 | 0.009 | 0.019 |

**Aicore and vector utilization each cost ~64 ms per device.** For 16 devices
that is ~1 s of driver time per full sweep. All other metrics are sub-2 ms.

## Architecture

```
┌──────────────────────────────────────────────────────┐
│ npu-monitord (one per host)                                │
│                                                      │
│  ┌──────────────┐   ┌─────────────────────────────┐   │
│  │ slow lane    │   │ fast lane                   │   │
│  │ utilization  │   │ temp/power/health/mem/proc  │   │
│  │ 2 workers    │   │ 1 worker                    │   │
│  └──────┬───────┘   └──────────────┬──────────────┘   │
│         │                        │                  │
│         └────────┬───────────────┘                  │
│                  ▼                                  │
│        ┌──────────────────┐                          │
│        │ SnapshotCache    │ immutable shared_ptr     │
│        └────────┬─────────┘                          │
│                 ▼                                    │
│        ┌──────────────────┐                          │
│        │ UnixSocketServer │ /var/run/npu-monitor.sock    │
│        └──────────────────┘                          │
└──────────────────────────────────────────────────────┘
             ▲              ▲
      npu-monitor snapshot   npu-monitor watch
```

Two lanes because the 64 ms aicore/vector round-trip would otherwise delay
cheap queries behind it. A two-thread concurrency probe measured 12 000+
mixed calls with zero errors and only 13.9 ms worst-case added latency, so
lane separation is safe on this driver. The utilization lane itself runs
2 workers across devices (measured 16-device aicore sweep: 1T 1.0 s → 2T
0.65 s → 4T 0.45 s).

## Data contract (snapshot v1)

Each snapshot is immutable once published. Fields:

- `generation` — daemon generation identifier, increments on collector restart.
- `sequence` — monotonic per-generation sample counter.
- `collected_at` — wall-clock ms of the newest metric in this snapshot.
- `daemon_state` — `init`, `ready`, `idle`, `stale`, or `shutdown`.
- `lanes[]` — per-lane status: `{name, state, consecutive_errors, total_errors,
  last_ok_at, last_attempt_at, last_error}`. Lane states: `idle`, `collecting`,
  `ok`, `error`, `unsupported`.
- `devices[]` — one entry per chip, each carrying `card_id`, `chip_id`,
  `physical_id`, `logical_id`, `model`, `bus_id`.
- `metrics{}` — per metric: `{value, unit, source, acquired_at, state, reason}`.
  States: `ok`, `stale`, `unsupported`, `permission`, `error`, `pending`.
- `processes[]` — `{device, pid, memory_bytes, pid_namespace, user, command}`
  when readable.
- `events[]` — recent fault/recovery events drained from the bounded queue.
- `collection_ms` — wall time of the last full sweep.
- `errors[]` — rate-limited diagnostic lines.

A valid zero is `state:"ok"`, `value:0`. Unsupported is `state:"unsupported"`.
Stale carries the original `acquired_at` of the last good reading.

## Sampling cadence

| Data | Policy |
|---|---|
| Utilization + device memory | 1 s (via `dcmi_get_device_multi_utilization_rate` when supported, else per-selector calls) |
| Temperature / power / clocks | 2 s |
| Process / device-memory info | 2 s |
| PCIe profiling | disabled by default; 5–10 s when enabled |
| Inventory / capabilities | startup + rediscovery |

Deadlines are monotonic; missed deadlines are skipped, never bursted.

## Idle/wake power management

When nobody is watching, the daemon stops making driver calls entirely. This
eliminates the ~1 s/s utilization sweep cost when no client is connected.

**Strategy:**

1. An `ActivityTracker` records the timestamp of the last client request.
2. Each lane checks `is_active()` before every collection cycle. If no client
   has requested data within `idle_timeout_ms` (default 2000 ms), the lane
   sleeps on a condition variable — **zero driver calls**.
3. When a client connects, the socket server calls `touch()`, which wakes all
   sleeping lanes instantly via the condition variable.
4. The lanes collect immediately and publish fresh snapshots.
5. Each subsequent client request resets the awake window. The daemon stays
   awake as long as clients are actively requesting (e.g., `npu-monitor watch`).
6. After `idle_timeout_ms` with no client activity, all lanes go back to sleep.

**First request after idle:** the client receives the last cached snapshot
(marked `daemon_state: "idle"`). Within 1–2 seconds the lanes produce fresh
data and subsequent requests get `daemon_state: "ready"`.

**Configuration:** `idle_timeout_ms=0` disables the feature (daemon always
collects). The default is 2000 ms.

## Fault events

`dcmi_subscribe_fault_event` is probed at startup with a no-op callback. If it
subscribes, events are copied into a bounded queue (1024 entries) from inside
the callback and processed on a worker thread. Overflow increments a drop
counter and triggers health reconciliation. If subscription fails, the
daemon continues with periodic health sampling and logs once.

## Socket protocol

Length-prefixed JSON frames over `AF_UNIX` (SOCK_SEQPACKET):

```json
{"v":1,"op":"snapshot"}
{"v":1,"op":"subscribe"}
{"v":1,"op":"unsubscribe"}
```

Server replies with a framed JSON snapshot. Frames are bounded to 4 MiB.
Subscriber queues coalesce: a slow subscriber always receives the latest
snapshot, never a backlog.

## Access control

Peer credentials via `SO_PEERCRED`. Root may see process details. Non-root
sees device totals only. PID namespace handling: the daemon records the
host PID namespace and reports `pid_namespace` alongside each process so
clients can disambiguate container PIDs. PID reuse is handled by comparing
process start time from `/proc/<pid>/stat` field 22 when available.
