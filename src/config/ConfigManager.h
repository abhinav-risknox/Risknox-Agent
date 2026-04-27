#pragma once

#include <string>
#include <vector>
#include <map>
#include <set>
#include <nlohmann/json.hpp>

namespace ResolutePulse {

// TlsConfig removed - TCP does not use TLS

struct ManagerConfig {
    bool enabled = false;
    std::string host = "localhost";
    int port = 1514;
    std::string certs_dir = "certs";
    int registration_retry_interval = 30;
    int heartbeat_interval = 60;
    int license_check_interval = 3600;
};

struct BufferConfig {
    size_t max_events = 50000;
    int flush_interval_sec = 10;
    std::string db_path = "events_buffer.db";
};

struct EventCollectionConfig {
    bool collect_historical_on_startup = false;
    int historical_hours_back = 24;
    size_t max_historical_events_per_channel = 10000;
};

struct FimConfigData {
    bool enabled = false;
    std::vector<std::string> directories;
    std::vector<std::string> exclude_patterns;
    uint64_t max_file_size_mb = 100;
    int baseline_interval_hours = 24;
    std::string db_path = "fim_baseline.db";
};

struct SystemInfoConfig {
    bool enabled = true;
    bool collect_on_startup = true;
    int collection_interval_hours = 24;
    
    struct AppsConfig {
        bool include_install_date = true;
        bool include_install_location = true;
        bool include_size = true;
    } apps;
    
    struct PortsConfig {
        bool include_listening = true;
        bool include_established = true;
        bool include_process_info = true;
    } ports;
};

struct PatchConfigData {
    bool enabled = false;
    bool auto_scan = true;
    int scan_interval_hours = 24;
    bool auto_install = false;
    std::vector<std::string> exclude_kbs;
};

struct AntivirusConfigData {
    bool enabled = false;
    bool auto_scan = false;              // run scheduled scans automatically
    int scan_interval_hours = 24;        // how often
    bool auto_update_definitions = true; // run freshclam before each scan
    int update_interval_hours = 12;      // how often to update defs independently
    std::vector<std::string> scan_paths; // paths to scan (defaults to C:\Users)
};

struct WebBlockConfigData {
    bool enabled = false;
};

struct AppBlockConfigData {
    bool enabled = false;
    int monitor_interval_ms = 300;
};

class ConfigManager {
public:
    static ConfigManager& instance();
    
    bool load(const std::string& configPath);
    bool isLoaded() const { return loaded_; }
    
    // Getters
    const std::string& getFluentBitHost() const { return fluentBitHost_; }
    int getFluentBitPort() const { return fluentBitPort_; }
    bool getFluentBitTlsEnabled() const { return fluentBitTlsEnabled_; }
    const std::string& getFluentBitCaCertPath() const { return fluentBitCaCertPath_; }
    const std::string& getAgentId() const { return agentId_; }
    const BufferConfig& getBufferConfig() const { return bufferConfig_; }
    const EventCollectionConfig& getEventCollectionConfig() const { return eventCollectionConfig_; }
    const ManagerConfig& getManagerConfig() const { return managerConfig_; }
    
    const std::vector<std::string>& getCriticalChannels() const { return criticalChannels_; }
    const std::vector<std::string>& getEventChannels() const { return eventChannels_; }
    const std::map<std::string, std::set<int>>& getEventFilters() const { return eventFilters_; }
    
    const std::string& getLogLevel() const { return logLevel_; }
    const FimConfigData& getFimConfig() const { return fimConfig_; }
    const SystemInfoConfig& getSysInfoConfig() const { return sysInfoConfig_; }
    const PatchConfigData& getPatchConfig() const { return patchConfig_; }
    const WebBlockConfigData& getWebBlockConfig() const { return webBlockConfig_; }
    const AppBlockConfigData& getAppBlockConfig() const { return appBlockConfig_; }
    const AntivirusConfigData& getAntivirusConfig() const { return avConfig_; }
    
    // Check if an event should be collected
    bool shouldCollectEvent(const std::string& channel, int eventId) const;
    
private:
    ConfigManager() = default;
    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;
    
    bool loaded_ = false;
    
    std::string fluentBitHost_ = "localhost";
    int fluentBitPort_ = 5170;
    bool fluentBitTlsEnabled_ = false;
    std::string fluentBitCaCertPath_;
    std::string agentId_;
    ManagerConfig managerConfig_;
    BufferConfig bufferConfig_;
    EventCollectionConfig eventCollectionConfig_;
    FimConfigData fimConfig_;
    SystemInfoConfig sysInfoConfig_;
    
    std::vector<std::string> criticalChannels_;
    std::vector<std::string> eventChannels_;
    std::map<std::string, std::set<int>> eventFilters_;
    
    std::string logLevel_ = "info";
    PatchConfigData patchConfig_;
    WebBlockConfigData webBlockConfig_;
    AppBlockConfigData appBlockConfig_;
    AntivirusConfigData avConfig_;
};

} // namespace ResolutePulse
