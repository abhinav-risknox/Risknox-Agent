#include "EventCollector.h"
#include "utils/Logger.h"
#include <sstream>
#include <iomanip>
#include <chrono>
#include <ctime>
#include <cstdint>

#pragma comment(lib, "wevtapi.lib")

namespace ResolutePulse {

EventCollector::EventCollector() = default;

EventCollector::~EventCollector() {
    stop();
}

bool EventCollector::initialize(
    const std::vector<std::string>& channels,
    const std::map<std::string, std::set<int>>& filters) 
{
    channels_ = channels;
    filters_ = filters;
    
    LOG_INFO("EventCollector initialized with {} channels", channels_.size());
    for (const auto& channel : channels_) {
        auto it = filters_.find(channel);
        if (it != filters_.end()) {
            LOG_DEBUG("  Channel '{}': {} event ID filters", channel, it->second.size());
        } else {
            LOG_DEBUG("  Channel '{}': all events (no filter)", channel);
        }
    }
    
    return true;
}

void EventCollector::setEventCallback(EventCallback callback) {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    callback_ = std::move(callback);
}

bool EventCollector::start() {
    if (running_.load()) {
        LOG_WARN("EventCollector already running");
        return true;
    }
    
    LOG_INFO("Starting event collection...");
    
    // Create subscriptions for each channel
    for (const auto& channel : channels_) {
        std::wstring wideChannel = utf8ToWide(channel);
        
        // Create context for this subscription
        auto context = std::make_unique<SubscriptionContext>();
        context->collector = this;
        context->channelName = channel;
        
        // Subscribe to future events
        EVT_HANDLE hSubscription = EvtSubscribe(
            nullptr,                        // Local computer
            nullptr,                        // No signal event
            wideChannel.c_str(),            // Channel path
            L"*",                           // Query (all events, we filter ourselves)
            nullptr,                        // No bookmark
            context.get(),                  // Context passed to callback
            subscriptionCallback,           // Callback function
            EvtSubscribeToFutureEvents      // Only future events
        );
        
        if (hSubscription == nullptr) {
            DWORD error = GetLastError();
            LOG_ERROR("Failed to subscribe to channel '{}': error {}", channel, error);
            // Continue with other channels instead of failing completely
            continue;
        }
        
        LOG_INFO("Subscribed to channel: {}", channel);
        subscriptions_.push_back(hSubscription);
        contexts_.push_back(std::move(context));
    }
    
    if (subscriptions_.empty()) {
        LOG_ERROR("No channels were successfully subscribed");
        return false;
    }
    
    running_ = true;
    LOG_INFO("Event collection started with {} active subscriptions", subscriptions_.size());
    
    return true;
}

void EventCollector::stop() {
    if (!running_.load()) {
        return;
    }
    
    LOG_INFO("Stopping event collection...");
    running_ = false;
    
    // Close all subscriptions
    for (auto& hSubscription : subscriptions_) {
        if (hSubscription != nullptr) {
            EvtClose(hSubscription);
        }
    }
    subscriptions_.clear();
    contexts_.clear();
    
    LOG_INFO("Event collection stopped. Collected: {}, Filtered: {}", 
             eventsCollected_.load(), eventsFiltered_.load());
}

DWORD WINAPI EventCollector::subscriptionCallback(
    EVT_SUBSCRIBE_NOTIFY_ACTION action,
    PVOID pContext,
    EVT_HANDLE hEvent) 
{
    auto* context = static_cast<SubscriptionContext*>(pContext);
    
    if (action == EvtSubscribeActionError) {
        DWORD error = static_cast<DWORD>(reinterpret_cast<uintptr_t>(hEvent));
        LOG_ERROR("Subscription error for channel '{}': {}", 
                  context->channelName, error);
        return ERROR_SUCCESS;
    }
    
    if (action == EvtSubscribeActionDeliver) {
        context->collector->processEvent(hEvent);
    }
    
    return ERROR_SUCCESS;
}

void EventCollector::processEvent(EVT_HANDLE hEvent) {
    // Extract event info (ID and timestamp)
    uint32_t eventId = 0;
    std::string timestamp;
    std::string channelName;
    
    // First, get the XML to extract event info
    DWORD bufferSize = 0;
    DWORD bufferUsed = 0;
    DWORD propertyCount = 0;
    
    // Get required buffer size
    EvtRender(nullptr, hEvent, EvtRenderEventXml, 0, nullptr, &bufferUsed, &propertyCount);
    
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
        LOG_ERROR("EvtRender failed to get buffer size: {}", GetLastError());
        return;
    }
    
    bufferSize = bufferUsed;
    std::vector<wchar_t> xmlBuffer(bufferSize / sizeof(wchar_t) + 1);
    
    if (!EvtRender(nullptr, hEvent, EvtRenderEventXml, bufferSize, 
                   xmlBuffer.data(), &bufferUsed, &propertyCount)) {
        LOG_ERROR("EvtRender failed: {}", GetLastError());
        return;
    }
    
    std::wstring xmlWide(xmlBuffer.data());
    
    // Parse event ID and channel from XML (simple extraction)
    // Look for <EventID>xxx</EventID>
    size_t eventIdStart = xmlWide.find(L"<EventID");
    if (eventIdStart != std::wstring::npos) {
        size_t valueStart = xmlWide.find(L'>', eventIdStart) + 1;
        size_t valueEnd = xmlWide.find(L'<', valueStart);
        if (valueStart != std::wstring::npos && valueEnd != std::wstring::npos) {
            std::wstring eventIdStr = xmlWide.substr(valueStart, valueEnd - valueStart);
            eventId = static_cast<uint32_t>(std::stoul(eventIdStr));
        }
    }
    
    // Extract channel name
    size_t channelStart = xmlWide.find(L"<Channel>");
    if (channelStart != std::wstring::npos) {
        channelStart += 9; // Length of "<Channel>"
        size_t channelEnd = xmlWide.find(L"</Channel>", channelStart);
        if (channelEnd != std::wstring::npos) {
            channelName = wideToUtf8(xmlWide.substr(channelStart, channelEnd - channelStart));
        }
    }
    
    // Check filter
    auto filterIt = filters_.find(channelName);
    if (filterIt != filters_.end() && !filterIt->second.empty()) {
        if (filterIt->second.count(eventId) == 0) {
            eventsFiltered_++;
            return; // Event not in allowed list
        }
    }
    
    // Convert the wide XML string to UTF-8 for transmission
    std::string xmlUtf8 = wideToUtf8(xmlWide);
    
    // Create event with decoded XML
    Event event;
    event.channel = channelName;
    event.eventId = eventId;
    event.timestamp = getCurrentTimestamp();
    event.xml = std::move(xmlUtf8);
    
    eventsCollected_++;
    
    // Call the callback
    {
        std::lock_guard<std::mutex> lock(callbackMutex_);
        if (callback_) {
            callback_(std::move(event));
        }
    }
}

std::string EventCollector::getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::tm tm;
    gmtime_s(&tm, &time);
    
    std::ostringstream ss;
    ss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");
    ss << '.' << std::setfill('0') << std::setw(3) << ms.count() << 'Z';
    
    return ss.str();
}

std::string EventCollector::wideToUtf8(const std::wstring& wide) {
    if (wide.empty()) return std::string();
    
    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), 
                                       static_cast<int>(wide.length()),
                                       nullptr, 0, nullptr, nullptr);
    if (utf8Len <= 0) return std::string();
    
    std::string utf8(utf8Len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), 
                        static_cast<int>(wide.length()),
                        &utf8[0], utf8Len, nullptr, nullptr);
    
    return utf8;
}

std::wstring EventCollector::utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) return std::wstring();
    
    int wideLen = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), 
                                       static_cast<int>(utf8.length()),
                                       nullptr, 0);
    if (wideLen <= 0) return std::wstring();
    
    std::wstring wide(wideLen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), 
                        static_cast<int>(utf8.length()),
                        &wide[0], wideLen);
    
    return wide;
}

} // namespace ResolutePulse
