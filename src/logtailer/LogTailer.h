#pragma once

// LogTailer.h
// Lightweight log file tailer that reads new lines from configured log files
// and pushes them as Event objects into the existing EventQueue pipeline.
// Handles log rotation (file truncation) and persists offsets across restarts.
//
// Log sources are fully configurable — specify any log file path and channel
// via config.json or remotely via config_push from the Manager.

#include "collector/Event.h"
#include "queue/EventQueue.h"
#include "utils/Logger.h"

#include <nlohmann/json.hpp>
#include <string>
#include <map>
#include <vector>
#include <fstream>
#include <thread>
#include <atomic>
#include <mutex>
#include <filesystem>
#include <chrono>
#include <ctime>

namespace ResolutePulse {

/// Defines a single log file to tail
struct LogSource {
    std::string name;       // Unique key for offset tracking (e.g. "freshclam")
    std::string path;       // File path — absolute or relative to agent install dir
    std::string channel;    // Channel value for routing in Data Prepper (e.g. "ClamAV")
};

class LogTailer {
public:
    explicit LogTailer(EventQueue& queue) : queue_(queue) {}

    ~LogTailer() { stop(); }

    /// Set the base directory for resolving relative log paths
    void setBaseDir(const std::string& dir) { baseDir_ = dir; }

    /// Set the path for persisting offsets across restarts
    void setOffsetPath(const std::string& path) { offsetPath_ = path; }

    /// Reconfigure which logs are tailed (safe to call while running).
    /// Each entry: { "name": "...", "path": "...", "channel": "..." }
    void reconfigure(const std::vector<LogSource>& sources) {
        std::lock_guard<std::mutex> lk(mutex_);
        activeSources_ = sources;
        LOG_INFO("LogTailer: reconfigured, {} source(s) active", activeSources_.size());
        for (const auto& s : activeSources_) {
            LOG_INFO("LogTailer:   [{}] path={} channel={}", s.name, s.path, s.channel);
        }
    }

    /// Parse a JSON array of log source definitions and reconfigure
    void reconfigureFromJson(const nlohmann::json& logsArray) {
        std::vector<LogSource> sources;
        for (const auto& entry : logsArray) {
            LogSource src;
            src.name    = entry.value("name", "");
            src.path    = entry.value("path", "");
            src.channel = entry.value("channel", "ClamAV");
            if (src.name.empty() || src.path.empty()) continue;
            sources.push_back(std::move(src));
        }
        reconfigure(sources);
    }

    /// Start the polling thread
    void start() {
        if (running_.load()) return;
        running_ = true;
        loadOffsets();
        thread_ = std::thread(&LogTailer::tailLoop, this);
        LOG_INFO("LogTailer: started");
    }

    /// Stop the polling thread
    void stop() {
        running_ = false;
        if (thread_.joinable()) thread_.join();
    }

    bool isRunning() const { return running_.load(); }

    /// Get count of active sources
    size_t getActiveSourceCount() const {
        std::lock_guard<std::mutex> lk(mutex_);
        return activeSources_.size();
    }

private:
    void tailLoop() {
        while (running_.load()) {
            {
                std::lock_guard<std::mutex> lk(mutex_);
                for (const auto& src : activeSources_) {
                    namespace fs = std::filesystem;
                    fs::path logPath(src.path);

                    // Resolve relative paths against the agent install directory
                    if (logPath.is_relative()) {
                        logPath = fs::path(baseDir_) / logPath;
                    }

                    if (!fs::exists(logPath)) continue;

                    tailFile(src.name, logPath.string(), src.channel);
                }
            }
            // Sleep ~5 seconds, waking every 500ms to check stop flag
            for (int i = 0; i < 10 && running_.load(); ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        saveOffsets();
        LOG_INFO("LogTailer: stopped");
    }

    void tailFile(const std::string& name, const std::string& path, const std::string& channel) {
        std::ifstream file(path, std::ios::binary);
        if (!file) return;

        // Get current file size
        file.seekg(0, std::ios::end);
        auto fileSize = static_cast<std::streamoff>(file.tellg());

        auto& offset = offsets_[name];

        // Handle log rotation: file shrunk → reset to beginning
        if (fileSize < offset) {
            LOG_INFO("LogTailer: {} rotated (size {} < offset {}), resetting",
                     name, fileSize, offset);
            offset = 0;
        }

        // Nothing new to read
        if (fileSize <= offset) return;

        // Seek to last known position and read new lines
        file.seekg(offset);
        std::string line;
        int linesRead = 0;
        while (std::getline(file, line)) {
            // Trim trailing \r if present (Windows line endings)
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;

            Event event;
            event.channel   = channel;
            event.eventId   = 0;
            event.timestamp = currentTimestamp();
            event.data      = line;

            queue_.push(std::move(event));
            ++linesRead;
        }

        // Update offset to current position
        auto newPos = static_cast<std::streamoff>(file.tellg());
        offset = (newPos >= 0) ? newPos : fileSize;  // EOF safety

        if (linesRead > 0) {
            LOG_DEBUG("LogTailer: read {} line(s) from {} [{}]", linesRead, name, channel);
            saveOffsets();
        }
    }

    // ── Offset persistence ─────────────────────────────────────

    void loadOffsets() {
        if (offsetPath_.empty()) return;
        try {
            std::ifstream f(offsetPath_);
            if (!f) return;
            nlohmann::json j; f >> j;
            for (auto& [k, v] : j.items()) {
                offsets_[k] = v.get<std::streamoff>();
            }
            LOG_INFO("LogTailer: loaded offsets from {}", offsetPath_);
        } catch (...) {
            LOG_WARN("LogTailer: failed to load offsets, starting fresh");
        }
    }

    void saveOffsets() {
        if (offsetPath_.empty()) return;
        try {
            nlohmann::json j;
            for (auto& [k, v] : offsets_) j[k] = v;
            std::ofstream f(offsetPath_);
            f << j.dump(2);
        } catch (...) {
            LOG_WARN("LogTailer: failed to save offsets");
        }
    }

    // ── Helpers ────────────────────────────────────────────────

    static std::string currentTimestamp() {
        time_t now = time(nullptr);
        char buf[32];
        strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));
        return std::string(buf);
    }

    EventQueue&                           queue_;
    std::string                           baseDir_;
    std::string                           offsetPath_;
    std::vector<LogSource>                activeSources_;
    std::map<std::string, std::streamoff> offsets_;
    std::thread                           thread_;
    std::atomic<bool>                     running_{false};
    mutable std::mutex                    mutex_;
};

} // namespace ResolutePulse
