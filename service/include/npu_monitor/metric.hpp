// Metric value model: validity states, units, sources, timestamps.
//
// A metric distinguishes a valid zero from unsupported, permission-denied,
// failed, and stale values. Stale values retain the acquisition timestamp of
// the last good reading.
//
// License: see LICENSE.
#ifndef NPU_MONITOR_METRIC_HPP
#define NPU_MONITOR_METRIC_HPP

#include <cstdint>
#include <string>

namespace npu_monitor {

enum class MetricState : uint8_t {
  Ok,           // measured successfully this cycle
  Stale,        // last good reading retained past freshness window
  Unsupported,  // driver/hardware does not provide this metric
  Permission,   // query rejected due to permissions
  Error,        // query failed with a transient error code
  Pending,      // never collected yet
};

struct MetricValue {
  double value = 0.0;
  double acquired_at = 0.0;  // wall-clock ms
  MetricState state = MetricState::Pending;
  std::string unit;      // "%", "MiB", "MHz", "W", "°C", "count", ...
  std::string source;    // DCMI function name
  std::string reason;    // diagnostic when not Ok
};

}  // namespace npu_monitor

#endif  // NPU_MONITOR_METRIC_HPP
