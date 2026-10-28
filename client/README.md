# npu-monitor — terminal client

`npu-monitor` is the reference client for the
[npu-monitord](../service/README.md) daemon: a live dashboard, a JSON
snapshot tool, and a watch loop.

It is **standalone** — it depends only on the
[wire protocol](../docs/protocol.md) and FTXUI for the dashboard. It builds
and runs on any Linux box, with or without Ascend hardware or drivers
installed. Use it as a reference for building your own client.

## Build

From the repository root:

```bash
./scripts/build.sh
```

The client binary is `build/client/npu-monitor`.

### Version

```bash
npu-monitor --version
```

The client version (currently `1.0.0`) is set at configure time with
`-DNPU_MONITOR_VERSION=<x.y.z>` and tracks the dashboard and CLI, not the
wire protocol.

## Usage

### Live dashboard (default)

```bash
npu-monitor
# or explicitly
npu-monitor top
```

Shows a live, color-coded dashboard with all devices, metrics, and processes
on one screen. Bars are green → yellow → red based on load. Press **Ctrl-C**
to quit.

```
 npu-monitord — Ascend910 · 16 chips / 8 cards                          daemon: ready  14:32:05
┌──────────────────────────────────────────────────────────────────────────────────────────────────┐
  C:Ch Bus-ID          AICore             Memory                Temp   Pwr    Freq   Hth
   0:0 0000:9D:00.0   ##########  85%  ########## 52/64GiB      65°C  310W 1800MHz  OK
   0:1 0000:9F:00.0   ###         23%  #          8/64GiB       45°C  160W  800MHz  OK
   1:0 0000:99:00.0   .            0%  .          3/64GiB       38°C  160W  800MHz  OK
   ...
├──────────────────────────────────────────────────────────────────────────────────────────────────┤
  Processes (3)
        PID User       C:Ch   NPU-MEM  Command
    12345 user1       0:0    48.0GiB  python train.py
    12346 user1       0:1     2.3GiB  python infer.py
└──────────────────────────────────────────────────────────────────────────────────────────────────┘
```

Custom refresh interval (default 1 second):

```bash
npu-monitor top /var/run/npu-monitor.sock 0.5   # refresh every 0.5s
```

### One-shot JSON snapshot

```bash
npu-monitor snapshot
```

Outputs a JSON snapshot ([format](../docs/protocol.md#response)). Pipe
through `python3 -m json.tool` for pretty-printing:

```bash
npu-monitor snapshot | python3 -m json.tool
```

### Continuous watch

```bash
# Default: 1 second interval, runs forever
npu-monitor watch

# Custom interval and count
npu-monitor watch /var/run/npu-monitor.sock 0.5 20   # 0.5s interval, 20 samples
```

Each `watch` request resets the daemon's idle timer, keeping it awake for
the duration of the watch session.

### Custom socket path

Pass the socket path as an argument, or set the `NPU_MONITOR_SOCKET`
environment variable:

```bash
npu-monitor top /tmp/npu-monitor.sock
NPU_MONITOR_SOCKET=/tmp/npu-monitor.sock npu-monitor
```

### Piping into scripts

```bash
# Extract just the device utilization values
npu-monitor snapshot | python3 -c "
import sys, json
d = json.load(sys.stdin)
for dev in d['devices']:
    m = dev['metrics']
    print(f\"chip {dev['chip_id']:2d}  aicore={m['aicore_util']['value']:5.1f}%  \"
          f\"mem={m['memory_used']['value']:.0f}/{m['memory_total']['value']:.0f} MiB  \"
          f\"temp={m['temperature']['value']:.0f}°C  power={m['power']['value']:.1f} W\")
"
```

## Writing your own client

Any process that can open a Unix socket can read NPU telemetry. The full
contract is [docs/protocol.md](../docs/protocol.md); the essentials:

- Connect to `/var/run/npu-monitor.sock` (`SOCK_SEQPACKET`).
- Send one JSON frame: `{"v":1,"op":"snapshot"}`.
- Receive one JSON frame (≤ 4 MiB) — the snapshot.
- Reconnect for the next sample.

### Python (standard library only)

```python
#!/usr/bin/env python3
"""Minimal npu-monitor client — poll and render."""
import json
import socket
import sys
import time

SOCKET_PATH = "/var/run/npu-monitor.sock"


def snapshot(path=SOCKET_PATH):
    """Fetch one snapshot. One connection per request."""
    s = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
    try:
        s.connect(path)
        s.send(b'{"v":1,"op":"snapshot"}')
        return json.loads(s.recv(4 * 1024 * 1024))  # max_frame_bytes
    finally:
        s.close()


def render(snap):
    for dev in snap["devices"]:
        m = dev["metrics"]
        util = m["aicore_util"]
        mem = m["memory_used"]
        total = m["memory_total"]
        line = (
            f"{dev['card_id']}:{dev['chip_id']}  "
            f"util={util['value']:5.1f}%  "
            f"mem={mem['value']:7.0f}/{total['value']:.0f} MiB"
        )
        if util["state"] != "ok":
            line += f"  (util: {util['state']})"
        print(line)


def main():
    while True:
        try:
            render(snapshot())
        except (FileNotFoundError, ConnectionRefusedError):
            print("npu-monitord is not running or socket path is wrong",
                  file=sys.stderr)
            return 1
        time.sleep(1.0)


if __name__ == "__main__":
    sys.exit(main())
```

### Shell / jq

```bash
# One-shot
echo '{"v":1,"op":"snapshot"}}' | socat - UNIX-CONNECT:/var/run/npu-monitor.sock | jq '.devices[0].metrics.aicore_util'

# Poll loop
while true; do
  npu-monitor snapshot | jq -r '.devices[] | "\(.card_id):\(.chip_id)  \(.metrics.aicore_util.value)%"'
  sleep 1
done
```

### C

```c
#include <sys/socket.h>
#include <sys/un.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(void) {
    int fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if (fd < 0) return 1;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, "/var/run/npu-monitor.sock", sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        perror("connect");
        close(fd);
        return 1;
    }

    const char req[] = "{\"v\":1,\"op\":\"snapshot\"}";
    if (send(fd, req, sizeof(req) - 1, MSG_NOSIGNAL) < 0) {
        perror("send");
        close(fd);
        return 1;
    }

    /* One recv() returns the whole frame on a sequential-packet socket. */
    static char buf[4 * 1024 * 1024];
    ssize_t n = recv(fd, buf, sizeof(buf) - 1, 0);
    if (n > 0) {
        buf[n] = '\0';
        puts(buf);
    }
    close(fd);
    return 0;
}
```

### Developing without hardware

If you don't have Ascend NPUs on your development machine, develop against
a recorded snapshot instead:

```bash
# On the NPU host: record one snapshot
npu-monitor snapshot > /tmp/snap.json

# Locally: replay it as many times as you like
cat /tmp/snap.json   # feed your renderer
```

Any JSON file with the snapshot schema is a valid fixture — the daemon
itself is optional for client development.
