// eventbus.hpp — fault/recovery event subscription with a bounded queue.
//
// License: see LICENSE.
#ifndef NPU_MONITOR_EVENTBUS_HPP
#define NPU_MONITOR_EVENTBUS_HPP

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace npu_monitor {

struct FaultEvent {
  uint64_t event_id = 0;
  uint8_t severity = 0;
  uint8_t node_type = 0;
  uint8_t node_id = 0;
  uint8_t sub_node_type = 0;
  uint8_t sub_node_id = 0;
  uint8_t assertion = 0;
  int64_t event_serial_num = 0;
  int64_t notify_serial_num = 0;
  double alarm_raised_time = 0.0;
  std::string event_name;
  std::string additional_info;
  int device_index = 0;
};

// Bounded queue for fault events. When full, drops the oldest and increments
// the drop counter. Processed outside the driver callback.
class EventBus {
 public:
  explicit EventBus(size_t queue_size = 1024);
  ~EventBus();

  // Push an event into the bounded queue (called from the driver callback).
  void push(const FaultEvent& event);

  // Pop up to max_events events. Returns false if the queue was empty.
  bool drain(std::vector<FaultEvent>& out, size_t max_events = 64);

  // Number of events dropped due to overflow.
  uint64_t dropped() const { return dropped_.load(); }

  void set_callback(std::function<void(const FaultEvent&)> callback) {
    callback_ = std::move(callback);
  }

 private:
  size_t queue_size_;
  std::deque<FaultEvent> queue_;
  std::mutex mutex_;
  std::condition_variable cv_;
  std::atomic<uint64_t> dropped_{0};
  std::function<void(const FaultEvent&)> callback_;
};

}  // namespace npu_monitor

#endif  // NPU_MONITOR_EVENTBUS_HPP
