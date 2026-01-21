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
        
        // Required fields
        if (!config.contains("manager_http_url")) {
            LOG_ERROR("Config missing required field: manager_http_url");
            return false;
        }
        managerHttpUrl_ = config["manager_http_url"].get<std::string>();
        
        if (!config.contains("agent_id")) {
            LOG_ERROR("Config missing required field: agent_id");
            return false;
        }
        agentId_ = config["agent_id"].get<std::string>();
        
        // Auth token (required)
        if (!config.contains("auth_token")) {
            LOG_ERROR("Config missing required field: auth_token");
            return false;
        }
        authToken_ = config["auth_token"].get<std::string>();
        
        // TLS config (optional)
        if (config.contains("tls")) {
            auto& tls = config["tls"];
            tlsConfig_.verify_peer = tls.value("verify_peer", false);
            tlsConfig_.ca_cert_path = tls.value("ca_cert_path", "");
            tlsConfig_.client_cert_path = tls.value("client_cert_path", "");
            tlsConfig_.client_key_path = tls.value("client_key_path", "");
        }
        
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
        
        // OpenSearch config (optional)
        if (config.contains("opensearch")) {
            auto& os = config["opensearch"];
            openSearchConfig_.enabled = os.value("enabled", false);
            openSearchConfig_.url = os.value("url", "");
            openSearchConfig_.indexPrefix = os.value("index_prefix", "windows-logs");
            openSearchConfig_.username = os.value("username", "");
            openSearchConfig_.password = os.value("password", "");
            openSearchConfig_.tlsVerify = os.value("tls_verify", false);
        }
        
        loaded_ = true;
        LOG_INFO("Configuration loaded successfully from: {}", configPath);
        LOG_INFO("Agent ID: {}", agentId_);
        LOG_INFO("Manager URL: {}", managerHttpUrl_);
        LOG_INFO("Event channels configured: {}", eventChannels_.size());
        LOG_INFO("Critical channels: {}", criticalChannels_.size());
        if (fimConfig_.enabled) {
            LOG_INFO("FIM enabled: {} directories", fimConfig_.directories.size());
        }
        if (openSearchConfig_.enabled) {
            LOG_INFO("OpenSearch enabled: {}", openSearchConfig_.url);
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
