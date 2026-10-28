// snapshot.hpp — immutable telemetry snapshot model.
//
// A snapshot is a consistent, atomically published view. Readers either see
// the previous snapshot or the next one, never a partial merge. The daemon
// state is embedded so clients render state transitions from the same
// frame that carries the data.
//
// License: see LICENSE.
#ifndef NPU_MONITOR_SNAPSHOT_HPP
#define NPU_MONITOR_SNAPSHOT_HPP

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "npu_monitor/dcmi.hpp"
#include "npu_monitor/json.hpp"
#include "npu_monitor/metric.hpp"
#include "npu_monitor/state.hpp"

namespace npu_monitor {

struct ProcessEntry {
  int device_index = 0;
  int pid = 0;
  double memory_mib = 0;
  std::string pid_namespace;  // host or container
  std::string user;
  std::string command;
};

struct DeviceMetrics {
  DeviceRef ref;
  MetricValue aicore_util;       // %
  MetricValue vector_util;      // %
  MetricValue npu_util;         // %
  MetricValue memory_used;      // MiB
  MetricValue memory_total;     // MiB
  MetricValue memory_bandwidth; // %
  MetricValue aicore_freq;      // MHz
  MetricValue hbm_temp;         // °C
  MetricValue temperature;     // °C
  MetricValue power;            // W
  MetricValue health;           // bitmap
  std::vector<ProcessEntry> processes;
};

struct Snapshot {
  uint64_t generation = 0;
  uint64_t sequence = 0;
  double collected_at = 0;   // wall-clock ms
  double collection_ms = 0;
  DaemonState daemon_state = DaemonState::INIT;
  std::vector<LaneStatus> lanes;
  std::vector<DeviceMetrics> devices;
  std::vector<ProcessEntry> processes;
  std::vector<std::string> errors;
};

using SnapshotPtr = std::shared_ptr<const Snapshot>;

void snapshot_to_json(const Snapshot& snap, JsonWriter& w);

}  // namespace npu_monitor

#endif  // NPU_MONITOR_SNAPSHOT_HPP
