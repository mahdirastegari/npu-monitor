// config.hpp — daemon configuration values.
//
// License: see LICENSE.
#ifndef NPU_MONITOR_CONFIG_HPP
#define NPU_MONITOR_CONFIG_HPP

#include <cstdint>
#include <string>

#include "npu_monitor/collector.hpp"
#include "npu_monitor/server.hpp"

namespace npu_monitor {

struct DaemonConfig {
  // Collector tuning.
  double utilization_interval_ms = 1000.0;
  double env_interval_ms = 2000.0;
  double process_interval_ms = 2000.0;
  double pcie_interval_ms = 0.0;  // 0 = disabled
  int utilization_workers = 1;
  double stale_after_ms = 5000.0;
  uint32_t error_threshold = 3;
  double idle_timeout_ms = 2000.0;  // sleep after this much client inactivity (0 = always on)

  // Server tuning.
  std::string socket_path = "/var/run/npu-monitor.sock";
  size_t max_frame_bytes = 4 * 1024 * 1024;
  size_t max_subscribers = 64;

  // Startup.
  double ready_timeout_ms = 30000.0;
  std::string log_level = "info";
  std::string log_file;  // empty = stderr
};

// Load configuration from the given path (or defaults if empty).
// Returns false on parse failure.
bool load_config(DaemonConfig& config, const std::string& path = "");

}  // namespace npu_monitor

#endif  // NPU_MONITOR_CONFIG_HPP
