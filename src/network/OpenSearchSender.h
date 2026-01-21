#pragma once

#include <string>
#include <vector>
#include "collector/Event.h"

namespace ResolutePulse {

struct OpenSearchConfig {
    bool enabled = false;
    std::string url;
    std::string indexPrefix = "windows-logs";
    std::string username;
    std::string password;
    bool tlsVerify = false;
    int batchSize = 100;
    int timeoutSec = 30;
};

class OpenSearchSender {
public:
    OpenSearchSender();
    ~OpenSearchSender();
    
    // Initialize with configuration
    bool initialize(const OpenSearchConfig& config);
    
    // Send events in bulk
    // Returns true if all events were indexed successfully
    bool sendBulk(const std::vector<Event>& events);
    
    // Check if OpenSearch is reachable
    bool isHealthy();
    
    // Get statistics
    size_t getEventsSent() const { return eventsSent_; }
    size_t getFailedBatches() const { return failedBatches_; }
    
private:
    // Format events as NDJSON for _bulk API
    std::string formatBulkRequest(const std::vector<Event>& events);
    
    // Format single event to OpenSearch-compatible JSON
    std::string formatEvent(const Event& event);
    
    // Get index name with date suffix
    std::string getIndexName();
    
    // Parse event XML to extract fields
    std::map<std::string, std::string> parseEventXml(const std::string& xml);
    
    OpenSearchConfig config_;
    std::string hostname_;
    size_t eventsSent_ = 0;
    size_t failedBatches_ = 0;
};

} // namespace ResolutePulse
