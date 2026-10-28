// state.hpp — daemon state machine.
//
// The daemon exposes a small, observable state system. Each collection
// lane has its own state; the daemon state is derived from lane states.
// State transitions are pushed to subscribed clients so a live view can
// render progress without polling.
//
// License: see LICENSE.
#ifndef NPU_MONITOR_STATE_HPP
#define NPU_MONITOR_STATE_HPP

#include <cstdint>
#include <string>
#include <vector>

namespace npu_monitor {

enum class LaneId : uint8_t {
  Utilization = 0,  // aicore/vector utilization (slow MCU round-trip)
  Environment = 1,  // temperature, power, health, memory, clocks
  Process = 2,      // per-process device-memory attribution
  kCount,
};

const char* lane_name(LaneId id);

enum class LaneState : uint8_t {
  Idle = 0,        // lane has not run yet
  Collecting,      // driver calls in flight
  Ok,             // last cycle completed without errors
  Error,          // last cycle had transient failures
  Unsupported,    // driver/hardware lacks this capability
};

const char* lane_state_name(LaneState s);

struct LaneStatus {
  LaneState state = LaneState::Idle;
  uint64_t consecutive_errors = 0;
  uint64_t total_errors = 0;
  double last_ok_at = 0.0;     // wall-clock ms
  double last_attempt_at = 0.0;
  std::string last_error;
};

enum class DaemonState : uint8_t {
  INIT = 0,      // no snapshot published yet
  READY,         // serving cached data, collector active
  IDLE,          // no client activity; collector sleeping to save driver load
  STALE,         // collector degraded; data may be stale
  SHUTDOWN,      // shutting down
};

const char* daemon_state_name(DaemonState s);

// Derive the daemon state from lane statuses and whether any snapshot
// has been published.
DaemonState derive_daemon_state(bool has_snapshot,
                                const std::vector<LaneStatus>& lanes,
                                uint32_t error_threshold);

}  // namespace npu_monitor

#endif  // NPU_MONITOR_STATE_HPP
