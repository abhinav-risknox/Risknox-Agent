#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include <windows.h>

namespace ResolutePulse {

struct InstalledApp {
    std::string name;
    std::string version;
    std::string publisher;
    std::string installDate;
    std::string installLocation;
    uint64_t sizeMB = 0;
    
    nlohmann::json toJson() const {
        nlohmann::json j;
        j["name"] = name;
        if (!version.empty()) j["version"] = version;
        if (!publisher.empty()) j["publisher"] = publisher;
        if (!installDate.empty()) j["install_date"] = installDate;
        if (!installLocation.empty()) j["install_location"] = installLocation;
        if (sizeMB > 0) j["size_mb"] = sizeMB;
        return j;
    }
};

class InstalledAppsCollector {
public:
    InstalledAppsCollector();
    ~InstalledAppsCollector() = default;
    
    // Initialize the collector
    bool initialize();
    
    // Collect installed applications
    nlohmann::json collect();
    
private:
    // Enumerate apps from a registry key
    void enumerateApps(HKEY rootKey, const std::wstring& subKey, std::vector<InstalledApp>& apps);
    
    // Read app info from a specific registry key
    bool readAppInfo(HKEY appKey, InstalledApp& app);
    
    // Helper to read string value from registry
    std::string readRegistryString(HKEY key, const std::wstring& valueName);
    
    // Helper to read DWORD value from registry
    DWORD readRegistryDWORD(HKEY key, const std::wstring& valueName);
    
    // Convert wide string to UTF-8 string
    std::string wideToUtf8(const std::wstring& wstr);
    
    // Convert UTF-8 to wide string
    std::wstring utf8ToWide(const std::string& str);
};

} // namespace ResolutePulse
