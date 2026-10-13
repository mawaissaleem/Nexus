#pragma once

#include <string>
#include <vector>
#include <string_view>

namespace nexus::core {

class Query {
public:
    // `prefix` is the provider-owned prefix the query starts with (chosen by SearchEngine from the
    // registered providers); empty = unprefixed. It is stripped from text() when raw() starts with it.
    explicit Query(std::string raw_query, std::string prefix = "");

    [[nodiscard]] const std::string& raw() const noexcept { return raw_; }
    [[nodiscard]] const std::string& text() const noexcept { return processed_text_; }
    [[nodiscard]] const std::string& normalized() const noexcept { return normalized_; }
    [[nodiscard]] const std::string& prefix() const noexcept { return prefix_; }
    [[nodiscard]] const std::vector<std::string>& tokens() const noexcept { return tokens_; }
    [[nodiscard]] bool empty() const noexcept { return raw_.empty(); }

private:
    void parse();

    std::string raw_;
    std::string prefix_;
    std::string processed_text_; // Trimmed, prefix stripped, case kept
    std::string normalized_;     // processed_text_ lowercased
    std::vector<std::string> tokens_;
};

} // namespace nexus::core

