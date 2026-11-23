#include "nexus/providers/url_provider.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <string>

namespace nexus::providers {

namespace {

bool is_alpha(char c) { return std::isalpha(static_cast<unsigned char>(c)) != 0; }

bool is_alnum(char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0; }

bool is_digit(char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; }

bool is_blank(char c) {
    const auto u = static_cast<unsigned char>(c);
    return std::isspace(u) != 0 || std::iscntrl(u) != 0;
}

std::string to_lower(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
    return out;
}

// digits only, at most `max`
bool is_number_up_to(std::string_view text, int max) {
    if (text.empty() || text.size() > 5 || !std::all_of(text.begin(), text.end(), is_digit)) {
        return false;
    }
    return std::stoi(std::string(text)) <= max;
}

bool is_port(std::string_view port) { return is_number_up_to(port, 65535); }

// four dot-separated parts, each 0-255; three parts (a version number like 1.2.3) don't qualify
bool is_ipv4(std::string_view host) {
    int parts = 0;
    std::size_t start = 0;
    while (true) {
        const std::size_t dot = host.find('.', start);
        const std::string_view part = host.substr(start, dot - start);
        // 010 reads as octal in some tools, so a leading zero is not a plain address
        const bool leading_zero = part.size() > 1 && part.front() == '0';
        if (leading_zero || !is_number_up_to(part, 255)) {
            return false;
        }
        ++parts;
        if (dot == std::string_view::npos) {
            return parts == 4;
        }
        start = dot + 1;
    }
}

// local dev servers and routers rarely have TLS, so these open as http://
bool is_local_host(std::string_view host) { return to_lower(host) == "localhost" || is_ipv4(host); }

// dotted labels of letters, digits and inner hyphens, ending in a letters-only label of at least two characters
bool is_domain(std::string_view host) {
    std::size_t label_start = 0;
    bool has_dot = false;
    while (true) {
        const std::size_t dot = host.find('.', label_start);
        const std::string_view label = host.substr(label_start, dot - label_start);
        if (label.empty() || label.front() == '-' || label.back() == '-') {
            return false;
        }
        if (!std::all_of(label.begin(), label.end(), [](char c) { return is_alnum(c) || c == '-'; })) {
            return false;
        }
        if (dot == std::string_view::npos) {
            return has_dot && label.size() >= 2 && std::all_of(label.begin(), label.end(), is_alpha);
        }
        has_dot = true;
        label_start = dot + 1;
    }
}

// a bare notes.txt or main.cpp is a file name, not a host; extensions that are real TLDs (.md, .py) still pass
bool ends_in_file_extension(std::string_view host) {
    static constexpr std::array<std::string_view, 28> extensions = {
        "txt", "cpp", "hpp",  "h",   "c",   "json", "pdf",  "png", "jpg", "jpeg", "gif", "svg", "html", "htm",
        "log", "ini", "conf", "cfg", "yml", "yaml", "toml", "csv", "tar", "gz",   "xml", "odt", "docx", "xlsx",
    };
    const std::string tld = to_lower(host.substr(host.rfind('.') + 1));
    return std::find(extensions.begin(), extensions.end(), tld) != extensions.end();
}

}  // namespace

std::optional<std::string> UrlProvider::normalize(std::string_view input) {
    const bool has_blank = std::any_of(input.begin(), input.end(), is_blank);
    if (input.empty() || has_blank) {
        return std::nullopt;
    }

    // a "://" after the path starts belongs to the path (github.com/r?u=https://x), not to a scheme
    const std::size_t scheme_end = input.find("://");
    const bool has_scheme = scheme_end < input.find_first_of("/?#");
    std::string_view rest = input;
    if (has_scheme) {
        const std::string scheme = to_lower(input.substr(0, scheme_end));
        if (scheme != "http" && scheme != "https") {
            return std::nullopt;  // file://, ftp://, ...
        }
        rest.remove_prefix(scheme_end + 3);
    }

    std::string_view host = rest.substr(0, rest.find_first_of("/?#"));
    const std::size_t colon = host.rfind(':');
    if (colon != std::string_view::npos) {
        if (!is_port(host.substr(colon + 1))) {
            return std::nullopt;  // also rejects javascript:, mailto: and similar
        }
        host = host.substr(0, colon);
    }

    const bool is_local = is_local_host(host);
    if (!is_local && !is_domain(host)) {
        return std::nullopt;
    }
    if (has_scheme) {
        return std::string(input);
    }
    if (!is_local && ends_in_file_extension(host)) {
        return std::nullopt;
    }
    const std::string default_scheme = is_local ? "http://" : "https://";
    return default_scheme + std::string(input);
}

std::vector<core::SearchResult> UrlProvider::search(const core::Query& query, const std::atomic<bool>& cancel_token) {
    if (cancel_token.load() || !query.prefix().empty()) {
        return {};
    }

    const auto url = normalize(query.text());
    if (!url) {
        return {};
    }

    core::SearchResult res;
    res.id = "url:" + *url;
    res.title = "Open URL";
    res.subtitle = *url;
    res.icon = "text-html";
    res.provider_id = id();
    res.score = 2000.0;  // above any fuzzy app hit once ranked; an exact alias still wins on its title match
    res.execution_payload = *url;
    res.action_kind = core::ActionKind::Open;
    return {res};
}

}  // namespace nexus::providers
