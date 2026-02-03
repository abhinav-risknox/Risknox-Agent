#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <nlohmann/json.hpp>

namespace ResolutePulse {

// Forward declaration for binary protocol
struct BinaryMessage;

struct Event {
    int64_t id = 0;             // Database ID (for buffer operations)
    std::string channel;        // Event log channel name
    uint32_t eventId = 0;       // Windows Event ID
    std::string timestamp;      // ISO 8601 timestamp
    std::string xml;            // Decoded XML content (UTF-8)
    
    // Convert to JSON for HTTP transmission
    nlohmann::json toJson() const {
        return nlohmann::json{
            {"channel", channel},
            {"event_id", eventId},
            {"timestamp", timestamp},
            {"xml", xml}
        };
    }
    
    // Convert to binary message (efficient transmission)
    BinaryMessage toBinary(const std::string& agentId) const;
    
    // Create from JSON (for deserialization)
    static Event fromJson(const nlohmann::json& j) {
        Event e;
        e.channel = j.value("channel", "");
        e.eventId = j.value("event_id", 0);
        e.timestamp = j.value("timestamp", "");
        e.xml = j.value("xml", "");
        return e;
    }
    
    // Create batch payload for HTTP POST
    static nlohmann::json createBatchPayload(const std::string& agentId,
                                             const std::vector<Event>& events) {
        nlohmann::json payload;
        payload["agent_id"] = agentId;
        payload["batch_timestamp"] = ""; // Will be set by caller if needed
        payload["event_count"] = events.size();
        
        nlohmann::json eventsArray = nlohmann::json::array();
        for (const auto& event : events) {
            eventsArray.push_back(event.toJson());
        }
        payload["events"] = eventsArray;
        
        return payload;
    }
};

} // namespace ResolutePulse
