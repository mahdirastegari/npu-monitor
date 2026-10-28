// protocol.cpp — request parsing.
//
// License: see LICENSE.
#include "npu_monitor/protocol.hpp"

#include <cstring>

namespace npu_monitor {

bool parse_request(const char* buf, size_t len, Request& out) {
  if (buf == nullptr || len == 0) return false;
  // Minimal parse: look for "op":"..." and "v":N.
  const char* op = std::strstr(buf, "\"op\"");
  if (!op) return false;
  const char* colon = std::strchr(op, ':');
  if (!colon) return false;
  const char* quote1 = std::strchr(colon, '"');
  if (!quote1) return false;
  const char* quote2 = std::strchr(quote1 + 1, '"');
  if (!quote2) return false;
  std::string op_name(quote1 + 1, quote2 - quote1 - 1);
  if (op_name == "snapshot") out.op = RequestOp::Snapshot;
  else if (op_name == "subscribe") out.op = RequestOp::Subscribe;
  else if (op_name == "unsubscribe") out.op = RequestOp::Unsubscribe;
  else if (op_name == "state") out.op = RequestOp::State;
  else return false;
  const char* v = std::strstr(buf, "\"v\"");
  if (v) {
    const char* colon2 = std::strchr(v, ':');
    if (colon2) out.version = std::atoi(colon2 + 1);
  }
  return true;
}

}  // namespace npu_monitor
