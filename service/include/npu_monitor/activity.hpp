// activity.hpp — idle/wake power management for the collector.
//
// The daemon should not make driver calls when nobody is watching. When a
// client requests data, the daemon wakes up, collects, and stays awake for
// a configurable idle timeout (default 2 seconds). Each client request
// resets the timer. After the timeout with no activity, all lanes sleep
// on a condition variable until the next touch().
//
// License: see LICENSE.
#ifndef NPU_MONITOR_ACTIVITY_HPP
#define NPU_MONITOR_ACTIVITY_HPP

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>

namespace npu_monitor {

class ActivityTracker {
 public:
  ActivityTracker() : last_activity_ms_(now_ms()) {}

  // Called by the socket server on every client request. Wakes any
  // sleeping lanes immediately.
  void touch() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      last_activity_ms_ = now_ms();
    }
    cv_.notify_all();
  }

  // Returns true if a client has been active within idle_timeout_ms.
  bool is_active(double idle_timeout_ms) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return (now_ms() - last_activity_ms_) < idle_timeout_ms;
  }

  // Block until a client touches or stop_flag is set. Returns immediately
  // if already active. Used by lanes when they detect they are idle.
  bool sleep_until_woken(double idle_timeout_ms, std::atomic<bool>& stop_flag) {
    std::unique_lock<std::mutex> lock(mutex_);
    while (!stop_flag.load(std::memory_order_relaxed)) {
      if ((now_ms() - last_activity_ms_) < idle_timeout_ms) {
        return true;  // became active
      }
      cv_.wait(lock);
    }
    return false;  // stopped
  }

  // Wait until deadline_ms, but wake early if a client touches. Used by
  // active lanes between collection cycles so they don't miss a wake-up
  // signal that arrives during their interval sleep.
  void wait_until_or_wakeup(double deadline_ms, std::atomic<bool>& stop_flag) {
    double now = now_ms();
    if (now >= deadline_ms || stop_flag.load(std::memory_order_relaxed)) return;
    std::unique_lock<std::mutex> lock(mutex_);
    double last = last_activity_ms_;
    auto deadline = SteadyClock::time_point(
        std::chrono::microseconds(static_cast<long long>(deadline_ms * 1000)));
    cv_.wait_until(lock, deadline, [&] {
      return stop_flag.load(std::memory_order_relaxed) ||
             last_activity_ms_ > last;
    });
  }

  // Wake all sleeping lanes (used during shutdown).
  void notify_stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    cv_.notify_all();
  }

  double last_activity_ms() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_activity_ms_;
  }

 private:
  static double now_ms() {
    return std::chrono::duration<double, std::milli>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
  }

  using SteadyClock = std::chrono::steady_clock;
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  double last_activity_ms_;
};

}  // namespace npu_monitor

#endif  // NPU_MONITOR_ACTIVITY_HPP
