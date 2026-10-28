// json_parser.cpp — minimal JSON parser implementation.
//
// Recursive-descent parser. Handles objects, arrays, strings (with
// \uXXXX escapes), numbers (including scientific notation), booleans, null.
//
// License: see LICENSE.
#include "npu_monitor/json_parser.hpp"

#include <cctype>
#include <cstdlib>
#include <cstring>

namespace npu_monitor {

// ---------------------------------------------------------------------------
// JsonValue convenience methods
// ---------------------------------------------------------------------------

const JsonValue* JsonValue::find(const std::string& key) const {
  if (type != Type::Object) return nullptr;
  for (const auto& kv : obj_val) {
    if (kv.first == key) return &kv.second;
  }
  return nullptr;
}

double JsonValue::get_num(const std::string& key, double def) const {
  const JsonValue* v = find(key);
  if (!v || v->type != Type::Number) return def;
  return v->num_val;
}

std::string JsonValue::get_str(const std::string& key,
                               const std::string& def) const {
  const JsonValue* v = find(key);
  if (!v || v->type != Type::String) return def;
  return v->str_val;
}

bool JsonValue::get_bool(const std::string& key, bool def) const {
  const JsonValue* v = find(key);
  if (!v || v->type != Type::Bool) return def;
  return v->bool_val;
}

double JsonValue::metric_value(const std::string& obj_key,
                               const std::string& metric_name,
                               double def) const {
  const JsonValue* obj = find(obj_key);
  if (!obj) return def;
  const JsonValue* metric = obj->find(metric_name);
  if (!metric) return def;
  return metric->get_num("value", def);
}

std::string JsonValue::metric_state(const std::string& obj_key,
                                    const std::string& metric_name,
                                    const std::string& def) const {
  const JsonValue* obj = find(obj_key);
  if (!obj) return def;
  const JsonValue* metric = obj->find(metric_name);
  if (!metric) return def;
  return metric->get_str("state", def);
}

const JsonValue* JsonValue::at(size_t i) const {
  if (type != Type::Array || i >= arr_val.size()) return nullptr;
  return &arr_val[i];
}

size_t JsonValue::size() const {
  if (type == Type::Array) return arr_val.size();
  if (type == Type::Object) return obj_val.size();
  return 0;
}

// ---------------------------------------------------------------------------
// Parser
// ---------------------------------------------------------------------------

namespace {

class Parser {
 public:
  Parser(const std::string& text) : text_(text), pos_(0) {}

  bool parse(JsonValue& out) {
    skip_ws();
    return parse_value(out) && pos_ == text_.size();
  }

 private:
  const std::string& text_;
  size_t pos_;

  void skip_ws() {
    while (pos_ < text_.size() &&
           std::isspace(static_cast<unsigned char>(text_[pos_]))) {
      pos_++;
    }
  }

  bool peek(char c) {
    skip_ws();
    return pos_ < text_.size() && text_[pos_] == c;
  }

  bool match(char c) {
    skip_ws();
    if (pos_ < text_.size() && text_[pos_] == c) {
      pos_++;
      return true;
    }
    return false;
  }

  bool parse_value(JsonValue& out) {
    skip_ws();
    if (pos_ >= text_.size()) return false;
    char c = text_[pos_];
    if (c == '{') return parse_object(out);
    if (c == '[') return parse_array(out);
    if (c == '"') { if (!parse_string(out.str_val)) return false; out.type = JsonValue::Type::String; return true; }
    if (c == 't' || c == 'f') return parse_bool(out);
    if (c == 'n') return parse_null(out);
    return parse_number(out);
  }

  bool parse_object(JsonValue& out) {
    out.type = JsonValue::Type::Object;
    pos_++;  // skip '{'
    skip_ws();
    if (peek('}')) { pos_++; return true; }
    for (;;) {
      std::string key;
      if (!parse_string(key)) return false;
      if (!match(':')) return false;
      JsonValue val;
      if (!parse_value(val)) return false;
      out.obj_val.emplace_back(std::move(key), std::move(val));
      if (match(',')) continue;
      if (match('}')) return true;
      return false;
    }
  }

  bool parse_array(JsonValue& out) {
    out.type = JsonValue::Type::Array;
    pos_++;  // skip '['
    skip_ws();
    if (peek(']')) { pos_++; return true; }
    for (;;) {
      JsonValue val;
      if (!parse_value(val)) return false;
      out.arr_val.push_back(std::move(val));
      if (match(',')) continue;
      if (match(']')) return true;
      return false;
    }
  }

  bool parse_string(std::string& out) {
    skip_ws();
    if (pos_ >= text_.size() || text_[pos_] != '"') return false;
    pos_++;  // skip opening quote
    out.clear();
    while (pos_ < text_.size()) {
      char c = text_[pos_++];
      if (c == '"') return true;
      if (c == '\\') {
        if (pos_ >= text_.size()) return false;
        char esc = text_[pos_++];
        switch (esc) {
          case '"': out += '"'; break;
          case '\\': out += '\\'; break;
          case '/': out += '/'; break;
          case 'b': out += '\b'; break;
          case 'f': out += '\f'; break;
          case 'n': out += '\n'; break;
          case 'r': out += '\r'; break;
          case 't': out += '\t'; break;
          case 'u': {
            if (pos_ + 4 > text_.size()) return false;
            unsigned code = 0;
            for (int i = 0; i < 4; i++) {
              char h = text_[pos_++];
              code <<= 4;
              if (h >= '0' && h <= '9') code |= h - '0';
              else if (h >= 'a' && h <= 'f') code |= h - 'a' + 10;
              else if (h >= 'A' && h <= 'F') code |= h - 'A' + 10;
              else return false;
            }
            // Encode as UTF-8
            if (code < 0x80) {
              out += static_cast<char>(code);
            } else if (code < 0x800) {
              out += static_cast<char>(0xC0 | (code >> 6));
              out += static_cast<char>(0x80 | (code & 0x3F));
            } else {
              out += static_cast<char>(0xE0 | (code >> 12));
              out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
              out += static_cast<char>(0x80 | (code & 0x3F));
            }
            break;
          }
          default: return false;
        }
      } else {
        out += c;
      }
    }
    return false;  // unterminated
  }

  bool parse_number(JsonValue& out) {
    skip_ws();
    size_t start = pos_;
    if (pos_ < text_.size() && (text_[pos_] == '-' || text_[pos_] == '+'))
      pos_++;
    while (pos_ < text_.size() &&
           (std::isdigit(static_cast<unsigned char>(text_[pos_])) ||
            text_[pos_] == '.' || text_[pos_] == 'e' || text_[pos_] == 'E' ||
            text_[pos_] == '+' || text_[pos_] == '-')) {
      pos_++;
    }
    if (pos_ == start) return false;
    out.type = JsonValue::Type::Number;
    out.num_val = std::strtod(text_.c_str() + start, nullptr);
    return true;
  }

  bool parse_bool(JsonValue& out) {
    if (text_.compare(pos_, 4, "true") == 0) {
      out.type = JsonValue::Type::Bool;
      out.bool_val = true;
      pos_ += 4;
      return true;
    }
    if (text_.compare(pos_, 5, "false") == 0) {
      out.type = JsonValue::Type::Bool;
      out.bool_val = false;
      pos_ += 5;
      return true;
    }
    return false;
  }

  bool parse_null(JsonValue& out) {
    if (text_.compare(pos_, 4, "null") == 0) {
      out.type = JsonValue::Type::Null;
      pos_ += 4;
      return true;
    }
    return false;
  }
};

}  // namespace

bool parse_json(const std::string& text, JsonValue& out) {
  Parser p(text);
  return p.parse(out);
}

}  // namespace npu_monitor
