#include "Logger.h"
#include <algorithm>

namespace ResolutePulse {

std::shared_ptr<spdlog::logger> Logger::logger_ = nullptr;

void Logger::initialize(const std::string& level, const std::string& logFile) {
    std::vector<spdlog::sink_ptr> sinks;
    
    // Console sink with colors
    auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    consoleSink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] %v");
    sinks.push_back(consoleSink);
    
    // File sink if path provided
    if (!logFile.empty()) {
        auto fileSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            logFile, 
            10 * 1024 * 1024,  // 10 MB max size
            5                   // 5 rotated files
        );
        fileSink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%t] %v");
        sinks.push_back(fileSink);
    }
    
    logger_ = std::make_shared<spdlog::logger>("ResolutePulse", sinks.begin(), sinks.end());
    logger_->flush_on(spdlog::level::warn);
    
    setLevel(level);
    
    spdlog::set_default_logger(logger_);
}

std::shared_ptr<spdlog::logger>& Logger::get() {
    if (!logger_) {
        initialize();
    }
    return logger_;
}

void Logger::setLevel(const std::string& level) {
    std::string lowerLevel = level;
    std::transform(lowerLevel.begin(), lowerLevel.end(), lowerLevel.begin(), ::tolower);
    
    if (lowerLevel == "trace") {
        logger_->set_level(spdlog::level::trace);
    } else if (lowerLevel == "debug") {
        logger_->set_level(spdlog::level::debug);
    } else if (lowerLevel == "info") {
        logger_->set_level(spdlog::level::info);
    } else if (lowerLevel == "warn" || lowerLevel == "warning") {
        logger_->set_level(spdlog::level::warn);
    } else if (lowerLevel == "error") {
        logger_->set_level(spdlog::level::err);
    } else if (lowerLevel == "critical") {
        logger_->set_level(spdlog::level::critical);
    } else {
        logger_->set_level(spdlog::level::info);
    }
}

} // namespace ResolutePulse
