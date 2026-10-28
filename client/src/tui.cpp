// tui.cpp — FTXUI-based terminal dashboard implementation.
//
// Layout — grid with gray column/row separators:
//
//  NPU │AICore           │Memory              │  PID  │User    │ NPU-MEM│Command
// ─────┼─────────────────┼────────────────────┼───────┼────────┼───────┼──────────────
//  0:0 │█████████████ 45%│██████░░ 27.4G/32.0G│ 12345│user1   │ 6.0GiB│python3 train.py
//     │                 │                    │ 12346│user2   │ 1.2GiB│python3 eval.py
// ─────┼─────────────────┼────────────────────┼───────┼────────┼───────┼──────────────
//  0:1 │░░░░░░░░░░░░░  0% │░░░░░░░░  0.0M/32.0G│       │        │       │(no processes)
//
// Table columns use fixed widths measured in terminal cells (not UTF-8 bytes).
// Mouse-drag selections are copied using OSC 52 where supported.
//
// License: see LICENSE.
#include "npu_monitor/tui.hpp"
#include "npu_monitor/json_parser.hpp"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <atomic>
#include <iostream>
#include <mutex>
#include <thread>
#include <utility>
#include <ctime>
#include <string>
#include <vector>
#include "ftxui/component/component.hpp"
#include "ftxui/component/screen_interactive.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/string.hpp"
#include "ftxui/screen/terminal.hpp"
namespace npu_monitor {
// Exact column widths (in terminal cells). Continuation rows use these
// widths to emit blank cells so the process columns line up.
namespace col {
constexpr int NPU    = 4;
constexpr int AICORE = 18;   // gauge(13) + " " + pct(4)
constexpr int MEMORY = 20;   // gauge(8) + " " + used(5) + "/" + total(5)
constexpr int PID    = 7;   // right-aligned
constexpr int USER   = 8;   // left-aligned
constexpr int MEM    = 7;   // right-aligned
}  // namespace col

namespace {
const ftxui::Color SEP = ftxui::Color::GrayDark;
const ftxui::Color DIM = ftxui::Color::GrayLight;
// Protect JSON while the background reader updates the UI.
std::mutex snapshot_mutex;
std::atomic<ftxui::ScreenInteractive*> refresh_screen{nullptr};

// ---------------------------------------------------------------------------
// Fixed-width string helpers — the core of the alignment guarantee
// ---------------------------------------------------------------------------

std::string fcell(const std::string& s, int width, bool right_align = false) {
  // std::string::size() measures bytes, but "█", "░", and Unicode usernames
  // may take multiple bytes per terminal cell. Never truncate UTF-8 mid-glyph.
  std::string clipped;
  int occupied = 0;
  for (const auto& glyph : ftxui::Utf8ToGlyphs(s)) {
    const int glyph_width = ftxui::string_width(glyph);
    if (glyph_width < 0 || occupied + glyph_width > width) break;
    clipped += glyph;
    occupied += glyph_width;
  }
  const std::string padding(static_cast<size_t>(width - occupied), ' ');
  return right_align ? padding + clipped : clipped + padding;
}
// Encode the FTXUI mouse selection for OSC 52 clipboard transfer.
// The terminal (including any SSH/tmux layers) must permit OSC 52.

std::string encode_base64(const std::string& input) {
  constexpr char digits[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string result;
  result.reserve(((input.size() + 2) / 3) * 4);
  for (size_t i = 0; i < input.size(); i += 3) {
    const unsigned int a = static_cast<unsigned char>(input[i]);
    const unsigned int b = i + 1 < input.size()
                               ? static_cast<unsigned char>(input[i + 1]) : 0;
    const unsigned int c = i + 2 < input.size()
                               ? static_cast<unsigned char>(input[i + 2]) : 0;
    const unsigned int value = (a << 16) | (b << 8) | c;
    result += digits[(value >> 18) & 63];
    result += digits[(value >> 12) & 63];
    result += i + 1 < input.size() ? digits[(value >> 6) & 63] : '=';
    result += i + 2 < input.size() ? digits[value & 63] : '=';
  }
  return result;
}

void copy_selection(const std::string& selected) {
  if (selected.empty() ||
      selected.find_first_not_of(" \t\r\n") == std::string::npos) {
    return;
  }
  std::cout << "\x1b]52;c;" << encode_base64(selected) << "\x07" << std::flush;
}

std::string fmt_pct(double pct) {
  char b[8];
  std::snprintf(b, sizeof(b), "%3.0f%%", pct);
  return b;
}

std::string fmt_mem(double mib) {
  char b[32];
  if (mib >= 1024) std::snprintf(b, sizeof(b), "%.1fGiB", mib / 1024.0);
  else             std::snprintf(b, sizeof(b), "%.0fMiB", mib);
  return b;
}

std::string fmt_mem_short(double mib) {
  char b[16];
  if (mib >= 1024) std::snprintf(b, sizeof(b), "%4.1fG", mib / 1024.0);
  else             std::snprintf(b, sizeof(b), "%4.0fM", mib);
  return b;
}

std::string fmt_time() {
  auto now = std::time(nullptr);
  struct tm tm{};
  localtime_r(&now, &tm);
  char b[16];
  std::strftime(b, sizeof(b), "%H:%M:%S", &tm);
  return b;
}
// Gauge as a string: filled blocks for used portion, light shade for rest.
// Uses UTF-8 string concatenation (not char literals) so the block
// characters survive the compiler's multi-byte handling.

std::string gauge_str(double pct, int width) {
  pct = std::max(0.0, std::min(100.0, pct));
  int filled = static_cast<int>(std::lround(pct / 100.0 * width));
  filled = std::max(0, std::min(width, filled));
  std::string s;
  for (int i = 0; i < filled; i++) s += "█";
  for (int i = filled; i < width; i++) s += "░";
  return s;
}

// ---------------------------------------------------------------------------
// Color helpers — green → yellow → gold → red gradient
// ---------------------------------------------------------------------------

ftxui::Color util_color(double pct) {
  if (pct < 30) return ftxui::Color::Green;
  if (pct < 60) return ftxui::Color::Yellow;
  if (pct < 80) return ftxui::Color::Gold1;
  return ftxui::Color::Red;
}
// Only the health state controls the NPU ID colour, never AICore usage.
// Other states (including ok, stale, and pending) remain green.
ftxui::Color npu_color(std::string hth_state) {
  std::transform(hth_state.begin(), hth_state.end(), hth_state.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (hth_state == "error") return ftxui::Color::Red;
  if (hth_state == "warning" || hth_state == "warn")
    return ftxui::Color::Yellow;
  return ftxui::Color::Green;
}

// ---------------------------------------------------------------------------
// Row structure: NPU │ AICORE │ MEMORY │ PID │ USER │ MEM │ Command
// ---------------------------------------------------------------------------
constexpr int N_COLS = 7;
struct Row {
  std::string text[N_COLS];
  ftxui::Color color[N_COLS];
};
// Row separator: keep junctions aligned with the six fixed-width columns,
// then draw across the Command column to the right edge of the terminal.
// The Command column has no fixed width, so this must be recalculated on
// every render (including terminal resizes). All widths are terminal cells.
ftxui::Element hsep_row() {
  const int widths[] = {
    col::NPU, col::AICORE, col::MEMORY, col::PID, col::USER, col::MEM,
  };
  int prefix_width = 0;
  std::string line;
  for (int width : widths) {
    for (int i = 0; i < width; ++i) line += "─";
    line += "┼";
    prefix_width += width + 1;
  }
  const int remaining = std::max(0, ftxui::Terminal::Size().dimx - prefix_width);
  for (int i = 0; i < remaining; ++i) line += "─";
  return ftxui::text(line) | ftxui::color(SEP);
}
// Header row — same column widths as data rows.

ftxui::Element header_row() {
  const char* labels[N_COLS] = {
    "NPU", "AICore", "Memory", "PID", "User", "NPU-MEM", "Command",
  };
  const int widths[N_COLS] = {
    col::NPU, col::AICORE, col::MEMORY, col::PID, col::USER, col::MEM, 0,
  };
  std::vector<ftxui::Element> parts;
  for (int i = 0; i < N_COLS; i++) {
    if (i > 0) parts.push_back(ftxui::text("│") | ftxui::color(SEP));
    std::string s = widths[i] > 0 ? fcell(labels[i], widths[i], i == 3 || i == 5) : labels[i];
    parts.push_back(ftxui::text(s) | ftxui::bold | ftxui::color(DIM));
  }
  return ftxui::hbox(parts);
}

// ---------------------------------------------------------------------------
// Device row + process rows
// ---------------------------------------------------------------------------

ftxui::Element render_device(const JsonValue* d) {
  int card = static_cast<int>(d->get_num("card_id", 0));
  int chip = static_cast<int>(d->get_num("chip_id", 0));
  std::string id = std::to_string(card) + ":" + std::to_string(chip);
  double aicore    = d->metric_value("metrics", "aicore_util", 0);
  std::string ai_st = d->metric_state("metrics", "aicore_util");
  double mem_used  = d->metric_value("metrics", "memory_used", 0);
  double mem_total = d->metric_value("metrics", "memory_total", 1);
  std::string mem_st = d->metric_state("metrics", "memory_used");
  std::string hth_st = d->metric_state("metrics", "health");
  const JsonValue* procs = d->find("processes");
  size_t n_procs = (procs && procs->is_array()) ? procs->size() : 0;
  ftxui::Color id_color = npu_color(hth_st);
  std::vector<ftxui::Element> rows;
  // Row 1: device info + first process (or "no processes")
  {
    Row r;
    r.text[0] = fcell(id, col::NPU);
    r.color[0] = id_color;
    r.text[1] = (ai_st == "ok" || ai_st == "stale")
                  ? fcell(gauge_str(aicore, 13) + " " + fmt_pct(aicore), col::AICORE)
                  : fcell(ai_st == "pending" ? "pending" : ai_st, col::AICORE);
    r.color[1] = util_color(aicore);
    double mem_pct = mem_total > 0 ? (mem_used / mem_total * 100.0) : 0;
    if (mem_st == "ok" || mem_st == "stale") {
      r.text[2] = fcell(gauge_str(mem_pct, 8) + " " + fmt_mem_short(mem_used) +
                       "/" + fmt_mem_short(mem_total), col::MEMORY);
      r.color[2] = ftxui::Color::CadetBlue;
    } else {
      r.text[2] = fcell(mem_st == "pending" ? "pending" : mem_st, col::MEMORY);
      r.color[2] = DIM;
    }
    if (n_procs > 0) {
      const JsonValue* p = procs->at(0);
      auto pid = static_cast<long long>(p->get_num("pid", 0));
      std::string user = p->get_str("user", "");
      double mem = p->get_num("memory_mib", 0);
      std::string cmd = p->get_str("command", "");
      if (cmd.empty()) cmd = p->get_str("command_line", "");
      if (cmd.empty()) cmd = p->get_str("cmdline", "");
      if (cmd.empty()) cmd = "(unknown)";
      if (user.empty()) user = "-";
      r.text[3] = fcell(std::to_string(pid), col::PID, true);
      r.color[3] = ftxui::Color::CadetBlue;
      r.text[4] = fcell(user, col::USER);
      r.color[4] = ftxui::Color::White;
      r.text[5] = fcell(fmt_mem(mem), col::MEM, true);
      r.color[5] = ftxui::Color::CadetBlue;
      r.text[6] = cmd;
      r.color[6] = ftxui::Color::White;
    } else {
      r.text[3] = fcell("", col::PID);
      r.color[3] = DIM;
      r.text[4] = std::string(col::USER, ' ');
      r.color[4] = DIM;
      r.text[5] = std::string(col::MEM, ' ');
      r.color[5] = DIM;
      r.text[6] = "(no processes)";
      r.color[6] = DIM;
    }
    std::vector<ftxui::Element> parts;
    for (int i = 0; i < N_COLS; i++) {
      if (i > 0) parts.push_back(ftxui::text("│") | ftxui::color(SEP));
      parts.push_back(ftxui::text(r.text[i]) | ftxui::color(r.color[i]));
    }
    rows.push_back(ftxui::hbox(parts));
  }
  // Continuation rows: remaining processes, aligned under their columns.
  auto blank_left = std::vector<std::string>{
    std::string(col::NPU, ' '),
    std::string(col::AICORE, ' '),
    std::string(col::MEMORY, ' '),
  };
  for (size_t i = 1; i < n_procs; i++) {
    const JsonValue* p = procs->at(i);
    if (!p) continue;
    auto pid = static_cast<long long>(p->get_num("pid", 0));
    std::string user = p->get_str("user", "");
    double mem = p->get_num("memory_mib", 0);
    std::string cmd = p->get_str("command", "");
    if (cmd.empty()) cmd = p->get_str("command_line", "");
    if (cmd.empty()) cmd = p->get_str("cmdline", "");
    if (cmd.empty()) cmd = "(unknown)";
    if (user.empty()) user = "-";
    std::vector<ftxui::Element> parts;
    auto add = [&](const std::string& s, ftxui::Color c) {
      parts.push_back(ftxui::text(s) | ftxui::color(c));
    };
    add(blank_left[0], ftxui::Color::White);
    add("│", SEP);
    add(blank_left[1], ftxui::Color::White);
    add("│", SEP);
    add(blank_left[2], ftxui::Color::White);
    add("│", SEP);
    add(fcell(std::to_string(pid), col::PID, true), ftxui::Color::CadetBlue);
    add("│", SEP);
    add(fcell(user, col::USER), ftxui::Color::White);
    add("│", SEP);
    add(fcell(fmt_mem(mem), col::MEM, true), ftxui::Color::CadetBlue);
    add("│", SEP);
    add(cmd, ftxui::Color::White);
    rows.push_back(ftxui::hbox(parts));
  }
  rows.push_back(hsep_row());
  return ftxui::vbox(rows);
}

// ---------------------------------------------------------------------------
// Title bar
// ---------------------------------------------------------------------------

ftxui::Element render_header(const JsonValue& snap) {
  std::string state = snap.get_str("daemon_state", "unknown");
  const JsonValue* devs = snap.find("devices");
  int n_dev = (devs && devs->is_array()) ? static_cast<int>(devs->size()) : 0;
  int max_card = 0;
  std::string model;
  if (devs && devs->is_array()) {
    for (size_t i = 0; i < devs->size(); i++) {
      const JsonValue* d = devs->at(i);
      if (!d) continue;
      int c = static_cast<int>(d->get_num("card_id", 0));
      if (c > max_card) max_card = c;
      if (model.empty()) model = d->get_str("model", "");
    }
  }
  ftxui::Color sc = ftxui::Color::Red;
  if (state == "ready")      sc = ftxui::Color::Green;
  else if (state == "idle")  sc = ftxui::Color::GrayLight;
  else if (state == "stale") sc = ftxui::Color::Yellow;
  else if (state == "init")  sc = ftxui::Color::CadetBlue;
  return ftxui::hbox({
    ftxui::text(" npu-monitord") | ftxui::bold | ftxui::color(ftxui::Color::CadetBlue),
    ftxui::text(" - " + model + " . " + std::to_string(n_dev) +
                " chips / " + std::to_string(max_card + 1) + " cards") | ftxui::dim,
    ftxui::filler(),
    ftxui::text("daemon: ") | ftxui::dim,
    ftxui::text(state) | ftxui::bold | ftxui::color(sc),
    ftxui::text("  " + fmt_time()) | ftxui::dim,
  });
}

// ---------------------------------------------------------------------------
// Full snapshot
// ---------------------------------------------------------------------------

ftxui::Element render_snapshot(const std::string& json) {
  JsonValue snap;
  if (!parse_json(json, snap))
    return ftxui::text(" JSON parse error") | ftxui::color(ftxui::Color::Red);
  const JsonValue* devs = snap.find("devices");
  if (!devs || !devs->is_array())
    return ftxui::text(" (no devices)");
  std::vector<ftxui::Element> children;
  children.push_back(render_header(snap));
  children.push_back(hsep_row());
  children.push_back(header_row());
  children.push_back(hsep_row());
  for (size_t i = 0; i < devs->size(); i++) {
    const JsonValue* d = devs->at(i);
    if (d) children.push_back(render_device(d));
  }
  children.push_back(ftxui::filler());
  return ftxui::vbox(children);
}
}  // namespace
// ---------------------------------------------------------------------------
// Tui implementation
// ---------------------------------------------------------------------------
Tui::Tui(std::string socket_path, double interval)
    : socket_path_(std::move(socket_path)),
      interval_(std::isfinite(interval) && interval > 0.0 ? interval : 1.0) {
  std::signal(SIGPIPE, SIG_IGN);
}
Tui::~Tui() {
  stop_flag_ = true;
  if (fetcher_.joinable()) fetcher_.join();
}

bool Tui::fetch_snapshot(std::string& json_out) {
  int fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
  if (fd < 0) return false;
  struct sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  if (socket_path_.size() >= sizeof(addr.sun_path)) { close(fd); return false; }
  std::memcpy(addr.sun_path, socket_path_.c_str(), socket_path_.size() + 1);
  if (connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) {
    close(fd); return false;
  }
  const char req[] = "{\"v\":1,\"op\":\"snapshot\"}";
  if (send(fd, req, sizeof(req) - 1, MSG_NOSIGNAL) < 0) { close(fd); return false; }
  std::vector<char> buf(4 * 1024 * 1024);
  ssize_t n = recv(fd, buf.data(), buf.size() - 1, 0);
  close(fd);
  if (n <= 0) return false;
  json_out.assign(buf.data(), static_cast<size_t>(n));
  return true;
}

void Tui::fetch_loop() {
  while (!stop_flag_) {
    std::string json;
    const bool ok = fetch_snapshot(json);
    {
      std::lock_guard<std::mutex> lock(snapshot_mutex);
      if (ok) latest_json_ = std::move(json);
      connected_ = ok;
    }
    // Refresh even when nobody presses a key (the reader runs on another thread).
    if (auto* screen = refresh_screen.load()) {
      screen->PostEvent(ftxui::Event::Custom);
    }
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::duration<double>(interval_);
    while (!stop_flag_ && std::chrono::steady_clock::now() < deadline)
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
}

int Tui::run() {
  auto screen = ftxui::ScreenInteractive::Fullscreen();
  refresh_screen.store(&screen);
  fetcher_ = std::thread([this]() { fetch_loop(); });
  // Built-in FTXUI selection works throughout the dashboard, including the
  // header and the process rows. Each changed highlighted range is copied.
  screen.SelectionChange([&screen]() {
    copy_selection(screen.GetSelection());
  });
  auto renderer = ftxui::Renderer([&]() -> ftxui::Element {
    std::string snapshot;
    bool connected = false;
    {
      std::lock_guard<std::mutex> lock(snapshot_mutex);
      snapshot = latest_json_;
      connected = connected_;
    }
    if (!connected || snapshot.empty()) {
      return ftxui::vbox({
          ftxui::text(""),
          ftxui::text(" npu-monitor: cannot connect to " + socket_path_) |
              ftxui::color(ftxui::Color::Red) | ftxui::bold,
          ftxui::text(" Retrying... (q to quit)") | ftxui::dim,
      });
    }
    return render_snapshot(snapshot);
  });
  auto component = ftxui::CatchEvent(renderer, [&](ftxui::Event event) {
    if (event == ftxui::Event::Character('q') ||
        event == ftxui::Event::Character('Q') ||
        event == ftxui::Event::Escape ||
        event == ftxui::Event::Special({3})) {
      screen.Exit();
      return true;
    }
    // Leave mouse events unhandled so FTXUI handles text highlighting.
    return false;
  });
  screen.Loop(component);
  refresh_screen.store(nullptr);
  stop_flag_ = true;
  if (fetcher_.joinable()) fetcher_.join();
  return 0;
}
}  // namespace npu_monitor
