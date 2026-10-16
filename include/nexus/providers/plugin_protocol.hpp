#pragma once

#include "nexus/core/result.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace nexus::providers::plugin {

constexpr int kApiVersion = 1;
constexpr std::size_t kMaxLineBytes = 64 * 1024;
constexpr std::size_t kMaxResults = 50;
constexpr double kMaxScore = 1500.0;  // below the exact-alias score (1600): a plugin never outranks the user's own alias

struct Manifest {
    std::string id;       // equals the directory name; stamped on every result as provider_id
    std::string name;
    std::string version;
    std::filesystem::path dir;
    std::filesystem::path exec;  // absolute path inside `dir`
    std::vector<std::string> prefixes;
    std::vector<core::ActionKind> permissions;  // Launch/Open/Copy/Internal only
};

// Reads `<dir>/plugin.json` and applies every trust check on it. On failure returns nullopt
// and, if `error` is given, says why. Reserved/duplicate ids are the loader's job, not checked here.
[[nodiscard]] std::optional<Manifest> parse_manifest(const std::filesystem::path& dir, std::string* error = nullptr);

// Host -> plugin messages. Each is one JSON object terminated by '\n'.
[[nodiscard]] std::string encode_hello();
[[nodiscard]] std::string encode_search(std::int64_t id, std::string_view text, std::string_view prefix, std::string_view normalized);
[[nodiscard]] std::string encode_cancel(std::int64_t id);
[[nodiscard]] std::string encode_executed(std::string_view result_id);
[[nodiscard]] std::string encode_shutdown();

// argv -> Desktop Entry Exec line that Executor::parse_desktop_exec turns back into the same argv.
[[nodiscard]] std::string argv_to_exec_line(const std::vector<std::string>& argv);

struct Decoded {
    // Invalid: not JSON, over-long, or not an object (the host should kill the plugin).
    // Ignored: unknown message type, stale/foreign reply id, or a hello/results without usable fields.
    enum class Kind { Invalid, Ignored, Hello, Results };
    Kind kind{Kind::Invalid};
    int api_version{0};                    // Hello
    std::vector<core::SearchResult> results;  // Results (already filtered and clamped)
};

// Decodes one plugin line (without its '\n'). A `results` reply whose id != expected_id is Ignored.
[[nodiscard]] Decoded decode_line(std::string_view line, const Manifest& manifest, std::int64_t expected_id);

} // namespace nexus::providers::plugin
