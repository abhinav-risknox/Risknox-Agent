#pragma once

#include "collector/Event.h"
#include <deque>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <vector>
#include <atomic>

namespace ResolutePulse {

class EventQueue {
public:
    EventQueue(size_t maxSize = 100000);
    ~EventQueue() = default;
    
    // Push an event to the queue
    // Returns false if queue is full and event was dropped
    bool push(Event&& event);
    
    // Pop a batch of events (blocks until events available or timeout)
    // @param maxCount - Maximum number of events to return
    // @param timeout - Maximum time to wait for events
    // @return Vector of events (may be empty if timeout)
    std::vector<Event> popBatch(size_t maxCount, 
                                std::chrono::milliseconds timeout);
    
    // Get current queue size
    size_t size() const;
    
    // Check if queue is empty
    bool empty() const;
    
    // Signal shutdown (unblocks waiting popBatch calls)
    void shutdown();
    
    // Get statistics
    uint64_t getTotalPushed() const { return totalPushed_.load(); }
    uint64_t getTotalPopped() const { return totalPopped_.load(); }
    uint64_t getTotalDropped() const { return totalDropped_.load(); }
    
private:
    std::deque<Event> queue_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    size_t maxSize_;
    std::atomic<bool> shutdown_{false};
    
    std::atomic<uint64_t> totalPushed_{0};
    std::atomic<uint64_t> totalPopped_{0};
    std::atomic<uint64_t> totalDropped_{0};
};

} // namespace ResolutePulse
