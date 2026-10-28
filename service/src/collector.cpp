// collector.cpp — sampling engine with idle/wake power management.
//
// Two lanes: one thread for aicore utilization (slow MCU round-trip, ~64 ms
// per device on 910) and one thread for all other driver queries (temp,
// power, health, memory, clocks, processes — all sub-2 ms per device).
//
// Idle/wake strategy:
//   - Before each cycle, a lane checks ActivityTracker::is_active().
//   - If idle (no client request within idle_timeout_ms), the lane sleeps
//     on a condition variable — zero driver calls.
//   - When a client connects, ActivityTracker::touch() wakes all lanes
//     instantly. The first request gets the last cached snapshot; the
//     lanes collect immediately and subsequent requests get fresh data.
//   - Each client request resets the awake window.
//
// License: see LICENSE.
#include "npu_monitor/collector.hpp"

#include <algorithm>
#include <chrono>

#include "npu_monitor/log.hpp"
#include "npu_monitor/proc_enrich.hpp"

namespace npu_monitor {

using SteadyClock = std::chrono::steady_clock;

static double monotonic_ms() {
  return std::chrono::duration<double, std::milli>(
             SteadyClock::now().time_since_epoch())
      .count();
}

Collector::Collector(DcmiBackend& backend, const CollectorConfig& config,
                     SnapshotCache& cache, ActivityTracker& activity)
    : backend_(backend), config_(config), cache_(cache), activity_(activity) {
  devices_ = backend.enumerate();
  latest_.resize(devices_.size());
  for (size_t i = 0; i < devices_.size(); i++) {
    latest_[i].ref = devices_[i];
  }
  lane_statuses_.resize(static_cast<size_t>(LaneId::kCount));
}

void Collector::start() {
  stop_flag_ = false;
  int n = std::max(1, config_.utilization_workers);
  for (int i = 0; i < n; i++) {
    workers_.emplace_back([this]() { utilization_lane(); });
  }
  workers_.emplace_back([this]() { env_lane(); });
  workers_.emplace_back([this]() { process_lane(); });
}

void Collector::stop() {
  stop_flag_ = true;
  activity_.notify_stop();
  for (auto& t : workers_) {
    if (t.joinable()) t.join();
  }
  workers_.clear();
}

SnapshotPtr Collector::latest() const { return cache_.latest(); }

bool Collector::wait_ready(double timeout_ms) {
  double deadline = monotonic_ms() + timeout_ms;
  while (monotonic_ms() < deadline) {
    if (latest() != nullptr) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return latest() != nullptr;
}

DaemonState Collector::daemon_state() const {
  if (stop_flag_) return DaemonState::SHUTDOWN;
  if (latest() == nullptr) return DaemonState::INIT;
  if (!activity_.is_active(config_.idle_timeout_ms)) return DaemonState::IDLE;
  return DaemonState::READY;
}

void Collector::set_publish_callback(std::function<void()> callback) {
  on_publish_ = std::move(callback);
}

// ---------------------------------------------------------------------------
// Lane loop pattern (shared by all lanes):
//
//   while (!stop):
//     if idle: sleep on condition variable until touch() wakes us
//     collect one cycle
//     publish
//     sleep until next deadline (wake early if touch() arrives)
//
// When a client first connects:
//   1. touch() wakes the sleeping lane.
//   2. Lane collects immediately and publishes.
//   3. Client's first read gets the previous cached snapshot (possibly
//      stale). Within 1–2 seconds the fresh snapshot is available.
// ---------------------------------------------------------------------------

void Collector::utilization_lane() {
  double next_deadline = monotonic_ms();
  bool was_idle = false;
  while (!stop_flag_) {
    // Idle check: sleep if no client activity.
    if (!activity_.is_active(config_.idle_timeout_ms)) {
      if (!was_idle) log_idle_transition("utilization", true);
      was_idle = true;
      activity_.sleep_until_woken(config_.idle_timeout_ms, stop_flag_);
      if (stop_flag_) return;
      log_idle_transition("utilization", false);
      was_idle = false;
      next_deadline = monotonic_ms();  // collect immediately
    }

    update_lane(LaneId::Utilization, LaneState::Collecting);

    bool had_error = false;
    for (size_t i = 0; i < devices_.size(); i++) {
      if (stop_flag_) return;
      auto mu = backend_.multi_utilization(devices_[i]);
      std::lock_guard<std::mutex> lock(latest_mutex_);
      if (mu.supported) {
        latest_[i].aicore_util.value = mu.aicore_util;
        latest_[i].aicore_util.acquired_at = monotonic_ms();
        latest_[i].aicore_util.state = MetricState::Ok;
        latest_[i].aicore_util.unit = "%";
        latest_[i].aicore_util.source = "dcmi_get_device_multi_utilization_rate";
        latest_[i].aicore_util.reason.clear();
        latest_[i].vector_util = latest_[i].aicore_util;
        latest_[i].vector_util.value = mu.aiv_util;
        latest_[i].npu_util = latest_[i].aicore_util;
        latest_[i].npu_util.value = mu.npu_util;
      } else {
        had_error = true;
        latest_[i].aicore_util.state = MetricState::Unsupported;
        latest_[i].aicore_util.reason = "multi_utilization_rate not supported";
        latest_[i].vector_util = latest_[i].aicore_util;
        latest_[i].npu_util = latest_[i].aicore_util;
      }
    }
    update_lane(LaneId::Utilization, had_error ? LaneState::Error : LaneState::Ok);
    publish();
    double now = monotonic_ms();
    if (now < next_deadline) {
      // Active: sleep until next deadline. touch() during this sleep
      // only extends the awake window (checked at the top of the next
      // iteration); it does not cause an early collection.
      std::this_thread::sleep_for(
          std::chrono::duration<double, std::milli>(next_deadline - now));
    } else {
      double missed = (now - next_deadline) / config_.utilization_interval_ms;
      next_deadline += (static_cast<long long>(missed) + 1) * config_.utilization_interval_ms;
    }
  }
}

void Collector::env_lane() {
  double next_deadline = monotonic_ms();
  bool was_idle = false;
  while (!stop_flag_) {
    if (!activity_.is_active(config_.idle_timeout_ms)) {
      if (!was_idle) log_idle_transition("environment", true);
      was_idle = true;
      activity_.sleep_until_woken(config_.idle_timeout_ms, stop_flag_);
      if (stop_flag_) return;
      log_idle_transition("environment", false);
      was_idle = false;
      next_deadline = monotonic_ms();
    }

    update_lane(LaneId::Environment, LaneState::Collecting);

    for (size_t i = 0; i < devices_.size(); i++) {
      if (stop_flag_) return;
      double value = 0;
      std::lock_guard<std::mutex> lock(latest_mutex_);
      if (backend_.temperature(devices_[i], value)) {
        latest_[i].temperature.value = value;
        latest_[i].temperature.acquired_at = monotonic_ms();
        latest_[i].temperature.state = MetricState::Ok;
        latest_[i].temperature.unit = "\u00b0C";
        latest_[i].temperature.source = "dcmi_get_device_temperature";
      } else {
        latest_[i].temperature.state = MetricState::Error;
        latest_[i].temperature.reason = "query failed";
      }
      if (backend_.power(devices_[i], value)) {
        latest_[i].power.value = value;
        latest_[i].power.acquired_at = monotonic_ms();
        latest_[i].power.state = MetricState::Ok;
        latest_[i].power.unit = "W";
        latest_[i].power.source = "dcmi_get_device_power_info";
      } else {
        latest_[i].power.state = MetricState::Error;
        latest_[i].power.reason = "query failed";
      }
      unsigned int health = 0;
      if (backend_.health(devices_[i], health)) {
        latest_[i].health.value = health;
        latest_[i].health.acquired_at = monotonic_ms();
        latest_[i].health.state = MetricState::Ok;
        latest_[i].health.unit = "bitmap";
        latest_[i].health.source = "dcmi_get_device_health";
      }
      double used = 0, total = 0;
      if (backend_.memory(devices_[i], used, total)) {
        latest_[i].memory_used.value = used;
        latest_[i].memory_used.acquired_at = monotonic_ms();
        latest_[i].memory_used.state = MetricState::Ok;
        latest_[i].memory_used.unit = "MiB";
        latest_[i].memory_used.source = "dcmi_get_device_hbm_info";
        latest_[i].memory_total.value = total;
        latest_[i].memory_total.acquired_at = monotonic_ms();
        latest_[i].memory_total.state = MetricState::Ok;
        latest_[i].memory_total.unit = "MiB";
        latest_[i].memory_total.source = "dcmi_get_device_hbm_info";
      }
      double freq = 0;
      if (backend_.aicore_freq(devices_[i], freq)) {
        latest_[i].aicore_freq.value = freq;
        latest_[i].aicore_freq.acquired_at = monotonic_ms();
        latest_[i].aicore_freq.state = MetricState::Ok;
        latest_[i].aicore_freq.unit = "MHz";
        latest_[i].aicore_freq.source = "dcmi_get_device_frequency";
      }
    }
    update_lane(LaneId::Environment, LaneState::Ok);
    publish();
    next_deadline += config_.env_interval_ms;
    double now = monotonic_ms();
    if (now < next_deadline) {
      std::this_thread::sleep_for(
          std::chrono::duration<double, std::milli>(next_deadline - now));
    } else {
      double missed = (now - next_deadline) / config_.env_interval_ms;
      next_deadline += (static_cast<long long>(missed) + 1) * config_.env_interval_ms;
    }
  }
}

void Collector::process_lane() {
  double next_deadline = monotonic_ms();
  bool was_idle = false;
  while (!stop_flag_) {
    if (!activity_.is_active(config_.idle_timeout_ms)) {
      if (!was_idle) log_idle_transition("process", true);
      was_idle = true;
      activity_.sleep_until_woken(config_.idle_timeout_ms, stop_flag_);
      if (stop_flag_) return;
      log_idle_transition("process", false);
      was_idle = false;
      next_deadline = monotonic_ms();
    }

    update_lane(LaneId::Process, LaneState::Collecting);

    for (size_t i = 0; i < devices_.size(); i++) {
      if (stop_flag_) return;
      std::vector<int> pids;
      std::vector<unsigned long long> mem;
      std::lock_guard<std::mutex> lock(latest_mutex_);
      if (backend_.process_info(devices_[i], pids, mem)) {
        latest_[i].processes.clear();
        for (size_t j = 0; j < pids.size(); j++) {
          ProcessEntry entry;
          entry.device_index = static_cast<int>(i);
          entry.pid = pids[j];
          entry.memory_mib = static_cast<double>(mem[j]) / (1024.0 * 1024.0);  // bytes → MiB
          entry.pid_namespace = proc_exists(pids[j]) ? "host" : "container";
          entry.user = proc_user(pids[j]);
          entry.command = proc_command(pids[j]);
          latest_[i].processes.push_back(entry);
        }
      }
    }
    update_lane(LaneId::Process, LaneState::Ok);
    publish();
    next_deadline += config_.process_interval_ms;
    double now = monotonic_ms();
    if (now < next_deadline) {
      std::this_thread::sleep_for(
          std::chrono::duration<double, std::milli>(next_deadline - now));
    } else {
      double missed = (now - next_deadline) / config_.process_interval_ms;
      next_deadline += (static_cast<long long>(missed) + 1) * config_.process_interval_ms;
    }
  }
}

void Collector::publish() {
  auto snap = std::make_shared<Snapshot>();
  snap->generation = generation_;
  snap->sequence = sequence_;
  snap->collected_at = monotonic_ms();
  snap->daemon_state = daemon_state();
  {
    std::lock_guard<std::mutex> lock(lane_mutex_);
    snap->lanes = lane_statuses_;
  }
  {
    std::lock_guard<std::mutex> lock(latest_mutex_);
    snap->devices = latest_;
    for (const auto& d : latest_) {
      for (const auto& p : d.processes) snap->processes.push_back(p);
    }
  }
  {
    std::lock_guard<std::mutex> lock(errors_mutex_);
    snap->errors = errors_;
  }
  cache_.publish(snap);
  sequence_++;
  if (on_publish_) on_publish_();
}

void Collector::log_error(const std::string& message) {
  std::lock_guard<std::mutex> lock(errors_mutex_);
  errors_.push_back(message);
  if (errors_.size() > 100) errors_.erase(errors_.begin());
}

void Collector::log_idle_transition(const char* lane_name, bool going_idle) {
  if (going_idle) {
    log_info(std::string(lane_name) + " lane: idle, sleeping until client activity");
  } else {
    log_info(std::string(lane_name) + " lane: woken up by client activity");
  }
}

void Collector::update_lane(LaneId id, LaneState state, const std::string& error) {
  std::lock_guard<std::mutex> lock(lane_mutex_);
  auto& lane = lane_statuses_[static_cast<size_t>(id)];
  lane.state = state;
  lane.last_attempt_at = monotonic_ms();
  if (state == LaneState::Ok) {
    lane.last_ok_at = lane.last_attempt_at;
    lane.consecutive_errors = 0;
  } else if (state == LaneState::Error) {
    lane.consecutive_errors++;
    lane.total_errors++;
    if (!error.empty()) lane.last_error = error;
  }
}

}  // namespace npu_monitor
