#pragma once
#include <map>
#include <string>
#include <vector>

/*
 * A small JSON reader. Lichess sends every event as one line of JSON, and this turns
 * a line into a tree that the rest of the code can read with event["game"]["id"].
 * Reading a field that is missing gives an empty (null) value instead of crashing.
 */
struct Json {
  enum Type { Null, Bool, Number, String, Array, Object };
  Type type = Null;
  bool boolean = false;
  double number = 0;
  std::string text;
  std::vector<Json> items;
  std::map<std::string, Json> fields;

  /** Parses `input`. Returns false if it is not valid JSON. */
  static bool parse(const std::string& input, Json& out);

  /** Field of an object; a null Json if this is not an object or the field is missing. */
  const Json& operator[](const std::string& key) const;

  bool isNull() const { return type == Null; }
  bool has(const std::string& key) const { return type == Object && fields.count(key) > 0; }
  std::string str(const std::string& fallback = "") const { return type == String ? text : fallback; }
  double num(double fallback = 0) const { return type == Number ? number : fallback; }
  bool truthy() const { return type == Bool ? boolean : type != Null; }
};
