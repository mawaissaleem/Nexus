#pragma once

#include <string>
#include <vector>
#include <string_view>

namespace nexus::core {

enum class QueryMode {
    General,
    Shell,       // Starts with '>'
    Calculator,  // Starts with 'calc ' or looks like pure arithmetic
    File,        // Starts with 'file '
    Web,         // Starts with 'web '
    Clipboard    // Starts with 'clip '
};

class Query {
public:
    explicit Query(std::string raw_query);

    [[nodiscard]] const std::string& raw() const noexcept { return raw_; }
    [[nodiscard]] const std::string& text() const noexcept { return processed_text_; }
    [[nodiscard]] const std::string& normalized() const noexcept { return normalized_; }
    [[nodiscard]] QueryMode mode() const noexcept { return mode_; }
    [[nodiscard]] const std::vector<std::string>& tokens() const noexcept { return tokens_; }
    [[nodiscard]] bool empty() const noexcept { return raw_.empty(); }

private:
    void parse();

    std::string raw_;
    std::string processed_text_; // Query with mode prefix stripped
    std::string normalized_;     // Lowercased, whitespace normalized
    QueryMode mode_{QueryMode::General};
    std::vector<std::string> tokens_;
};

} // namespace nexus::core

