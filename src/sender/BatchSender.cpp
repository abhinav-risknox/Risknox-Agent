#include "BatchSender.h"
#include "utils/Logger.h"
#include "network/TcpSender.h"

namespace ResolutePulse {

BatchSender::BatchSender(EventQueue& queue,
                        SenderInterface& sender,
                        EventBuffer& buffer,
                        const std::string& agentId,
                        size_t batchSize,
                        int flushIntervalSec)
    : queue_(queue)
    , sender_(sender)
    , buffer_(buffer)
    , agentId_(agentId)
    , batchSize_(batchSize)
    , flushInterval_(flushIntervalSec)
{
}

BatchSender::~BatchSender() {
    stop();
}

void BatchSender::start() {
    if (running_.load()) {
        LOG_WARN("BatchSender already running");
        return;
    }
    
    stopRequested_ = false;
    running_ = true;

    // Clear the TCP sender's stop flag so reconnects work after a license-driven restart
    auto* tcpSender = dynamic_cast<TcpSender*>(&sender_);
    if (tcpSender) {
        tcpSender->resetStop();
    }

    senderThread_ = std::thread(&BatchSender::senderLoop, this);
    
    LOG_INFO("BatchSender started (batch size: {}, flush interval: {}s)", 
             batchSize_, flushInterval_.count());
}

void BatchSender::stop() {
    if (!running_.load()) {
        return;
    }
    
    LOG_INFO("Stopping BatchSender...");
    stopRequested_ = true;
    
    // Signal the underlying TCP sender to abort any pending connection attempts
    auto* tcpSender = dynamic_cast<TcpSender*>(&sender_);
    if (tcpSender) {
        tcpSender->requestStop();
    }
    
    queue_.shutdown();
    
    if (senderThread_.joinable()) {
        senderThread_.join();
    }
    
    running_ = false;
    LOG_INFO("BatchSender stopped. Batches: {}, Events: {}, Buffered: {}", 
             batchesSent_.load(), eventsSent_.load(), eventsBuffered_.load());
}

void BatchSender::senderLoop() {
    LOG_DEBUG("BatchSender loop started");
    
    auto lastBufferDrain = std::chrono::steady_clock::now();
    
    while (!stopRequested_.load()) {
        // Try to drain buffered events periodically
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - lastBufferDrain).count() 
            >= BUFFER_DRAIN_INTERVAL_SEC) {
            drainBuffer();
            lastBufferDrain = now;
        }
        
        // Pop a batch of events (blocks up to flush interval)
        auto events = queue_.popBatch(
            batchSize_,
            std::chrono::duration_cast<std::chrono::milliseconds>(flushInterval_)
        );
        
        if (events.empty()) {
            continue;
        }
        
        LOG_DEBUG("Processing batch of {} events", events.size());
        
        // Try to send with retries
        bool sent = false;
        for (int retry = 0; retry < MAX_RETRIES && !stopRequested_.load(); ++retry) {
            if (retry > 0) {
                LOG_DEBUG("Retry {} of {}", retry, MAX_RETRIES);
                // Interruptible sleep: wake up every 100ms to check stop flag
                int delayMs = RETRY_DELAY_MS * retry;
                for (int elapsed = 0; elapsed < delayMs && !stopRequested_.load(); elapsed += 100) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
                if (stopRequested_.load()) break;
            }
            
            SendResult result = sender_.sendBatch(events);
            
            switch (result) {
                case SendResult::Success:
                    batchesSent_++;
                    eventsSent_ += events.size();
                    sent = true;
                    break;
                    
                case SendResult::AuthError:
                    // Don't retry auth errors
                    LOG_ERROR("Authentication error, not retrying");
                    retry = MAX_RETRIES;  // Exit retry loop
                    break;
                    
                case SendResult::ClientError:
                    // Don't retry client errors (4xx except auth)
                    LOG_ERROR("Client error, not retrying");
                    retry = MAX_RETRIES;
                    break;
                    
                case SendResult::ServerError:
                case SendResult::NetworkError:
                    // Retry these
                    LOG_WARN("Send failed, will retry: {}", sender_.getLastError());
                    break;
            }
            
            if (sent) break;
        }
        
        // If still not sent, buffer the events
        if (!sent) {
            LOG_WARN("Failed to send batch after {} retries, buffering {} events", 
                     MAX_RETRIES, events.size());
            
            if (buffer_.addEvents(events)) {
                eventsBuffered_ += events.size();
            } else {
                LOG_ERROR("Failed to buffer events, data loss occurred");
            }
        }
    }
    
    // Final flush: save remaining events (buffer to SQLite if server is unreachable)
    LOG_DEBUG("Final flush of remaining events");
    while (!queue_.empty()) {
        auto events = queue_.popBatch(batchSize_, std::chrono::milliseconds(100));
        if (events.empty()) break;
        
        // Only attempt network send if we're still connected;
        // don't waste time on reconnection attempts during shutdown
        bool sent = false;
        if (sender_.isConnected()) {
            SendResult result = sender_.sendBatch(events);
            if (result == SendResult::Success) {
                batchesSent_++;
                eventsSent_ += events.size();
                sent = true;
            }
        }
        
        if (!sent) {
            LOG_WARN("Final flush: buffering {} events to disk", events.size());
            buffer_.addEvents(events);
            eventsBuffered_ += events.size();
        }
    }
    
    LOG_DEBUG("BatchSender loop ended");
}

void BatchSender::drainBuffer() {
    size_t bufferCount = buffer_.getEventCount();
    if (bufferCount == 0) {
        return;
    }
    
    LOG_INFO("Attempting to drain {} buffered events", bufferCount);
    
    while (!stopRequested_.load()) {
        auto events = buffer_.getEvents(batchSize_);
        if (events.empty()) {
            break;
        }
        
        SendResult result = sender_.sendBatch(events);
        
        if (result == SendResult::Success) {
            // Remove sent events from buffer
            std::vector<int64_t> ids;
            ids.reserve(events.size());
            for (const auto& event : events) {
                ids.push_back(event.id);
            }
            
            if (buffer_.removeEvents(ids)) {
                eventsBuffered_ -= events.size();
                eventsSent_ += events.size();
                batchesSent_++;
                LOG_DEBUG("Drained {} events from buffer", events.size());
            }
        } else {
            // Server still down, stop trying
            LOG_DEBUG("Buffer drain failed, will retry later");
            break;
        }
    }
}

} // namespace ResolutePulse
