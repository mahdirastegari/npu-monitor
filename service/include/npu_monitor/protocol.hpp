// protocol.hpp — request parsing for the npu-monitord socket protocol.
//
// Requests are JSON objects: {"v":1,"op":"snapshot"} etc.
// License: see LICENSE.
#ifndef NPU_MONITOR_PROTOCOL_HPP
#define NPU_MONITOR_PROTOCOL_HPP

#include <cstddef>
#include <string>

namespace npu_monitor {

enum class RequestOp {
  Snapshot,
  Subscribe,
  Unsubscribe,
  State,
};

struct Request {
  RequestOp op;
  int version = 1;
};

// Parse a request frame. Returns false on malformed input.
bool parse_request(const char* buf, size_t len, Request& out);

}  // namespace npu_monitor

#endif  // NPU_MONITOR_PROTOCOL_HPP
