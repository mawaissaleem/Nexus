#include "nexus/core/executor.hpp"
#include "nexus/providers/plugin_protocol.hpp"
#include "nexus/utils/json.hpp"

#include <cassert>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>

using namespace nexus;
using namespace nexus::providers::plugin;
namespace fs = std::filesystem;

static fs::path root;

static void write(const fs::path& p, const std::string& s) {
    fs::create_directories(p.parent_path());
    std::ofstream(p) << s;
}

// Creates <root>/<dir_name>/{plugin.json, run.sh}; `json_body` is the manifest text.
static fs::path make_plugin(const std::string& dir_name, const std::string& json_body, bool executable = true) {
    const fs::path d = root / dir_name;
    write(d / "plugin.json", json_body);
    write(d / "run.sh", "#!/bin/sh\n");
    if (executable) fs::permissions(d / "run.sh", fs::perms::owner_exec, fs::perm_options::add);
    return d;
}

static std::string manifest(const std::string& id, const std::string& extra = "") {
    return "{\"id\":\"" + id + "\",\"name\":\"N\",\"version\":\"1\",\"api_version\":1,\"exec\":\"run.sh\"" + extra + "}";
}

static bool rejected(const fs::path& d, const std::string& reason = "") {
    std::string err;
    const bool r = !parse_manifest(d, &err).has_value();
    assert(!r || !err.empty());
    if (r && err.find(reason) == std::string::npos) { std::cerr << "wrong reason: " << err << "\n"; return false; }
    return r;
}

static void test_manifest() {
    auto d = make_plugin("good", manifest("good", ",\"prefixes\":[\"g\",\"\"],\"permissions\":[\"launch\",\"copy\"]"));
    auto m = parse_manifest(d);
    assert(m && m->id == "good" && m->prefixes.size() == 2 && m->permissions.size() == 2);
    assert(m->exec == d / "run.sh");
    assert(parse_manifest(root / "good" / "")); // trailing slash

    assert(rejected(make_plugin("other", manifest("good"))));            // id != dir name
    assert(rejected(make_plugin("Bad", manifest("Bad"))));               // uppercase
    assert(rejected(make_plugin("a.b", manifest("a.b"))));               // '.'
    assert(rejected(make_plugin("v2", "{\"id\":\"v2\",\"api_version\":2,\"exec\":\"run.sh\"}")));
    assert(rejected(make_plugin("nov", "{\"id\":\"nov\",\"exec\":\"run.sh\"}")));
    // Absolute path to the plugin's own executable: only the "must be relative" rule rejects it.
    assert(rejected(make_plugin("abs", "{\"id\":\"abs\",\"api_version\":1,\"exec\":\"" + (root / "abs" / "run.sh").string() + "\"}"), "relative"));
    {
        // sub/ exists, so sub/../run.sh resolves inside the dir: only the ".." rule rejects it.
        fs::create_directories(root / "dots" / "sub");
        assert(rejected(make_plugin("dots", "{\"id\":\"dots\",\"api_version\":1,\"exec\":\"sub/../run.sh\"}"), ".."));
    }
    assert(rejected(make_plugin("noexec", manifest("noexec"), false)));  // not executable
    {
        auto p = make_plugin("isdir", "{\"id\":\"isdir\",\"api_version\":1,\"exec\":\"sub\"}");
        fs::create_directories(p / "sub");
        fs::permissions(p / "sub", fs::perms::owner_all, fs::perm_options::add);
        assert(rejected(p));                                             // directory, not a file
    }
    {
        auto p = make_plugin("link", manifest("link"));
        fs::remove(p / "run.sh");
        fs::create_symlink("/bin/sh", p / "run.sh");
        assert(rejected(p));                                             // symlink leaves the dir
        // Sibling dir whose name shares a prefix with the plugin dir must not count as inside it.
        auto sib = make_plugin("link-evil", manifest("link-evil"));
        fs::remove(p / "run.sh");
        fs::create_symlink(sib / "run.sh", p / "run.sh");
        assert(rejected(p));
        // A symlink that stays inside is fine.
        fs::remove(p / "run.sh");
        write(p / "real.sh", "#!/bin/sh\n");
        fs::permissions(p / "real.sh", fs::perms::owner_exec, fs::perm_options::add);
        fs::create_symlink("real.sh", p / "run.sh");
        auto m = parse_manifest(p);
        assert(m && m->exec == fs::canonical(p / "run.sh") && m->exec.filename() == "real.sh");  // spawn what was checked
    }
    assert(rejected(make_plugin("sh", manifest("sh", ",\"permissions\":[\"shell\"]"))));
    assert(rejected(make_plugin("unk", manifest("unk", ",\"permissions\":[\"root\"]"))));
    assert(rejected(make_plugin("pnone", manifest("pnone", ",\"permissions\":[\"none\"]")), "permission"));
    {
        auto p = make_plugin("big", manifest("big", ",\"name\":\"" + std::string(kMaxLineBytes, 'x') + "\""));
        assert(rejected(p, "64 KiB"));
        fs::remove(p / "plugin.json");
        assert(mkfifo((p / "plugin.json").c_str(), 0600) == 0);
        assert(rejected(p, "regular file"));                             // FIFO: refused after open+fstat (D-102), not blocked on
    }
    assert(rejected(make_plugin("badjson", "{nope")));
    assert(rejected(root / "missing"));
}

static Manifest test_manifest_obj(std::vector<core::ActionKind> perms) {
    Manifest m;
    m.id = "web";
    m.permissions = std::move(perms);
    return m;
}

static std::string results_line(int id, const std::string& items) {
    return "{\"type\":\"results\",\"id\":" + std::to_string(id) + ",\"results\":[" + items + "]}";
}

static std::string item(const std::string& action, const std::string& extra = "") {
    return "{\"id\":\"r\",\"title\":\"T\",\"action\":\"" + action + "\"" + extra + "}";
}

static void test_decode() {
    using K = core::ActionKind;
    const Manifest all = test_manifest_obj({K::Launch, K::Open, K::Copy, K::Internal});

    // provider_id stamped from manifest, plugin value ignored; unknown fields ignored.
    auto d = decode_line(results_line(7, item("open", ",\"payload\":\"http://x\",\"provider_id\":\"settings\",\"zzz\":1,\"confirm\":true")), all, 7);
    assert(d.kind == Decoded::Kind::Results && d.results.size() == 1);
    assert(d.results[0].provider_id == "web" && d.results[0].action_kind == K::Open && d.results[0].execution_payload == "http://x");

    // shell, unknown and unpermitted kinds are dropped; none needs no permission.
    d = decode_line(results_line(1, item("shell", ",\"payload\":\"rm\"") + "," + item("teleport") + "," + item("none")), all, 1);
    assert(d.results.size() == 1 && d.results[0].action_kind == K::None);
    const Manifest display_only = test_manifest_obj({});
    d = decode_line(results_line(1, item("open", ",\"payload\":\"x\"") + "," + item("launch", ",\"argv\":[\"a\"]")), display_only, 1);
    assert(d.kind == Decoded::Kind::Results && d.results.empty());
    const Manifest copy_only = test_manifest_obj({K::Copy});
    d = decode_line(results_line(1, item("copy", ",\"payload\":\"x\"") + "," + item("open", ",\"payload\":\"x\"")), copy_only, 1);
    assert(d.results.size() == 1 && d.results[0].action_kind == K::Copy);

    // Stale id, over-long line, over 50 results.
    assert(decode_line(results_line(6, item("none")), all, 7).kind == Decoded::Kind::Ignored);
    assert(decode_line("{\"type\":\"results\",\"results\":[]}", all, 7).kind == Decoded::Kind::Ignored);
    const std::string pad(kMaxLineBytes, 'x');
    assert(decode_line(results_line(1, item("none", ",\"icon\":\"" + pad + "\"")), all, 1).kind == Decoded::Kind::Invalid);
    assert(decode_line("not json", all, 1).kind == Decoded::Kind::Invalid);
    std::string many;
    for (int i = 0; i < 60; ++i) many += (i ? "," : "") + item("none");
    assert(decode_line(results_line(1, many), all, 1).results.size() == kMaxResults);

    // Score: raw number text, clamped, bad ones dropped.
    auto score = [&](const std::string& s) {
        return decode_line(results_line(1, item("none", ",\"score\":" + s)), all, 1).results;
    };
    assert(score("12.5")[0].score == 12.5);
    assert(score("1e2")[0].score == 100.0);
    assert(score("99999")[0].score == 1500.0);
    assert(score("-5")[0].score == 0.0);
    assert(score("1e999").empty());                 // overflows to non-finite / unparsable
    assert(score("\"12\"").empty());                // string, not a number
    assert(score("null").empty());
    assert(decode_line(results_line(1, item("none")), all, 1).results[0].score == 0.0);  // absent -> 0

    // terminal honoured only for launch.
    d = decode_line(results_line(1, item("launch", ",\"argv\":[\"x\"],\"terminal\":true") + "," + item("open", ",\"payload\":\"p\",\"terminal\":true")), all, 1);
    assert(d.results.size() == 2 && d.results[0].terminal && !d.results[1].terminal);

    // Launch: empty argv / empty argv[0] / non-string element / missing argv dropped; payload ignored.
    d = decode_line(results_line(1, item("launch", ",\"argv\":[]") + "," + item("launch", ",\"argv\":[\"\",\"a\"]") + "," +
                                        item("launch", ",\"argv\":[\"a\",1]") + "," + item("launch", ",\"payload\":\"sh -c x\"")), all, 1);
    assert(d.results.empty());

    // Embedded NUL (would truncate at exec time) drops the result: argv element, open/copy payload.
    d = decode_line(results_line(1, item("launch", ",\"argv\":[\"a\",\"b\\u0000c\"]") + "," +
                                        item("open", ",\"payload\":\"x\\u0000y\"") + "," + item("copy", ",\"payload\":\"\\u0000\"")), all, 1);
    assert(d.kind == Decoded::Kind::Results && d.results.empty());

    // hello and unknown types.
    d = decode_line("{\"type\":\"hello\",\"api_version\":1}", all, 1);
    assert(d.kind == Decoded::Kind::Hello && d.api_version == 1);
    assert(decode_line("{\"type\":\"hello\",\"api_version\":3,\"x\":[]}", all, 1).api_version == 3);
    assert(decode_line("{\"type\":\"progress\",\"id\":1}", all, 1).kind == Decoded::Kind::Ignored);
    assert(decode_line("{\"id\":1}", all, 1).kind == Decoded::Kind::Ignored);
}

static void test_exec_round_trip() {
    const std::vector<std::vector<std::string>> argvs = {
        {"xdg-open", "https://x.org/?q=a b&c=d"},
        {"echo", "say \"hi\"", "it's", "back\\slash", "trail\\"},
        {"prog", "%f", "%%", "%U", "100%", "%"},
        {"prog", "$HOME", "${X}", "`id`", "$(id)"},
        {"p", "", "  ", "tab\there", "two\nlines", "\\n", "\\s"},
        {"a"},
    };
    for (const auto& argv : argvs) {
        const std::string line = argv_to_exec_line(argv);
        auto back = core::Executor::parse_desktop_exec(line);
        if (!back || *back != argv) { std::cerr << "round trip failed: " << line << "\n"; assert(false); }
    }
    // $ and backtick are escaped (a round trip alone can't see this: the parser tolerates them bare).
    assert(argv_to_exec_line({"a$`"}) == "\"a\\\\$\\\\`\"");
    // And through a decoded result.
    Manifest m = test_manifest_obj({core::ActionKind::Launch});
    auto d = decode_line(results_line(1, item("launch", ",\"argv\":[\"echo\",\"a b\",\"%f\",\"$HOME\"]")), m, 1);
    assert(d.results.size() == 1);
    auto back = core::Executor::parse_desktop_exec(d.results[0].execution_payload);
    assert(back && *back == (std::vector<std::string>{"echo", "a b", "%f", "$HOME"}));
}

static void test_encode() {
    assert(encode_hello() == "{\"type\":\"hello\",\"api_version\":1}\n");
    assert(encode_cancel(4) == "{\"type\":\"cancel\",\"id\":4}\n");
    assert(encode_shutdown() == "{\"type\":\"shutdown\"}\n");
    const std::string s = encode_search(9, "a\"b\n", "w", "a\"b");
    assert(s.back() == '\n' && s.find('\n') == s.size() - 1);  // one line, newline only at the end
    utils::JsonValue v;
    assert(utils::parse_json(s, v) && v.find("text")->string == "a\"b\n" && v.find("id")->string == "9");
    assert(utils::parse_json(encode_executed("r\\1"), v) && v.find("result_id")->string == "r\\1");
}

int main() {
    root = fs::temp_directory_path() / ("nexus_plugin_proto_" + std::to_string(getpid()));
    fs::remove_all(root);
    fs::create_directories(root);
    test_manifest();
    test_decode();
    test_exec_round_trip();
    test_encode();
    fs::remove_all(root);
    std::cout << "test_plugin_protocol OK\n";
    return 0;
}
