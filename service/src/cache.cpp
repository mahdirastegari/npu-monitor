// cache.cpp — snapshot cache.
//
// License: see LICENSE.
#include "npu_monitor/cache.hpp"

#include <mutex>

namespace npu_monitor {

void SnapshotCache::publish(SnapshotPtr snap) {
  std::lock_guard<std::mutex> lock(mutex_);
  current_ = std::move(snap);
}

SnapshotPtr SnapshotCache::latest() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return current_;
}

}  // namespace npu_monitor
