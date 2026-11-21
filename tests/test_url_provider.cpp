#include "nexus/core/search_engine.hpp"
#include "nexus/core/search_provider.hpp"
#include "nexus/providers/url_provider.hpp"
#include <cassert>
#include <ctime>
#include <memory>

using nexus::core::ActionKind;
using nexus::core::Query;
using nexus::core::SearchResult;
using nexus::providers::UrlProvider;

namespace {

// The strongest app hit a URL can meet: top app score, launched often and just now, title not starting with the input.
class HeavyAppProvider : public nexus::core::ISearchProvider {
public:
    [[nodiscard]] std::string id() const override { return "applications"; }
    [[nodiscard]] std::string name() const override { return "Applications"; }

    std::vector<SearchResult> search(const Query& /*query*/, const std::atomic<bool>& /*cancel_token*/) override {
        SearchResult app;
        app.id = "app:github-desktop";
        app.title = "GitHub Desktop";
        app.provider_id = id();
        app.score = 1000.0;
        app.usage_count = 10000;
        app.last_used = std::time(nullptr);
        app.action_kind = ActionKind::Launch;
        app.execution_payload = "github-desktop";
        return {app};
    }
};

}  // namespace

int main() {
    // Addresses that are offered, bare hosts get https://
    assert(UrlProvider::normalize("github.com/x") == "https://github.com/x");
    assert(UrlProvider::normalize("example.org") == "https://example.org");
    assert(UrlProvider::normalize("localhost.dev:8080/a?b=1#c") == "https://localhost.dev:8080/a?b=1#c");
    assert(UrlProvider::normalize("https://github.com/x") == "https://github.com/x");
    assert(UrlProvider::normalize("HTTP://Example.com") == "HTTP://Example.com");
    assert(UrlProvider::normalize("github.com/r?u=https://x") == "https://github.com/r?u=https://x");

    // localhost and IPv4 are hosts too; bare ones open as http://
    assert(UrlProvider::normalize("localhost:8080") == "http://localhost:8080");
    assert(UrlProvider::normalize("localhost") == "http://localhost");
    assert(UrlProvider::normalize("https://localhost:3000/") == "https://localhost:3000/");
    assert(UrlProvider::normalize("192.168.1.1") == "http://192.168.1.1");
    assert(UrlProvider::normalize("http://10.0.0.1:8080/x") == "http://10.0.0.1:8080/x");
    assert(!UrlProvider::normalize("1.2.3.4.5"));
    assert(!UrlProvider::normalize("256.1.1.1"));
    assert(!UrlProvider::normalize("http://256.1.1.1"));

    // Ports stop at 65535
    assert(UrlProvider::normalize("example.com:65535") == "https://example.com:65535");
    assert(!UrlProvider::normalize("example.com:65536"));
    assert(!UrlProvider::normalize("localhost:99999"));

    // Bare file names are not hosts; an explicit scheme still opens them, and real TLDs still pass
    assert(!UrlProvider::normalize("notes.txt"));
    assert(!UrlProvider::normalize("main.cpp"));
    assert(!UrlProvider::normalize("index.html"));
    assert(!UrlProvider::normalize("Report.PDF"));
    assert(UrlProvider::normalize("https://example.com/notes.txt") == "https://example.com/notes.txt");
    assert(UrlProvider::normalize("https://notes.txt") == "https://notes.txt");
    assert(UrlProvider::normalize("x.md") == "https://x.md");
    assert(UrlProvider::normalize("x.py") == "https://x.py");

    // Plain words, versions, fragments and spaces are not URLs
    assert(!UrlProvider::normalize("firefox"));
    assert(!UrlProvider::normalize("foo."));
    assert(!UrlProvider::normalize(".com"));
    assert(!UrlProvider::normalize("1.2.3"));
    assert(!UrlProvider::normalize("a.b"));
    assert(!UrlProvider::normalize("github.com x"));
    assert(!UrlProvider::normalize("me@example.com"));
    assert(!UrlProvider::normalize(""));

    // Other schemes are never opened
    assert(!UrlProvider::normalize("javascript:alert(1)"));
    assert(!UrlProvider::normalize("file:///etc/passwd"));
    assert(!UrlProvider::normalize("mailto:a@example.com"));
    assert(!UrlProvider::normalize("ftp://example.com"));
    assert(!UrlProvider::normalize("data:text/html,x.com"));
    assert(!UrlProvider::normalize("://example.com"));

    // Provider result: Open action, high score
    UrlProvider provider;
    const std::atomic<bool> no_cancel{false};
    const auto hits = provider.search(Query("github.com/x"), no_cancel);
    assert(hits.size() == 1);
    assert(hits[0].title == "Open URL");
    assert(hits[0].action_kind == ActionKind::Open);
    assert(hits[0].execution_payload == "https://github.com/x");
    assert(provider.search(Query("firefox"), no_cancel).empty());

    const std::atomic<bool> cancelled{true};
    assert(provider.search(Query("github.com/x"), cancelled).empty());

    // Once ranked, a whole-input URL beats even a heavily used app
    nexus::core::SearchEngine engine;
    engine.register_provider(std::make_shared<UrlProvider>());
    engine.register_provider(std::make_shared<HeavyAppProvider>());
    for (const char* input : {"github.com", "https://github.com/x"}) {
        const auto results = engine.search(Query(input));
        assert(results.size() == 2);
        assert(results[0].provider_id == "url");
    }

    return 0;
}
