#include "ChannelDiscovery.h"
#include "utils/Logger.h"

#include <Windows.h>
#include <winevt.h>
#include <algorithm>
#include <set>

#pragma comment(lib, "wevtapi.lib")

namespace ResolutePulse {

bool ChannelDiscovery::channelExists(const std::string& channelName) {
    // Convert to wide string for Windows API
    int wideLen = MultiByteToWideChar(CP_UTF8, 0, channelName.c_str(), -1, nullptr, 0);
    if (wideLen <= 0) {
        return false;
    }
    
    std::wstring wideChannel(wideLen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, channelName.c_str(), -1, &wideChannel[0], wideLen);
    
    // Try to open the channel configuration
    EVT_HANDLE hChannel = EvtOpenChannelConfig(nullptr, wideChannel.c_str(), 0);
    
    if (hChannel != nullptr) {
        EvtClose(hChannel);
        return true;
    }
    
    // Channel doesn't exist or we don't have access
    DWORD error = GetLastError();
    if (error == ERROR_EVT_CHANNEL_NOT_FOUND) {
        return false;
    }
    
    // Other errors might mean the channel exists but we can't access it
    // Log and treat as not available
    LOG_DEBUG("EvtOpenChannelConfig failed for '{}': error {}", channelName, error);
    return false;
}

DiscoveryResult ChannelDiscovery::discover(
    const std::vector<std::string>& desiredChannels,
    const std::vector<std::string>& criticalChannels) 
{
    DiscoveryResult result;
    
    // Build a set of critical channel names for fast lookup
    std::set<std::string> criticalSet(criticalChannels.begin(), criticalChannels.end());
    
    LOG_INFO("Starting channel discovery for {} channels...", desiredChannels.size());
    
    for (const auto& channel : desiredChannels) {
        bool exists = channelExists(channel);
        bool isCritical = criticalSet.count(channel) > 0;
        
        if (exists) {
            result.available.push_back(channel);
            LOG_INFO("  [OK] {}", channel);
        } else {
            if (isCritical) {
                result.missing_critical.push_back(channel);
                LOG_ERROR("  [CRITICAL MISSING] {}", channel);
            } else {
                result.missing_optional.push_back(channel);
                LOG_WARN("  [OPTIONAL MISSING] {}", channel);
            }
        }
    }
    
    LOG_INFO("Channel discovery complete:");
    LOG_INFO("  Available: {}", result.available.size());
    LOG_INFO("  Missing optional: {}", result.missing_optional.size());
    LOG_INFO("  Missing critical: {}", result.missing_critical.size());
    
    return result;
}

std::vector<std::string> ChannelDiscovery::getAllSystemChannels() {
    std::vector<std::string> channels;
    
    EVT_HANDLE hChannelEnum = EvtOpenChannelEnum(nullptr, 0);
    if (hChannelEnum == nullptr) {
        LOG_ERROR("EvtOpenChannelEnum failed: {}", GetLastError());
        return channels;
    }
    
    WCHAR buffer[512];
    DWORD bufferUsed = 0;
    
    while (EvtNextChannelPath(hChannelEnum, 512, buffer, &bufferUsed)) {
        // Convert wide string to UTF-8
        int utf8Len = WideCharToMultiByte(CP_UTF8, 0, buffer, -1, nullptr, 0, nullptr, nullptr);
        if (utf8Len > 0) {
            std::string channel(utf8Len, '\0');
            WideCharToMultiByte(CP_UTF8, 0, buffer, -1, &channel[0], utf8Len, nullptr, nullptr);
            // Remove null terminator
            if (!channel.empty() && channel.back() == '\0') {
                channel.pop_back();
            }
            channels.push_back(channel);
        }
    }
    
    DWORD error = GetLastError();
    if (error != ERROR_NO_MORE_ITEMS) {
        LOG_WARN("EvtNextChannelPath ended with error: {}", error);
    }
    
    EvtClose(hChannelEnum);
    
    return channels;
}

} // namespace ResolutePulse
