#include "nexus/core/action_dispatcher.hpp"
#include "nexus/core/alias_manager.hpp"
#include "nexus/providers/alias_provider.hpp"
#include "nexus/core/query.hpp"
#include "nexus/core/search_engine.hpp"
#include "check.hpp"
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
    const std::vector<SearchProviderPtr> providers{std::make_shared<AliasProvider>(mgr)};
    std::atomic<bool> cancel{false};

    Query q("gh");
    auto results = provider.search(q, cancel);
    assert(!results.empty());
    assert(results[0].title == "gh");
    assert(results[0].score >= 1500.0);
    assert(results[0].icon == "web-browser");

    // Explicit mode prefixes never get alias results (">gh" must mean the shell).
    for (const auto& [raw, prefix] : {std::pair{">gh", ">"}, {"file gh", "file "}, {"calc gh", "calc "}}) {
        assert(provider.search(Query(raw, prefix), cancel).empty());
    }

    // Bare prefixes (empty text) never create or run anything
    assert(provider.search(Query("alias ", "alias "), cancel).empty() ||
           provider.search(Query("alias ", "alias "), cancel)[0].id.rfind("alias:create:", 0) != 0);

    // Inline creation, with and without leading/trailing spaces; missing target offers no create result.
    for (const char* q_str : {"alias zz /tmp", "  alias zz /tmp  "}) {
        auto r = provider.search(Query(q_str, "alias "), cancel);
        assert(r.size() == 1 && r[0].id == "alias:create:zz" && r[0].score == 2500.0);
    }
    for (const char* q_str : {"alias", "alias zz", " alias zz "}) {
        for (const auto& r : provider.search(Query(q_str, "alias "), cancel))
            assert(r.id.rfind("alias:create:", 0) != 0);
    }

    // Inline creation keeps the whole rest of the line (multi-word and '>' targets);
    for (const char* q_str : {"alias up >sudo apt update", "  alias up   >sudo apt update  "}) {
        auto r = provider.search(Query(q_str, "alias "), cancel);
        assert(r.size() == 1 && r[0].id == "alias:create:up");
        assert(r[0].title == "Save alias 'up' → >sudo apt update");
    }
    {
        auto r = provider.search(Query("alias dk docker ps -a", "alias "), cancel);
        assert(r.size() == 1 && r[0].title == "Save alias 'dk' → docker ps -a");
    }

    // Through SearchEngine the "alias " prefix is claimed by AliasProvider (no manual prefix), creation still works,
    // and "alias zz" (no target) or plain "alias" fall back to nothing / ordinary matching.
    {
        SearchEngine engine;
        engine.register_provider(providers[0]);
        auto r = engine.search(Query("alias viaengine /tmp"));
        assert(r.size() == 1 && r[0].id == "alias:create:viaengine");
        assert(dispatch_action(r[0], providers));
        assert(mgr.remove_alias("viaengine"));
        assert(engine.search(Query("alias zz")).empty());
    }

    // Executing the inline-creation result saves the alias (kind Internal, handled by AliasProvider::on_executed).
    {
        auto r = provider.search(Query("alias made /tmp", "alias "), cancel);
        assert(r.size() == 1 && r[0].action_kind == ActionKind::Internal);
        assert(dispatch_action(r[0], providers));
        bool found = false;
        for (const auto& a : mgr.get_aliases()) found |= (a.name == "made" && a.target == "/tmp");
        assert(found);
        assert(mgr.remove_alias("made"));
    }

    // Subtitle shows when an alias runs a shell (search only; the action is never invoked here).
    assert(mgr.add_alias("upd", ">sudo apt update"));
    assert(mgr.add_alias("dps", "docker ps"));
    {
        auto r = provider.search(Query("upd"), cancel);
        assert(!r.empty() && r[0].subtitle.find("shell") != std::string::npos);
        auto r2 = provider.search(Query("dps"), cancel);
        assert(!r2.empty() && r2[0].subtitle.find("shell") == std::string::npos);
        assert(!r[0].confirm && !r2[0].confirm);
    }
    // A destructive '>' target asks first, like the same command typed after '>'.
    assert(mgr.add_alias("nuke", ">rm -rf ~/build"));
    {
        auto r = provider.search(Query("nuke"), cancel);
        assert(!r.empty() && r[0].title == "nuke");
        assert(r[0].confirm && r[0].execution_payload == "rm -rf ~/build");
        assert(r[0].subtitle.find("rm -rf ~/build") != std::string::npos);  // the confirm prompt shows the subtitle
    }
    assert(mgr.remove_alias("upd") && mgr.remove_alias("dps") && mgr.remove_alias("nuke"));

    // A destructive plain command alias asks first and its prompt names the target; harmless ones do not.
    assert(mgr.add_alias("clean", "rm -rf ~/build"));
    assert(mgr.add_alias("off", "shutdown now"));
    assert(mgr.add_alias("wipe", "sudo dd if=/dev/zero of=/dev/sda"));
    assert(mgr.add_alias("dk", "docker ps"));
    assert(mgr.add_alias("ed", "code ."));
    // Exec-line rules: `\s` is a space, and `;`/`&`/`>` inside a word do not start a new command.
    assert(mgr.add_alias("esc", "rm\\s-rf x"));
    assert(mgr.add_alias("semi", "echo a;rm -rf /"));
    assert(mgr.add_alias("amp", "echo a&&reboot"));
    assert(mgr.add_alias("sp", "dd\\sif=/dev/zero"));
    {
        for (const char* name : {"semi", "amp"}) {
            auto r = provider.search(Query(name), cancel);
            assert(!r.empty() && r[0].title == name && !r[0].confirm);
        }
        // `\s` is a space in an Exec line, so these really launch rm and dd.
        for (const char* name : {"esc", "sp"}) {
            auto r = provider.search(Query(name), cancel);
            assert(!r.empty() && r[0].title == name && r[0].confirm);
        }
    }
    assert(mgr.remove_alias("esc") && mgr.remove_alias("semi") && mgr.remove_alias("amp") && mgr.remove_alias("sp"));
    {
        for (const char* name : {"clean", "off", "wipe"}) {
            auto r = provider.search(Query(name), cancel);
            assert(!r.empty() && r[0].title == name);
            assert(r[0].confirm && r[0].action_kind == ActionKind::Launch);
            assert(r[0].subtitle.find(r[0].execution_payload) != std::string::npos);
        }
        for (const char* name : {"dk", "ed"}) {
            auto r = provider.search(Query(name), cancel);
            assert(!r.empty() && r[0].title == name && !r[0].confirm);
        }
    }
    assert(mgr.remove_alias("clean") && mgr.remove_alias("off") && mgr.remove_alias("wipe"));
    assert(mgr.remove_alias("dk") && mgr.remove_alias("ed"));

    // Which executor path an alias action takes: a fake program on PATH records the argv it received.
    {
        const fs::path dir = fs::temp_directory_path() / ("nexus_test_alias_bin_" + pid);
        fs::remove_all(dir);
        fs::create_directories(dir);
        const fs::path out = dir / "args.txt";
        {
            std::ofstream f(dir / "nexus-fake-rec");
            f << "#!/bin/sh\nfor a in \"$@\"; do printf '[%s]' \"$a\"; done > '" << out.string() << ".tmp' && mv '"
              << out.string() << ".tmp' '" << out.string() << "'\n";
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
            const bool ok = dispatch_action(r[0], providers);
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
    for (const auto& a : mgr.get_aliases())
        if (a.name == "q") {
            found = true;
            assert(a.target == "echo \"hi\" \\n");
        }
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

    // No usable per-user config dir: planted files in cwd are ignored, nothing is written to cwd or /tmp.
    {
        const fs::path planted_dir = fs::temp_directory_path() / ("nexus_alias_planted_" + pid);
        const fs::path abs_home = fs::temp_directory_path() / ("nexus_alias_home_" + pid);
        fs::remove_all(planted_dir);
        fs::remove_all(abs_home);
        fs::create_directories(planted_dir);
        const fs::path saved_cwd = fs::current_path();
        fs::current_path(planted_dir);
        // The old cwd fallback, and where XDG_CONFIG_HOME=rel or HOME=. would resolve if honoured.
        const fs::path planted[] = {".nexus_aliases.json", fs::path("rel") / "nexus" / "aliases.json",
                                    fs::path(".config") / "nexus" / "aliases.json"};
        const std::string payload = "[{\"name\": \"evil\", \"target\": \"/bin/sh\"}]";
        for (const auto& p : planted) {
            if (p.has_parent_path()) fs::create_directories(p.parent_path());
            std::ofstream(p) << payload;
        }
        const fs::path home_file = planted_dir / "home_is_a_file";
        std::ofstream(home_file) << "x";
        const auto entries_before = std::distance(fs::directory_iterator(planted_dir), fs::directory_iterator{});
        const bool tmp_existed = fs::exists("/tmp/nexus");

        std::optional<std::string> saved_home;
        std::optional<std::string> saved_xdg;
        if (const char* v = std::getenv("HOME")) saved_home = v;
        if (const char* v = std::getenv("XDG_CONFIG_HOME")) saved_xdg = v;
        unsetenv("HOME");
        unsetenv("XDG_CONFIG_HOME");
        CHECK_EQ(AliasManager::resolve_aliases_path(), std::string());

        // Relative values count as unset.
        setenv("XDG_CONFIG_HOME", "rel", 1);
        CHECK_EQ(AliasManager::resolve_aliases_path(), std::string());
        setenv("HOME", ".", 1);
        CHECK_EQ(AliasManager::resolve_aliases_path(), std::string());
        // Built-in directory aliases come from HOME too: none may point at a relative path.
        const AliasManager rel_home((planted_dir / "absent.json").string());
        for (const auto& a : rel_home.get_aliases()) {
            if (a.type == AliasType::Directory) CHECK_EQ(fs::path(a.target).is_absolute(), true);
        }
        unsetenv("XDG_CONFIG_HOME");
        CHECK_EQ(AliasManager::resolve_aliases_path(), std::string());

        // A relative XDG_CONFIG_HOME falls back to an absolute HOME.
        setenv("XDG_CONFIG_HOME", "rel", 1);
        setenv("HOME", abs_home.c_str(), 1);
        CHECK_EQ(AliasManager::resolve_aliases_path(), (abs_home / ".config" / "nexus" / "aliases.json").string());
        unsetenv("XDG_CONFIG_HOME");

        // HOME that is a regular file: the config dir cannot be created.
        setenv("HOME", home_file.c_str(), 1);
        CHECK_EQ(AliasManager::resolve_aliases_path(), std::string());
        unsetenv("HOME");

        AliasManager unusable(AliasManager::resolve_aliases_path());
        for (const auto& a : unusable.get_aliases()) CHECK_EQ(a.name == "evil", false);
        CHECK_EQ(unusable.save(), false);

        // Unwritable path: save fails and does not fall back to cwd.
        AliasManager unwritable((planted_dir / "missing_dir" / "x.json").string());
        CHECK_EQ(unwritable.save(), false);

        for (const auto& p : planted) CHECK_EQ(fs::file_size(p), payload.size());
        CHECK_EQ(fs::exists("/tmp/nexus"), tmp_existed);
        CHECK_EQ(std::distance(fs::directory_iterator(planted_dir), fs::directory_iterator{}), entries_before);

        if (saved_home) setenv("HOME", saved_home->c_str(), 1);
        if (saved_xdg) setenv("XDG_CONFIG_HOME", saved_xdg->c_str(), 1);
        fs::current_path(saved_cwd);
        fs::remove_all(planted_dir);
        fs::remove_all(abs_home);
    }

    std::cout << "All Alias tests passed successfully!\n";
    return 0;
}
