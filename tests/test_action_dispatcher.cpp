#include "nexus/core/action_dispatcher.hpp"
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace nexus::core;

namespace {
struct HookProvider : ISearchProvider {
    int calls{0};
    bool outcome{true};
    std::string id() const override { return "hook"; }
    std::string name() const override { return "hook"; }
    std::vector<SearchResult> search(const Query&, const std::atomic<bool>&) override { return {}; }
    bool on_executed(const SearchResult&) override { ++calls; return outcome; }
};

SearchResult make(ActionKind kind, std::string payload, std::string provider = "x") {
    SearchResult r;
    r.action_kind = kind;
    r.execution_payload = std::move(payload);
    r.provider_id = std::move(provider);
    return r;
}
} // namespace

int main() {
    const fs::path dir = fs::temp_directory_path() / ("nexus_test_dispatch_" + std::to_string(getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);
    const fs::path out = dir / "out.txt";
    // Fake programs on PATH record their name and every argv entry as [arg]; xdg-open and the terminal
    // are faked too, so nothing real starts. Write to a temp file then mv, so the reader never sees half a line.
    for (const char* name : {"nexus-fake-launch", "xdg-open", "x-terminal-emulator"}) {
        std::ofstream(dir / name) << "#!/bin/sh\n{ printf '" << name << ":'; for a in \"$@\"; do printf '[%s]' \"$a\"; done; } > '"
                                  << out.string() << ".tmp' && mv '" << out.string() << ".tmp' '" << out.string() << "'\n";
        fs::permissions(dir / name, fs::perms::owner_all);
    }
    const char* cp = std::getenv("PATH");
    const std::string saved = cp ? cp : "";
    setenv("PATH", (dir.string() + ":" + saved).c_str(), 1);

    auto got = [&] {
        for (int i = 0; i < 250 && !fs::exists(out); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(20));
        std::string s;
        std::getline(std::ifstream(out), s);
        fs::remove(out);
        return s;
    };
    const std::vector<SearchProviderPtr> none;

    // Launch is argv, never a shell: metacharacters arrive literally.
    assert(dispatch_action(make(ActionKind::Launch, "nexus-fake-launch a|b $HOME"), none));
    assert(got() == "nexus-fake-launch:[a|b][$HOME]");
    // Terminal=true wraps the argv in the terminal emulator's -e.
    {
        SearchResult r = make(ActionKind::Launch, "nexus-fake-launch a|b");
        r.terminal = true;
        assert(dispatch_action(r, none));
        assert(got() == "x-terminal-emulator:[-e][nexus-fake-launch][a|b]");
    }
    assert(dispatch_action(make(ActionKind::Open, "/tmp/whatever"), none));
    assert(got() == "xdg-open:[/tmp/whatever]");

    // Shell goes through the shell: the pipe is interpreted, so only the right-hand side records.
    assert(dispatch_action(make(ActionKind::Shell, "true | nexus-fake-launch viashell"), none));
    assert(got() == "nexus-fake-launch:[viashell]");
    assert(!dispatch_action(make(ActionKind::Shell, ""), none));  // empty command never reaches the shell

    // Copy is a UI concern: core succeeds without spawning anything. None fails.
    assert(dispatch_action(make(ActionKind::Copy, "42"), none));
    assert(!dispatch_action(make(ActionKind::None, ""), none));
    assert(!fs::exists(out));

    // Hook: runs for the matching provider on every kind, before the action, outcome ignored unless Internal.
    auto hook = std::make_shared<HookProvider>();
    const std::vector<SearchProviderPtr> provs{hook};
    hook->outcome = false;
    assert(dispatch_action(make(ActionKind::Copy, "1", "hook"), provs) && hook->calls == 1);
    assert(dispatch_action(make(ActionKind::Copy, "1", "other"), provs) && hook->calls == 1);
    // Internal: the hook is the action; no provider or a failing hook means failure.
    assert(!dispatch_action(make(ActionKind::Internal, "", "hook"), provs) && hook->calls == 2);
    hook->outcome = true;
    assert(dispatch_action(make(ActionKind::Internal, "", "hook"), provs));
    assert(!dispatch_action(make(ActionKind::Internal, "", "missing"), provs));

    // Shell kind is only produced by the shell provider and '>' aliases (CLAUDE.md, D-71).
    {
        const fs::path src = fs::path(NEXUS_SOURCE_DIR) / "src";
        int seen = 0;
        for (const auto& e : fs::recursive_directory_iterator(src)) {
            if (!e.is_regular_file()) continue;
            std::ifstream f(e.path());
            const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            if (text.find("ActionKind::Shell") == std::string::npos) continue;
            const std::string n = e.path().filename().string();
            assert(n == "shell_provider.cpp" || n == "alias_provider.cpp" || n == "action_dispatcher.cpp");
            ++seen;
        }
        assert(seen == 3);
    }

    setenv("PATH", saved.c_str(), 1);
    fs::remove_all(dir);
    return 0;
}
