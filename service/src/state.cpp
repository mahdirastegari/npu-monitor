// state.cpp — state machine implementation.
//
// License: see LICENSE.
#include "npu_monitor/state.hpp"

namespace npu_monitor {

const char* lane_name(LaneId id) {
  switch (id) {
    case LaneId::Utilization: return "utilization";
    case LaneId::Environment: return "environment";
    case LaneId::Process: return "process";
    default: return "unknown";
  }
}

const char* lane_state_name(LaneState s) {
  switch (s) {
    case LaneState::Idle: return "idle";
    case LaneState::Collecting: return "collecting";
    case LaneState::Ok: return "ok";
    case LaneState::Error: return "error";
    case LaneState::Unsupported: return "unsupported";
    default: return "unknown";
  }
}

const char* daemon_state_name(DaemonState s) {
  switch (s) {
    case DaemonState::INIT: return "init";
    case DaemonState::READY: return "ready";
    case DaemonState::IDLE: return "idle";
    case DaemonState::STALE: return "stale";
    case DaemonState::SHUTDOWN: return "shutdown";
    default: return "unknown";
  }
}

DaemonState derive_daemon_state(bool has_snapshot, const LaneStatus* lanes,
                                uint32_t error_threshold) {
  if (!has_snapshot) return DaemonState::INIT;

  // All lanes erroring or unsupported -> stale.
  bool any_ok = false;
  for (int i = 0; i < static_cast<int>(LaneId::kCount); i++) {
    if (lanes[i].state == LaneState::Ok) any_ok = true;
  }
  if (!any_ok) return DaemonState::STALE;

  // Any lane with sustained errors -> degraded/stale.
  for (int i = 0; i < static_cast<int>(LaneId::kCount); i++) {
    if (lanes[i].consecutive_errors >= error_threshold) return DaemonState::STALE;
  }
  return DaemonState::READY;
}

}  // namespace npu_monitor
