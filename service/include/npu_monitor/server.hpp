// server.hpp — Unix-domain socket server with idle/wake support.
//
// Reads cached snapshots and serves them to lightweight clients. Every
// client request calls ActivityTracker::touch() to wake the collector.
// Client connections, refresh requests, and subscriptions never trigger
// additional hardware queries — they only wake the existing collector.
//
// License: see LICENSE.
#ifndef NPU_MONITOR_SERVER_HPP
#define NPU_MONITOR_SERVER_HPP

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

#include "npu_monitor/activity.hpp"
#include "npu_monitor/cache.hpp"
#include "npu_monitor/protocol.hpp"

namespace npu_monitor {

struct ServerConfig {
  std::string socket_path = "/var/run/npu-monitor.sock";
  size_t max_frame_bytes = 4 * 1024 * 1024;
  size_t max_subscribers = 64;
};

class SocketServer {
 public:
  SocketServer(const ServerConfig& config, SnapshotCache& cache,
               ActivityTracker& activity);
  ~SocketServer();

  bool start();
  void stop();

 private:
  void accept_loop();
  void handle_client(int fd);
  void cleanup_socket();

  ServerConfig config_;
  SnapshotCache& cache_;
  ActivityTracker& activity_;
  int listen_fd_ = -1;
  std::atomic<bool> running_{false};
  std::thread accept_thread_;
};

}  // namespace npu_monitor

#endif  // NPU_MONITOR_SERVER_HPP
