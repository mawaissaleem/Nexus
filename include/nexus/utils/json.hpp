#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace nexus::utils {

// Minimal JSON document model for the app's own small config files. Numbers are parsed
// (so valid files are accepted) but kept as their raw text; callers convert the ones they need.
struct JsonValue {
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type type{Type::Null};
    bool boolean{false};
    std::string string;  // String value, or raw text of a Number
    std::vector<JsonValue> array;
    std::vector<std::pair<std::string, JsonValue>> object;

    [[nodiscard]] bool is_string() const noexcept { return type == Type::String; }
    [[nodiscard]] bool is_array() const noexcept { return type == Type::Array; }
    [[nodiscard]] bool is_object() const noexcept { return type == Type::Object; }
    [[nodiscard]] bool is_bool() const noexcept { return type == Type::Bool; }

    // Object member by key (last duplicate wins), or nullptr.
    [[nodiscard]] const JsonValue* find(std::string_view key) const;
};

// Parses a complete JSON document; on failure returns false and, if `error` is given, a message with a byte offset.
// Rejects trailing garbage and nesting > 64. Lenient for Nexus 0.3.0 files and hand edits: skips a UTF-8 BOM, accepts
// trailing commas, and keeps an unknown escape (`\d`, `\u` without 4 hex digits) as literal text.
bool parse_json(std::string_view text, JsonValue& out, std::string* error = nullptr);

// Returns `s` as a quoted, escaped JSON string literal.
[[nodiscard]] std::string json_quote(std::string_view s);

// Copies `contents` (a config file that failed to parse) to the first free
// `<path>.malformed[.N]`, so a later save() cannot destroy the user's only copy.
// Reuses an existing backup with identical bytes. Returns the backup path, or "" on failure.
std::string preserve_malformed_file(const std::string& path, const std::string& contents);

}  // namespace nexus::utils
