#pragma once

#include "nexus/core/search_provider.hpp"
#include <deque>
#include <functional>
#include <mutex>

namespace nexus::providers {

// Clipboard history, memory only (never written to disk). The UI feeds it through add(); `clip <text>` filters it.
class ClipboardProvider : public core::ISearchProvider {
public:
    // `retention`: entries kept (oldest dropped first). `recording_enabled`: the history switch; empty = always on.
    explicit ClipboardProvider(std::size_t retention, std::function<bool()> recording_enabled = {});

    [[nodiscard]] std::string id() const override { return "clipboard"; }
    [[nodiscard]] std::string name() const override { return "Clipboard History"; }
    [[nodiscard]] std::string description() const override { return "Recent clipboard entries"; }
    [[nodiscard]] std::vector<std::string> prefixes() const override { return {"clip "}; }
    [[nodiscard]] int priority() const override { return 70; }

    // Thread-safe. Ignored when recording is off, `secret` is set (password-manager hint) or `text` is empty.
    // An entry already in the list moves to the front, so copying one back never duplicates it.
    void add(const std::string& text, bool secret = false);

    void clear();  // thread-safe; also what "clip clear" runs

    std::vector<core::SearchResult> search(const core::Query& query, const std::atomic<bool>& cancel_token) override;
    bool on_executed(const core::SearchResult& result) override;  // "clip clear" empties the list

private:
    std::size_t retention_;
    std::function<bool()> recording_enabled_;
    mutable std::mutex mutex_;
    std::deque<std::string> entries_;  // newest first
};

} // namespace nexus::providers
