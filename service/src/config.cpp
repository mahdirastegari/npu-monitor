// config.cpp — configuration loading.
//
// License: see LICENSE.
#include "npu_monitor/config.hpp"

#include <fstream>
#include <sstream>

namespace npu_monitor {

bool load_config(DaemonConfig& config, const std::string& path) {
  if (path.empty()) return true;  // use defaults

  std::ifstream in(path);
  if (!in) return false;

  std::string line;
  while (std::getline(in, line)) {
    // Strip whitespace and comments.
    auto first = line.find_first_not_of(" \t");
    if (first == std::string::npos) continue;
    auto last = line.find_last_not_of(" \t");
    line = line.substr(first, last - first + 1);
    if (line.empty() || line[0] == '#' || line[0] == ';') continue;
    auto eq = line.find('=');
    if (eq == std::string::npos) continue;
    std::string key = line.substr(0, eq);
    std::string value = line.substr(eq + 1);
    auto trim = [](std::string& s) {
      auto f = s.find_first_not_of(" \t\"");
      if (f == std::string::npos) { s.clear(); return; }
      auto l = s.find_last_not_of(" \t\"");
      s = s.substr(f, l - f + 1);
    };
    trim(key);
    trim(value);

    if (key == "utilization_interval_ms") config.utilization_interval_ms = std::stod(value);
    else if (key == "env_interval_ms") config.env_interval_ms = std::stod(value);
    else if (key == "process_interval_ms") config.process_interval_ms = std::stod(value);
    else if (key == "pcie_interval_ms") config.pcie_interval_ms = std::stod(value);
    else if (key == "utilization_workers") config.utilization_workers = std::stoi(value);
    else if (key == "stale_after_ms") config.stale_after_ms = std::stod(value);
    else if (key == "error_threshold") config.error_threshold = std::stoul(value);
    else if (key == "idle_timeout_ms") config.idle_timeout_ms = std::stod(value);
    else if (key == "socket_path") config.socket_path = value;
    else if (key == "max_frame_bytes") config.max_frame_bytes = std::stoul(value);
    else if (key == "max_subscribers") config.max_subscribers = std::stoul(value);
    else if (key == "ready_timeout_ms") config.ready_timeout_ms = std::stod(value);
    else if (key == "log_level") config.log_level = value;
    else if (key == "log_file") config.log_file = value;
  }
  return true;
}

}  // namespace npu_monitor
