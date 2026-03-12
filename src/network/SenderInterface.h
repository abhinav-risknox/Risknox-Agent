#pragma once

#include "collector/Event.h"
#include <string>
#include <vector>

namespace ResolutePulse {

enum class SendResult {
    Success,        // Events sent successfully
    NetworkError,   // Network/connection error (should retry)
    ServerError,    // Server error (should retry)
    ClientError,    // Client error (don't retry)
    AuthError       // Authentication failed
};

class SenderInterface {
public:
    virtual ~SenderInterface() = default;

    // Send a batch of events
    // @param events - Events to send
    // @return SendResult indicating success or type of failure
    virtual SendResult sendBatch(const std::vector<Event>& events) = 0;

    // Send a heartbeat to the server
    // @param heartbeat - Heartbeat details
    // @return SendResult indicating success or type of failure
    virtual SendResult sendHeartbeat(const std::string& agentId, uint64_t eventsCollected, uint64_t eventsSent) {
        return SendResult::ClientError; // Default for senders that don't support heartbeats
    }

    // Get last error message
    virtual const std::string& getLastError() const = 0;
    
    // Check if connected
    virtual bool isConnected() const = 0;
};

} // namespace ResolutePulse
