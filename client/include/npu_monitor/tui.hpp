// tui.hpp — FTXUI-based terminal dashboard for npu-monitor.
//
// Uses FTXUI for all terminal handling. A background thread fetches
// snapshots from the daemon; the FTXUI loop renders from the latest
// cached snapshot.
//
// License: see LICENSE.
#ifndef NPU_MONITOR_TUI_HPP
#define NPU_MONITOR_TUI_HPP

#include <atomic>
#include <string>
#include <thread>

namespace npu_monitor {

class Tui {
 public:
  Tui(std::string socket_path, double interval);
  ~Tui();

  int run();

 private:
  std::string socket_path_;
  double interval_;
  std::string latest_json_;
  std::atomic<bool> connected_{false};
  std::atomic<bool> stop_flag_{false};
  std::thread fetcher_;
  void fetch_loop();
  bool fetch_snapshot(std::string& json_out);
};

}  // namespace npu_monitor

#endif  // NPU_MONITOR_TUI_HPP
