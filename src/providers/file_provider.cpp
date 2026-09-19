#include "nexus/providers/file_provider.hpp"
#include "nexus/core/config_manager.hpp"
#include "nexus/core/executor.hpp"
#include "nexus/utils/fuzzy.hpp"
#include "nexus/utils/logger.hpp"
#include <filesystem>
#include <algorithm>

namespace nexus::providers {

namespace {

bool should_ignore_entry(std::string_view name) {
    if (name.empty()) return true;
    if (name.front() == '.') return true; // hidden files/folders

    static const std::string_view ignored[] = {
        "node_modules", "build", "target", "__pycache__", "venv", ".venv",
        "CMakeFiles", "Testing", "dist", "out"
    };

    for (auto ign : ignored) {
        if (name == ign) return true;
    }
    return false;
}

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

FileProvider::FileProvider(size_t max_depth)
    : max_depth_(max_depth) {}

std::vector<core::SearchResult> FileProvider::search(
    const core::Query& query,
    const std::atomic<bool>& cancel_token
) {
    std::vector<core::SearchResult> results;

    bool is_file_mode = (query.mode() == core::QueryMode::File);
    if (!is_file_mode) {
        return results;
    }

    std::string term = is_file_mode ? query.text() : query.normalized();
    if (term.empty() || term.size() < 2) {
        return results;
    }

    auto search_dirs = core::ConfigManager::instance().get_search_directories();
    std::vector<std::pair<double, core::SearchResult>> scored_candidates;

    for (const auto& base_dir_str : search_dirs) {
        if (cancel_token.load()) return {};

        std::filesystem::path base_path(base_dir_str);
        std::error_code ec;
        if (!std::filesystem::exists(base_path, ec) || !std::filesystem::is_directory(base_path, ec)) {
            continue;
        }

        // Also check base directory name itself!
        std::string base_name = base_path.filename().string();
        if (!base_name.empty()) {
            auto match = utils::FuzzyMatcher::match(term, base_name);
            if (match.matched) {
                core::SearchResult res;
                res.id = "dir:" + base_path.string();
                res.title = base_name;
                res.subtitle = base_path.string();
                res.icon = "folder";
                res.provider_id = id();
                res.score = match.score + (is_file_mode ? 400.0 : 50.0);
                res.matched_indices = match.matched_indices;
                res.execution_payload = base_path.string();
                std::string path_str = base_path.string();
                res.action = [path_str]() {
                    return core::Executor::open_path_or_url(path_str);
                };
                scored_candidates.emplace_back(res.score, std::move(res));
            }
        }

        std::filesystem::recursive_directory_iterator it(
            base_path,
            std::filesystem::directory_options::skip_permission_denied,
            ec
        );
        std::filesystem::recursive_directory_iterator end_it;

        while (it != end_it && !ec) {
            if (cancel_token.load()) return {};

            // Check depth limit
            if (static_cast<size_t>(it.depth()) > max_depth_) {
                it.pop();
                continue;
            }

            const auto& entry = *it;
            std::string filename = entry.path().filename().string();

            if (should_ignore_entry(filename)) {
                if (entry.is_directory(ec)) {
                    it.disable_recursion_pending();
                }
                it.increment(ec);
                continue;
            }

            bool is_dir = entry.is_directory(ec);
            auto match = utils::FuzzyMatcher::match(term, filename);
            if (match.matched) {
                core::SearchResult res;
                res.id = (is_dir ? "dir:" : "file:") + entry.path().string();
                res.title = filename;
                res.subtitle = entry.path().parent_path().string();
                res.icon = get_icon_for_path(entry.path(), is_dir);
                res.provider_id = id();

                // Give directories a boost over plain files
                double dir_boost = is_dir ? 40.0 : 0.0;
                double mode_boost = is_file_mode ? 300.0 : 0.0;
                res.score = match.score + dir_boost + mode_boost;
                res.matched_indices = match.matched_indices;
                res.execution_payload = entry.path().string();

                std::string path_str = entry.path().string();
                res.action = [path_str]() {
                    return core::Executor::open_path_or_url(path_str);
                };

                scored_candidates.emplace_back(res.score, std::move(res));

                if (scored_candidates.size() >= 30) {
                    break;
                }
            }

            it.increment(ec);
        }
    }

    std::sort(scored_candidates.begin(), scored_candidates.end(), [](const auto& a, const auto& b) {
        return a.first > b.first;
    });

    for (size_t i = 0; i < std::min<size_t>(scored_candidates.size(), 10); ++i) {
        results.push_back(std::move(scored_candidates[i].second));
    }

    return results;
}

} // namespace nexus::providers
