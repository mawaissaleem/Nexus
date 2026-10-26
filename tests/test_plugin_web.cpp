// The shipped web plugin (plugins/web), run for real through PluginProvider from a temp copy. No network, no browser.
#include "nexus/providers/plugin_provider.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <unistd.h>

using namespace nexus;
namespace fs = std::filesystem;

static const std::atomic<bool> no_cancel{false};

static std::vector<core::SearchResult> search(const fs::path& dir, const std::string& raw) {
    auto manifest = providers::plugin::parse_manifest(dir);
    assert(manifest);
    providers::PluginProvider provider(*manifest, {std::chrono::milliseconds(3000)});
    assert(provider.prefixes() == std::vector<std::string>{"web "});
    return provider.search(core::Query(raw, "web "), no_cancel);
}

int main() {
    const fs::path root = fs::temp_directory_path() / ("nexus_web_test_" + std::to_string(getpid()));
    const fs::path dir = root / "web";
    fs::create_directories(dir);
    for (const char* f : {"plugin.json", "web.py"})
        fs::copy_file(fs::path(NEXUS_SOURCE_DIR) / "plugins/web" / f, dir / f);

    auto r = search(dir, "web foo bar&x");  // default engine
    assert(r.size() == 1 && r[0].action_kind == core::ActionKind::Open);
    assert(r[0].execution_payload == "https://duckduckgo.com/?q=foo+bar%26x");

    assert(search(dir, "   ").empty());  // empty text (the engine trims "web " away, but the plugin must cope anyway)

    std::ofstream(dir / "config.json") << R"({"engine": "https://example.com/?q={query}"})";
    r = search(dir, "web foo bar");
    assert(r.size() == 1 && r[0].execution_payload == "https://example.com/?q=foo+bar");

    std::ofstream(dir / "config.json") << R"({"engine": "no placeholder"})";  // unusable: fall back
    r = search(dir, "web x");
    assert(r.size() == 1 && r[0].execution_payload == "https://duckduckgo.com/?q=x");

    // Driven directly: junk lines (non-object, non-JSON, a search without id) are skipped, not fatal.
    {
        auto child = core::Executor::spawn_with_pipes({(dir / "web.py").string()}, dir);
        assert(child.valid());
        const auto line = [&] {
            std::string l;
            const auto st = child.read_line(l, std::chrono::steady_clock::now() + std::chrono::seconds(5), no_cancel, providers::plugin::kMaxLineBytes);
            assert(st == core::ChildProcess::ReadStatus::Line);
            return l;
        };
        assert(line() == R"({"type": "hello", "api_version": 1})");
        for (const char* l : {R"({"type":"hello","api_version":1})", "[]", "garbage", R"({"type":"search"})",
                              R"({"type":"search","id":7,"text":"x","prefix":"web ","normalized":"x"})", R"({"type":"shutdown"})"})
            assert(child.write_line(std::string(l) + "\n"));
        assert(line().rfind(R"({"type": "results", "id": 7, "results": [{"id": "web")", 0) == 0);
        std::string rest;  // exits on shutdown: nothing more, then EOF
        assert(child.read_line(rest, std::chrono::steady_clock::now() + std::chrono::seconds(5), no_cancel, providers::plugin::kMaxLineBytes) ==
               core::ChildProcess::ReadStatus::Closed);
    }  // ~ChildProcess kills and reaps it if still running

    fs::remove_all(root);
}
