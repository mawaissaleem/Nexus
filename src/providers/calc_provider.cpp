#include "nexus/providers/calc_provider.hpp"
#include "nexus/utils/fuzzy.hpp"
#include <cmath>
#include <sstream>
#include <iomanip>
#include <cctype>

namespace nexus::providers {

namespace {

enum class TokenType {
    Number,
    Plus,
    Minus,
    Multiply,
    Divide,
    Modulo,
    Power,
    LParen,
    RParen,
    Identifier,
    End
};

struct Token {
    TokenType type{TokenType::End};
    double value{0.0};
    std::string text;
};

class MathParser {
public:
    explicit MathParser(std::string_view input) : input_(input) {
        next_token();
    }

    std::optional<double> parse() {
        if (current_token_.type == TokenType::End) {
            return std::nullopt;
        }
        auto res = parse_expression();
        if (res && current_token_.type == TokenType::End) {
            return res;
        }
        return std::nullopt;
    }

private:
    void next_token() {
        while (pos_ < input_.size() && std::isspace(static_cast<unsigned char>(input_[pos_]))) {
            ++pos_;
        }

        if (pos_ >= input_.size()) {
            current_token_ = {TokenType::End, 0.0, ""};
            return;
        }

        char c = input_[pos_];
        if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') {
            size_t start = pos_;
            bool has_dot = (c == '.');
            ++pos_;
            while (pos_ < input_.size() && (std::isdigit(static_cast<unsigned char>(input_[pos_])) || (!has_dot && input_[pos_] == '.'))) {
                if (input_[pos_] == '.') has_dot = true;
                ++pos_;
            }
            std::string s(input_.substr(start, pos_ - start));
            try {
                current_token_ = {TokenType::Number, std::stod(s), s};
            } catch (...) {
                current_token_ = {TokenType::End, 0.0, ""};
            }
            return;
        }

        if (std::isalpha(static_cast<unsigned char>(c))) {
            size_t start = pos_;
            while (pos_ < input_.size() && std::isalpha(static_cast<unsigned char>(input_[pos_]))) {
                ++pos_;
            }
            std::string id = utils::FuzzyMatcher::to_lower(input_.substr(start, pos_ - start));
            if (id == "pi") {
                current_token_ = {TokenType::Number, 3.141592653589793, "pi"};
            } else if (id == "e") {
                current_token_ = {TokenType::Number, 2.718281828459045, "e"};
            } else {
                current_token_ = {TokenType::Identifier, 0.0, id};
            }
            return;
        }

        ++pos_;
        switch (c) {
            case '+': current_token_ = {TokenType::Plus, 0.0, "+"}; return;
            case '-': current_token_ = {TokenType::Minus, 0.0, "-"}; return;
            case '*': current_token_ = {TokenType::Multiply, 0.0, "*"}; return;
            case '/': current_token_ = {TokenType::Divide, 0.0, "/"}; return;
            case '%': current_token_ = {TokenType::Modulo, 0.0, "%"}; return;
            case '^': current_token_ = {TokenType::Power, 0.0, "^"}; return;
            case '(': current_token_ = {TokenType::LParen, 0.0, "("}; return;
            case ')': current_token_ = {TokenType::RParen, 0.0, ")"}; return;
            default:  current_token_ = {TokenType::End, 0.0, ""}; return;
        }
    }

    std::optional<double> parse_expression() {
        auto left = parse_term();
        if (!left) return std::nullopt;

        while (current_token_.type == TokenType::Plus || current_token_.type == TokenType::Minus) {
            TokenType op = current_token_.type;
            next_token();
            auto right = parse_term();
            if (!right) return std::nullopt;

            if (op == TokenType::Plus) {
                *left += *right;
            } else {
                *left -= *right;
            }
        }
        return left;
    }

    std::optional<double> parse_term() {
        auto left = parse_power();
        if (!left) return std::nullopt;

        while (current_token_.type == TokenType::Multiply ||
               current_token_.type == TokenType::Divide ||
               current_token_.type == TokenType::Modulo) {
            TokenType op = current_token_.type;
            next_token();
            auto right = parse_power();
            if (!right) return std::nullopt;

            if (op == TokenType::Multiply) {
                *left *= *right;
            } else if (op == TokenType::Divide) {
                if (*right == 0.0) return std::nullopt; // divide by zero
                *left /= *right;
            } else if (op == TokenType::Modulo) {
                if (*right == 0.0) return std::nullopt;
                *left = std::fmod(*left, *right);
            }
        }
        return left;
    }

    std::optional<double> parse_power() {
        auto left = parse_factor();
        if (!left) return std::nullopt;

        if (current_token_.type == TokenType::Power) {
            next_token();
            auto right = parse_power(); // right associative
            if (!right) return std::nullopt;
            *left = std::pow(*left, *right);
        }
        return left;
    }

    std::optional<double> parse_factor() {
        if (current_token_.type == TokenType::Plus) {
            next_token();
            return parse_factor();
        }
        if (current_token_.type == TokenType::Minus) {
            next_token();
            auto val = parse_factor();
            if (val) return -(*val);
            return std::nullopt;
        }
        if (current_token_.type == TokenType::Number) {
            double val = current_token_.value;
            next_token();
            return val;
        }
        if (current_token_.type == TokenType::Identifier) {
            std::string func = current_token_.text;
            next_token();
            if (current_token_.type != TokenType::LParen) return std::nullopt;
            next_token();
            auto arg = parse_expression();
            if (!arg) return std::nullopt;
            if (current_token_.type != TokenType::RParen) return std::nullopt;
            next_token();

            if (func == "sqrt") {
                if (*arg < 0.0) return std::nullopt;
                return std::sqrt(*arg);
            } else if (func == "abs") {
                return std::abs(*arg);
            } else if (func == "sin") {
                return std::sin(*arg);
            } else if (func == "cos") {
                return std::cos(*arg);
            } else if (func == "tan") {
                return std::tan(*arg);
            } else if (func == "log" || func == "ln") {
                if (*arg <= 0.0) return std::nullopt;
                return std::log(*arg);
            } else if (func == "exp") {
                return std::exp(*arg);
            }
            return std::nullopt;
        }
        if (current_token_.type == TokenType::LParen) {
            next_token();
            auto val = parse_expression();
            if (!val) return std::nullopt;
            if (current_token_.type != TokenType::RParen) return std::nullopt;
            next_token();
            return val;
        }
        return std::nullopt;
    }

    std::string_view input_;
    size_t pos_{0};
    Token current_token_;
};

} // namespace

std::optional<double> CalculatorProvider::evaluate(std::string_view expression) {
    MathParser parser(expression);
    return parser.parse();
}

std::vector<core::SearchResult> CalculatorProvider::search(
    const core::Query& query,
    const std::atomic<bool>& cancel_token
) {
    if (cancel_token.load()) {
        return {};
    }

    std::string_view expr;
    if (query.mode() == core::QueryMode::Calculator) {
        expr = query.text();
    } else if (query.mode() == core::QueryMode::General) {
        // Test if expression contains math symbols (+, -, *, /, ^, sqrt)
        std::string raw = query.raw();
        bool has_operator = false;
        for (char c : raw) {
            if (c == '+' || c == '-' || c == '*' || c == '/' || c == '%' || c == '^') {
                has_operator = true;
                break;
            }
        }
        if (has_operator || raw.find("sqrt") != std::string::npos) {
            expr = query.raw();
        } else {
            return {};
        }
    } else {
        return {};
    }

    auto value = evaluate(expr);
    if (!value) {
        return {};
    }

    std::ostringstream ss;
    // Format double cleanly (remove trailing zeros if integer)
    if (std::floor(*value) == *value && !std::isinf(*value)) {
        ss << static_cast<long long>(*value);
    } else {
        ss << std::setprecision(10) << *value;
    }

    std::string result_str = ss.str();

    core::SearchResult res;
    res.id = "calc:" + result_str;
    res.title = result_str;
    res.subtitle = "= " + std::string(expr);
    res.icon = "accessories-calculator";
    res.provider_id = id();
    res.score = (query.mode() == core::QueryMode::Calculator) ? 1000.0 : 750.0;
    res.execution_payload = result_str;
    res.action = [result_str]() -> bool {
        // Output to stdout / can integrate with clipboard
        return true;
    };

    return {res};
}

} // namespace nexus::providers

