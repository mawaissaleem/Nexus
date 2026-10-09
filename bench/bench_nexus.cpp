// Benchmark harness (not part of ctest). Usage:
//   bench_nexus [--apps N] [--files M] [--iters I] [--usage-rows a,b,c]
// Everything runs in a per-process temp dir; HOME, XDG_* are redirected before any
// singleton reads them, so the real ~/.config/nexus and ~/.local/share/nexus are never touched.
#include "nexus/core/config_manager.hpp"
#include "nexus/core/search_engine.hpp"
#include "nexus/index/app_indexer.hpp"
#include "nexus/index/database.hpp"
#include "nexus/index/file_indexer.hpp"
#include "nexus/providers/alias_provider.hpp"
#include "nexus/providers/app_provider.hpp"
#include "nexus/providers/calc_provider.hpp"
#include "nexus/providers/file_provider.hpp"
#include "nexus/providers/shell_provider.hpp"
#include <sqlite3.h>
#include <unistd.h>
#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
using namespace nexus;

static double ms_since(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

// Nearest-rank percentile (p in (0,100]) of an unsorted sample.
static double percentile(std::vector<double> v, double p) {
    std::sort(v.begin(), v.end());
    size_t rank = static_cast<size_t>(std::ceil(p / 100.0 * static_cast<double>(v.size())));
    return v[std::max<size_t>(rank, 1) - 1];
}

static const char* kWords[] = {"fire", "code", "term", "image", "music", "video", "note", "mail", "calc",
                               "paint", "chat", "game", "draw", "view", "edit", "sheet", "slide", "pdf",
                               "map", "clock", "photo", "cloud", "sync", "mon", "disk", "net", "key", "font"};
static constexpr size_t kNumWords = sizeof(kWords) / sizeof(kWords[0]);

static void make_apps(const fs::path& dir, int n) {
    fs::create_directories(dir);
    for (int i = 0; i < n; ++i) {
        std::string name = std::string(kWords[i % kNumWords]) + (i / kNumWords ? "-" + std::to_string(i / kNumWords) : "");
        std::ofstream(dir / ("app" + std::to_string(i) + ".desktop"))
            << "[Desktop Entry]\nType=Application\nName=" << name << " Studio\nGenericName=" << kWords[(i * 7) % kNumWords]
            << "\nComment=Synthetic app " << i << "\nExec=app" << i << "\nIcon=app" << i << "\nCategories=Utility;\n";
    }
}

// top/mid/file: files sit at depth 2 below the root, within the indexer's default max depth 3.
static void make_tree(const fs::path& root, int files) {
    const int tops = 20, mids = 50;
    const int per_dir = std::max(1, files / (tops * mids));
    int made = 0;
    for (int t = 0; t < tops && made < files; ++t)
        for (int m = 0; m < mids && made < files; ++m) {
            fs::path d = root / ("proj" + std::to_string(t)) / ("mod" + std::to_string(m));
            fs::create_directories(d);
            for (int f = 0; f < per_dir && made < files; ++f, ++made)
                std::ofstream(d / (std::string(kWords[(t + m + f) % kNumWords]) + "_" + std::to_string(made) +
                                   (f % 3 == 0 ? ".txt" : f % 3 == 1 ? ".cpp" : ".pdf")));
        }
}

// Own connection + one transaction: Database::record_usage would fsync per row.
static void set_usage_rows(const std::string& db_path, int rows, int n_apps) {
    sqlite3* h = nullptr;
    int rc = sqlite3_open(db_path.c_str(), &h);
    assert(rc == SQLITE_OK);
    (void)rc;
    std::ostringstream sql;
    // Queries spread over prefixes of the benchmark's app queries, so rows actually match (P5.1).
    // query_norm is filled directly: the strings are already lower-case and trimmed.
    sql << "BEGIN; DELETE FROM usage_events; WITH RECURSIVE c(i) AS (SELECT 0 UNION ALL SELECT i+1 FROM c WHERE i<" << rows - 1
        << "), q(k, s) AS (VALUES (0,'c'),(1,'co'),(2,'cod'),(3,'code'),(4,'codium'),(5,'fire'),(6,'firefox'),"
           "(7,'term'),(8,'terminal'),(9,'pho'),(10,'photos'))"
        << " INSERT INTO usage_events(app_id, query, query_norm) SELECT 'app' || ((i*i) % " << n_apps
        << "), q.s, q.s FROM c JOIN q ON q.k = i % 11; COMMIT;";
    char* err = nullptr;
    if (rows > 0) rc = sqlite3_exec(h, sql.str().c_str(), nullptr, nullptr, &err);
    else rc = sqlite3_exec(h, "DELETE FROM usage_events;", nullptr, nullptr, &err);
    assert(rc == SQLITE_OK);
    sqlite3_free(err);
    sqlite3_close(h);
}

struct Stats { double p50, p95; };

static Stats time_queries(core::SearchEngine& eng, const std::vector<std::string>& queries, int iters, size_t& results_seen) {
    std::vector<double> samples;
    for (int i = 0; i < 20; ++i)  // warm-up
        for (const auto& q : queries) eng.search(core::Query(q));
    for (int i = 0; i < iters; ++i)
        for (const auto& q : queries) {
            core::Query query(q);
            auto t0 = Clock::now();
            auto r = eng.search(query);
            samples.push_back(ms_since(t0));
            results_seen += r.size();
        }
    return {percentile(samples, 50), percentile(samples, 95)};
}

static void row(const std::string& label, Stats s, size_t n) {
    std::cout << "| " << label << " | " << std::fixed << std::setprecision(3) << s.p50 << " | " << s.p95 << " | " << n << " |\n";
}

int main(int argc, char** argv) {
    // Self-check of the one helper that could silently skew every number.
    assert(percentile({5, 1, 3, 2, 4}, 50) == 3);
    assert(percentile({5, 1, 3, 2, 4}, 95) == 5);
    assert(percentile({7}, 95) == 7);
    std::vector<double> hundred;
    for (int i = 1; i <= 100; ++i) hundred.push_back(i);
    assert(percentile(hundred, 95) == 95 && percentile(hundred, 50) == 50);

    int n_apps = 200, n_files = 20000, iters = 200;
    std::vector<int> usage_rows = {0, 10000, 50000};
    for (int i = 1; i + 1 < argc; i += 2) {
        std::string a = argv[i];
        if (a == "--apps") n_apps = std::atoi(argv[i + 1]);
        else if (a == "--files") n_files = std::atoi(argv[i + 1]);
        else if (a == "--iters") iters = std::atoi(argv[i + 1]);
        else if (a == "--usage-rows") {
            usage_rows.clear();
            std::stringstream ss(argv[i + 1]);
            for (std::string t; std::getline(ss, t, ',');) usage_rows.push_back(std::atoi(t.c_str()));
        } else { std::cerr << "unknown arg " << a << "\n"; return 2; }
    }
    if (n_apps < 1 || n_files < 1 || iters < 1 || usage_rows.empty()) { std::cerr << "bad arguments\n"; return 2; }

    const fs::path root = fs::temp_directory_path() / ("nexus_bench_" + std::to_string(getpid()));
    fs::remove_all(root);
    fs::create_directories(root / "home");
    // Must precede resolve_config_path()/resolve_aliases_path() and the first DB open (they read env).
    setenv("HOME", (root / "home").c_str(), 1);
    setenv("XDG_CONFIG_HOME", (root / "config").c_str(), 1);
    setenv("XDG_DATA_HOME", (root / "data").c_str(), 1);
    setenv("XDG_DATA_DIRS", (root / "sys").c_str(), 1);

    make_apps(root / "data" / "applications", n_apps);
    const fs::path tree = root / "tree";
    make_tree(tree, n_files);
    core::ConfigManager cfg(core::ConfigManager::resolve_config_path());
    core::AliasManager aliases(core::AliasManager::resolve_aliases_path());
    for (const auto& d : cfg.get_search_directories()) cfg.remove_search_directory(d);
    cfg.add_search_directory(tree.string());

    const std::string db_path = (root / "nexus.db").string();
    std::cout << "bench_nexus: apps=" << n_apps << " files=" << n_files << " iters=" << iters << "\n"
              << "(snap/flatpak dirs are hardcoded in AppIndexer and may add host apps; see indexed count below)\n\n";

    // Startup (core part of main(), no Qt): Database open + AppIndexer::initialize + provider registration.
    auto build_engine = [&](std::shared_ptr<index::Database>& db, std::shared_ptr<index::AppIndexer>& idx) {
        db = std::make_shared<index::Database>(db_path);
        db->open();
        idx = std::make_shared<index::AppIndexer>(db);
        idx->initialize();
        auto eng = std::make_shared<core::SearchEngine>();
        eng->register_provider(std::make_shared<providers::AliasProvider>(aliases));
        eng->register_provider(std::make_shared<providers::ApplicationProvider>(idx, db));
        eng->register_provider(std::make_shared<providers::FileProvider>(db));
        eng->register_provider(std::make_shared<providers::CalculatorProvider>());
        eng->register_provider(std::make_shared<providers::ShellProvider>());
        return eng;
    };
    std::shared_ptr<index::Database> db;
    std::shared_ptr<index::AppIndexer> idx;
    auto t0 = Clock::now();
    auto eng = build_engine(db, idx);  // empty DB: scans .desktop files
    const double startup_cold = ms_since(t0);
    const size_t indexed = idx->get_applications()->size();
    eng.reset(); idx.reset(); db.reset();
    t0 = Clock::now();
    eng = build_engine(db, idx);       // populated DB: loads rows
    const double startup_warm = ms_since(t0);

    // Index build: rebuild_index (scan + parse + save to DB), median of 10.
    std::vector<double> builds;
    for (int i = 0; i < 10; ++i) { t0 = Clock::now(); idx->rebuild_index(); builds.push_back(ms_since(t0)); }

    std::cout << "indexed apps: " << indexed << "\n\n| Metric | ms |\n|---|---|\n"
              << std::fixed << std::setprecision(2)
              << "| startup, first run (scan " << indexed << " apps) | " << startup_cold << " |\n"
              << "| startup, warm (load from DB) | " << startup_warm << " |\n"
              << "| index build p50 (10 runs) | " << percentile(builds, 50) << " |\n"
              << "| index build max | " << *std::max_element(builds.begin(), builds.end()) << " |\n\n";

    // File index build (walk + one-transaction swap), outside every query timing. Median of 5.
    index::FileIndexer file_indexer(db);
    const index::FileIndexOptions file_opts{cfg.get_search_directories(), cfg.get_file_exclusions()};
    const std::atomic<bool> no_cancel{false};
    std::vector<double> file_builds;
    size_t file_rows = 0;
    for (int i = 0; i < 5; ++i) { t0 = Clock::now(); file_rows = file_indexer.rebuild(file_opts, no_cancel); file_builds.push_back(ms_since(t0)); }
    assert(file_rows >= static_cast<size_t>(n_files));
    std::cout << "| Metric | ms |\n|---|---|\n| file index build p50 (5 runs, " << file_rows << " entries) | "
              << percentile(file_builds, 50) << " |\n| file index build max | "
              << *std::max_element(file_builds.begin(), file_builds.end()) << " |\n\n";

    std::cout << "| Query set | p50 ms | p95 ms | samples |\n|---|---|---|---|\n";
    size_t seen = 0;
    const std::vector<std::string> app_q = {"fire", "code", "term", "pho", "xyzzy"};
    // Same four queries as the pre-index baseline (then: "fire_"/"pdf" stopped after 30 hits, the others walked the tree).
    const std::vector<std::string> file_q = {"file fire_", "file pdf", "file report", "file zzqx"};
    for (int rows : usage_rows) {
        set_usage_rows(db_path, rows, n_apps);
        auto s = time_queries(*eng, app_q, iters, seen);
        row("app query, usage_events=" + std::to_string(rows), s, static_cast<size_t>(iters) * app_q.size());
    }
    set_usage_rows(db_path, 0, n_apps);
    const int file_iters = std::max(10, iters / 10);
    for (const auto& q : file_q)
        row("`" + q + "`, " + std::to_string(n_files) + " files", time_queries(*eng, {q}, file_iters, seen), static_cast<size_t>(file_iters));
    std::cout << "\n(total results returned: " << seen << ", guards against empty-result measurements)\n";
    assert(seen > 0);  // note: "xyzzy"/"zzqx" legitimately return nothing

    fs::remove_all(root);
    return 0;
}
