#include <cassert>
#include "check.hpp"
#include <iostream>
#include "nexus/core/query.hpp"

using namespace nexus::core;

int main() {
    // Plain text: no prefix, text unchanged
    {
        Query q("hello world");
        assert(q.prefix().empty());
        CHECK_EQ(q.text(), "hello world");
    }

    // A prefix given by the caller is stripped (and trimmed); shell text keeps its case, normalized() lowercases
    {
        Query q("> LS -la", ">");
        CHECK_EQ(q.prefix(), ">");
        CHECK_EQ(q.text(), "LS -la");
        CHECK_EQ(q.normalized(), "ls -la");
        Query nospace(">ls", ">");
        CHECK_EQ(nospace.text(), "ls");
        Query c("  calc 5 + 5  ", "calc ");
        assert(c.prefix() == "calc " && c.text() == "5 + 5");
    }

    // A prefix that raw() does not start with is ignored; without one nothing is stripped
    {
        Query q("5 + 5", "calc ");
        assert(q.prefix().empty() && q.text() == "5 + 5");
        Query w("web robotics");
        assert(w.prefix().empty() && w.text() == "web robotics");
    }

    // A bare space-terminated prefix matches; leading space ignored, trailing kept for the prefix check only
    {
        Query w("web ", "web ");
        assert(w.prefix() == "web " && w.text().empty());
        Query l("   web ", "web ");
        assert(l.prefix() == "web " && l.text().empty());
        Query bare("web", "web ");
        assert(bare.prefix().empty() && bare.text() == "web");
        Query ws("   ");
        assert(ws.prefix().empty() && ws.text().empty());
        Query t("hello ");
        assert(t.text() == "hello");
    }

    // normalized() lowercases (note: it does not collapse internal multiple spaces)
    {
        Query q("  Fire   Fox  ");
        CHECK_EQ(q.normalized(), "fire   fox");
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
