#include "OSInfoCollector.h"
#include "utils/Logger.h"
#include <windows.h>
#include <lm.h>
#include <sstream>

#pragma comment(lib, "netapi32.lib")

namespace ResolutePulse {

OSInfoCollector::OSInfoCollector() = default;

bool OSInfoCollector::initialize() {
    LOG_DEBUG("Initializing OS Info Collector");
    return true;
}

nlohmann::json OSInfoCollector::collect() {
    LOG_DEBUG("Collecting OS information");
    
    nlohmann::json osInfo;
    
    try {
        // Get Windows version
        std::string osName, version, build;
        if (getWindowsVersion(osName, version, build)) {
            osInfo["os_name"] = osName;
            osInfo["version"] = version;
            osInfo["build"] = build;
        }
        
        // Get hostname
        osInfo["hostname"] = getHostname();
        
        // Get architecture
        osInfo["architecture"] = getArchitecture();
        
        // Get domain info
        osInfo["domain"] = getDomainInfo();
        
        // Get uptime
        osInfo["uptime_seconds"] = getUptime();
        
        // Get system manufacturer and model
        std::string manufacturer, model;
        if (getSystemInfo(manufacturer, model)) {
            osInfo["manufacturer"] = manufacturer;
            osInfo["model"] = model;
        }
        
        LOG_INFO("OS info collected successfully");
    } catch (const std::exception& e) {
        LOG_ERROR("Error collecting OS info: {}", e.what());
    }
    
    return osInfo;
}

bool OSInfoCollector::getWindowsVersion(std::string& osName, std::string& version, std::string& build) {
    // Use RtlGetVersion to get accurate version info (GetVersionEx is deprecated and lies)
    typedef LONG (WINAPI* RtlGetVersionPtr)(PRTL_OSVERSIONINFOW);
    
    HMODULE hMod = GetModuleHandleW(L"ntdll.dll");
    if (hMod) {
        RtlGetVersionPtr RtlGetVersion = (RtlGetVersionPtr)GetProcAddress(hMod, "RtlGetVersion");
        if (RtlGetVersion) {
            RTL_OSVERSIONINFOW osvi = {};
            osvi.dwOSVersionInfoSize = sizeof(osvi);
            
            if (RtlGetVersion(&osvi) == 0) {
                // Build version string
                std::ostringstream versionStream;
                versionStream << osvi.dwMajorVersion << "." << osvi.dwMinorVersion << "." << osvi.dwBuildNumber;
                version = versionStream.str();
                
                // Build number
                build = std::to_string(osvi.dwBuildNumber);
                
                // Determine OS name based on version
                if (osvi.dwMajorVersion == 10 && osvi.dwMinorVersion == 0) {
                    if (osvi.dwBuildNumber >= 22000) {
                        osName = "Windows 11";
                    } else {
                        osName = "Windows 10";
                    }
                } else if (osvi.dwMajorVersion == 6 && osvi.dwMinorVersion == 3) {
                    osName = "Windows 8.1";
                } else if (osvi.dwMajorVersion == 6 && osvi.dwMinorVersion == 2) {
                    osName = "Windows 8";
                } else if (osvi.dwMajorVersion == 6 && osvi.dwMinorVersion == 1) {
                    osName = "Windows 7";
                } else {
                    osName = "Windows";
                }
                
                return true;
            }
        }
    }
    
    return false;
}

std::string OSInfoCollector::getHostname() {
    wchar_t computerName[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD size = sizeof(computerName) / sizeof(wchar_t);
    
    if (GetComputerNameExW(ComputerNameDnsHostname, computerName, &size)) {
        // Convert wide string to UTF-8
        int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, computerName, -1, nullptr, 0, nullptr, nullptr);
        std::string result(sizeNeeded - 1, 0);
        WideCharToMultiByte(CP_UTF8, 0, computerName, -1, &result[0], sizeNeeded, nullptr, nullptr);
        return result;
    }
    
    return "Unknown";
}

std::string OSInfoCollector::getArchitecture() {
    SYSTEM_INFO sysInfo;
    GetNativeSystemInfo(&sysInfo);
    
    switch (sysInfo.wProcessorArchitecture) {
        case PROCESSOR_ARCHITECTURE_AMD64:
            return "x64";
        case PROCESSOR_ARCHITECTURE_INTEL:
            return "x86";
        case PROCESSOR_ARCHITECTURE_ARM:
            return "ARM";
        case PROCESSOR_ARCHITECTURE_ARM64:
            return "ARM64";
        default:
            return "Unknown";
    }
}

std::string OSInfoCollector::getDomainInfo() {
    LPWSTR domainName = nullptr;
    NETSETUP_JOIN_STATUS joinStatus;
    
    NET_API_STATUS status = NetGetJoinInformation(nullptr, &domainName, &joinStatus);
    
    std::string result = "Unknown";
    if (status == NERR_Success && domainName) {
        // Convert wide string to UTF-8
        int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, domainName, -1, nullptr, 0, nullptr, nullptr);
        result.resize(sizeNeeded - 1);
        WideCharToMultiByte(CP_UTF8, 0, domainName, -1, &result[0], sizeNeeded, nullptr, nullptr);
        
        NetApiBufferFree(domainName);
    }
    
    return result;
}

uint64_t OSInfoCollector::getUptime() {
    return GetTickCount64() / 1000;  // Convert milliseconds to seconds
}

bool OSInfoCollector::getSystemInfo(std::string& manufacturer, std::string& model) {
    // Simplified implementation without WMI to avoid COM support library dependencies
    // Can use registry instead
    HKEY hKey;
    LONG result = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                                L"SYSTEM\\CurrentControlSet\\Control\\SystemInformation",
                                0, KEY_READ, &hKey);
    
    if (result == ERROR_SUCCESS) {
        wchar_t buffer[256];
        DWORD bufferSize = sizeof(buffer);
        
        // Try to read manufacturer
        if (RegQueryValueExW(hKey, L"SystemManufacturer", nullptr, nullptr, 
                            (LPBYTE)buffer, &bufferSize) == ERROR_SUCCESS) {
            int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, buffer, -1, nullptr, 0, nullptr, nullptr);
            manufacturer.resize(sizeNeeded - 1);
            WideCharToMultiByte(CP_UTF8, 0, buffer, -1, &manufacturer[0], sizeNeeded, nullptr, nullptr);
        }
        
        bufferSize = sizeof(buffer);
        // Try to read model
        if (RegQueryValueExW(hKey, L"SystemProductName", nullptr, nullptr,
                            (LPBYTE)buffer, &bufferSize) == ERROR_SUCCESS) {
            int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, buffer, -1, nullptr, 0, nullptr, nullptr);
            model.resize(sizeNeeded - 1);
            WideCharToMultiByte(CP_UTF8, 0, buffer, -1, &model[0], sizeNeeded, nullptr, nullptr);
        }
        
        RegCloseKey(hKey);
        return !manufacturer.empty() || !model.empty();
    }
    
    // Fallback: set to generic values
    manufacturer = "Unknown";
    model = "Unknown";
    return false;
}

} // namespace ResolutePulse
