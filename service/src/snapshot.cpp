// snapshot.cpp — JSON serialization of the snapshot model.
//
// License: see LICENSE.
#include "npu_monitor/snapshot.hpp"
#include "npu_monitor/state.hpp"

#include <cmath>

namespace npu_monitor {

namespace {

void write_metric(JsonWriter& w, const char* key, const MetricValue& m) {
  w.key(key);
  w.begin_object();
  w.key("value");
  if (std::isfinite(m.value)) {
    w.value(m.value);
  } else {
    w.null();
  }
  w.key("unit"); w.value(m.unit);
  w.key("source"); w.value(m.source);
  w.key("acquired_at"); w.value(m.acquired_at);
  w.key("state"); w.value(m.state == MetricState::Ok ? "ok"
                       : m.state == MetricState::Stale ? "stale"
                       : m.state == MetricState::Unsupported ? "unsupported"
                       : m.state == MetricState::Permission ? "permission"
                       : m.state == MetricState::Error ? "error"
                       : "pending");
  w.key("reason"); w.value(m.reason);
  w.end_object();
}

void write_device(JsonWriter& w, const DeviceMetrics& d) {
  w.begin_object();
  w.key("card_id"); w.value(static_cast<double>(d.ref.card_id));
  w.key("chip_id"); w.value(static_cast<double>(d.ref.chip_id));
  w.key("physical_id"); w.value(static_cast<double>(d.ref.physical_id));
  w.key("logical_id"); w.value(static_cast<double>(d.ref.logical_id));
  w.key("model"); w.value(d.ref.model);
  w.key("bus_id"); w.value(d.ref.bus_id);

  w.key("metrics");
  w.begin_object();
  write_metric(w, "aicore_util", d.aicore_util);
  write_metric(w, "vector_util", d.vector_util);
  write_metric(w, "npu_util", d.npu_util);
  write_metric(w, "memory_used", d.memory_used);
  write_metric(w, "memory_total", d.memory_total);
  write_metric(w, "memory_bandwidth", d.memory_bandwidth);
  write_metric(w, "aicore_freq", d.aicore_freq);
  write_metric(w, "hbm_temp", d.hbm_temp);
  write_metric(w, "temperature", d.temperature);
  write_metric(w, "power", d.power);
  write_metric(w, "health", d.health);
  w.end_object();

  w.key("processes");
  w.begin_array();
  for (const auto& p : d.processes) {
    w.begin_object();
    w.key("pid"); w.value(static_cast<double>(p.pid));
    w.key("memory_mib"); w.value(p.memory_mib);
    w.key("pid_namespace"); w.value(p.pid_namespace);
    w.key("user"); w.value(p.user);
    w.key("command"); w.value(p.command);
    w.end_object();
  }
  w.end_array();
  w.end_object();
}

}  // namespace

void snapshot_to_json(const Snapshot& snap, JsonWriter& w) {
  w.begin_object();
  w.key("v"); w.value(1.0);
  w.key("generation"); w.value(static_cast<double>(snap.generation));
  w.key("sequence"); w.value(static_cast<double>(snap.sequence));
  w.key("collected_at"); w.value(snap.collected_at);
  w.key("collection_ms"); w.value(snap.collection_ms);
  w.key("daemon_state"); w.value(daemon_state_name(snap.daemon_state));

  w.key("lanes");
  w.begin_array();
  for (size_t i = 0; i < snap.lanes.size(); i++) {
    const auto& lane = snap.lanes[i];
    w.begin_object();
    w.key("name"); w.value(lane_name(static_cast<LaneId>(i)));
    w.key("state"); w.value(lane_state_name(lane.state));
    w.key("consecutive_errors"); w.value(static_cast<double>(lane.consecutive_errors));
    w.key("total_errors"); w.value(static_cast<double>(lane.total_errors));
    w.key("last_ok_at"); w.value(lane.last_ok_at);
    w.key("last_attempt_at"); w.value(lane.last_attempt_at);
    if (!lane.last_error.empty()) {
      w.key("last_error"); w.value(lane.last_error);
    }
    w.end_object();
  }
  w.end_array();

  w.key("devices");
  w.begin_array();
  for (const auto& d : snap.devices) write_device(w, d);
  w.end_array();

  w.key("processes");
  w.begin_array();
  for (const auto& p : snap.processes) {
    w.begin_object();
    w.key("device_index"); w.value(static_cast<double>(p.device_index));
    w.key("pid"); w.value(static_cast<double>(p.pid));
    w.key("memory_mib"); w.value(p.memory_mib);
    w.key("pid_namespace"); w.value(p.pid_namespace);
    w.key("user"); w.value(p.user);
    w.key("command"); w.value(p.command);
    w.end_object();
  }
  w.end_array();

  w.key("errors");
  w.begin_array();
  for (const auto& e : snap.errors) w.value(e);
  w.end_array();

  w.end_object();
}

}  // namespace npu_monitor
