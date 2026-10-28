// json_parser.hpp — minimal recursive-descent JSON parser for the client.
//
// Only needs to parse the daemon's snapshot response. No streaming, no
// schema validation. Produces a JsonValue tree that the TUI queries.
//
// License: see LICENSE.
#ifndef NPU_MONITOR_JSON_PARSER_HPP
#define NPU_MONITOR_JSON_PARSER_HPP

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace npu_monitor {

class JsonValue {
 public:
  enum class Type { Null, Bool, Number, String, Array, Object };

  Type type = Type::Null;
  bool bool_val = false;
  double num_val = 0.0;
  std::string str_val;
  std::vector<JsonValue> arr_val;
  std::vector<std::pair<std::string, JsonValue>> obj_val;

  bool is_null() const { return type == Type::Null; }
  bool is_number() const { return type == Type::Number; }
  bool is_string() const { return type == Type::String; }
  bool is_array() const { return type == Type::Array; }
  bool is_object() const { return type == Type::Object; }
  bool is_bool() const { return type == Type::Bool; }

  // Object lookup. Returns nullptr if not found or not an object.
  const JsonValue* find(const std::string& key) const;

  // Convenience getters with defaults.
  double get_num(const std::string& key, double def = 0.0) const;
  std::string get_str(const std::string& key,
                      const std::string& def = "") const;
  bool get_bool(const std::string& key, bool def = false) const;

  // Nested metric lookup: finds obj_key, then inside it finds
  // metric_name's sub-object, and inside that gets "value" as a number.
  // For snapshot format: metrics.aicore_util.value
  double metric_value(const std::string& obj_key,
                      const std::string& metric_name,
                      double def = 0.0) const;
  std::string metric_state(const std::string& obj_key,
                           const std::string& metric_name,
                           const std::string& def = "") const;

  // For arrays — bounds-checked.
  const JsonValue* at(size_t i) const;
  size_t size() const;
};

// Parse a JSON text. Returns false on syntax error.
bool parse_json(const std::string& text, JsonValue& out);

}  // namespace npu_monitor

#endif  // NPU_MONITOR_JSON_PARSER_HPP
