#pragma once

#include <string>
#include <vector>

namespace ResolutePulse {

struct DiscoveryResult {
    std::vector<std::string> available;           // Channels that exist on this system
    std::vector<std::string> missing_optional;    // Optional channels not found
    std::vector<std::string> missing_critical;    // Critical channels not found (startup should fail)
};

class ChannelDiscovery {
public:
    // Discover which channels are available on this system
    // @param desiredChannels - List of channels we want to subscribe to
    // @param criticalChannels - Channels that MUST exist (startup fails if missing)
    // @return DiscoveryResult with categorized channels
    static DiscoveryResult discover(
        const std::vector<std::string>& desiredChannels,
        const std::vector<std::string>& criticalChannels
    );
    
    // Check if a specific channel exists
    static bool channelExists(const std::string& channelName);
    
    // Get all available channels on the system (for diagnostics)
    static std::vector<std::string> getAllSystemChannels();
};

} // namespace ResolutePulse
