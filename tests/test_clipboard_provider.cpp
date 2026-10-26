// ClipboardProvider: Qt-free in-memory history. Covers filter order, dedup, retention, clear, switch, secret skip.
#include "nexus/providers/clipboard_provider.hpp"
#include <atomic>
#include <cassert>
#include <iostream>

using namespace nexus;
using providers::ClipboardProvider;

// A bare "clip " is trimmed to "clip" by Query (D-105) and never reaches the provider, so tests list with "x":
// every entry below contains an x.
static std::vector<core::SearchResult> clip(ClipboardProvider& p, const std::string& text, bool cancel = false) {
    std::atomic<bool> c{cancel};
    return p.search(core::Query("clip " + text, "clip "), c);
}

int main() {
    // Newest first, fuzzy filter, Copy payload
    {
        ClipboardProvider p(50);
        p.add("xalpha one");
        p.add("xbeta two");
        p.add("xalpha three");
        auto all = clip(p, "x");
        assert(all.size() == 3);
        assert(all[0].execution_payload == "xalpha three" && all[2].execution_payload == "xalpha one");
        assert(all[0].score > all[1].score && all[1].score > all[2].score);
        assert(all[0].action_kind == core::ActionKind::Copy && all[0].provider_id == "clipboard");
        auto f = clip(p, "alph");
        assert(f.size() == 2 && f[0].execution_payload == "xalpha three");
        assert(clip(p, "zzzz").empty());
        assert(clip(p, "x", true).empty());  // cancelled
        // Another prefix never reaches this provider's results
        std::atomic<bool> c{false};
        assert(p.search(core::Query("alpha", ""), c).empty());
    }
    // Consecutive duplicates collapse; copying an older entry back moves it to the front, no duplicate
    {
        ClipboardProvider p(50);
        p.add("xa"); p.add("xa");
        assert(clip(p, "x").size() == 1);
        p.add("xb"); p.add("xc");
        p.add("xa");  // what the clipboard signal does after Enter on an old entry
        auto r = clip(p, "x");
        assert(r.size() == 3 && r[0].execution_payload == "xa" && r[1].execution_payload == "xc");
    }
    // Retention drops the oldest
    {
        ClipboardProvider p(2);
        p.add("xone"); p.add("xtwo"); p.add("xthree");
        auto r = clip(p, "x");
        assert(r.size() == 2 && r[0].execution_payload == "xthree" && r[1].execution_payload == "xtwo");
    }
    // clip clear: Internal result, executing empties the list; ordinary queries show no clear result
    {
        ClipboardProvider p(50);
        p.add("xclearly kept");
        assert(clip(p, "clipp").empty() || clip(p, "clipp")[0].action_kind != core::ActionKind::Internal);
        auto r = clip(p, "clear");
        assert(r.size() == 2 && r[0].action_kind == core::ActionKind::Internal && r[0].provider_id == "clipboard");
        assert(p.on_executed(r[1]));  // a Copy result leaves the history alone
        assert(clip(p, "x").size() == 1);
        assert(p.on_executed(r[0]));
        assert(clip(p, "x").empty());
    }
    // History switch off records nothing; secret-flagged and empty text are skipped
    {
        bool on = false;
        ClipboardProvider p(50, [&on] { return on; });
        p.add("xhidden");
        assert(clip(p, "x").empty());
        on = true;
        p.add("xvisible");
        p.add("xhunter2", true);
        p.add("");
        auto r = clip(p, "x");
        assert(r.size() == 1 && r[0].execution_payload == "xvisible");
        on = false;  // turning it off keeps what is already there, like usage history
        assert(clip(p, "x").size() == 1);
    }
    // Multi-line entries show on one line, full text in the payload
    {
        ClipboardProvider p(5);
        p.add("xline1\nline2");
        auto r = clip(p, "x");
        assert(r[0].title == "xline1 line2" && r[0].execution_payload == "xline1\nline2");
    }
    std::cout << "All clipboard provider tests passed\n";
    return 0;
}
