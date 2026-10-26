#include "nexus/providers/clipboard_provider.hpp"
#include "nexus/utils/fuzzy.hpp"
#include <algorithm>

namespace nexus::providers {

namespace {
constexpr const char* kClearId = "clip:clear";

// One-line title for a possibly multi-line entry; the full text stays in the payload.
std::string one_line(const std::string& s) {
    std::string t = s.substr(0, 200);
    std::replace(t.begin(), t.end(), '\n', ' ');
    std::replace(t.begin(), t.end(), '\r', ' ');
    return t;
}
} // namespace

ClipboardProvider::ClipboardProvider(std::size_t retention, std::function<bool()> recording_enabled)
    : retention_(std::max<std::size_t>(1, retention)), recording_enabled_(std::move(recording_enabled)) {}

void ClipboardProvider::add(const std::string& text, bool secret) {
    if (secret || text.empty() || (recording_enabled_ && !recording_enabled_())) return;
    std::lock_guard<std::mutex> lock(mutex_);
    if (auto it = std::find(entries_.begin(), entries_.end(), text); it != entries_.end()) entries_.erase(it);
    entries_.push_front(text);
    if (entries_.size() > retention_) entries_.pop_back();
}

std::vector<core::SearchResult> ClipboardProvider::search(const core::Query& query,
                                                          const std::atomic<bool>& cancel_token) {
    std::vector<core::SearchResult> results;
    if (cancel_token.load() || query.prefix() != "clip ") return results;

    std::deque<std::string> snapshot;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        snapshot = entries_;
    }

    if (query.text() == "clear") {
        core::SearchResult r;
        r.id = kClearId;
        r.title = "Clear clipboard history";
        r.subtitle = std::to_string(snapshot.size()) + " entries";
        r.icon = "edit-clear";
        r.provider_id = id();
        r.score = 1000.0;
        r.action_kind = core::ActionKind::Internal;
        results.push_back(std::move(r));
    }

    for (std::size_t i = 0; i < snapshot.size(); ++i) {
        if (cancel_token.load()) return {};
        const auto m = utils::FuzzyMatcher::match(query.text(), snapshot[i]);
        if (!m.matched) continue;
        core::SearchResult r;
        r.id = "clip:" + std::to_string(i);
        r.title = one_line(snapshot[i]);
        r.subtitle = "Enter to copy";
        r.icon = "edit-paste";
        r.provider_id = id();
        r.score = 500.0 / (1.0 + static_cast<double>(i));  // newest first
        for (auto idx : m.matched_indices) if (idx < r.title.size()) r.matched_indices.push_back(idx);
        r.action_kind = core::ActionKind::Copy;
        r.execution_payload = snapshot[i];
        results.push_back(std::move(r));
    }
    return results;
}

bool ClipboardProvider::on_executed(const core::SearchResult& result) {
    if (result.id != kClearId) return true;  // Copy results need nothing here
    std::lock_guard<std::mutex> lock(mutex_);
    entries_.clear();
    return true;
}

} // namespace nexus::providers
