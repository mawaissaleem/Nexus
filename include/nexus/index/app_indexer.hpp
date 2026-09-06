#pragma once

#include "nexus/index/desktop_entry.hpp"
#include "nexus/index/database.hpp"
#include <vector>
#include <string>
#include <memory>
#include <mutex>
#include <filesystem>

namespace nexus::index {

class AppIndexer {
public:
    explicit AppIndexer(std::shared_ptr<Database> db = nullptr);

    // Rebuild index by scanning filesystem locations and storing to database
    size_t rebuild_index();

    // Load applications (from database if available and non-empty, otherwise scans)
    size_t initialize();

    // Get current indexed applications
    [[nodiscard]] std::vector<DesktopEntry> get_applications() const;

    // Standard application directory search paths
    static std::vector<std::filesystem::path> get_default_app_directories();

private:
    std::shared_ptr<Database> db_;
    std::vector<DesktopEntry> applications_;
    mutable std::mutex mutex_;
};

} // namespace nexus::index

