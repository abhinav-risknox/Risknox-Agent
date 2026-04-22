#pragma once

#include <string>
#include <vector>
#include <map>
#include <thread>
#include <atomic>
#include <mutex>
#include <functional>
#include <nlohmann/json.hpp>

namespace ResolutePulse {

struct BlockedApp {
    std::string name;
    std::string executable;
    std::string blockedAt;
    std::string status;           // "active"
    int kills = 0;
    bool registryApplied = false;
    bool explorerRestarted = false;

    nlohmann::json toJson() const {
        return {
            {"name", name},
            {"executable", executable},
            {"blockedAt", blockedAt},
            {"status", status},
            {"kills", kills},
            {"registryApplied", registryApplied},
            {"explorerRestarted", explorerRestarted}
        };
    }
};

struct AppBlockConfig {
    bool enabled = false;
    std::string configPath;       // Path to blocked_apps.json
    int monitorIntervalMs = 300;
};

class SoftwareBlocker {
public:
    using BlockEventCallback = std::function<void(const nlohmann::json&)>;

    SoftwareBlocker();
    ~SoftwareBlocker();

    bool initialize(const AppBlockConfig& config);
    bool start();
    void stop();

    // Operations
    nlohmann::json blockApplication(const std::string& name, const std::string& executable);
    nlohmann::json unblockApplication(const std::string& executable);
    std::vector<BlockedApp> getBlockedApplications() const;

    // Policy from Manager
    bool applyPolicy(const nlohmann::json& policy);

    // Status for reporting
    nlohmann::json getStatus() const;

    void setEventCallback(BlockEventCallback callback);

    bool isRunning() const { return running_.load(); }

private:
    // Process management
    int terminateProcess(const std::string& executable);

    // Registry blocking (3 locations)
    bool blockWithRegistry(const std::string& executable);
    bool removeRegistryBlock(const std::string& executable);

    // System operations
    void updateGroupPolicy();
    bool restartExplorer();

    // Background monitor
    void monitorLoop(const std::string& executable);
    void startProcessMonitor(const std::string& executable);
    void stopProcessMonitor(const std::string& executable);
    void stopAllMonitors();

    // Persistence
    bool loadBlockedApps();
    bool saveBlockedApps();

    // Helpers
    std::string toLower(const std::string& s) const;
    std::string getCurrentTimestamp() const;

    AppBlockConfig config_;
    std::vector<BlockedApp> blockedApps_;
    std::map<std::string, std::thread> monitors_;
    std::map<std::string, std::atomic<bool>*> monitorFlags_;
    mutable std::mutex mutex_;
    std::atomic<bool> running_{false};
    BlockEventCallback eventCallback_;
};

} // namespace ResolutePulse
