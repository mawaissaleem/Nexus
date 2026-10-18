#pragma once

#include "nexus/core/executor.hpp"
#include "nexus/core/search_provider.hpp"
#include "nexus/providers/plugin_protocol.hpp"

#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace nexus::providers {

// Runs one plugin (see plugin_protocol.hpp) as a child process behind ISearchProvider. Owns everything it
// needs (D-99): safe to outlive whoever created it. The process is spawned lazily on a search and is
// killed when the provider is destroyed.
class PluginProvider : public core::ISearchProvider {
public:
    struct Options {
        std::chrono::milliseconds budget{200};  // one search, including a spawn + handshake
        // Wait after the 1st, 2nd, ... failed start; one more failure than there are entries disables the plugin.
        std::vector<std::chrono::milliseconds> backoff{std::chrono::seconds(1), std::chrono::seconds(5), std::chrono::seconds(30)};
        std::function<std::chrono::steady_clock::time_point()> clock = [] { return std::chrono::steady_clock::now(); };
    };

    explicit PluginProvider(plugin::Manifest manifest) : PluginProvider(std::move(manifest), Options{}) {}
    PluginProvider(plugin::Manifest manifest, Options options);

    [[nodiscard]] std::string id() const override { return manifest_.id; }
    [[nodiscard]] std::string name() const override { return manifest_.name.empty() ? manifest_.id : manifest_.name; }
    [[nodiscard]] std::vector<std::string> prefixes() const override { return manifest_.prefixes; }

    std::vector<core::SearchResult> search(const core::Query& query, const std::atomic<bool>& cancel_token) override;
    bool on_executed(const core::SearchResult& result) override;

    // Two-step shutdown so many plugins share one wait: request_shutdown() on all of them (sends `shutdown`,
    // detaches the process, makes later searches return nothing), then finish_shutdown(grace) on each with the
    // time left. finish_shutdown first waits for a search still running (bounded by its budget), then for a clean
    // exit until `grace` (counted from the call) is used up, then SIGKILLs and reaps. Both are idempotent.
    void request_shutdown();
    void finish_shutdown(std::chrono::milliseconds grace);

private:
    struct Pending { std::int64_t id; std::chrono::steady_clock::time_point written_at; };
    enum class Handled { Skip, Reply, Bad };

    [[nodiscard]] std::chrono::steady_clock::time_point real_deadline(std::chrono::steady_clock::time_point d) const;
    bool start(std::chrono::steady_clock::time_point deadline, const std::atomic<bool>& cancel);
    Handled handle_line(const std::string& line, std::int64_t current, std::vector<core::SearchResult>* out);
    void drain(core::ChildProcess& proc, std::chrono::steady_clock::time_point deadline, const std::atomic<bool>& cancel);
    [[nodiscard]] std::shared_ptr<core::ChildProcess> current_proc();
    void expire_pending();
    void fail(const char* why);

    const plugin::Manifest manifest_;
    const Options opts_;

    std::mutex search_mutex_;  // one search at a time; guards everything below except proc_
    std::deque<Pending> pending_;
    std::int64_t next_id_{1};
    int misses_{0};
    std::size_t failures_{0};
    bool disabled_{false};
    std::chrono::steady_clock::time_point next_spawn_{};

    std::mutex mutex_;  // guards proc_, write_failed_ and writes to proc_; never held while waiting for a reply
    std::shared_ptr<core::ChildProcess> proc_;
    bool write_failed_{false};  // on_executed could not write to proc_
    std::shared_ptr<core::ChildProcess> stopping_;  // detached by request_shutdown, reaped by finish_shutdown
    std::atomic<bool> stopped_{false};
};

} // namespace nexus::providers
