#include "ConfigManager.h"
#include "utils/Logger.h"
#include <fstream>

namespace ResolutePulse {

ConfigManager& ConfigManager::instance() {
    static ConfigManager instance;
    return instance;
}

bool ConfigManager::load(const std::string& configPath) {
    try {
        std::ifstream file(configPath);
        if (!file.is_open()) {
            LOG_ERROR("Failed to open config file: {}", configPath);
            return false;
        }
        
        nlohmann::json config = nlohmann::json::parse(file);
        
        // TCP configuration (optional - has defaults)
        fluentBitHost_ = config.value("fluent_bit_host", "localhost");
        fluentBitPort_ = config.value("fluent_bit_port", 5170);
        
        // Validate port
        if (fluentBitPort_ < 1 || fluentBitPort_ > 65535) {
            LOG_ERROR("Invalid fluent_bit_port: {}", fluentBitPort_);
            return false;
        }
        
        // Agent ID (required)
        if (!config.contains("agent_id")) {
            LOG_ERROR("Config missing required field: agent_id");
            return false;
        }
        agentId_ = config["agent_id"].get<std::string>();
        
        // Buffer config (optional)
        if (config.contains("buffer")) {
            auto& buffer = config["buffer"];
            bufferConfig_.max_events = buffer.value("max_events", 50000);
            bufferConfig_.flush_interval_sec = buffer.value("flush_interval_sec", 10);
            bufferConfig_.db_path = buffer.value("db_path", "events_buffer.db");
        }
        
        // Event collection config (optional)
        if (config.contains("event_collection")) {
            auto& ec = config["event_collection"];
            eventCollectionConfig_.collect_historical_on_startup = 
                ec.value("collect_historical_on_startup", false);
            eventCollectionConfig_.historical_hours_back = 
                ec.value("historical_hours_back", 24);
            eventCollectionConfig_.max_historical_events_per_channel = 
                ec.value("max_historical_events_per_channel", 10000);
        }
        
        // Critical channels (default to Security and System)
        if (config.contains("critical_channels")) {
            criticalChannels_ = config["critical_channels"].get<std::vector<std::string>>();
        } else {
            criticalChannels_ = {"Security", "System"};
        }
        
        // Event channels (required)
        if (!config.contains("event_channels")) {
            LOG_ERROR("Config missing required field: event_channels");
            return false;
        }
        eventChannels_ = config["event_channels"].get<std::vector<std::string>>();
        
        // Event filters (optional - if not specified, collect all events)
        if (config.contains("event_filters")) {
            auto& filters = config["event_filters"];
            for (auto& [channel, ids] : filters.items()) {
                std::set<int> idSet;
                for (auto& id : ids) {
                    idSet.insert(id.get<int>());
                }
                eventFilters_[channel] = idSet;
            }
        }
        
        // Log level (optional)
        logLevel_ = config.value("log_level", "info");
        
        // FIM config (optional)
        if (config.contains("fim")) {
            auto& fim = config["fim"];
            fimConfig_.enabled = fim.value("enabled", false);
            if (fim.contains("directories")) {
                fimConfig_.directories = fim["directories"].get<std::vector<std::string>>();
            }
            if (fim.contains("exclude_patterns")) {
                fimConfig_.exclude_patterns = fim["exclude_patterns"].get<std::vector<std::string>>();
            }
            fimConfig_.max_file_size_mb = fim.value("max_file_size_mb", 100);
            fimConfig_.baseline_interval_hours = fim.value("baseline_interval_hours", 24);
            fimConfig_.db_path = fim.value("db_path", "fim_baseline.db");
        }
        
        // System info config (optional)
        if (config.contains("system_info")) {
            auto& sysInfo = config["system_info"];
            sysInfoConfig_.enabled = sysInfo.value("enabled", true);
            sysInfoConfig_.collect_on_startup = sysInfo.value("collect_on_startup", true);
            sysInfoConfig_.collection_interval_hours = sysInfo.value("collection_interval_hours", 24);
            
            if (sysInfo.contains("installed_apps")) {
                auto& apps = sysInfo["installed_apps"];
                sysInfoConfig_.apps.include_install_date = apps.value("include_install_date", true);
                sysInfoConfig_.apps.include_install_location = apps.value("include_install_location", true);
                sysInfoConfig_.apps.include_size = apps.value("include_size", true);
            }
            
            if (sysInfo.contains("open_ports")) {
                auto& ports = sysInfo["open_ports"];
                sysInfoConfig_.ports.include_listening = ports.value("include_listening", true);
                sysInfoConfig_.ports.include_established = ports.value("include_established", true);
                sysInfoConfig_.ports.include_process_info = ports.value("include_process_info", true);
            }
        }
        
        loaded_ = true;
        LOG_INFO("Configuration loaded successfully from: {}", configPath);
        LOG_INFO("Agent ID: {}", agentId_);
        LOG_INFO("Fluent Bit: {}:{}", fluentBitHost_, fluentBitPort_);
        LOG_INFO("Event channels configured: {}", eventChannels_.size());
        LOG_INFO("Critical channels: {}", criticalChannels_.size());
        if (fimConfig_.enabled) {
            LOG_INFO("FIM enabled: {} directories", fimConfig_.directories.size());
        }
        if (sysInfoConfig_.enabled) {
            LOG_INFO("System Info enabled: collect on startup={}, interval={}h", 
                     sysInfoConfig_.collect_on_startup, 
                     sysInfoConfig_.collection_interval_hours);
        }
        
        return true;
        
    } catch (const nlohmann::json::exception& e) {
        LOG_ERROR("JSON parsing error: {}", e.what());
        return false;
    } catch (const std::exception& e) {
        LOG_ERROR("Error loading config: {}", e.what());
        return false;
    }
}

bool ConfigManager::shouldCollectEvent(const std::string& channel, int eventId) const {
    // If no filter is defined for this channel, collect all events
    auto it = eventFilters_.find(channel);
    if (it == eventFilters_.end()) {
        return true;
    }
    
    // Check if eventId is in the allowed set
    return it->second.count(eventId) > 0;
}

} // namespace ResolutePulse
