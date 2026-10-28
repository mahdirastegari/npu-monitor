// main.cpp — npu-monitord daemon entry point.
//
// Runs the daemon in the foreground for systemd. The daemon sleeps when no
// client is connected and wakes instantly when a client requests data.
//
// License: see LICENSE.
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstring>
#include <iostream>
#include <thread>

#include "npu_monitor/activity.hpp"
#include "npu_monitor/cache.hpp"
#include "npu_monitor/collector.hpp"
#include "npu_monitor/config.hpp"
#include "npu_monitor/dcmi.hpp"
#include "npu_monitor/log.hpp"
#include "npu_monitor/server.hpp"

namespace {

#ifndef NPU_MONITORD_VERSION
#define NPU_MONITORD_VERSION "0.0.0"
#endif

std::atomic<bool> g_shutdown{false};

void signal_handler(int) { g_shutdown = true; }

void install_signal_handlers() {
  std::signal(SIGINT, signal_handler);
  std::signal(SIGTERM, signal_handler);
  std::signal(SIGPIPE, SIG_IGN);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc > 1) {
    const std::string arg = argv[1];
    if (arg == "--version" || arg == "-V") {
      std::cout << "npu-monitord " << NPU_MONITORD_VERSION << "\n";
      return 0;
    }
    if (arg == "--help" || arg == "-h") {
      std::cout << "usage: npu-monitord [config_file]\n"
                << "\n"
                << "Ascend NPU telemetry daemon. Runs in the foreground.\n"
                << "Reads the configuration from config_file (defaults are\n"
                << "built in), samples the DCMI driver, and serves cached\n"
                << "snapshots on a Unix-domain socket (see docs/protocol.md).\n"
                << "\n"
                << "  -h, --help       show this help and exit\n"
                << "  -V, --version    print version and exit\n";
      return 0;
    }
  }

  npu_monitor::DaemonConfig config;
  std::string config_path = (argc > 1) ? argv[1] : "";
  if (!npu_monitor::load_config(config, config_path)) {
    std::cerr << "npu-monitord: failed to load configuration from " << config_path << "\n";
    return 1;
  }

  npu_monitor::set_log_level(config.log_level == "debug" ? npu_monitor::LogLevel::Debug
                         : config.log_level == "warn" ? npu_monitor::LogLevel::Warn
                         : config.log_level == "error" ? npu_monitor::LogLevel::Error
                         : npu_monitor::LogLevel::Info);

  npu_monitor::DcmiDlopen backend;
  if (!backend.init()) {
    std::cerr << "npu-monitord: DCMI init failed; driver or SDK not available\n";
    std::cerr << "npu-monitord: hint: ensure libdcmi.so is installed and you have\n"
              << "         read access to /dev/davinci* devices\n";
    return 1;
  }

  npu_monitor::log_info("DCMI initialized, driver=" + backend.driver_version());

  npu_monitor::CollectorConfig collector_cfg;
  collector_cfg.utilization_interval_ms = config.utilization_interval_ms;
  collector_cfg.env_interval_ms = config.env_interval_ms;
  collector_cfg.process_interval_ms = config.process_interval_ms;
  collector_cfg.pcie_interval_ms = config.pcie_interval_ms;
  collector_cfg.utilization_workers = config.utilization_workers;
  collector_cfg.stale_after_ms = config.stale_after_ms;
  collector_cfg.error_threshold = config.error_threshold;
  collector_cfg.idle_timeout_ms = config.idle_timeout_ms;

  npu_monitor::SnapshotCache cache;
  npu_monitor::ActivityTracker activity;
  npu_monitor::Collector collector(backend, collector_cfg, cache, activity);

  npu_monitor::ServerConfig server_cfg;
  server_cfg.socket_path = config.socket_path;
  server_cfg.max_frame_bytes = config.max_frame_bytes;
  server_cfg.max_subscribers = config.max_subscribers;
  npu_monitor::SocketServer server(server_cfg, cache, activity);

  // The daemon starts in idle mode. It will collect once at startup to
  // populate the cache, then sleep until the first client connects.
  // Touch once to trigger the initial collection.
  activity.touch();
  collector.start();
  if (!collector.wait_ready(config.ready_timeout_ms)) {
    std::cerr << "npu-monitord: collector did not produce a snapshot within "
              << config.ready_timeout_ms << " ms\n";
    collector.stop();
    return 1;
  }
  npu_monitor::log_info("collector ready, idle_timeout=" +
                    std::to_string(static_cast<long long>(config.idle_timeout_ms)) +
                    "ms");

  if (!server.start()) {
    std::cerr << "npu-monitord: failed to start socket server on " << config.socket_path << "\n";
    collector.stop();
    return 1;
  }
  npu_monitor::log_info("serving on " + config.socket_path);

  install_signal_handlers();
  while (!g_shutdown) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  npu_monitor::log_info("shutting down");
  server.stop();
  collector.stop();
  return 0;
}
