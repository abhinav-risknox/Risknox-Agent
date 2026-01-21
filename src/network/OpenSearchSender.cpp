#include "OpenSearchSender.h"
#include "utils/Logger.h"

// Disable OpenSSL in httplib for this file
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
#undef CPPHTTPLIB_OPENSSL_SUPPORT
#endif
#define CPPHTTPLIB_NO_EXCEPTIONS
#include <httplib/httplib.h>
#include <nlohmann/json.hpp>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <regex>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

namespace ResolutePulse {

OpenSearchSender::OpenSearchSender() {
    // Get hostname
    char computerName[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD size = sizeof(computerName);
    if (GetComputerNameA(computerName, &size)) {
        hostname_ = computerName;
    } else {
        hostname_ = "unknown";
    }
}

OpenSearchSender::~OpenSearchSender() = default;

bool OpenSearchSender::initialize(const OpenSearchConfig& config) {
    config_ = config;
    
    if (config_.url.empty()) {
        LOG_ERROR("OpenSearch URL is empty");
        return false;
    }
    
    LOG_INFO("OpenSearch initialized: {}", config_.url);
    LOG_INFO("  Index prefix: {}", config_.indexPrefix);
    LOG_INFO("  Hostname: {}", hostname_);
    
    return true;
}

std::string OpenSearchSender::getIndexName() {
    // Format: windows-logs-2026-01-20 (dashes, not dots - dots break Security Analytics)
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm;
    gmtime_s(&tm, &time);
    
    std::ostringstream ss;
    ss << config_.indexPrefix << "-"
       << std::put_time(&tm, "%Y-%m-%d");
    return ss.str();
}

std::map<std::string, std::string> OpenSearchSender::parseEventXml(const std::string& xml) {
    std::map<std::string, std::string> fields;
    
    // Simple XML parsing for key fields
    // Extract Provider Name
    std::regex providerRegex(R"(Provider Name='([^']*)')");
    std::smatch match;
    if (std::regex_search(xml, match, providerRegex)) {
        fields["provider_name"] = match[1].str();
    }
    
    // Extract Computer
    std::regex computerRegex(R"(<Computer>([^<]*)</Computer>)");
    if (std::regex_search(xml, match, computerRegex)) {
        fields["computer"] = match[1].str();
    }
    
    // Extract EventData fields (common patterns)
    std::regex dataRegex(R"(<Data Name='([^']*)'>([^<]*)</Data>)");
    auto it = xml.cbegin();
    while (std::regex_search(it, xml.cend(), match, dataRegex)) {
        std::string name = match[1].str();
        std::string value = match[2].str();
        fields["event_data." + name] = value;
        it = match.suffix().first;
    }
    
    return fields;
}

std::string OpenSearchSender::formatEvent(const Event& event) {
    nlohmann::json doc;
    
    // Timestamp in ISO format
    doc["@timestamp"] = event.timestamp;
    
    // Event metadata
    doc["event"]["code"] = event.eventId;
    doc["event"]["kind"] = "event";
    
    // Determine event type
    if (event.channel == "FIM") {
        doc["event"]["category"] = "file";
        doc["event"]["type"] = "change";
        
        // FIM events have JSON in xml field
        try {
            auto fimData = nlohmann::json::parse(event.xml);
            doc["file"]["path"] = fimData.value("path", "");
            doc["file"]["hash"]["sha256"] = fimData.value("new_hash", "");
            doc["fim"] = fimData;
        } catch (...) {
            doc["message"] = event.xml;
        }
    } else {
        // Windows Event Log
        doc["event"]["category"] = "process";
        doc["event"]["provider"] = event.channel;
        
        // Winlog format for Sigma compatibility
        doc["winlog"]["channel"] = event.channel;
        doc["winlog"]["event_id"] = event.eventId;
        
        // Parse XML for additional fields
        auto fields = parseEventXml(event.xml);
        
        if (fields.count("provider_name")) {
            doc["winlog"]["provider_name"] = fields["provider_name"];
        }
        if (fields.count("computer")) {
            doc["host"]["name"] = fields["computer"];
        } else {
            doc["host"]["name"] = hostname_;
        }
        
        // Add event_data for Sigma field mapping
        nlohmann::json eventData;
        for (const auto& [key, value] : fields) {
            if (key.find("event_data.") == 0) {
                std::string fieldName = key.substr(11); // Remove "event_data." prefix
                eventData[fieldName] = value;
            }
        }
        if (!eventData.empty()) {
            doc["winlog"]["event_data"] = eventData;
        }
        
        // Store raw XML
        doc["winlog"]["raw"] = event.xml;
    }
    
    // Agent info
    doc["agent"]["type"] = "resolutepulse";
    doc["agent"]["hostname"] = hostname_;
    
    return doc.dump();
}

std::string OpenSearchSender::formatBulkRequest(const std::vector<Event>& events) {
    std::ostringstream ss;
    std::string indexName = getIndexName();
    
    for (const auto& event : events) {
        // Action line
        nlohmann::json action;
        action["index"]["_index"] = indexName;
        ss << action.dump() << "\n";
        
        // Document line
        ss << formatEvent(event) << "\n";
    }
    
    return ss.str();
}

bool OpenSearchSender::sendBulk(const std::vector<Event>& events) {
    if (events.empty()) {
        return true;
    }
    
    // Parse URL
    std::string host;
    int port = 9200;
    bool useHttps = false;
    
    std::string url = config_.url;
    if (url.find("https://") == 0) {
        useHttps = true;
        url = url.substr(8);
    } else if (url.find("http://") == 0) {
        url = url.substr(7);
    }
    
    auto colonPos = url.find(':');
    if (colonPos != std::string::npos) {
        host = url.substr(0, colonPos);
        port = std::stoi(url.substr(colonPos + 1));
    } else {
        host = url;
    }
    
    // Format bulk request
    std::string body = formatBulkRequest(events);
    
    try {
        httplib::Client client(host, port);
        client.set_connection_timeout(config_.timeoutSec);
        client.set_read_timeout(config_.timeoutSec);
        
        // Set basic auth if configured
        if (!config_.username.empty()) {
            client.set_basic_auth(config_.username, config_.password);
        }
        
        // Send bulk request
        auto res = client.Post("/_bulk", body, "application/x-ndjson");
        
        if (!res) {
            LOG_ERROR("OpenSearch request failed: no response");
            failedBatches_++;
            return false;
        }
        
        if (res->status >= 200 && res->status < 300) {
            // Check for individual item errors
            try {
                auto response = nlohmann::json::parse(res->body);
                if (response.value("errors", false)) {
                    LOG_WARN("OpenSearch bulk had some errors");
                    // Still count as success for successfully indexed items
                }
            } catch (...) {
                // Ignore parsing errors
            }
            
            eventsSent_ += events.size();
            LOG_DEBUG("OpenSearch: indexed {} events", events.size());
            return true;
        } else {
            LOG_ERROR("OpenSearch error {}: {}", res->status, res->body.substr(0, 200));
            failedBatches_++;
            return false;
        }
    } catch (const std::exception& e) {
        LOG_ERROR("OpenSearch exception: {}", e.what());
        failedBatches_++;
        return false;
    }
}

bool OpenSearchSender::isHealthy() {
    std::string host;
    int port = 9200;
    
    std::string url = config_.url;
    if (url.find("https://") == 0) {
        url = url.substr(8);
    } else if (url.find("http://") == 0) {
        url = url.substr(7);
    }
    
    auto colonPos = url.find(':');
    if (colonPos != std::string::npos) {
        host = url.substr(0, colonPos);
        port = std::stoi(url.substr(colonPos + 1));
    } else {
        host = url;
    }
    
    try {
        httplib::Client client(host, port);
        client.set_connection_timeout(5);
        
        if (!config_.username.empty()) {
            client.set_basic_auth(config_.username, config_.password);
        }
        
        auto res = client.Get("/_cluster/health");
        return res && res->status == 200;
    } catch (...) {
        return false;
    }
}

} // namespace ResolutePulse
