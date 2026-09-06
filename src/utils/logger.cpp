#include "nexus/utils/logger.hpp"
#include <chrono>
#include <iomanip>
#include <ctime>

namespace nexus::utils {

Logger& Logger::instance() {
    static Logger inst;
    return inst;
}

void Logger::set_level(LogLevel level) {
    std::lock_guard<std::mutex> lock(mutex_);
    level_ = level;
}

LogLevel Logger::get_level() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return level_;
}

void Logger::log(LogLevel level, std::string_view message, const char* file, int line) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (static_cast<int>(level) < static_cast<int>(level_)) {
        return;
    }

    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    std::tm tm_buf{};
    localtime_r(&time_t_now, &tm_buf);

    const char* level_str = "INFO";
    switch (level) {
        case LogLevel::Debug:   level_str = "DEBUG"; break;
        case LogLevel::Info:    level_str = "INFO "; break;
        case LogLevel::Warning: level_str = "WARN "; break;
        case LogLevel::Error:   level_str = "ERROR"; break;
    }

    std::ostream& out = (level == LogLevel::Error || level == LogLevel::Warning) ? std::cerr : std::cout;
    out << "[" << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S")
        << '.' << std::setfill('0') << std::setw(3) << ms.count() << "] "
        << "[" << level_str << "] "
        << message;

    if (level == LogLevel::Debug && file) {
        out << " (" << file << ":" << line << ")";
    }
    out << '\n';
}

} // namespace nexus::utils

