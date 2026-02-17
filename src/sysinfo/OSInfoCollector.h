#pragma once

#include <string>
#include <nlohmann/json.hpp>

namespace ResolutePulse {

class OSInfoCollector {
public:
    OSInfoCollector();
    ~OSInfoCollector() = default;
    
    // Initialize the collector
    bool initialize();
    
    // Collect OS information
    nlohmann::json collect();
    
private:
    // Get Windows version information
    bool getWindowsVersion(std::string& osName, std::string& version, std::string& build);
    
    // Get computer name/hostname
    std::string getHostname();
    
    // Get architecture (x86/x64)
    std::string getArchitecture();
    
    // Get domain or workgroup name
    std::string getDomainInfo();
    
    // Get system uptime in seconds
    uint64_t getUptime();
    
    // Get system manufacturer and model via WMI
    bool getSystemInfo(std::string& manufacturer, std::string& model);
};

} // namespace ResolutePulse
