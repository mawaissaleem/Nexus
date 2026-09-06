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

Query::Query(std::string raw_query)
    : raw_(std::move(raw_query)) {
    parse();
}

void Query::parse() {
    std::string trimmed = trim(raw_);
    if (trimmed.empty()) {
        mode_ = QueryMode::General;
        processed_text_ = "";
        normalized_ = "";
        return;
    }

    // Check prefixes
    if (trimmed.front() == '>') {
        mode_ = QueryMode::Shell;
        processed_text_ = trim(std::string_view(trimmed).substr(1));
        normalized_ = processed_text_; // do not lowercase shell commands!
    } else if (trimmed.rfind("calc ", 0) == 0) {
        mode_ = QueryMode::Calculator;
        processed_text_ = trim(std::string_view(trimmed).substr(5));
        normalized_ = utils::FuzzyMatcher::to_lower(processed_text_);
    } else if (trimmed.rfind("file ", 0) == 0) {
        mode_ = QueryMode::File;
        processed_text_ = trim(std::string_view(trimmed).substr(5));
        normalized_ = utils::FuzzyMatcher::to_lower(processed_text_);
    } else if (trimmed.rfind("web ", 0) == 0) {
        mode_ = QueryMode::Web;
        processed_text_ = trim(std::string_view(trimmed).substr(4));
        normalized_ = processed_text_;
    } else if (trimmed.rfind("clip ", 0) == 0) {
        mode_ = QueryMode::Clipboard;
        processed_text_ = trim(std::string_view(trimmed).substr(5));
        normalized_ = utils::FuzzyMatcher::to_lower(processed_text_);
    } else {
        mode_ = QueryMode::General;
        processed_text_ = trimmed;
        normalized_ = utils::FuzzyMatcher::to_lower(trimmed);
    }

    // Tokenize normalized text
    std::istringstream iss(normalized_);
    std::string token;
    while (iss >> token) {
        tokens_.push_back(token);
    }
}

} // namespace nexus::core

