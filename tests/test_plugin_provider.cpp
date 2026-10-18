// PluginProvider against fake plugins: small /bin/sh scripts in a per-process temp dir.
#include "nexus/providers/plugin_provider.hpp"

#include <cassert>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <unistd.h>

using namespace nexus;
using namespace std::chrono_literals;
using providers::PluginProvider;
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

static fs::path root;
static const std::atomic<bool> no_cancel{false};

// A clock the test can push forward, so backoff and budget expiry need no real waiting.
struct FakeClock {
    std::atomic<long long> offset_ns{0};
    void advance(std::chrono::milliseconds d) { offset_ns += std::chrono::nanoseconds(d).count(); }
    Clock::time_point operator()() const { return Clock::now() + std::chrono::nanoseconds(offset_ns.load()); }
};

struct Plugin {
    fs::path dir;
    std::shared_ptr<FakeClock> clock = std::make_shared<FakeClock>();
    std::unique_ptr<PluginProvider> provider;

    int pid() const { int p = 0; std::ifstream(dir / "pid") >> p; return p; }
    int spawns() const { std::ifstream f(dir / "spawns"); int n = 0; std::string l; while (std::getline(f, l)) ++n; return n; }
    // Dead means reaped by Nexus: a zombie would still answer kill(pid, 0).
    bool dead() const { return kill(pid(), 0) != 0; }
    std::vector<core::SearchResult> search(const std::string& text, const std::atomic<bool>& cancel = no_cancel) {
        return provider->search(core::Query(text), cancel);
    }
};

// `hello` is the shell command that answers the handshake; "slow" replies after `slow_delay` seconds.
static std::unique_ptr<Plugin> make_plugin(const std::string& id, int budget_ms = 2000,
                                           std::string hello = "echo '{\"type\":\"hello\",\"api_version\":1}'",
                                           const std::string& slow_delay = "0.05") {
    auto p = std::make_unique<Plugin>();
    p->dir = root / id;
    fs::create_directories(p->dir);
    std::ofstream(p->dir / "plugin.json") << "{\"id\":\"" << id << "\",\"api_version\":1,\"exec\":\"run.sh\",\"prefixes\":[\"\"]}";
    std::ofstream(p->dir / "run.sh") << R"(#!/bin/sh
echo $$ > pid
echo s >> spawns
)" << hello << R"(
reply() { printf '{"type":"results","id":%s,"results":[{"id":"r%s","title":"T%s"}]}\n' "$id" "$id" "$id"; }
while read -r line; do
  case "$line" in
    '{"type":"executed"'*) echo "$line" >> executed.log ;;
    *'"type":"search"'*)
      id=${line#*\"id\":}; id=${id%%,*}
      case "$line" in
        *'"text":"hang"'*) ;;
        *'"text":"slow"'*) sleep )" << slow_delay << R"(; reply ;;
        *'"text":"split"'*) printf '{"type":"results","id":%s,' "$id"; sleep 0.4; echo '"results":[{"id":"late","title":"Late"}]}' ;;
        *'"text":"bad"'*) echo 'not json' ;;
        *'"text":"zero"'*) reply; echo '{"type":"results","id":0,"results":[]}' ;;
        *'"text":"flood"'*) exec yes '{"type":"x"}' ;;
        *'"text":"exit"'*) exit 0 ;;
        *) reply ;;
      esac ;;
  esac
done
)";
    fs::permissions(p->dir / "run.sh", fs::perms::owner_exec, fs::perm_options::add);
    auto manifest = providers::plugin::parse_manifest(p->dir);
    assert(manifest);
    PluginProvider::Options o;
    o.budget = std::chrono::milliseconds(budget_ms);
    o.clock = [c = p->clock] { return (*c)(); };
    p->provider = std::make_unique<PluginProvider>(*manifest, o);
    return p;
}

static void wait_zombie(int pid) {  // the pipes are closed once the process is a zombie
    for (int i = 0; i < 200; ++i) {
        std::ifstream f("/proc/" + std::to_string(pid) + "/stat");
        std::string l;
        std::getline(f, l);
        if (l.find(") Z") != std::string::npos) return;
        std::this_thread::sleep_for(5ms);
    }
    assert(false);
}

static double elapsed_ms(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

static void test_basic() {
    auto p = make_plugin("basic");
    assert(p->provider->id() == "basic" && p->provider->prefixes() == std::vector<std::string>{""});
    assert(!p->provider->on_executed({}));  // not started yet
    auto r = p->search("a");
    assert(r.size() == 1 && r[0].id == "r1" && r[0].title == "T1" && r[0].provider_id == "basic");
    r = p->search("b");
    assert(r.size() == 1 && r[0].id == "r2");
    assert(p->spawns() == 1);
}

static void test_killed_between_searches() {
    auto p = make_plugin("killed");
    assert(p->search("a").size() == 1);
    const int pid = p->pid();
    kill(pid, SIGKILL);
    wait_zombie(pid);
    assert(!p->provider->on_executed({}));  // EPIPE with SIGPIPE ignored: we are still here
    assert(p->search("b").empty());         // failure seen; backoff applies
    assert(p->search("c").empty() && p->spawns() == 1);
    p->clock->advance(2s);
    assert(p->search("d").size() == 1 && p->spawns() == 2);  // lazy respawn after the backoff
}

static void test_partial_line_and_late_reply() {
    auto p = make_plugin("late", 250);
    assert(p->search("a").size() == 1);
    assert(p->search("split").empty());  // first half of the reply arrives, the rest after the budget
    std::this_thread::sleep_for(400ms);
    auto r = p->search("b");             // the buffered halves form the late reply to #2: skipped by id
    assert(r.size() == 1 && r[0].id == "r3");
}

static void test_timeout() {
    auto p = make_plugin("hang", 250);
    assert(p->search("a").size() == 1);
    const auto t0 = Clock::now();
    assert(p->search("hang").empty());
    assert(elapsed_ms(t0) < 300);
}

// A reply nobody asked for, claiming id 0, arrives while nothing is outstanding (used to crash in drain()).
static void test_unsolicited_reply_with_id_zero() {
    auto p = make_plugin("zero");
    assert(p->search("zero").size() == 1);
    std::this_thread::sleep_for(100ms);  // the id-0 line is waiting in the pipe before the next search
    auto r = p->search("b");
    assert(r.size() == 1 && r[0].id == "r2" && p->spawns() == 1);
}

// A plugin that never stops writing lines can't hold the search past its budget: not while waiting for
// the reply, not while draining old output on the next search, not while waiting for hello.
static void test_flood_respects_budget() {
    auto p = make_plugin("flood", 250);
    assert(p->search("a").size() == 1);
    auto t0 = Clock::now();
    assert(p->search("flood").empty());
    assert(elapsed_ms(t0) < 300);
    t0 = Clock::now();
    assert(p->search("b").empty());  // drain path
    assert(elapsed_ms(t0) < 300);

    auto q = make_plugin("floodhello", 250, "exec yes '{\"type\":\"x\"}'");
    t0 = Clock::now();
    assert(q->search("a").empty());
    assert(elapsed_ms(t0) < 300 && q->dead());
}

static void test_early_cancel_is_not_a_miss() {
    auto p = make_plugin("early", 300);
    assert(p->search("a").size() == 1);
    const int pid = p->pid();
    for (int i = 0; i < 4; ++i) {  // 3 misses would kill it
        std::atomic<bool> cancel{false};
        std::thread t([&] { std::this_thread::sleep_for(5ms); cancel = true; });
        assert(p->search("slow", cancel).empty());
        t.join();
        std::this_thread::sleep_for(330ms);  // reply arrived at ~50 ms, the budget has run out since
    }
    assert(p->pid() == pid && !p->dead() && p->search("b").size() == 1 && p->spawns() == 1);
}

static void test_silent_plugin_killed_even_when_cancel_ends_the_wait() {
    auto p = make_plugin("silent", 300);
    assert(p->search("a").size() == 1);
    for (int i = 0; i < 3; ++i) {
        assert(!p->dead());
        std::atomic<bool> cancel{false};
        // The engine overran: the budget is gone on the provider's clock when the cancel arrives.
        std::thread t([&] { std::this_thread::sleep_for(20ms); p->clock->advance(300ms); cancel = true; });
        assert(p->search("hang", cancel).empty());
        t.join();
    }
    assert(p->dead());
}

static void test_invalid_json_and_exit() {
    auto p = make_plugin("bad");
    assert(p->search("a").size() == 1);
    assert(p->search("bad").empty() && p->dead());
    p->clock->advance(2s);
    assert(p->search("a").size() == 1 && !p->dead());
    assert(p->search("exit").empty() && p->dead());
}

static void test_handshake_and_search_share_one_budget() {
    // hello after 0.2 s, "slow" reply 0.2 s later: each fits a 300 ms budget, the two together don't.
    auto p = make_plugin("shared", 300, "sleep 0.2; echo '{\"type\":\"hello\",\"api_version\":1}'", "0.2");
    assert(p->search("slow").empty());
    // Spawn + handshake + search all fit: results come back on the very first search.
    auto q = make_plugin("fits", 1000, "sleep 0.1; echo '{\"type\":\"hello\",\"api_version\":1}'");
    assert(q->search("a").size() == 1 && q->spawns() == 1);
}

static void test_missing_hello_disables_after_three_restarts() {
    auto p = make_plugin("nohello", 80, "sleep 30");
    assert(p->search("a").empty() && p->spawns() == 1 && p->dead());
    assert(p->search("a").empty() && p->spawns() == 1);  // still backing off
    for (int expected = 2; expected <= 4; ++expected) {  // 3 restarts, each failing
        p->clock->advance(31s);
        assert(p->search("a").empty() && p->spawns() == expected);
    }
    p->clock->advance(31s);
    assert(p->search("a").empty() && p->spawns() == 4);  // disabled: never started again
}

static void test_api_version_mismatch_disables() {
    auto p = make_plugin("v2", 2000, "echo '{\"type\":\"hello\",\"api_version\":2}'");
    assert(p->search("a").empty() && p->dead());
    p->clock->advance(60s);
    assert(p->search("a").empty() && p->spawns() == 1);
}

static void test_on_executed_while_search_waits() {
    auto p = make_plugin("conc", 3000, "echo '{\"type\":\"hello\",\"api_version\":1}'", "0.4");
    assert(p->search("a").size() == 1);
    core::SearchResult res;
    res.id = std::string(3000, 'z');
    bool sent = false;
    double executed_ms = 1e9;
    std::thread t([&] {
        std::this_thread::sleep_for(100ms);
        const auto t1 = Clock::now();
        sent = p->provider->on_executed(res);
        executed_ms = elapsed_ms(t1);
    });
    const auto t0 = Clock::now();
    assert(p->search("slow").size() == 1);  // waits ~400 ms on the plugin while on_executed writes
    t.join();
    assert(sent && elapsed_ms(t0) < 1500);
    assert(executed_ms < 100);  // the search's wait does not hold the write lock
    std::string line;
    for (int i = 0; i < 200 && line.empty(); ++i) {
        std::ifstream(p->dir / "executed.log") >> line;
        if (line.empty()) std::this_thread::sleep_for(10ms);
    }
    assert(line == providers::plugin::encode_executed(res.id).substr(0, providers::plugin::encode_executed(res.id).size() - 1));
}

// on_executed that cannot write the whole line (pipe full) leaves the stream desynchronised: the next
// search restarts the plugin instead of appending its request to the half-written line.
static void test_failed_executed_write_restarts() {
    auto p = make_plugin("wfail", 2000, "echo '{\"type\":\"hello\",\"api_version\":1}'", "0.3");
    assert(p->search("a").size() == 1);
    const std::atomic<bool> cancelled{true};
    assert(p->search("slow", cancelled).empty());  // the plugin now sleeps 0.3 s without reading stdin
    core::SearchResult res;
    res.id = std::string(100000, 'z');             // more than a pipe holds
    assert(!p->provider->on_executed(res));
    std::this_thread::sleep_for(500ms);            // the plugin drains the pipe, waits for the line's end
    const auto t0 = Clock::now();
    assert(p->search("b").empty() && p->dead() && elapsed_ms(t0) < 1000);
    p->clock->advance(2s);
    assert(p->search("c").size() == 1 && p->spawns() == 2);
}

// Shutdown while a search waits on another thread (D-99/P4.4): the search ends cleanly, nothing is respawned,
// and the process is reaped by the time finish_shutdown returns.
static void test_shutdown_during_search() {
    auto p = make_plugin("shutwait", 300);
    assert(p->search("a").size() == 1);
    bool empty = false;
    std::thread t([&] { empty = p->search("hang").empty(); });  // never answered: waits its whole budget
    std::this_thread::sleep_for(50ms);
    const auto t0 = Clock::now();
    p->provider->request_shutdown();
    p->provider->finish_shutdown(500ms);
    assert(elapsed_ms(t0) < 600 && p->dead());
    t.join();
    assert(empty && p->search("b").empty() && p->spawns() == 1);

    // Shutdown during the hello handshake: the new process is never published and is killed.
    auto q = make_plugin("shuthello", 1000, "sleep 0.2; echo '{\"type\":\"hello\",\"api_version\":1}'");
    std::thread u([&] { empty = q->search("a").empty(); });
    std::this_thread::sleep_for(50ms);
    q->provider->request_shutdown();
    q->provider->finish_shutdown(500ms);
    u.join();
    assert(empty && q->dead() && q->search("b").empty() && q->spawns() == 1);
    q->provider->finish_shutdown(500ms);  // idempotent
}

int main() {
    root = fs::temp_directory_path() / ("nexus_plugin_prov_" + std::to_string(getpid()));
    fs::remove_all(root);
    fs::create_directories(root);

    test_basic();
    test_killed_between_searches();
    test_partial_line_and_late_reply();
    test_timeout();
    test_unsolicited_reply_with_id_zero();
    test_flood_respects_budget();
    test_early_cancel_is_not_a_miss();
    test_silent_plugin_killed_even_when_cancel_ends_the_wait();
    test_invalid_json_and_exit();
    test_handshake_and_search_share_one_budget();
    test_missing_hello_disables_after_three_restarts();
    test_api_version_mismatch_disables();
    test_on_executed_while_search_waits();
    test_failed_executed_write_restarts();
    test_shutdown_during_search();

    fs::remove_all(root);
    std::cout << "test_plugin_provider OK\n";
    return 0;
}
