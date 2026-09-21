#include <cassert>
#include <iostream>
#include "nexus/core/query.hpp"

using namespace nexus::core;

int main() {
    // Plain text -> General mode, text unchanged
    {
        Query q("hello world");
        assert(q.mode() == QueryMode::General);
        assert(q.text() == "hello world");
    }

    // Shell prefix: leading '>' should be stripped and mode Shell
    {
        Query q("> ls -la");
        assert(q.mode() == QueryMode::Shell);
        assert(q.text() == "ls -la");
    }

    // Explicit calc prefix
    {
        Query q("calc 5 + 5");
        assert(q.mode() == QueryMode::Calculator);
    }

    // Implicit calculator detection: current implementation does NOT
    // detect bare arithmetic as Calculator, so this should be General.
    {
        Query q("5 + 5");
        assert(q.mode() == QueryMode::General);
    }

    // Negative arithmetic-like case: contains number but should be General
    {
        Query q("iphone 15");
        assert(q.mode() == QueryMode::General);
    }

    // File prefix
    {
        Query q("file resume.pdf");
        assert(q.mode() == QueryMode::File);
        assert(q.text() == "resume.pdf");
    }

    // Web prefix
    {
        Query q("web robotics");
        assert(q.mode() == QueryMode::Web);
        assert(q.text() == "robotics");
    }

    // Clipboard prefix (must include following text to match current parsing)
    {
        Query q("clip some-clipboard-text");
        assert(q.mode() == QueryMode::Clipboard);
        assert(q.text() == "some-clipboard-text");
    }

    // normalized() lowercases (note: it does not collapse internal multiple spaces)
    {
        Query q("  Fire   Fox  ");
        assert(q.normalized() == "fire   fox");
    }

    // tokens() splits normalized into words
    {
        Query q("visual studio code");
        auto t = q.tokens();
        assert(t.size() == 3);
        assert(t[0] == "visual" && t[1] == "studio" && t[2] == "code");
    }

    // empty()
    {
        Query q("");
        assert(q.empty());
        Query q2("not empty");
        assert(!q2.empty());
    }

    std::cout << "All Query tests passed successfully!\n";
    return 0;
}
