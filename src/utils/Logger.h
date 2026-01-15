#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <memory>
#include <string>

namespace ResolutePulse {

class Logger {
public:
    static void initialize(const std::string& level = "info", 
                          const std::string& logFile = "");
    
    static std::shared_ptr<spdlog::logger>& get();
    
    static void setLevel(const std::string& level);
    
private:
    static std::shared_ptr<spdlog::logger> logger_;
};

// Convenience macros
#define LOG_TRACE(...) SPDLOG_LOGGER_TRACE(ResolutePulse::Logger::get(), __VA_ARGS__)
#define LOG_DEBUG(...) SPDLOG_LOGGER_DEBUG(ResolutePulse::Logger::get(), __VA_ARGS__)
#define LOG_INFO(...)  SPDLOG_LOGGER_INFO(ResolutePulse::Logger::get(), __VA_ARGS__)
#define LOG_WARN(...)  SPDLOG_LOGGER_WARN(ResolutePulse::Logger::get(), __VA_ARGS__)
#define LOG_ERROR(...) SPDLOG_LOGGER_ERROR(ResolutePulse::Logger::get(), __VA_ARGS__)
#define LOG_CRITICAL(...) SPDLOG_LOGGER_CRITICAL(ResolutePulse::Logger::get(), __VA_ARGS__)

} // namespace ResolutePulse
