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

} // namespace

Query::Query(std::string raw_query, std::string prefix)
    : raw_(std::move(raw_query)), prefix_(std::move(prefix)) {
    parse();
}

void Query::parse() {
    std::string trimmed = trim(raw_);
    if (!prefix_.empty() && trimmed.rfind(prefix_, 0) == 0) {
        processed_text_ = trim(std::string_view(trimmed).substr(prefix_.size()));
    } else {
        prefix_.clear();
        processed_text_ = trimmed;
    }
    normalized_ = utils::FuzzyMatcher::to_lower(processed_text_);

    // Tokenize normalized text
    std::istringstream iss(normalized_);
    std::string token;
    while (iss >> token) {
        tokens_.push_back(token);
    }
}

} // namespace nexus::core

