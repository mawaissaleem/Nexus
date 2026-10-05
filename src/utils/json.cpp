#include "nexus/utils/json.hpp"
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace nexus::utils {

namespace {

constexpr int kMaxDepth = 64;

class Parser {
public:
    Parser(std::string_view t, std::string* err) : t_(t), err_(err) {}

    bool parse(JsonValue& out) {
        if (t_.substr(0, 3) == "\xEF\xBB\xBF") pos_ = 3;  // UTF-8 BOM some editors add
        skip_ws();
        if (!value(out, 0)) return false;
        skip_ws();
        return pos_ == t_.size() || fail("unexpected trailing characters");
    }

private:
    std::string_view t_;
    std::string* err_;
    size_t pos_{0};

    bool fail(const char* msg) {
        if (err_) *err_ = std::string(msg) + " at offset " + std::to_string(pos_);
        return false;
    }

    void skip_ws() {
        while (pos_ < t_.size() && (t_[pos_] == ' ' || t_[pos_] == '\t' || t_[pos_] == '\n' || t_[pos_] == '\r')) ++pos_;
    }

    bool literal(std::string_view word) {
        if (t_.substr(pos_, word.size()) != word) return fail("invalid literal");
        pos_ += word.size();
        return true;
    }

    bool value(JsonValue& v, int depth) {
        if (depth > kMaxDepth) return fail("nesting too deep");
        if (pos_ >= t_.size()) return fail("unexpected end of input");
        char c = t_[pos_];
        if (c == '{') return object(v, depth);
        if (c == '[') return array(v, depth);
        if (c == '"') { v.type = JsonValue::Type::String; return str(v.string); }
        if (c == 't') { v.type = JsonValue::Type::Bool; v.boolean = true; return literal("true"); }
        if (c == 'f') { v.type = JsonValue::Type::Bool; v.boolean = false; return literal("false"); }
        if (c == 'n') { v.type = JsonValue::Type::Null; return literal("null"); }
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) return number(v);
        return fail("unexpected character");
    }

    bool number(JsonValue& v) {
        size_t start = pos_;
        auto digits = [&] { size_t s = pos_; while (pos_ < t_.size() && std::isdigit(static_cast<unsigned char>(t_[pos_]))) ++pos_; return pos_ > s; };
        if (t_[pos_] == '-') ++pos_;
        if (!digits()) return fail("invalid number");
        if (pos_ < t_.size() && t_[pos_] == '.') { ++pos_; if (!digits()) return fail("invalid number"); }
        if (pos_ < t_.size() && (t_[pos_] == 'e' || t_[pos_] == 'E')) {
            ++pos_;
            if (pos_ < t_.size() && (t_[pos_] == '+' || t_[pos_] == '-')) ++pos_;
            if (!digits()) return fail("invalid number");
        }
        v.type = JsonValue::Type::Number;
        v.string = std::string(t_.substr(start, pos_ - start));
        return true;
    }

    // Reads 4 hex digits at pos_; on failure pos_ is unchanged.
    bool hex4(unsigned& cp) {
        if (pos_ + 4 > t_.size()) return false;
        cp = 0;
        for (size_t i = 0; i < 4; ++i) {
            char h = t_[pos_ + i];
            cp <<= 4;
            if (h >= '0' && h <= '9') cp |= h - '0';
            else if (h >= 'a' && h <= 'f') cp |= h - 'a' + 10;
            else if (h >= 'A' && h <= 'F') cp |= h - 'A' + 10;
            else return false;
        }
        pos_ += 4;
        return true;
    }

    static void append_utf8(std::string& s, unsigned cp) {
        if (cp < 0x80) s += static_cast<char>(cp);
        else if (cp < 0x800) { s += static_cast<char>(0xC0 | (cp >> 6)); s += static_cast<char>(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { s += static_cast<char>(0xE0 | (cp >> 12)); s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); s += static_cast<char>(0x80 | (cp & 0x3F)); }
        else { s += static_cast<char>(0xF0 | (cp >> 18)); s += static_cast<char>(0x80 | ((cp >> 12) & 0x3F)); s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); s += static_cast<char>(0x80 | (cp & 0x3F)); }
    }

    bool str(std::string& out) {
        ++pos_;  // opening quote
        out.clear();
        while (pos_ < t_.size()) {
            unsigned char c = static_cast<unsigned char>(t_[pos_++]);
            if (c == '"') return true;
            if (c < 0x20) { --pos_; return fail("control character in string"); }
            if (c != '\\') { out += static_cast<char>(c); continue; }
            if (pos_ >= t_.size()) break;
            char e = t_[pos_++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    unsigned cp;
                    if (!hex4(cp)) { out += "\\u"; break; }  // not an escape: keep literally (0.3.0 files)
                    if (cp >= 0xD800 && cp < 0xDC00) {  // high surrogate needs a low one
                        unsigned lo;
                        if (t_.substr(pos_, 2) != "\\u") return fail("unpaired surrogate");
                        pos_ += 2;
                        if (!hex4(lo)) return fail("bad \\u escape");
                        if (lo < 0xDC00 || lo > 0xDFFF) return fail("unpaired surrogate");
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                        return fail("unpaired surrogate");
                    }
                    append_utf8(out, cp);
                    break;
                }
                // Nexus 0.3.0 wrote backslashes unescaped, so "/srv/my\dir" must still load.
                default:
                    if (static_cast<unsigned char>(e) < 0x20) { --pos_; return fail("control character in string"); }
                    out += '\\';
                    out += e;
                    break;
            }
        }
        return fail("unterminated string");
    }

    bool array(JsonValue& v, int depth) {
        v.type = JsonValue::Type::Array;
        ++pos_;
        skip_ws();
        if (pos_ < t_.size() && t_[pos_] == ']') { ++pos_; return true; }
        while (true) {
            skip_ws();
            if (pos_ < t_.size() && t_[pos_] == ']' && !v.array.empty()) { ++pos_; return true; }  // trailing comma
            v.array.emplace_back();
            if (!value(v.array.back(), depth + 1)) return false;
            skip_ws();
            if (pos_ >= t_.size()) return fail("unterminated array");
            if (t_[pos_] == ',') { ++pos_; continue; }
            if (t_[pos_] == ']') { ++pos_; return true; }
            return fail("expected ',' or ']'");
        }
    }

    bool object(JsonValue& v, int depth) {
        v.type = JsonValue::Type::Object;
        ++pos_;
        skip_ws();
        if (pos_ < t_.size() && t_[pos_] == '}') { ++pos_; return true; }
        while (true) {
            skip_ws();
            if (pos_ < t_.size() && t_[pos_] == '}' && !v.object.empty()) { ++pos_; return true; }  // trailing comma
            if (pos_ >= t_.size() || t_[pos_] != '"') return fail("expected string key");
            std::string key;
            if (!str(key)) return false;
            skip_ws();
            if (pos_ >= t_.size() || t_[pos_] != ':') return fail("expected ':'");
            ++pos_;
            skip_ws();
            JsonValue member;
            if (!value(member, depth + 1)) return false;
            v.object.emplace_back(std::move(key), std::move(member));
            skip_ws();
            if (pos_ >= t_.size()) return fail("unterminated object");
            if (t_[pos_] == ',') { ++pos_; continue; }
            if (t_[pos_] == '}') { ++pos_; return true; }
            return fail("expected ',' or '}'");
        }
    }
};

} // namespace

const JsonValue* JsonValue::find(std::string_view key) const {
    for (auto it = object.rbegin(); it != object.rend(); ++it) {
        if (it->first == key) return &it->second;
    }
    return nullptr;
}

bool parse_json(std::string_view text, JsonValue& out, std::string* error) {
    out = JsonValue{};
    return Parser(text, error).parse(out);
}

std::string json_quote(std::string_view s) {
    std::string r = "\"";
    for (unsigned char c : s) {
        switch (c) {
            case '"': r += "\\\""; break;
            case '\\': r += "\\\\"; break;
            case '\n': r += "\\n"; break;
            case '\r': r += "\\r"; break;
            case '\t': r += "\\t"; break;
            default:
                if (c < 0x20) { char buf[8]; std::snprintf(buf, sizeof buf, "\\u%04x", c); r += buf; }
                else r += static_cast<char>(c);
        }
    }
    return r + '"';
}

std::string preserve_malformed_file(const std::string& path, const std::string& contents) {
    for (int n = 0; n < 100; ++n) {
        std::string candidate = path + ".malformed" + (n ? "." + std::to_string(n) : "");
        std::error_code ec;
        if (std::filesystem::exists(candidate, ec)) {
            std::ifstream in(candidate, std::ios::binary);
            if (std::string(std::istreambuf_iterator<char>(in), {}) == contents) return candidate;  // already saved
            continue;
        }
        std::ofstream out(candidate, std::ios::binary);
        if (out << contents && out.flush()) return candidate;
        return "";
    }
    return "";
}

} // namespace nexus::utils
