// eventbus.cpp — fault event subscription with a bounded queue.
//
// License: see LICENSE.
#include "npu_monitor/eventbus.hpp"

#include <algorithm>

namespace npu_monitor {

EventBus::EventBus(size_t queue_size) : queue_size_(queue_size) {}

EventBus::~EventBus() = default;

void EventBus::push(const FaultEvent& event) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (queue_.size() >= queue_size_) {
    queue_.pop_front();
    dropped_.fetch_add(1, std::memory_order_relaxed);
  }
  queue_.push_back(event);
  cv_.notify_one();
}

bool EventBus::drain(std::vector<FaultEvent>& out, size_t max_events) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (queue_.empty()) return false;
  size_t n = std::min(max_events, queue_.size());
  out.reserve(out.size() + n);
  for (size_t i = 0; i < n; i++) {
    out.push_back(std::move(queue_.front()));
    queue_.pop_front();
  }
  return true;
}

}  // namespace npu_monitor
