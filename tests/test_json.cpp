#include "nexus/utils/json.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <unistd.h>
#include <iostream>

int main() {
    using namespace nexus::utils;
    JsonValue v;
    std::string err;

    assert(parse_json(R"({"a": [1, -2.5e3, "x\n\u00e9\ud83d\ude00", true, null], "b": {"c": false}})", v, &err));
    assert(v.is_object());
    const auto* a = v.find("a");
    assert(a && a->is_array() && a->array.size() == 5);
    assert(a->array[0].string == "1" && a->array[1].string == "-2.5e3");
    assert(a->array[2].string == "x\n\xC3\xA9\xF0\x9F\x98\x80");
    assert(a->array[3].is_bool() && a->array[3].boolean);
    assert(v.find("b")->find("c")->boolean == false);
    assert(v.find("missing") == nullptr);

    assert(parse_json(" [ ] ", v) && v.is_array() && v.array.empty());
    assert(parse_json(R"({"k":1,"k":2})", v) && v.find("k")->string == "2");  // last duplicate wins

    // Malformed input is rejected and reports an offset
    for (const char* bad : {"", "{", "[,]", "[1,,]", "{,}", "{\"a\" 1}", "{'a':1}", "[1] x", "\"abc", "\"\\ud800\"", "\"\\ud800\\uzzzz\"", "\"a\\\nb\"",
                            "tru", "01x", "-", "1.", "\"a\nb\"", "{\"a\":}"}) {
        assert(!parse_json(bad, v, &err));
        assert(err.find("offset") != std::string::npos);
    }

    // Leniency for 0.3.0 files and hand edits: BOM, trailing commas, unknown escapes kept literally
    assert(parse_json("\xEF\xBB\xBF{\"a\": [1, 2,],}", v, &err) && v.find("a")->array.size() == 2);
    assert(parse_json(R"("/srv/my\dir\users\u12")", v) && v.string == R"(/srv/my\dir\users\u12)");

    // preserve_malformed_file never overwrites a different earlier backup, and reuses an identical one
    namespace fs = std::filesystem;
    const std::string base = (fs::temp_directory_path() / ("nexus_json_bak_" + std::to_string(getpid()))).string();
    assert(preserve_malformed_file(base, "first") == base + ".malformed");
    assert(preserve_malformed_file(base, "first") == base + ".malformed");
    assert(preserve_malformed_file(base, "second") == base + ".malformed.1");
    std::ifstream first(base + ".malformed");
    assert(std::string(std::istreambuf_iterator<char>(first), {}) == "first");
    fs::remove(base + ".malformed");
    fs::remove(base + ".malformed.1");

    // Nesting limit
    assert(!parse_json(std::string(200, '[') + std::string(200, ']'), v));

    // json_quote round trip, including control characters and non-ASCII
    const std::string nasty = std::string("q\"b\\s/\n\t\r\x01") + "\xC3\xA9";
    assert(parse_json(json_quote(nasty), v) && v.is_string() && v.string == nasty);

    std::cout << "All JSON tests passed successfully!\n";
    return 0;
}
