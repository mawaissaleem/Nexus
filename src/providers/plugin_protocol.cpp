#include "nexus/providers/plugin_protocol.hpp"
#include "nexus/utils/json.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace nexus::providers::plugin {

namespace fs = std::filesystem;
using utils::JsonValue;
using utils::json_quote;

namespace {

bool fail(std::string* error, std::string msg) {
    if (error) *error = std::move(msg);
    return false;
}

bool valid_id(std::string_view id) {
    return !id.empty() && std::all_of(id.begin(), id.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
    });
}

// Whole-text integer, or nullopt (JSON numbers are kept as raw text by the parser).
std::optional<std::int64_t> number_as_int(const JsonValue* v) {
    if (!v || v->type != JsonValue::Type::Number) return std::nullopt;
    std::int64_t n = 0;
    const auto [p, ec] = std::from_chars(v->string.data(), v->string.data() + v->string.size(), n);
    if (ec != std::errc{} || p != v->string.data() + v->string.size()) return std::nullopt;
    return n;
}

std::optional<core::ActionKind> kind_from_name(std::string_view s) {
    if (s == "launch") return core::ActionKind::Launch;
    if (s == "open") return core::ActionKind::Open;
    if (s == "copy") return core::ActionKind::Copy;
    if (s == "internal") return core::ActionKind::Internal;
    if (s == "none") return core::ActionKind::None;
    return std::nullopt;  // "shell" and anything unknown
}

// Executor passes payloads and argv to exec as C strings; an embedded NUL would silently truncate them.
bool has_nul(std::string_view s) { return s.find('\0') != std::string_view::npos; }

const std::string* string_field(const JsonValue& obj, std::string_view key) {
    const JsonValue* v = obj.find(key);
    return v && v->is_string() ? &v->string : nullptr;
}

std::optional<core::SearchResult> decode_result(const JsonValue& item, const Manifest& m) {
    if (!item.is_object()) return std::nullopt;
    const std::string* id = string_field(item, "id");
    const std::string* title = string_field(item, "title");
    if (!id || !title) return std::nullopt;

    core::SearchResult r;
    r.id = *id;
    r.title = *title;
    if (auto* s = string_field(item, "subtitle")) r.subtitle = *s;
    if (auto* s = string_field(item, "icon")) r.icon = *s;
    r.provider_id = m.id;  // never taken from the plugin

    if (const JsonValue* sc = item.find("score")) {
        double d = 0.0;
        if (sc->type != JsonValue::Type::Number) return std::nullopt;
        const char* b = sc->string.data();
        const char* e = b + sc->string.size();
        const auto [p, ec] = std::from_chars(b, e, d);
        if (ec != std::errc{} || p != e || !std::isfinite(d)) return std::nullopt;
        r.score = std::clamp(d, 0.0, kMaxScore);
    }

    core::ActionKind kind = core::ActionKind::None;
    if (const JsonValue* a = item.find("action")) {
        if (!a->is_string()) return std::nullopt;
        auto k = kind_from_name(a->string);
        if (!k) return std::nullopt;
        kind = *k;
    }
    if (kind != core::ActionKind::None &&
        std::find(m.permissions.begin(), m.permissions.end(), kind) == m.permissions.end())
        return std::nullopt;
    r.action_kind = kind;

    if (kind == core::ActionKind::Launch) {
        const JsonValue* argv = item.find("argv");
        if (!argv || !argv->is_array() || argv->array.empty()) return std::nullopt;
        std::vector<std::string> v;
        for (const JsonValue& e : argv->array) {
            if (!e.is_string() || has_nul(e.string)) return std::nullopt;
            v.push_back(e.string);
        }
        if (v.front().empty()) return std::nullopt;
        r.execution_payload = argv_to_exec_line(v);
        const JsonValue* t = item.find("terminal");
        r.terminal = t && t->is_bool() && t->boolean;
    } else if (kind != core::ActionKind::None) {
        if (auto* p = string_field(item, "payload")) {
            if (has_nul(*p)) return std::nullopt;
            r.execution_payload = *p;
        }
    }

    if (const JsonValue* md = item.find("metadata"); md && md->is_object())
        for (const auto& [k, v] : md->object)
            if (v.is_string()) r.metadata[k] = v.string;
    return r;
}

} // namespace

std::optional<Manifest> parse_manifest(const fs::path& dir, std::string* error) {
    auto bad = [&](std::string msg) -> std::optional<Manifest> { fail(error, std::move(msg)); return std::nullopt; };

    fs::path name_path = dir.filename().empty() ? dir.parent_path().filename() : dir.filename();
    // O_NONBLOCK so a FIFO swapped in for plugin.json can't block startup; fstat on the opened file (not a
    // path check first) so what we measure is what we read.
    const std::string json_path = (dir / "plugin.json").string();
    const int fd = ::open(json_path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return bad("cannot read plugin.json");
    struct stat st{};
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) { ::close(fd); return bad("plugin.json is not a regular file"); }
    if (static_cast<std::uintmax_t>(st.st_size) > kMaxLineBytes) { ::close(fd); return bad("plugin.json is larger than 64 KiB"); }
    std::string text;
    char chunk[4096];
    ssize_t n;
    while (text.size() <= kMaxLineBytes && (n = ::read(fd, chunk, sizeof chunk)) > 0) text.append(chunk, static_cast<size_t>(n));
    ::close(fd);
    if (text.size() > kMaxLineBytes) return bad("plugin.json is larger than 64 KiB");
    JsonValue root;
    std::string perr;
    if (!utils::parse_json(text, root, &perr) || !root.is_object()) return bad("plugin.json is not a JSON object: " + perr);

    Manifest m;
    m.dir = dir;
    if (auto* s = string_field(root, "id")) m.id = *s;
    if (!valid_id(m.id)) return bad("id must be non-empty and match [a-z0-9_-]");
    if (m.id != name_path.string()) return bad("id does not match the directory name");
    if (auto* s = string_field(root, "name")) m.name = *s;
    if (auto* s = string_field(root, "version")) m.version = *s;
    if (number_as_int(root.find("api_version")) != kApiVersion) return bad("unsupported api_version");

    const std::string* exec = string_field(root, "exec");
    if (!exec || exec->empty()) return bad("exec is missing");
    const fs::path rel(*exec);
    if (rel.is_absolute()) return bad("exec must be relative");
    for (const auto& part : rel)
        if (part == "..") return bad("exec must not contain ..");

    std::error_code ec;
    const fs::path canon_dir = fs::canonical(dir, ec);
    if (ec) return bad("plugin dir not found");
    const fs::path canon_exec = fs::canonical(dir / rel, ec);
    if (ec) return bad("exec not found");
    // Component-wise prefix test: "/a/plug" must not accept "/a/plugin-evil/x".
    auto [dend, eit] = std::mismatch(canon_dir.begin(), canon_dir.end(), canon_exec.begin(), canon_exec.end());
    if (dend != canon_dir.end()) return bad("exec resolves outside the plugin directory");
    if (!fs::is_regular_file(canon_exec, ec)) return bad("exec is not a regular file");
    const auto perms = fs::status(canon_exec, ec).permissions();
    if (ec || (perms & (fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec)) == fs::perms::none)
        return bad("exec is not executable");
    m.exec = canon_exec;  // spawn exactly what was checked, not a path that can be re-pointed via a symlink

    if (const JsonValue* p = root.find("prefixes"); p && p->is_array())
        for (const JsonValue& e : p->array)
            if (e.is_string()) m.prefixes.push_back(e.string);

    if (const JsonValue* p = root.find("permissions")) {
        if (!p->is_array()) return bad("permissions must be an array");
        for (const JsonValue& e : p->array) {
            auto k = e.is_string() ? kind_from_name(e.string) : std::nullopt;
            if (!k || *k == core::ActionKind::None) return bad("invalid permission (shell and unknown kinds are refused)");
            m.permissions.push_back(*k);
        }
    }
    return m;
}

std::string encode_hello() { return "{\"type\":\"hello\",\"api_version\":" + std::to_string(kApiVersion) + "}\n"; }

std::string encode_search(std::int64_t id, std::string_view text, std::string_view prefix, std::string_view normalized) {
    return "{\"type\":\"search\",\"id\":" + std::to_string(id) + ",\"text\":" + json_quote(text) +
           ",\"prefix\":" + json_quote(prefix) + ",\"normalized\":" + json_quote(normalized) + "}\n";
}

std::string encode_cancel(std::int64_t id) { return "{\"type\":\"cancel\",\"id\":" + std::to_string(id) + "}\n"; }

std::string encode_executed(std::string_view result_id) {
    return "{\"type\":\"executed\",\"result_id\":" + json_quote(result_id) + "}\n";
}

std::string encode_shutdown() { return "{\"type\":\"shutdown\"}\n"; }

std::string argv_to_exec_line(const std::vector<std::string>& argv) {
    std::string line;
    for (const std::string& a : argv) {
        if (!line.empty()) line += ' ';
        line += '"';
        for (char c : a) {
            // Quoting level escapes " ` $ \ with a backslash; the string level then doubles every backslash.
            if (c == '\\') line += "\\\\\\\\";
            else if (c == '"' || c == '`' || c == '$') { line += "\\\\"; line += c; }
            else if (c == '%') line += "%%";
            else line += c;
        }
        line += '"';
    }
    return line;
}

Decoded decode_line(std::string_view line, const Manifest& manifest, std::int64_t expected_id) {
    Decoded out;
    JsonValue root;
    if (line.size() > kMaxLineBytes || !utils::parse_json(line, root) || !root.is_object()) return out;
    out.kind = Decoded::Kind::Ignored;
    const std::string* type = string_field(root, "type");
    if (!type) return out;

    if (*type == "hello") {
        auto v = number_as_int(root.find("api_version"));
        if (!v) return out;
        out.kind = Decoded::Kind::Hello;
        out.api_version = static_cast<int>(*v);
    } else if (*type == "results") {
        const JsonValue* list = root.find("results");
        if (number_as_int(root.find("id")) != expected_id || !list || !list->is_array()) return out;
        out.kind = Decoded::Kind::Results;
        const std::size_t n = std::min(list->array.size(), kMaxResults);
        for (std::size_t i = 0; i < n; ++i)
            if (auto r = decode_result(list->array[i], manifest)) out.results.push_back(std::move(*r));
    }
    return out;
}

} // namespace nexus::providers::plugin
