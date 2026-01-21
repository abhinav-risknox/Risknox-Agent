#pragma once

#include "queue/EventQueue.h"
#include "network/HttpSender.h"
#include "network/OpenSearchSender.h"
#include "buffer/EventBuffer.h"
#include <thread>
#include <atomic>
#include <chrono>

namespace ResolutePulse {

class BatchSender {
public:
    BatchSender(EventQueue& queue,
               HttpSender& sender,
               EventBuffer& buffer,
               const std::string& agentId,
               size_t batchSize,
               int flushIntervalSec);
    
    ~BatchSender();
    
    // Start the sender thread
    void start();
    
    // Stop the sender thread
    void stop();
    
    // Set optional OpenSearch sender (call before start)
    void setOpenSearchSender(OpenSearchSender* ossSender);
    
    // Check if running
    bool isRunning() const { return running_.load(); }
    
    // Get statistics
    uint64_t getBatchesSent() const { return batchesSent_.load(); }
    uint64_t getEventsSent() const { return eventsSent_.load(); }
    uint64_t getEventsBuffered() const { return eventsBuffered_.load(); }
    uint64_t getOpenSearchEventsSent() const { return openSearchEventsSent_.load(); }
    
private:
    // Main sender loop
    void senderLoop();
    
    // Try to drain buffered events
    void drainBuffer();
    
    // Send batch to OpenSearch if configured
    void sendToOpenSearch(const std::vector<Event>& events);
    
    EventQueue& queue_;
    HttpSender& sender_;
    EventBuffer& buffer_;
    std::string agentId_;
    size_t batchSize_;
    std::chrono::seconds flushInterval_;
    
    OpenSearchSender* openSearchSender_ = nullptr;
    
    std::thread senderThread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stopRequested_{false};
    
    std::atomic<uint64_t> batchesSent_{0};
    std::atomic<uint64_t> eventsSent_{0};
    std::atomic<uint64_t> eventsBuffered_{0};
    std::atomic<uint64_t> openSearchEventsSent_{0};
    
    // Retry configuration
    static constexpr int MAX_RETRIES = 3;
    static constexpr int RETRY_DELAY_MS = 1000;
    static constexpr int BUFFER_DRAIN_INTERVAL_SEC = 60;
};

} // namespace ResolutePulse
