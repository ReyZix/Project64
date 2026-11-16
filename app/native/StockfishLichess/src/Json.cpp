#include "Json.h"

#include <cstdlib>

namespace {

// Walks through the text one character at a time (a recursive descent parser).
struct Parser {
  const std::string& s;
  size_t i = 0;

  void skipSpaces() {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\t' || s[i] == '\r')) i++;
  }

  bool word(const char* w) {
    size_t n = std::char_traits<char>::length(w);
    if (s.compare(i, n, w) != 0) return false;
    i += n;
    return true;
  }

  // Turns a \uXXXX code point into UTF-8 bytes.
  static void appendUtf8(std::string& out, unsigned cp) {
    if (cp < 0x80) {
      out += static_cast<char>(cp);
    } else if (cp < 0x800) {
      out += static_cast<char>(0xC0 | (cp >> 6));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
      out += static_cast<char>(0xE0 | (cp >> 12));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    }
  }

  bool string(std::string& out) {
    if (i >= s.size() || s[i] != '"') return false;
    i++;
    while (i < s.size() && s[i] != '"') {
      char c = s[i++];
      if (c != '\\') {
        out += c;
        continue;
      }
      if (i >= s.size()) return false;
      char e = s[i++];
      if (e == 'n') out += '\n';
      else if (e == 't') out += '\t';
      else if (e == 'r') out += '\r';
      else if (e == 'b') out += '\b';
      else if (e == 'f') out += '\f';
      else if (e == 'u') {
        if (i + 4 > s.size()) return false;
        appendUtf8(out, static_cast<unsigned>(std::strtoul(s.substr(i, 4).c_str(), nullptr, 16)));
        i += 4;
      } else out += e; // \" \\ \/
    }
    if (i >= s.size()) return false;
    i++; // closing quote
    return true;
  }

  bool value(Json& out) {
    skipSpaces();
    if (i >= s.size()) return false;
    char c = s[i];
    if (c == '"') {
      out.type = Json::String;
      return string(out.text);
    }
    if (c == '{') {
      out.type = Json::Object;
      i++;
      skipSpaces();
      if (i < s.size() && s[i] == '}') return ++i, true;
      for (;;) {
        skipSpaces();
        std::string key;
        if (!string(key)) return false;
        skipSpaces();
        if (i >= s.size() || s[i++] != ':') return false;
        if (!value(out.fields[key])) return false;
        skipSpaces();
        if (i >= s.size()) return false;
        if (s[i] == ',') { i++; continue; }
        return s[i++] == '}';
      }
    }
    if (c == '[') {
      out.type = Json::Array;
      i++;
      skipSpaces();
      if (i < s.size() && s[i] == ']') return ++i, true;
      for (;;) {
        out.items.emplace_back();
        if (!value(out.items.back())) return false;
        skipSpaces();
        if (i >= s.size()) return false;
        if (s[i] == ',') { i++; continue; }
        return s[i++] == ']';
      }
    }
    if (word("true")) return out.type = Json::Bool, out.boolean = true, true;
    if (word("false")) return out.type = Json::Bool, out.boolean = false, true;
    if (word("null")) return out.type = Json::Null, true;
    const char* start = s.c_str() + i;
    char* end = nullptr;
    double n = std::strtod(start, &end);
    if (end == start) return false;
    i += static_cast<size_t>(end - start);
    out.type = Json::Number;
    out.number = n;
    return true;
  }
};

} // namespace

bool Json::parse(const std::string& input, Json& out) {
  out = Json();
  Parser p{input};
  if (!p.value(out)) return false;
  p.skipSpaces();
  return p.i == input.size();
}

const Json& Json::operator[](const std::string& key) const {
  static const Json none;
  if (type != Object) return none;
  auto it = fields.find(key);
  return it == fields.end() ? none : it->second;
}
