#include "nexus/providers/file_provider.hpp"
#include "nexus/core/executor.hpp"
#include "nexus/utils/fuzzy.hpp"
#include "nexus/utils/logger.hpp"
#include <filesystem>
#include <algorithm>

namespace nexus::providers {

namespace {

std::string get_icon_for_path(const std::filesystem::path& p, bool is_dir) {
    if (is_dir) return "folder";

    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    if (ext == ".pdf") return "application-pdf";
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".svg" || ext == ".webp") return "image-x-generic";
    if (ext == ".mp4" || ext == ".mkv" || ext == ".avi" || ext == ".webm") return "video-x-generic";
    if (ext == ".mp3" || ext == ".wav" || ext == ".flac" || ext == ".ogg") return "audio-x-generic";
    if (ext == ".zip" || ext == ".tar" || ext == ".gz" || ext == ".xz" || ext == ".7z") return "package-x-generic";
    if (ext == ".cpp" || ext == ".hpp" || ext == ".c" || ext == ".h" || ext == ".py" || ext == ".rs" || ext == ".js" || ext == ".ts" || ext == ".html") return "text-x-script";

    return "text-x-generic";
}

} // namespace

FileProvider::FileProvider(std::shared_ptr<index::Database> db)
    : db_(std::move(db)) {}

std::vector<core::SearchResult> FileProvider::search(
    const core::Query& query,
    const std::atomic<bool>& cancel_token
) {
    std::vector<core::SearchResult> results;

    if (query.mode() != core::QueryMode::File || !db_) {
        return results;
    }

    const std::string term = query.text();
    if (term.size() < 2) {
        return results;
    }

    struct Scored {
        double score;
        utils::FuzzyMatchResult match;
        index::FileEntry entry;
    };
    std::vector<Scored> scored;

    for (auto& entry : db_->find_files(term)) {
        if (cancel_token.load()) return {};
        auto match = utils::FuzzyMatcher::match(term, entry.name);
        if (!match.matched) continue;

        // Search roots outrank everything; directories get a small boost over plain files.
        const double boost = entry.is_root ? 400.0 : 300.0 + (entry.is_dir ? 40.0 : 0.0);
        scored.push_back({match.score + boost, std::move(match), std::move(entry)});
    }

    const size_t top = std::min<size_t>(scored.size(), 10);
    std::partial_sort(scored.begin(), scored.begin() + static_cast<std::ptrdiff_t>(top), scored.end(),
                      [](const Scored& a, const Scored& b) {
                          return a.score != b.score ? a.score > b.score : a.entry.path < b.entry.path;
                      });

    for (size_t i = 0; i < top; ++i) {
        auto& s = scored[i];
        const std::filesystem::path path(s.entry.path);
        core::SearchResult res;
        res.id = (s.entry.is_dir ? "dir:" : "file:") + s.entry.path;
        res.title = s.entry.name;
        res.subtitle = s.entry.is_root ? s.entry.path : path.parent_path().string();
        res.icon = get_icon_for_path(path, s.entry.is_dir);
        res.provider_id = id();
        res.score = s.score;
        res.matched_indices = std::move(s.match.matched_indices);
        res.execution_payload = s.entry.path;
        res.action_kind = core::ActionKind::Open;
        results.push_back(std::move(res));
    }

    return results;
}

} // namespace nexus::providers
