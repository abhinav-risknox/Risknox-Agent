#include "EventQueue.h"
#include "utils/Logger.h"

namespace ResolutePulse {

EventQueue::EventQueue(size_t maxSize) 
    : maxSize_(maxSize) 
{
}

bool EventQueue::push(Event&& event) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (queue_.size() >= maxSize_) {
            totalDropped_++;
            LOG_WARN("Event queue full, dropping event. Total dropped: {}", 
                     totalDropped_.load());
            return false;
        }
        
        queue_.push_back(std::move(event));
        totalPushed_++;
    }
    
    cv_.notify_one();
    return true;
}

std::vector<Event> EventQueue::popBatch(size_t maxCount, 
                                         std::chrono::milliseconds timeout) {
    std::vector<Event> batch;
    batch.reserve(maxCount);
    
    std::unique_lock<std::mutex> lock(mutex_);
    
    // Wait for events or timeout
    if (queue_.empty()) {
        cv_.wait_for(lock, timeout, [this] {
            return !queue_.empty() || shutdown_.load();
        });
    }
    
    if (shutdown_.load() && queue_.empty()) {
        return batch;
    }
    
    // Pop up to maxCount events
    size_t count = std::min(maxCount, queue_.size());
    for (size_t i = 0; i < count; ++i) {
        batch.push_back(std::move(queue_.front()));
        queue_.pop_front();
    }
    
    totalPopped_ += batch.size();
    
    return batch;
}

size_t EventQueue::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
}

bool EventQueue::empty() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.empty();
}

void EventQueue::shutdown() {
    shutdown_ = true;
    cv_.notify_all();
}

} // namespace ResolutePulse
