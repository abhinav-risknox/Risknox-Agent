#pragma once

#include "Event.h"
#include <functional>
#include <vector>
#include <map>
#include <set>
#include <string>
#include <atomic>
#include <thread>
#include <mutex>

#include <Windows.h>
#include <winevt.h>

namespace ResolutePulse {

class EventCollector {
public:
    using EventCallback = std::function<void(Event&&)>;
    
    EventCollector();
    ~EventCollector();
    
    // Initialize the collector with channels and filters
    // @param channels - List of channel names to subscribe to
    // @param filters - Map of channel name to allowed event IDs (empty set = all events)
    bool initialize(
        const std::vector<std::string>& channels,
        const std::map<std::string, std::set<int>>& filters
    );
    
    // Set callback for received events
    void setEventCallback(EventCallback callback);
    
    // Start collecting events
    bool start();
    
    // Stop collecting events
    void stop();
    
    // Check if collector is running
    bool isRunning() const { return running_.load(); }
    
    // Get statistics
    uint64_t getEventsCollected() const { return eventsCollected_.load(); }
    uint64_t getEventsFiltered() const { return eventsFiltered_.load(); }
    
private:
    // Windows event callback (static for Windows API)
    static DWORD WINAPI subscriptionCallback(
        EVT_SUBSCRIBE_NOTIFY_ACTION action,
        PVOID pContext,
        EVT_HANDLE hEvent
    );
    
    // Process a single event
    void processEvent(EVT_HANDLE hEvent);
    
    // Extract event ID from XML
    bool extractEventInfo(EVT_HANDLE hEvent, uint32_t& eventId, std::string& timestamp);
    
    // Get ISO 8601 timestamp
    static std::string getCurrentTimestamp();
    
    // Convert wide string to UTF-8
    static std::string wideToUtf8(const std::wstring& wide);
    static std::wstring utf8ToWide(const std::string& utf8);
    
    // Subscription context for callback
    struct SubscriptionContext {
        EventCollector* collector;
        std::string channelName;
    };
    
    std::vector<std::string> channels_;
    std::map<std::string, std::set<int>> filters_;
    std::vector<EVT_HANDLE> subscriptions_;
    std::vector<std::unique_ptr<SubscriptionContext>> contexts_;
    
    EventCallback callback_;
    std::mutex callbackMutex_;
    
    std::atomic<bool> running_{false};
    std::atomic<uint64_t> eventsCollected_{0};
    std::atomic<uint64_t> eventsFiltered_{0};
};

} // namespace ResolutePulse
