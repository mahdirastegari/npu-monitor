// npu-monitor.cpp — lightweight CLI frontend.
//
// Reads the cached snapshot from the daemon over a Unix-domain socket.
// Never touches the driver. Commands:
//
//   npu-monitor top     [socket_path] [interval_sec]   — fancy live dashboard
//   npu-monitor snapshot [socket_path]                 — one-shot JSON output
//   npu-monitor watch    [socket_path] [interval_sec] [count]  — JSON stream
//
// Default command is "top" when no arguments are given.
//
// License: see LICENSE.
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "npu_monitor/tui.hpp"

#ifndef NPU_MONITOR_VERSION
#define NPU_MONITOR_VERSION "0.0.0"
#endif

namespace {

int connect_daemon(const std::string& socket_path) {
  int fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
  if (fd < 0) return -1;
  struct sockaddr_un addr;
  std::memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  std::strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);
  if (connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) {
    close(fd);
    return -1;
  }
  return fd;
}

bool send_request(int fd, const std::string& request, std::string& response) {
  if (sendto(fd, request.data(), request.size(), 0, nullptr, 0) < 0) return false;
  std::vector<char> buf(4 * 1024 * 1024);
  ssize_t n = recvfrom(fd, buf.data(), buf.size() - 1, 0, nullptr, nullptr);
  if (n <= 0) return false;
  response.assign(buf.data(), static_cast<size_t>(n));
  return true;
}

bool fetch_snapshot(const std::string& socket_path, std::string& response) {
  int fd = connect_daemon(socket_path);
  if (fd < 0) return false;
  bool ok = send_request(fd, "{\"v\":1,\"op\":\"snapshot\"}", response);
  close(fd);
  return ok;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc > 1) {
    const std::string arg = argv[1];
    if (arg == "--version" || arg == "-V") {
      std::cout << "npu-monitor " << NPU_MONITOR_VERSION << "\n";
      return 0;
    }
    if (arg == "--help" || arg == "-h") {
      std::cerr << "npu-monitor " << NPU_MONITOR_VERSION << "\n\n"
                << "usage: npu-monitor [command] [socket_path] [interval_sec] [count]\n"
                << "\n"
                << "  top        live dashboard (default)\n"
                << "  snapshot   one-shot JSON snapshot\n"
                << "  watch      JSON stream, one snapshot per interval\n"
                << "\n"
                << "  -h, --help     show this help and exit\n"
                << "  -V, --version  print version and exit\n"
                << "\n"
                << "Set NPU_MONITOR_SOCKET to override the socket path.\n";
      return 0;
    }
  }

  std::string command = (argc > 1) ? argv[1] : "top";
  std::string socket_path = (argc > 2) ? argv[2] : "/var/run/npu-monitor.sock";

  // Also check NPU_MONITOR_SOCKET environment variable.
  if (const char* env = std::getenv("NPU_MONITOR_SOCKET")) {
    socket_path = env;
  }

  // Resolve command — accept abbreviated forms.
  if (command == "top" || command == "dashboard" || command == "tui") {
    double interval = 1.0;
    if (argc > 3) interval = std::stod(argv[3]);
    npu_monitor::Tui tui(socket_path, interval);
    return tui.run();

  } else if (command == "snapshot") {
    std::string response;
    if (!fetch_snapshot(socket_path, response)) {
      std::cerr << "npu-monitor: cannot connect to " << socket_path << "\n";
      std::cerr << "hint: set NPU_MONITOR_SOCKET or pass the path as the second argument\n";
      return 1;
    }
    std::cout << response << "\n";

  } else if (command == "watch") {
    double interval = 1.0;
    int count = 0;  // 0 = infinite
    if (argc > 3) interval = std::stod(argv[3]);
    if (argc > 4) count = std::stoi(argv[4]);
    int elapsed = 0;
    for (;;) {
      if (count > 0 && elapsed >= count) break;
      std::string response;
      if (!fetch_snapshot(socket_path, response)) {
        std::cerr << "npu-monitor: cannot connect to " << socket_path << "\n";
        break;
      }
      std::cout << response << "\n";
      std::cout.flush();
      elapsed++;
      if (count > 0 && elapsed >= count) break;
      std::this_thread::sleep_for(
          std::chrono::duration<double>(interval));
    }
  } else {
    std::cerr << "usage: npu-monitor top     [socket_path] [interval_sec]\n"
              << "       npu-monitor snapshot [socket_path]\n"
              << "       npu-monitor watch    [socket_path] [interval_sec] [count]\n"
              << "\n"
              << "Default command is 'top' (live dashboard).\n"
              << "Set NPU_MONITOR_SOCKET to override the socket path.\n";
    return 1;
  }

  return 0;
}
