// log.cpp — rate-limited logging.
//
// License: see LICENSE.
#include "npu_monitor/log.hpp"

#include <chrono>
#include <cstdio>
#include <mutex>

namespace npu_monitor {

namespace {

std::mutex g_log_mutex;
LogLevel g_level = LogLevel::Info;
std::string g_last_message;
int g_repeat_count = 0;
double g_last_emit_at = 0.0;

double now_ms() {
  return std::chrono::duration<double, std::milli>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

}  // namespace

void set_log_level(LogLevel level) { g_level = level; }

void log_message(LogLevel level, const std::string& message) {
  if (static_cast<int>(level) < static_cast<int>(g_level)) return;
  std::lock_guard<std::mutex> lock(g_log_mutex);
  double now = now_ms();
  if (message == g_last_message && now - g_last_emit_at < 1000.0) {
    g_repeat_count++;
    return;
  }
  if (g_repeat_count > 0) {
    std::fprintf(stderr, "npu-monitord: (repeated %d times) %s\n", g_repeat_count,
                 g_last_message.c_str());
  }
  const char* prefix = "info";
  if (level == LogLevel::Warn) prefix = "warn";
  else if (level == LogLevel::Error) prefix = "error";
  else if (level == LogLevel::Debug) prefix = "debug";
  std::fprintf(stderr, "npu-monitord: [%s] %s\n", prefix, message.c_str());
  g_last_message = message;
  g_last_emit_at = now;
  g_repeat_count = 0;
}

void log_error(const std::string& message) { log_message(LogLevel::Error, message); }
void log_warn(const std::string& message) { log_message(LogLevel::Warn, message); }
void log_info(const std::string& message) { log_message(LogLevel::Info, message); }
void log_debug(const std::string& message) { log_message(LogLevel::Debug, message); }

}  // namespace npu_monitor
