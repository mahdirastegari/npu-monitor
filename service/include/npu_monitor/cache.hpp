// cache.hpp — thread-safe snapshot cache with atomic publication.
//
// The cache holds the latest published snapshot. Readers get a consistent
// view via shared_ptr; writers swap in a new snapshot atomically. Client
// connections, refresh requests, and subscriptions never trigger
// additional hardware queries.
//
// License: see LICENSE.
#ifndef NPU_MONITOR_CACHE_HPP
#define NPU_MONITOR_CACHE_HPP

#include <atomic>
#include <memory>
#include <mutex>

#include "npu_monitor/snapshot.hpp"

namespace npu_monitor {

class SnapshotCache {
 public:
  void publish(SnapshotPtr snap);
  SnapshotPtr latest() const;

 private:
  mutable std::mutex mutex_;
  SnapshotPtr current_;
};

}  // namespace npu_monitor

#endif  // NPU_MONITOR_CACHE_HPP
