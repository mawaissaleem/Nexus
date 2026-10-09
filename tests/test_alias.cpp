#include "nexus/core/alias_manager.hpp"
#include "nexus/providers/alias_provider.hpp"
#include "nexus/core/query.hpp"
#include <iostream>
#include <cassert>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <thread>
#include <chrono>
#include <cstdlib>
#include <unistd.h>

int main() {
    using namespace nexus::core;
    using namespace nexus::providers;

    namespace fs = std::filesystem;
    const std::string pid = std::to_string(getpid());
    fs::path temp_alias_file = fs::temp_directory_path() / ("nexus_test_aliases_" + pid + ".json");
    fs::remove(temp_alias_file);

    AliasManager mgr(temp_alias_file.string());

    // Test 1: Type detection
    assert(AliasManager::detect_type("https://github.com") == AliasType::Url);
    assert(AliasManager::detect_type("http://example.com") == AliasType::Url);
    assert(AliasManager::detect_type("/tmp") == AliasType::Directory);
    assert(AliasManager::detect_type("docker ps") == AliasType::Command);
    // Only a leading '>' means shell; pipes/&&/$VAR in a plain command stay Command (run directly).
    assert(AliasManager::detect_type(">sudo apt update") == AliasType::Shell);
    assert(AliasManager::detect_type("ls | wc -l && echo $HOME") == AliasType::Command);
    // A file alias whose file is missing is a direct command, never Shell.
    assert(AliasManager::detect_type("/nonexistent_nexus_dir/file.txt") == AliasType::Command);

    // Test 2: Add and list aliases
    assert(mgr.add_alias("gh", "https://github.com"));
    assert(mgr.add_alias("tmpdir", "/tmp"));
    assert(mgr.add_alias("ls_cmd", "ls -la"));

    auto list = mgr.get_aliases();
    assert(list.size() >= 3);

    // Test 3: Search with AliasProvider
    AliasProvider provider(mgr);
    std::atomic<bool> cancel{false};

    Query q("gh");
    auto results = provider.search(q, cancel);
    assert(!results.empty());
    assert(results[0].title == "gh");
    assert(results[0].score >= 1500.0);
    assert(results[0].icon == "web-browser");

    // Explicit mode prefixes never get alias results (">gh" must mean the shell).
    for (const char* prefixed : {">gh", "file gh", "calc gh", "web gh", "clip gh"}) {
        assert(provider.search(Query(prefixed), cancel).empty());
    }

    // Inline creation, with and without leading/trailing spaces; missing target offers no create result.
    for (const char* q_str : {"alias zz /tmp", "  alias zz /tmp  "}) {
        auto r = provider.search(Query(q_str), cancel);
        assert(r.size() == 1 && r[0].id == "alias:create:zz" && r[0].score == 2500.0);
    }
    for (const char* q_str : {"alias", "alias zz", " alias zz "}) {
        for (const auto& r : provider.search(Query(q_str), cancel)) assert(r.id.rfind("alias:create:", 0) != 0);
    }

    // Inline creation keeps the whole rest of the line (multi-word and '>' targets); stays in General mode.
    for (const char* q_str : {"alias up >sudo apt update", "  alias up   >sudo apt update  "}) {
        assert(Query(q_str).mode() == QueryMode::General);
        auto r = provider.search(Query(q_str), cancel);
        assert(r.size() == 1 && r[0].id == "alias:create:up");
        assert(r[0].title == "Save alias 'up' → >sudo apt update");
    }
    {
        auto r = provider.search(Query("alias dk docker ps -a"), cancel);
        assert(r.size() == 1 && r[0].title == "Save alias 'dk' → docker ps -a");
    }

    // Subtitle shows when an alias runs a shell (search only; the action is never invoked here).
    assert(mgr.add_alias("upd", ">sudo apt update"));
    assert(mgr.add_alias("dps", "docker ps"));
    {
        auto r = provider.search(Query("upd"), cancel);
        assert(!r.empty() && r[0].subtitle.find("shell") != std::string::npos);
        auto r2 = provider.search(Query("dps"), cancel);
        assert(!r2.empty() && r2[0].subtitle.find("shell") == std::string::npos);
    }
    assert(mgr.remove_alias("upd") && mgr.remove_alias("dps"));

    // Which executor path an alias action takes: a fake program on PATH records the argv it received.
    {
        const fs::path dir = fs::temp_directory_path() / ("nexus_test_alias_bin_" + pid);
        fs::remove_all(dir);
        fs::create_directories(dir);
        const fs::path out = dir / "args.txt";
        {
            std::ofstream f(dir / "nexus-fake-rec");
            f << "#!/bin/sh\nfor a in \"$@\"; do printf '[%s]' \"$a\"; done > '" << out.string() << "'\n";
        }
        fs::permissions(dir / "nexus-fake-rec", fs::perms::owner_all);
        const char* cp = std::getenv("PATH");
        const std::string saved_path = cp ? cp : "";
        setenv("PATH", (dir.string() + ":" + saved_path).c_str(), 1);

        auto run = [&](const std::string& name, const std::string& target) {
            fs::remove(out);
            assert(mgr.add_alias(name, target));
            auto r = provider.search(Query(name), cancel);
            assert(!r.empty() && r[0].title == name);
            const bool ok = r[0].action();
            // Children are detached; poll briefly for the recorder output.
            for (int i = 0; ok && i < 250 && !fs::exists(out); ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            std::string got;
            std::getline(std::ifstream(out), got);
            assert(mgr.remove_alias(name));
            return std::make_pair(ok, got);
        };

        // (a) plain command: no shell, so '|' and $HOME stay literal.
        auto r = run("plainrec", "nexus-fake-rec a|b $HOME");
        assert(r.first && r.second == "[a|b][$HOME]");

        // (b) '>' target: runs through the shell, which expands $HOME.
        const char* home = std::getenv("HOME");
        r = run("shellrec", ">nexus-fake-rec $HOME");
        assert(r.first && r.second == "[" + std::string(home ? home : "") + "]");

        // (c) file alias whose file is missing: fails, never reaches a shell.
        r = run("missingrec", "/nonexistent_nexus_dir/file.txt");
        assert(!r.first && !fs::exists(out));

        setenv("PATH", saved_path.c_str(), 1);
        fs::remove_all(dir);
    }

    // Test 4: Remove alias
    assert(mgr.remove_alias("ls_cmd"));
    Query q_removed("ls_cmd");
    auto res_removed = provider.search(q_removed, cancel);
    assert(res_removed.empty());

    // Test 5: quotes/backslashes round-trip; malformed file is rejected without losing aliases
    assert(mgr.add_alias("q", "echo \"hi\" \\n"));
    mgr.load(temp_alias_file.string());
    bool found = false;
    for (const auto& a : mgr.get_aliases()) if (a.name == "q") { found = true; assert(a.target == "echo \"hi\" \\n"); }
    assert(found);
    { std::ofstream(temp_alias_file) << "[{\"name\": \"x\", "; }
    assert(!mgr.load(temp_alias_file.string()));
    assert(!mgr.get_aliases().empty());

    // Test 6: the malformed file was kept aside, so the save() below cannot destroy the only copy
    assert(mgr.add_alias("after", "/tmp"));
    {
        std::ifstream in(temp_alias_file.string() + ".malformed");
        assert(std::string(std::istreambuf_iterator<char>(in), {}) == "[{\"name\": \"x\", ");
    }

    // Test 7: a 0.3.0 file with an unescaped backslash still loads
    { std::ofstream(temp_alias_file) << "[\n  {\n    \"name\": \"win\",\n    \"target\": \"/srv/my\\dir\"\n  }\n]\n"; }
    assert(mgr.load(temp_alias_file.string()));
    assert(mgr.get_aliases().size() == 1 && mgr.get_aliases()[0].target == "/srv/my\\dir");

    // Clean up
    fs::remove(temp_alias_file);
    fs::remove(temp_alias_file.string() + ".malformed");

    std::cout << "All Alias tests passed successfully!\n";
    return 0;
}
