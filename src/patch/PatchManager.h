#pragma once

#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <functional>
#include <nlohmann/json.hpp>

namespace ResolutePulse {

struct PatchInfo {
    std::string title;
    std::string kb;
    std::string updateId;
    bool isInstalled = false;
    std::string severity;
    std::string description;
    std::string categories;

    nlohmann::json toJson() const {
        return {
            {"title", title},
            {"kb", kb},
            {"updateId", updateId},
            {"isInstalled", isInstalled},
            {"severity", severity},
            {"description", description},
            {"categories", categories}
        };
    }
};

struct PatchConfig {
    bool enabled = false;
    bool autoScan = true;
    int scanIntervalHours = 24;
    bool autoInstall = false;
    std::vector<std::string> excludeKBs;
};

class PatchManager {
public:
    using PatchEventCallback = std::function<void(const nlohmann::json&)>;
    using StatusReportCallback = std::function<void(const std::string& reportType,
                                                     const nlohmann::json& data)>;

    PatchManager();
    ~PatchManager();

    bool initialize(const PatchConfig& config);
    bool start();
    void stop();

    // On-demand operations
    std::vector<PatchInfo> scanForUpdates();
    bool installUpdates(const std::vector<std::string>& updateIds);
    std::vector<PatchInfo> getInstalledUpdates();

    // Policy from Manager
    bool applyPolicy(const nlohmann::json& policy);

    // Status for reporting
    nlohmann::json getStatus() const;

    void setEventCallback(PatchEventCallback callback);
    void setStatusReportCallback(StatusReportCallback cb) { statusCallback_ = std::move(cb); }

    bool isRunning() const { return running_.load(); }

private:
    void scanLoop();
    std::string getCurrentTimestamp() const;

    PatchConfig config_;
    std::thread scanThread_;
    std::atomic<bool> running_{false};
    PatchEventCallback eventCallback_;
    StatusReportCallback statusCallback_;
    mutable std::mutex mutex_;

    // Cached scan results
    std::vector<PatchInfo> pendingUpdates_;
    std::vector<PatchInfo> installedUpdates_;
    std::string lastScanTime_;
    int lastScanPendingCount_ = 0;
};

} // namespace ResolutePulse
