#include "nexus/core/query.hpp"
#include "nexus/utils/fuzzy.hpp"
#include <algorithm>
#include <sstream>

namespace nexus::core {

namespace {

std::string trim(std::string_view s) {
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) {
        ++start;
    }
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
        --end;
    }
    return std::string(s.substr(start, end - start));
}

}  // namespace

Query::Query(std::string raw_query, std::string prefix) : raw_(std::move(raw_query)), prefix_(std::move(prefix)) {
    parse();
}

void Query::parse() {
    // Trailing whitespace is kept for the prefix check only, so the bare prefix "web " matches.
    const size_t lead = raw_.find_first_not_of(" \t\n\r\f\v");
    const std::string_view head = lead == std::string::npos ? std::string_view{} : std::string_view(raw_).substr(lead);
    if (!prefix_.empty() && head.rfind(prefix_, 0) == 0) {
        processed_text_ = trim(head.substr(prefix_.size()));
    } else {
        prefix_.clear();
        processed_text_ = trim(head);
    }
    normalized_ = utils::FuzzyMatcher::to_lower(processed_text_);

    // Tokenize normalized text
    std::istringstream iss(normalized_);
    std::string token;
    while (iss >> token) {
        tokens_.push_back(token);
    }
}

}  // namespace nexus::core
