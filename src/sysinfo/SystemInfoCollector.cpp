#include "SystemInfoCollector.h"
#include "utils/Logger.h"
#include <chrono>
#include <iomanip>
#include <sstream>

namespace ResolutePulse {

SystemInfoCollector::SystemInfoCollector() = default;

bool SystemInfoCollector::initialize(const SystemInfoCollectionConfig& config) {
    config_ = config;
    
    LOG_INFO("Initializing System Info Collector");
    
    if (config_.collectOS) {
        osCollector_ = std::make_unique<OSInfoCollector>();
        if (!osCollector_->initialize()) {
            LOG_WARN("Failed to initialize OS Info Collector");
            osCollector_.reset();
        }
    }
    
    if (config_.collectApps) {
        appsCollector_ = std::make_unique<InstalledAppsCollector>();
        if (!appsCollector_->initialize()) {
            LOG_WARN("Failed to initialize Installed Apps Collector");
            appsCollector_.reset();
        }
    }
    
    if (config_.collectPorts) {
        portsCollector_ = std::make_unique<OpenPortsCollector>();
        if (!portsCollector_->initialize()) {
            LOG_WARN("Failed to initialize Open Ports Collector");
            portsCollector_.reset();
        }
    }
    
    LOG_INFO("System Info Collector initialized successfully");
    return true;
}

SystemInfoCollector::SystemInfoData SystemInfoCollector::collectAll() {
    LOG_INFO("Collecting all system information");
    
    SystemInfoData data;
    data.timestamp = getCurrentTimestamp();
    
    if (osCollector_) {
        try {
            data.osInfo = osCollector_->collect();
            LOG_DEBUG("OS info collected");
        } catch (const std::exception& e) {
            LOG_ERROR("Error collecting OS info: {}", e.what());
        }
    }
    
    if (appsCollector_) {
        try {
            data.installedApps = appsCollector_->collect();
            LOG_DEBUG("Installed apps collected");
        } catch (const std::exception& e) {
            LOG_ERROR("Error collecting installed apps: {}", e.what());
        }
    }
    
    if (portsCollector_) {
        try {
            data.networkConnections = portsCollector_->collect();
            LOG_DEBUG("Network connections collected");
        } catch (const std::exception& e) {
            LOG_ERROR("Error collecting network connections: {}", e.what());
        }
    }
    
    LOG_INFO("System information collection completed");
    return data;
}

std::string SystemInfoCollector::getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    
    std::tm tm_buf;
    gmtime_s(&tm_buf, &time_t_now);
    
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%S");
    oss << '.' << std::setfill('0') << std::setw(3) << ms.count() << 'Z';
    
    return oss.str();
}

} // namespace ResolutePulse
