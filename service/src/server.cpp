// server.cpp — Unix-domain socket server implementation.
//
// Every client request calls activity_.touch() to wake the collector lanes.
// The first request after idle gets the last cached snapshot; the collector
// then starts producing fresh data. Subsequent requests within the idle
// timeout get fresh data and keep the daemon awake.
//
// License: see LICENSE.
#include "npu_monitor/server.hpp"

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstring>

#include "npu_monitor/json.hpp"
#include "npu_monitor/log.hpp"
#include "npu_monitor/protocol.hpp"
#include "npu_monitor/state.hpp"

namespace npu_monitor {

SocketServer::SocketServer(const ServerConfig& config, SnapshotCache& cache,
                           ActivityTracker& activity)
    : config_(config), cache_(cache), activity_(activity) {}

SocketServer::~SocketServer() { stop(); }

bool SocketServer::start() {
  listen_fd_ = socket(AF_UNIX, SOCK_SEQPACKET, 0);
  if (listen_fd_ < 0) {
    log_error("socket(AF_UNIX, SOCK_SEQPACKET) failed");
    return false;
  }

  struct sockaddr_un addr;
  std::memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  std::strncpy(addr.sun_path, config_.socket_path.c_str(), sizeof(addr.sun_path) - 1);

  unlink(config_.socket_path.c_str());
  if (bind(listen_fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) {
    log_error("bind failed on " + config_.socket_path);
    close(listen_fd_);
    listen_fd_ = -1;
    return false;
  }

  // Make the socket world-connectable. The daemon runs as root, but
  // non-root users need write permission on the socket file to connect.
  chmod(config_.socket_path.c_str(), 0666);

  if (listen(listen_fd_, 8) != 0) {
    log_error("listen failed on " + config_.socket_path);
    close(listen_fd_);
    listen_fd_ = -1;
    return false;
  }

  running_ = true;
  accept_thread_ = std::thread([this]() { accept_loop(); });
  return true;
}

void SocketServer::stop() {
  running_ = false;
  if (listen_fd_ >= 0) {
    shutdown(listen_fd_, SHUT_RDWR);
    close(listen_fd_);
    listen_fd_ = -1;
  }
  if (accept_thread_.joinable()) accept_thread_.join();
  cleanup_socket();
}

void SocketServer::accept_loop() {
  while (running_) {
    struct sockaddr_un peer;
    socklen_t peer_len = sizeof(peer);
    int fd = accept(listen_fd_, reinterpret_cast<struct sockaddr*>(&peer), &peer_len);
    if (fd < 0) {
      if (running_) log_error("accept failed");
      continue;
    }
    handle_client(fd);
    close(fd);
  }
}

void SocketServer::handle_client(int fd) {
  char buf[256];
  ssize_t n = recvfrom(fd, buf, sizeof(buf) - 1, 0, nullptr, nullptr);
  if (n <= 0) return;
  buf[n] = '\0';

  Request request;
  if (!parse_request(buf, static_cast<size_t>(n), request)) {
    log_error("malformed request");
    return;
  }

  // Wake the collector on every request. This is the core of the
  // idle/wake strategy: the client's request is what keeps the daemon
  // alive. The first touch after idle causes the lanes to collect
  // immediately; subsequent touches extend the awake window.
  activity_.touch();

  JsonWriter w;
  switch (request.op) {
    case RequestOp::Snapshot:
    case RequestOp::Subscribe: {
      auto snap = cache_.latest();
      if (snap) {
        snapshot_to_json(*snap, w);
      } else {
        w.begin_object();
        w.key("error"); w.value("no snapshot yet");
        w.end_object();
      }
      break;
    }
    case RequestOp::Unsubscribe:
    case RequestOp::State: {
      w.begin_object();
      w.key("ok"); w.value(true);
      w.end_object();
      break;
    }
  }

  std::string response = w.take();
  if (response.size() > config_.max_frame_bytes) {
    log_error("response exceeds frame limit");
    return;
  }
  sendto(fd, response.data(), response.size(), 0, nullptr, 0);
}

void SocketServer::cleanup_socket() {
  if (!config_.socket_path.empty()) {
    unlink(config_.socket_path.c_str());
  }
}

}  // namespace npu_monitor
