#pragma once

#include <string>
#include <vector>
#include <map>
#include <set>
#include <nlohmann/json.hpp>

namespace ResolutePulse {

struct TlsConfig {
    bool verify_peer = false;
    std::string ca_cert_path;
    std::string client_cert_path;
    std::string client_key_path;
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

class ConfigManager {
public:
    static ConfigManager& instance();
    
    bool load(const std::string& configPath);
    bool isLoaded() const { return loaded_; }
    
    // Getters
    const std::string& getManagerHttpUrl() const { return managerHttpUrl_; }
    const std::string& getAgentId() const { return agentId_; }
    const std::string& getAuthToken() const { return authToken_; }
    const TlsConfig& getTlsConfig() const { return tlsConfig_; }
    const BufferConfig& getBufferConfig() const { return bufferConfig_; }
    const EventCollectionConfig& getEventCollectionConfig() const { return eventCollectionConfig_; }
    
    const std::vector<std::string>& getCriticalChannels() const { return criticalChannels_; }
    const std::vector<std::string>& getEventChannels() const { return eventChannels_; }
    const std::map<std::string, std::set<int>>& getEventFilters() const { return eventFilters_; }
    
    const std::string& getLogLevel() const { return logLevel_; }
    
    // Check if an event should be collected
    bool shouldCollectEvent(const std::string& channel, int eventId) const;
    
private:
    ConfigManager() = default;
    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;
    
    bool loaded_ = false;
    
    std::string managerHttpUrl_;
    std::string agentId_;
    std::string authToken_;
    TlsConfig tlsConfig_;
    BufferConfig bufferConfig_;
    EventCollectionConfig eventCollectionConfig_;
    
    std::vector<std::string> criticalChannels_;
    std::vector<std::string> eventChannels_;
    std::map<std::string, std::set<int>> eventFilters_;
    
    std::string logLevel_ = "info";
};

} // namespace ResolutePulse
