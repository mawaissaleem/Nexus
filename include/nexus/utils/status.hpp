#pragma once

#include <string>
#include <system_error>
#include <utility>

namespace nexus::utils {

// Outcome of an operation that can fail: success, or a message that says why in plain words
// (what could not be done, the path involved, and the system's reason). Tests it with `if (status)`.
class [[nodiscard]] Status {
public:
    Status() = default;

    [[nodiscard]] static Status error(std::string message) { return Status(std::move(message)); }

    // "<what>: <reason for errno value>", e.g. `can't write /etc/x: Permission denied`.
    [[nodiscard]] static Status from_errno(const std::string& what, int err) {
        return error(what + ": " + std::generic_category().message(err));
    }

    explicit operator bool() const noexcept { return !failed_; }
    [[nodiscard]] const std::string& message() const noexcept { return message_; }

private:
    explicit Status(std::string message) : failed_(true), message_(std::move(message)) {}

    bool failed_{false};
    std::string message_;
};

}  // namespace nexus::utils
