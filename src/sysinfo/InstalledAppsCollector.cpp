#include "InstalledAppsCollector.h"
#include "utils/Logger.h"
#include <windows.h>
#include <sstream>
#include <set>

namespace ResolutePulse {

InstalledAppsCollector::InstalledAppsCollector() = default;

bool InstalledAppsCollector::initialize() {
    LOG_DEBUG("Initializing Installed Apps Collector");
    return true;
}

nlohmann::json InstalledAppsCollector::collect() {
    LOG_DEBUG("Collecting installed applications");
    
    std::vector<InstalledApp> apps;
    std::set<std::string> seenApps;  // To avoid duplicates
    
    try {
        // Collect from HKEY_LOCAL_MACHINE for all users (64-bit apps)
        enumerateApps(HKEY_LOCAL_MACHINE, 
                     L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall", 
                     apps);
        
        // Collect from HKEY_LOCAL_MACHINE for 32-bit apps on 64-bit systems
        enumerateApps(HKEY_LOCAL_MACHINE, 
                     L"SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall", 
                     apps);
        
        // Collect from HKEY_CURRENT_USER
        enumerateApps(HKEY_CURRENT_USER, 
                     L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall", 
                     apps);
        
        LOG_INFO("Collected {} installed applications", apps.size());
    } catch (const std::exception& e) {
        LOG_ERROR("Error collecting installed apps: {}", e.what());
    }
    
    // Convert to JSON array
    nlohmann::json appsArray = nlohmann::json::array();
    for (const auto& app : apps) {
        // Filter out apps with no name
        if (!app.name.empty()) {
            // Check for duplicates (same name and version)
            std::string key = app.name + "|" + app.version;
            if (seenApps.find(key) == seenApps.end()) {
                seenApps.insert(key);
                appsArray.push_back(app.toJson());
            }
        }
    }
    
    return appsArray;
}

void InstalledAppsCollector::enumerateApps(HKEY rootKey, const std::wstring& subKey, std::vector<InstalledApp>& apps) {
    HKEY hUninstKey = nullptr;
    LONG result = RegOpenKeyExW(rootKey, subKey.c_str(), 0, KEY_READ, &hUninstKey);
    
    if (result != ERROR_SUCCESS) {
        return;
    }
    
    DWORD index = 0;
    wchar_t subKeyName[256];
    DWORD subKeyNameSize = sizeof(subKeyName) / sizeof(wchar_t);
    
    while (RegEnumKeyExW(hUninstKey, index, subKeyName, &subKeyNameSize, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
        HKEY appKey = nullptr;
        std::wstring appPath = subKey + L"\\" + subKeyName;
        
        if (RegOpenKeyExW(rootKey, appPath.c_str(), 0, KEY_READ, &appKey) == ERROR_SUCCESS) {
            InstalledApp app;
            if (readAppInfo(appKey, app) && !app.name.empty()) {
                apps.push_back(app);
            }
            RegCloseKey(appKey);
        }
        
        subKeyNameSize = sizeof(subKeyName) / sizeof(wchar_t);
        index++;
    }
    
    RegCloseKey(hUninstKey);
}

bool InstalledAppsCollector::readAppInfo(HKEY appKey, InstalledApp& app) {
    app.name = readRegistryString(appKey, L"DisplayName");
    
    // Skip if no display name or if it's a system component
    if (app.name.empty()) {
        return false;
    }
    
    // Skip system components
    DWORD systemComponent = readRegistryDWORD(appKey, L"SystemComponent");
    if (systemComponent == 1) {
        return false;
    }
    
    // Skip Windows updates
    std::string parentKeyName = readRegistryString(appKey, L"ParentKeyName");
    if (!parentKeyName.empty()) {
        return false;
    }
    
    app.version = readRegistryString(appKey, L"DisplayVersion");
    app.publisher = readRegistryString(appKey, L"Publisher");
    app.installDate = readRegistryString(appKey, L"InstallDate");
    app.installLocation = readRegistryString(appKey, L"InstallLocation");
    
    // Get size in MB (registry stores in KB)
    DWORD sizeKB = readRegistryDWORD(appKey, L"EstimatedSize");
    if (sizeKB > 0) {
        app.sizeMB = sizeKB / 1024;
    }
    
    return true;
}

std::string InstalledAppsCollector::readRegistryString(HKEY key, const std::wstring& valueName) {
    wchar_t buffer[1024];
    DWORD bufferSize = sizeof(buffer);
    DWORD type = 0;
    
    LONG result = RegQueryValueExW(key, valueName.c_str(), nullptr, &type, (LPBYTE)buffer, &bufferSize);
    
    if (result == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ)) {
        return wideToUtf8(buffer);
    }
    
    return "";
}

DWORD InstalledAppsCollector::readRegistryDWORD(HKEY key, const std::wstring& valueName) {
    DWORD value = 0;
    DWORD bufferSize = sizeof(DWORD);
    DWORD type = 0;
    
    LONG result = RegQueryValueExW(key, valueName.c_str(), nullptr, &type, (LPBYTE)&value, &bufferSize);
    
    if (result == ERROR_SUCCESS && type == REG_DWORD) {
        return value;
    }
    
    return 0;
}

std::string InstalledAppsCollector::wideToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    
    int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (sizeNeeded <= 0) return "";
    
    std::string result(sizeNeeded - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &result[0], sizeNeeded, nullptr, nullptr);
    return result;
}

std::wstring InstalledAppsCollector::utf8ToWide(const std::string& str) {
    if (str.empty()) return L"";
    
    int sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
    if (sizeNeeded <= 0) return L"";
    
    std::wstring result(sizeNeeded - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &result[0], sizeNeeded);
    return result;
}

} // namespace ResolutePulse
