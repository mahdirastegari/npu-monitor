// log.hpp — rate-limited logging.
//
// License: see LICENSE.
#ifndef NPU_MONITOR_LOG_HPP
#define NPU_MONITOR_LOG_HPP

#include <string>

namespace npu_monitor {

enum class LogLevel : int {
  Debug = 0,
  Info = 1,
  Warn = 2,
  Error = 3,
};

void log_message(LogLevel level, const std::string& message);
void log_error(const std::string& message);
void log_warn(const std::string& message);
void log_info(const std::string& message);
void log_debug(const std::string& message);

void set_log_level(LogLevel level);

}  // namespace npu_monitor

#endif  // NPU_MONITOR_LOG_HPP
