// Minimal JSON writer for npu-monitord. Values are numbers, strings, bools,
// nulls, arrays, and objects. No external dependency.
//
// License: see LICENSE.
#ifndef NPU_MONITOR_JSON_HPP
#define NPU_MONITOR_JSON_HPP

#include <cstdint>
#include <string>
#include <string_view>

namespace npu_monitor {

class JsonWriter {
 public:
  void begin_object() {
    maybe_comma();
    buf_ += '{';
    depth_++;
    pending_comma_ = false;
  }

  void end_object() {
    buf_ += '}';
    depth_--;
    pending_comma_ = true;
  }

  void begin_array() {
    maybe_comma();
    buf_ += '[';
    depth_++;
    pending_comma_ = false;
  }

  void end_array() {
    buf_ += ']';
    depth_--;
    pending_comma_ = true;
  }

  void key(std::string_view k) {
    maybe_comma();
    write_string(k);
    buf_ += ':';
    pending_comma_ = false;
  }

  void value(double v) {
    maybe_comma();
    char tmp[32];
    int n = std::snprintf(tmp, sizeof(tmp), "%.6g", v);
    buf_.append(tmp, static_cast<size_t>(n));
    pending_comma_ = true;
  }

  void value(long long v) {
    maybe_comma();
    char tmp[24];
    int n = std::snprintf(tmp, sizeof(tmp), "%lld", v);
    buf_.append(tmp, static_cast<size_t>(n));
    pending_comma_ = true;
  }

  void value(std::string_view s) {
    maybe_comma();
    write_string(s);
    pending_comma_ = true;
  }

  void value(const char* s) {
    maybe_comma();
    write_string(s ? s : "");
    pending_comma_ = true;
  }

  void value(bool b) {
    maybe_comma();
    buf_ += b ? "true" : "false";
    pending_comma_ = true;
  }

  void null() {
    maybe_comma();
    buf_ += "null";
    pending_comma_ = true;
  }

  std::string take() { return std::move(buf_); }

  void clear() { buf_.clear(); }

 private:
  void maybe_comma() {
    if (pending_comma_) {
      buf_ += ',';
      pending_comma_ = false;
    }
  }

  void write_string(std::string_view s) {
    buf_ += '"';
    for (char c : s) {
      switch (c) {
        case '"': buf_ += "\\\""; break;
        case '\\': buf_ += "\\\\"; break;
        case '\n': buf_ += "\\n"; break;
        case '\r': buf_ += "\\r"; break;
        case '\t': buf_ += "\\t"; break;
        default:
          if (static_cast<unsigned char>(c) < 0x20) {
            char tmp[8];
            std::snprintf(tmp, sizeof(tmp), "\\u%04x", c);
            buf_ += tmp;
          } else {
            buf_ += c;
          }
      }
    }
    buf_ += '"';
  }

  std::string buf_;
  int depth_ = 0;
  bool pending_comma_ = false;
};

}  // namespace npu_monitor

#endif  // NPU_MONITOR_JSON_HPP
