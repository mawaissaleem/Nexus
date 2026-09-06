#pragma once

#include <iostream>
#include <mutex>
#include <string>
#include <string_view>
#include <sstream>

namespace nexus::utils {

enum class LogLevel {
    Debug,
    Info,
    Warning,
    Error
};

class Logger {
public:
    static Logger& instance();

    void set_level(LogLevel level);
    LogLevel get_level() const;

    void log(LogLevel level, std::string_view message, const char* file = nullptr, int line = 0);

private:
    Logger() = default;
    ~Logger() = default;
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    LogLevel level_{LogLevel::Info};
    mutable std::mutex mutex_;
};

} // namespace nexus::utils

#define NEXUS_LOG_DEBUG(msg) ::nexus::utils::Logger::instance().log(::nexus::utils::LogLevel::Debug, (msg), __FILE__, __LINE__)
#define NEXUS_LOG_INFO(msg)  ::nexus::utils::Logger::instance().log(::nexus::utils::LogLevel::Info, (msg), __FILE__, __LINE__)
#define NEXUS_LOG_WARN(msg)  ::nexus::utils::Logger::instance().log(::nexus::utils::LogLevel::Warning, (msg), __FILE__, __LINE__)
#define NEXUS_LOG_ERROR(msg) ::nexus::utils::Logger::instance().log(::nexus::utils::LogLevel::Error, (msg), __FILE__, __LINE__)

