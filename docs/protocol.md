# npu-monitor wire protocol — v1

This is the contract between `npu-monitord` (the service) and any client.
It is intentionally minimal: **connect, send one JSON request, receive one
JSON snapshot**. Everything else — refresh intervals, watch loops, dashboards
— is client-side.

## Service contract

`npu-monitord` samples the Ascend NPU driver once per host and serves
**cached snapshots** to any number of clients. Your client never touches the
driver:

- The daemon collects on its own schedule (default: 1 s for utilization,
  2 s for environment and process data).
- Every request is answered from the cache — connecting, refreshing, or
  subscribing never triggers additional hardware queries.
- Between requests the daemon may be **idle**: with no client activity for
  `idle_timeout_ms` (default 2000 ms), all collection lanes sleep and make
  zero driver calls. A request wakes them instantly.

## Socket

| Property | Value |
|---|---|
| Transport | Unix-domain socket, `SOCK_SEQPACKET` |
| Default path | `/var/run/npu-monitor.sock` (daemon config: `socket_path`) |
| Framing | One request frame in, one response frame out |
| Max response | `max_frame_bytes` (default 4194304 = 4 MiB) |
| Access | Socket is chmod `0666` — any local user may connect |
| Versioning | `"v": 1` in every request |

## Request

A single JSON frame:

```json
{"v":1,"op":"snapshot"}
```

| `op` | Response |
|---|---|
| `snapshot` | The latest cached snapshot (JSON object). |
| `subscribe` | Same as `snapshot` (the daemon is pull-based; no push stream). |
| `state` | `{"ok":true}` — liveness check. |
| `unsubscribe` | `{"ok":true}` — no-op for stateless clients. |

Each connection handles exactly one request/response pair. Open a new
connection per sample. On `SOCK_SEQPACKET`, one `recv()` returns the whole
frame — no partial reads to reassemble.

If no snapshot has been published yet (daemon starting up), the response is
`{"error":"no snapshot yet"}`.

## Response

```jsonc
{
  "v": 1,                        // protocol version
  "generation": 0,               // increments on collector restart
  "sequence": 42,                // monotonic per-generation sample counter
  "collected_at": 959039000.0,   // wall-clock ms of the newest metric
  "collection_ms": 0,            // wall time of the last sweep
  "daemon_state": "ready",       // init | ready | idle | stale | shutdown
  "lanes": [
    {
      "name": "utilization",
      "state": "ok",             // idle | collecting | ok | error | unsupported
      "consecutive_errors": 0,
      "total_errors": 0,
      "last_ok_at": 959039000.0,
      "last_attempt_at": 959039000.0
    },
    // ... environment, process lanes
  ],
  "devices": [
    {
      "card_id": 0,
      "chip_id": 0,
      "physical_id": 0,
      "logical_id": 0,
      "model": "Ascend910",
      "bus_id": "0000:9D:00.0",
      "metrics": {
        "aicore_util": {
          "value": 0,
          "unit": "%",
          "source": "dcmi_get_device_multi_utilization_rate",
          "acquired_at": 959038000.0,
          "state": "ok",
          "reason": ""
        },
        "vector_util":  { /* ... same structure ... */ },
        "npu_util":    { /* ... same structure ... */ },
        "memory_used":   { "value": 3132,  "unit": "MiB",    "state": "ok", ... },
        "memory_total":  { "value": 65536, "unit": "MiB",    "state": "ok", ... },
        "temperature":   { "value": 39,    "unit": "°C",     "state": "ok", ... },
        "power":         { "value": 162.8, "unit": "W",      "state": "ok", ... },
        "health":        { "value": 0,     "unit": "bitmap", "state": "ok", ... },
        "aicore_freq":   { "value": 1800,  "unit": "MHz",    "state": "ok", ... }
      },
      "processes": [
        {
          "pid": 12345,
          "memory_mib": 6144.0,
          "pid_namespace": "host",       // "host" or "container"
          "user": "root",                // from /proc/<pid>/status → /etc/passwd
          "command": "python3 train.py"  // from /proc/<pid>/cmdline
        }
      ]
    }
    // ... one entry per chip
  ],
  "processes": [],               // flattened process list across all devices
  "errors": []                   // rate-limited diagnostic messages
}
```

Every metric carries **value, unit, source, timestamp, and validity state**.
`source` is the DCMI function that produced the reading — provenance is
auditable end to end.

### Metric states

| State | Meaning |
|---|---|
| `ok` | Measured successfully this cycle. A value of 0 is a valid zero. |
| `stale` | Last good reading retained past the freshness window. Original timestamp preserved. |
| `unsupported` | Driver/hardware does not provide this metric. Never fabricated as zero. |
| `error` | Query failed with a transient error. Previous value retained, not overwritten. |
| `pending` | Never collected yet (lane hasn't run). |

### Daemon states

| State | Meaning |
|---|---|
| `init` | No snapshot published yet (starting up). |
| `ready` | Collector active, serving fresh data. |
| `idle` | No client activity within `idle_timeout_ms`. Collector sleeping. |
| `stale` | Collector degraded; data may be stale. |
| `shutdown` | Shutting down. |

### Process fields

| Field | Source | Notes |
|---|---|---|
| `pid` | DCMI `proc_id` | Container-namespace PID for containerized workloads. |
| `memory_mib` | DCMI `proc_mem_usage` ÷ 1024² | Driver reports **bytes**. |
| `pid_namespace` | host `/proc` lookup | `host` = resolvable, `container` = not visible on host. |
| `user` | `/proc/<pid>/status` → `/etc/passwd` | Empty when `pid_namespace` is `container`. |
| `command` | `/proc/<pid>/cmdline`, fallback `/proc/<pid>/comm` | Empty when `pid_namespace` is `container`. |

For workloads inside Docker/Kubernetes, DCMI reports the **container-namespace
PID**, which does not exist in the host `/proc` — the `user` and `command`
fields cannot be resolved from the host. To find the owner of a container
process, use the container runtime on the node:

```bash
# Which container owns which PID?
docker ps -q | xargs docker inspect --format '{{.Name}} {{.State.Pid}}'

# List processes inside each container
docker ps -q | xargs -I{} sh -c 'echo "== $(docker inspect --format "{{.Name}}" {})" && docker top {} -o pid,user,cmd'
```

## Rules for client authors

1. **One request per connection.** The server answers once and closes.
   Reconnect for the next sample.
2. **Read the whole frame.** Responses are self-contained JSON messages.
   Size your receive buffer to `max_frame_bytes` (default 4 MiB).
3. **Don't assume field order** — parse JSON, don't slice strings.
4. **Render `stale` / `error` / `unsupported` / `pending` honestly.** These
   states exist so you never mistake a failed query for a healthy zero.
   Never fabricate `0` for a metric that is not `ok`.
5. **Retry after idle.** The very first request after a long idle period may
   return lanes marked `pending` or data marked `stale` — that is the cache
   waking up. Poll again in ~1 s.
6. **Poll at ≥ 1 s.** Collection intervals are 1–2 s; polling faster adds
   connections without adding information.
7. **Tolerate unknown fields.** New fields may appear; missing fields fall
   back to your default rendering.
