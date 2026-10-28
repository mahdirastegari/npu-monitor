// collector.hpp — sampling engine with per-lane state, atomic publication,
// and idle/wake power management.
//
// Two lanes: one thread for aicore utilization (slow MCU round-trip) and one
// thread for all other driver queries. Each lane checks the ActivityTracker
// before every cycle: if no client has requested data within the idle
// timeout, the lane sleeps on a condition variable (zero driver calls).
// When a client connects, touch() wakes all lanes instantly.
//
// License: see LICENSE.
#ifndef NPU_MONITOR_COLLECTOR_HPP
#define NPU_MONITOR_COLLECTOR_HPP

#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "npu_monitor/activity.hpp"
#include "npu_monitor/cache.hpp"
#include "npu_monitor/dcmi.hpp"
#include "npu_monitor/snapshot.hpp"
#include "npu_monitor/state.hpp"

namespace npu_monitor {

struct CollectorConfig {
  double utilization_interval_ms = 1000.0;
  double env_interval_ms = 2000.0;
  double process_interval_ms = 2000.0;
  double pcie_interval_ms = 0.0;  // 0 = disabled
  int utilization_workers = 1;   // 1 thread for aicore utilization (user spec)
  double stale_after_ms = 5000.0;
  uint32_t error_threshold = 3;  // consecutive errors before marking stale
  double idle_timeout_ms = 2000.0;  // sleep after this much client inactivity
};

class Collector {
 public:
  Collector(DcmiBackend& backend, const CollectorConfig& config,
            SnapshotCache& cache, ActivityTracker& activity);

  void start();
  void stop();
  SnapshotPtr latest() const;

  // Register a callback invoked after each publish cycle.
  void set_publish_callback(std::function<void()> callback);

  // Block until the first snapshot is published. Returns false on timeout.
  bool wait_ready(double timeout_ms);

  // Current daemon state (derived from activity and cache).
  DaemonState daemon_state() const;

 private:
  void utilization_lane();
  void env_lane();
  void process_lane();
  void publish();
  void log_error(const std::string& message);
  void log_idle_transition(const char* lane_name, bool going_idle);
  void update_lane(LaneId id, LaneState state, const std::string& error = "");

  DcmiBackend& backend_;
  CollectorConfig config_;
  SnapshotCache& cache_;
  ActivityTracker& activity_;
  std::vector<DeviceRef> devices_;
  std::vector<DeviceMetrics> latest_;
  mutable std::mutex latest_mutex_;
  std::vector<LaneStatus> lane_statuses_;  // one per LaneId
  mutable std::mutex lane_mutex_;
  std::atomic<uint64_t> generation_{0};
  std::atomic<uint64_t> sequence_{0};
  std::atomic<bool> stop_flag_{false};
  std::vector<std::thread> workers_;
  std::vector<std::string> errors_;
  mutable std::mutex errors_mutex_;
  std::function<void()> on_publish_;
};

}  // namespace npu_monitor

#endif  // NPU_MONITOR_COLLECTOR_HPP
