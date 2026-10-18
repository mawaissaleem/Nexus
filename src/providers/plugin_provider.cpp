#include "nexus/providers/plugin_provider.hpp"
#include "nexus/utils/logger.hpp"
#include <algorithm>

namespace nexus::providers {

namespace {
constexpr int kMaxMisses = 3;
const std::atomic<bool> kNeverCancel{false};
}

PluginProvider::PluginProvider(plugin::Manifest manifest, Options options)
    : manifest_(std::move(manifest)), opts_(std::move(options)) {}

// The budget is measured on the injected clock; waiting itself always runs on the real one.
std::chrono::steady_clock::time_point PluginProvider::real_deadline(std::chrono::steady_clock::time_point d) const {
    return std::chrono::steady_clock::now() + (d - opts_.clock());
}

// Kills the process (if any) and schedules the next start, or disables the plugin once the backoff list is used up.
void PluginProvider::fail(const char* why) {
    std::shared_ptr<core::ChildProcess> dead;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        dead = std::move(proc_);
        write_failed_ = false;
    }
    dead.reset();  // kills and reaps outside the lock
    pending_.clear();
    if (stopped_) return;  // exiting after our `shutdown` is not a failure
    misses_ = 0;
    ++failures_;
    NEXUS_LOG_WARN("Plugin '" + manifest_.id + "' failed: " + why);
    if (failures_ > opts_.backoff.size()) {
        disabled_ = true;
        NEXUS_LOG_WARN("Plugin '" + manifest_.id + "' disabled until Nexus restarts");
    } else {
        next_spawn_ = opts_.clock() + opts_.backoff[failures_ - 1];
    }
}

// Spawn, hello handshake; publishes the process only once the plugin has said hello.
bool PluginProvider::start(std::chrono::steady_clock::time_point deadline, const std::atomic<bool>& cancel) {
    if (stopped_) return false;
    auto child = std::make_shared<core::ChildProcess>(core::Executor::spawn_with_pipes({manifest_.exec.string()}, manifest_.dir));
    if (!child->valid()) { fail("cannot start"); return false; }
    if (!child->write_line(plugin::encode_hello())) { fail("cannot write hello"); return false; }
    for (;;) {
        std::string line;
        if (cancel.load() || opts_.clock() >= deadline ||  // a flood of other lines must not outlast the budget
            child->read_line(line, real_deadline(deadline), cancel, plugin::kMaxLineBytes) != core::ChildProcess::ReadStatus::Line) {
            fail("no hello in time");  // a cancel here is the engine's budget running out too
            return false;
        }
        const auto d = plugin::decode_line(line, manifest_, 0);
        if (d.kind == plugin::Decoded::Kind::Invalid) { fail("invalid reply to hello"); return false; }
        if (d.kind != plugin::Decoded::Kind::Hello) continue;
        if (d.api_version != plugin::kApiVersion) {
            fail("unsupported api_version");
            disabled_ = true;  // will not get better by restarting
            return false;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopped_) return false;  // shutdown began mid-handshake: `child` is killed on return
        proc_ = std::move(child);
        return true;
    }
}

// Matches a line against every outstanding request (newest first), so a late reply to an earlier
// request still clears its miss. Reply = the answer to `current`, stored in *out.
PluginProvider::Handled PluginProvider::handle_line(const std::string& line, std::int64_t current,
                                                    std::vector<core::SearchResult>* out) {
    // Nothing outstanding: no reply can match (not even one claiming id 0); only check the line is valid.
    if (pending_.empty()) return plugin::decode_line(line, manifest_, 0).kind == plugin::Decoded::Kind::Invalid ? Handled::Bad : Handled::Skip;
    std::vector<std::int64_t> ids;
    for (auto it = pending_.rbegin(); it != pending_.rend(); ++it) ids.push_back(it->id);
    for (const std::int64_t id : ids) {
        auto d = plugin::decode_line(line, manifest_, id);
        if (d.kind == plugin::Decoded::Kind::Invalid) return Handled::Bad;
        if (d.kind != plugin::Decoded::Kind::Results) continue;
        std::erase_if(pending_, [id](const Pending& p) { return p.id == id; });
        misses_ = 0;
        failures_ = 0;
        if (id != current) return Handled::Skip;
        *out = std::move(d.results);
        return Handled::Reply;
    }
    return Handled::Skip;
}

// Reads whatever the plugin has already sent, without waiting; a plugin that keeps talking stops it at the deadline.
void PluginProvider::drain(core::ChildProcess& proc, std::chrono::steady_clock::time_point deadline, const std::atomic<bool>& cancel) {
    for (;;) {
        if (cancel.load() || opts_.clock() >= deadline) return;
        std::string line;
        switch (proc.read_line(line, std::chrono::steady_clock::now(), kNeverCancel, plugin::kMaxLineBytes)) {
        case core::ChildProcess::ReadStatus::Line:
            if (handle_line(line, 0, nullptr) == Handled::Bad) { fail("invalid JSON"); return; }
            break;
        case core::ChildProcess::ReadStatus::Timeout: return;
        default: fail("exited or sent an over-long line"); return;
        }
    }
}

// A request with no reply `budget` after it was written is a miss, whoever ended the wait.
void PluginProvider::expire_pending() {
    const auto now = opts_.clock();
    while (!pending_.empty() && now - pending_.front().written_at >= opts_.budget) {
        pending_.pop_front();
        if (++misses_ >= kMaxMisses) { fail("did not answer 3 searches in time"); return; }
    }
}

std::vector<core::SearchResult> PluginProvider::search(const core::Query& query, const std::atomic<bool>& cancel) {
    std::unique_lock<std::mutex> busy(search_mutex_, std::try_to_lock);
    if (!busy.owns_lock() || disabled_ || stopped_) return {};
    const auto deadline = opts_.clock() + opts_.budget;

    bool write_failed;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        write_failed = write_failed_;
    }
    if (write_failed) fail("write failed");
    // request_shutdown() may take proc_ from another thread at any time: always work on a locked snapshot.
    if (const auto p = current_proc()) {
        drain(*p, deadline, cancel);
        if (current_proc()) expire_pending();
    }
    auto proc = current_proc();
    if (!proc) {
        // A search that is already cancelled must not spawn a process only to fail its handshake.
        if (cancel.load() || opts_.clock() < next_spawn_ || !start(deadline, cancel)) return {};
        proc = current_proc();
        if (!proc) return {};
    }
    const std::int64_t id = next_id_++;
    pending_.push_back({id, opts_.clock()});
    bool wrote;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        wrote = proc->write_line(plugin::encode_search(id, query.text(), query.prefix(), query.normalized()));
    }
    if (!wrote) { fail("write failed"); return {}; }

    std::vector<core::SearchResult> out;
    for (;;) {
        std::string line;
        // Checked here too: read_line returns buffered lines without looking at the clock or the token.
        const auto status = cancel.load()                ? core::ChildProcess::ReadStatus::Cancelled
                            : opts_.clock() >= deadline ? core::ChildProcess::ReadStatus::Timeout
                                                         : proc->read_line(line, real_deadline(deadline), cancel, plugin::kMaxLineBytes);
        switch (status) {
        case core::ChildProcess::ReadStatus::Line:
            switch (handle_line(line, id, &out)) {
            case Handled::Reply: return out;
            case Handled::Bad: fail("invalid JSON"); return {};
            case Handled::Skip: break;
            }
            break;
        case core::ChildProcess::ReadStatus::Cancelled: {
            bool sent;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                sent = proc->write_line(plugin::encode_cancel(id));
            }
            if (!sent) fail("write failed"); else expire_pending();
            return {};
        }
        case core::ChildProcess::ReadStatus::Timeout:
            expire_pending();
            return {};
        default:
            fail("exited or sent an over-long line");
            return {};
        }
    }
}

std::shared_ptr<core::ChildProcess> PluginProvider::current_proc() {
    std::lock_guard<std::mutex> lock(mutex_);
    return proc_;
}

void PluginProvider::request_shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    stopped_ = true;
    if (!proc_) return;
    proc_->write_line(plugin::encode_shutdown());  // a failed write just means it gets killed after the wait
    stopping_ = std::move(proc_);
}

void PluginProvider::finish_shutdown(std::chrono::milliseconds grace) {
    const auto until = std::chrono::steady_clock::now() + grace;
    // Wait out a search still using the process (it ends within its budget), so we own the last reference.
    std::lock_guard<std::mutex> busy(search_mutex_);
    std::shared_ptr<core::ChildProcess> p;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        p = std::move(stopping_);
    }
    if (p) p->terminate(std::max(std::chrono::milliseconds(0),
                                 std::chrono::duration_cast<std::chrono::milliseconds>(until - std::chrono::steady_clock::now())));
}

bool PluginProvider::on_executed(const core::SearchResult& result) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!proc_) return false;
    if (proc_->write_line(plugin::encode_executed(result.id))) return true;
    write_failed_ = true;  // a full pipe or a partial line leaves the stream unusable; the next search() restarts it
    return false;
}

} // namespace nexus::providers
