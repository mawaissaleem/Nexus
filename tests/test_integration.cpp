// The real pipeline without a GUI: temp config, DB, files and .desktop entries -> index -> search through the
// engine with every built-in provider -> ranking -> dispatch. Launchers on PATH are fakes that record their argv.
#include "check.hpp"
#include "nexus/app/services.hpp"
#include "nexus/core/action_dispatcher.hpp"
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <thread>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace nexus;

static fs::path root;

static void write_file(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream(path) << text;
}

// Fake programs record "<name>:[arg][arg]" to out.txt; write-then-rename so the reader never sees half a line.
static void install_fake_launchers(const fs::path& bin, const fs::path& out) {
    fs::create_directories(bin);
    const std::string tmp = "'" + out.string() + ".tmp'";
    const std::string dest = "'" + out.string() + "'";
    for (const char* name : {"nexus-fake-app", "xdg-open", "x-terminal-emulator"}) {
        std::ofstream script(bin / name);
        script << "#!/bin/sh\n";
        script << "{ printf '" << name << ":'; for a in \"$@\"; do printf '[%s]' \"$a\"; done; } > " << tmp << "\n";
        script << "mv " << tmp << " " << dest << "\n";
        script.close();
        fs::permissions(bin / name, fs::perms::owner_all);
    }
}

static std::string recorded(const fs::path& out) {
    for (int i = 0; i < 250 && !fs::exists(out); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    std::string line;
    std::getline(std::ifstream(out), line);
    fs::remove(out);
    return line;
}

int main() {
    root = fs::temp_directory_path() / ("nexus_test_integration_" + std::to_string(getpid()));
    fs::remove_all(root);
    setenv("HOME", (root / "home").c_str(), 1);
    setenv("XDG_CONFIG_HOME", (root / "config").c_str(), 1);
    setenv("XDG_DATA_HOME", (root / "data").c_str(), 1);
    setenv("XDG_DATA_DIRS", (root / "sys").c_str(), 1);
    const char* path_env = std::getenv("PATH");
    const std::string old_path = path_env ? path_env : "";
    const fs::path out = root / "out.txt";
    install_fake_launchers(root / "bin", out);
    setenv("PATH", ((root / "bin").string() + ":" + old_path).c_str(), 1);

    write_file(root / "data/applications/zebra.desktop",
               "[Desktop Entry]\nType=Application\nName=Zebra Editor\nExec=nexus-fake-app --zebra %f\n");
    write_file(root / "data/applications/quagga.desktop",
               "[Desktop Entry]\nType=Application\nName=Quagga Console\nExec=nexus-fake-app --quagga\nTerminal=true\n");
    write_file(root / "files/zebra_report.txt", "");

    fs::create_directories(root / "config/nexus");
    core::ConfigManager config((root / "config/nexus/config.json").string());
    for (const auto& dir : config.get_search_directories()) config.remove_search_directory(dir);
    config.add_search_directory((root / "files").string());
    core::AliasManager aliases((root / "config/nexus/aliases.json").string());
    aliases.add_alias("zed", "nexus-fake-app a|b");

    app::Services services = app::make_services(config, aliases, {});
    const std::atomic<bool> no_cancel{false};
    services.file_indexer->rebuild(app::file_index_options(config), no_cancel);
    const auto& providers = services.engine->providers();

    // Every built-in provider is registered.
    CHECK_EQ(providers.size(), static_cast<size_t>(8));

    // An app search finds the .desktop entry; its Exec runs as argv with the field code dropped.
    auto results = services.engine->search(core::Query("zebra"));
    assert(!results.empty());
    CHECK_EQ(results[0].title, std::string("Zebra Editor"));
    CHECK_EQ(results[0].action_kind == core::ActionKind::Launch, true);
    assert(core::dispatch_action(results[0], providers));
    CHECK_EQ(recorded(out), std::string("nexus-fake-app:[--zebra]"));

    // Terminal=true apps are wrapped in the terminal emulator.
    results = services.engine->search(core::Query("quagga"));
    assert(!results.empty());
    CHECK_EQ(results[0].title, std::string("Quagga Console"));
    assert(results[0].terminal);
    assert(core::dispatch_action(results[0], providers));
    CHECK_EQ(recorded(out), std::string("x-terminal-emulator:[-e][nexus-fake-app][--quagga]"));

    // `file ` mode finds the indexed file and opens it with xdg-open.
    results = services.engine->search(core::Query("file zebra_report"));
    assert(!results.empty());
    CHECK_EQ(results[0].action_kind == core::ActionKind::Open, true);
    CHECK_EQ(results[0].execution_payload, (root / "files/zebra_report.txt").string());
    assert(core::dispatch_action(results[0], providers));
    CHECK_EQ(recorded(out), "xdg-open:[" + (root / "files/zebra_report.txt").string() + "]");

    // The `>` prefix is the only way to the shell: the command runs through it, so the pipe is interpreted.
    results = services.engine->search(core::Query(">true | nexus-fake-app viashell"));
    assert(!results.empty());
    CHECK_EQ(results[0].action_kind == core::ActionKind::Shell, true);
    assert(core::dispatch_action(results[0], providers));
    CHECK_EQ(recorded(out), std::string("nexus-fake-app:[viashell]"));

    // A command alias runs directly, not through a shell: the pipe reaches the program as a plain argument.
    results = services.engine->search(core::Query("zed"));
    assert(!results.empty());
    CHECK_EQ(results[0].provider_id, std::string("alias"));
    CHECK_EQ(results[0].action_kind == core::ActionKind::Shell, false);
    assert(core::dispatch_action(results[0], providers));
    CHECK_EQ(recorded(out), std::string("nexus-fake-app:[a|b]"));

    // The calculator answers inline and nothing is launched.
    results = services.engine->search(core::Query("2+3*4"));
    assert(!results.empty());
    CHECK_EQ(results[0].provider_id, std::string("calculator"));
    CHECK_EQ(results[0].title, std::string("14"));
    assert(!fs::exists(out));

    app::shutdown_services(services);
    setenv("PATH", old_path.c_str(), 1);
    fs::remove_all(root);
}
