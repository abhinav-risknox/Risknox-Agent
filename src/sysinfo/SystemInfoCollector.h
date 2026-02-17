#pragma once

#include "OSInfoCollector.h"
#include "InstalledAppsCollector.h"
#include "OpenPortsCollector.h"
#include <nlohmann/json.hpp>
#include <memory>
#include <string>

namespace ResolutePulse {

// Configuration for what to collect
struct SystemInfoCollectionConfig {
    bool collectOS = true;
    bool collectApps = true;
    bool collectPorts = true;
};

class SystemInfoCollector {
public:
    struct SystemInfoData {
        nlohmann::json osInfo;
        nlohmann::json installedApps;
        nlohmann::json networkConnections;
        std::string timestamp;
        
        // Convert to single JSON object
        nlohmann::json toJson() const {
            nlohmann::json j;
            j["type"] = "system_info";
            j["collected_at"] = timestamp;
            if (!osInfo.empty()) j["os_info"] = osInfo;
            if (!installedApps.empty()) j["installed_applications"] = installedApps;
            if (!networkConnections.empty()) j["network_connections"] = networkConnections;
            return j;
        }
    };
    
    SystemInfoCollector();
    ~SystemInfoCollector() = default;
    
    // Initialize all collectors
    bool initialize(const SystemInfoCollectionConfig& config = SystemInfoCollectionConfig());
    
    // Collect all system information
    SystemInfoData collectAll();
    
private:
    SystemInfoCollectionConfig config_;
    std::unique_ptr<OSInfoCollector> osCollector_;
    std::unique_ptr<InstalledAppsCollector> appsCollector_;
    std::unique_ptr<OpenPortsCollector> portsCollector_;
    
    // Get current timestamp in ISO 8601 format
    std::string getCurrentTimestamp();
};

} // namespace ResolutePulse
