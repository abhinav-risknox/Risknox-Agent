#include "InventoryCollector.h"
#include "sysinfo/OSInfoCollector.h"
#include "utils/Logger.h"

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <cmath>
#include <set>
#include <vector>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "advapi32.lib")

namespace ResolutePulse {
namespace Endpoint {

// ─── Helpers ────────────────────────────────────────────────────────────────

static std::string wideToUtf8(const wchar_t* wstr) {
    if (!wstr || !wstr[0]) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return "";
    std::string result(n - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, &result[0], n, nullptr, nullptr);
    return result;
}

static std::string readRegistryString(HKEY key, const wchar_t* valueName) {
    wchar_t buffer[1024];
    DWORD bufferSize = sizeof(buffer);
    DWORD type = 0;
    if (RegQueryValueExW(key, valueName, nullptr, &type, (LPBYTE)buffer, &bufferSize) == ERROR_SUCCESS &&
        (type == REG_SZ || type == REG_EXPAND_SZ)) {
        return wideToUtf8(buffer);
    }
    return "";
}

static DWORD readRegistryDWORD(HKEY key, const wchar_t* valueName) {
    DWORD value = 0;
    DWORD bufferSize = sizeof(DWORD);
    DWORD type = 0;
    if (RegQueryValueExW(key, valueName, nullptr, &type, (LPBYTE)&value, &bufferSize) == ERROR_SUCCESS &&
        type == REG_DWORD) {
        return value;
    }
    return 0;
}

// ─── Disk Collector (existing) ──────────────────────────────────────────────

static nlohmann::json collectDisks() {
    nlohmann::json disks = nlohmann::json::array();
    
    DWORD drives = GetLogicalDrives();
    for (int i = 0; i < 26; i++) {
        if (drives & (1 << i)) {
            char driveName[] = { (char)('A' + i), ':', '\\', '\0' };
            UINT type = GetDriveTypeA(driveName);
            
            if (type == DRIVE_FIXED || type == DRIVE_REMOVABLE) {
                ULARGE_INTEGER freeBytesAvailable, totalNumberOfBytes, totalNumberOfFreeBytes;
                if (GetDiskFreeSpaceExA(driveName, &freeBytesAvailable, &totalNumberOfBytes, &totalNumberOfFreeBytes)) {
                    nlohmann::json d;
                    d["drive"] = std::string(1, (char)('A' + i)) + ":";
                    d["type"] = (type == DRIVE_FIXED) ? "Fixed" : "Removable";
                    d["total_bytes"] = totalNumberOfBytes.QuadPart;
                    d["free_bytes"] = freeBytesAvailable.QuadPart;
                    disks.push_back(d);
                }
            }
        }
    }
    return disks;
}

// ─── CPU Info Collector ─────────────────────────────────────────────────────

static nlohmann::json collectCPUInfo() {
    nlohmann::json cpu;
    
    // CPU name and speed from registry
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
            L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
            0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        wchar_t cpuName[256];
        DWORD bufSize = sizeof(cpuName);
        if (RegQueryValueExW(hKey, L"ProcessorNameString", nullptr, nullptr,
                (LPBYTE)cpuName, &bufSize) == ERROR_SUCCESS) {
            cpu["name"] = wideToUtf8(cpuName);
        }
        DWORD mhz = 0;
        bufSize = sizeof(DWORD);
        if (RegQueryValueExW(hKey, L"~MHz", nullptr, nullptr,
                (LPBYTE)&mhz, &bufSize) == ERROR_SUCCESS) {
            cpu["max_speed_mhz"] = mhz;
        }
        RegCloseKey(hKey);
    }
    
    // Logical processors (threads) via GetSystemInfo
    SYSTEM_INFO sysInfo;
    GetNativeSystemInfo(&sysInfo);
    cpu["threads"] = (int)sysInfo.dwNumberOfProcessors;
    
    // Physical cores via GetLogicalProcessorInformation
    DWORD len = 0;
    GetLogicalProcessorInformation(nullptr, &len);
    if (len > 0) {
        std::vector<SYSTEM_LOGICAL_PROCESSOR_INFORMATION> buffer(len / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION));
        if (GetLogicalProcessorInformation(buffer.data(), &len)) {
            int cores = 0;
            for (const auto& info : buffer) {
                if (info.Relationship == RelationProcessorCore) cores++;
            }
            cpu["cores"] = cores;
        }
    }
    
    return cpu;
}

// ─── RAM Info Collector ─────────────────────────────────────────────────────

static nlohmann::json collectRAMInfo() {
    nlohmann::json ram;
    
    MEMORYSTATUSEX memStatus;
    memStatus.dwLength = sizeof(memStatus);
    if (GlobalMemoryStatusEx(&memStatus)) {
        double totalGB = static_cast<double>(memStatus.ullTotalPhys) / (1024.0 * 1024.0 * 1024.0);
        ram["total_gb"] = std::round(totalGB * 100.0) / 100.0;
        ram["total_bytes"] = memStatus.ullTotalPhys;
    }
    
    // Slot count via SMBIOS (Type 17 = Memory Device)
    DWORD smbiosSize = GetSystemFirmwareTable('RSMB', 0, nullptr, 0);
    if (smbiosSize > 0) {
        std::vector<BYTE> smbiosData(smbiosSize);
        if (GetSystemFirmwareTable('RSMB', 0, smbiosData.data(), smbiosSize) == smbiosSize) {
            int slotCount = 0;
            BYTE* data = smbiosData.data() + 8;  // skip SMBIOS header
            BYTE* end = smbiosData.data() + smbiosSize;
            while (data < end) {
                if (data + 4 > end) break;
                BYTE type = data[0];
                BYTE length = data[1];
                if (length < 4) break;
                if (type == 17) slotCount++;
                // Skip formatted area
                data += length;
                // Skip unformatted area (double null terminated strings)
                while (data + 1 < end && !(data[0] == 0 && data[1] == 0)) data++;
                data += 2;
            }
            if (slotCount > 0) ram["slot_count"] = slotCount;
        }
    }
    
    return ram;
}

// ─── Network Details Collector (replaces old collectIPs) ────────────────────

static nlohmann::json collectNetworkDetails() {
    nlohmann::json adapters = nlohmann::json::array();
    
    ULONG outBufLen = 15000;
    PIP_ADAPTER_ADDRESSES pAddresses = (IP_ADAPTER_ADDRESSES*)HeapAlloc(GetProcessHeap(), 0, outBufLen);
    if (!pAddresses) return adapters;
    
    // Include gateways and DNS servers (removed GAA_FLAG_SKIP_DNS_SERVER from old flags)
    ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_INCLUDE_GATEWAYS;
    DWORD dwRetVal = GetAdaptersAddresses(AF_UNSPEC, flags, NULL, pAddresses, &outBufLen);
    
    if (dwRetVal == ERROR_BUFFER_OVERFLOW) {
        HeapFree(GetProcessHeap(), 0, pAddresses);
        pAddresses = (IP_ADAPTER_ADDRESSES*)HeapAlloc(GetProcessHeap(), 0, outBufLen);
        if (!pAddresses) return adapters;
        dwRetVal = GetAdaptersAddresses(AF_UNSPEC, flags, NULL, pAddresses, &outBufLen);
    }
    
    if (dwRetVal == NO_ERROR) {
        PIP_ADAPTER_ADDRESSES pCurr = pAddresses;
        while (pCurr) {
            if (pCurr->OperStatus == IfOperStatusUp &&
                pCurr->IfType != IF_TYPE_SOFTWARE_LOOPBACK) {
                nlohmann::json adapter;
                adapter["adapter"] = pCurr->AdapterName;
                
                // Friendly name
                if (pCurr->FriendlyName) {
                    adapter["friendly_name"] = wideToUtf8(pCurr->FriendlyName);
                }
                
                // MAC address
                if (pCurr->PhysicalAddressLength > 0) {
                    char mac[18] = {};
                    snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
                        pCurr->PhysicalAddress[0], pCurr->PhysicalAddress[1],
                        pCurr->PhysicalAddress[2], pCurr->PhysicalAddress[3],
                        pCurr->PhysicalAddress[4], pCurr->PhysicalAddress[5]);
                    adapter["mac_address"] = mac;
                }
                
                // IP addresses (preserves existing per-adapter IP collection)
                nlohmann::json ips = nlohmann::json::array();
                PIP_ADAPTER_UNICAST_ADDRESS pUni = pCurr->FirstUnicastAddress;
                while (pUni) {
                    char ipStr[INET6_ADDRSTRLEN] = {};
                    sockaddr* sa = pUni->Address.lpSockaddr;
                    if (sa->sa_family == AF_INET) {
                        inet_ntop(AF_INET, &((sockaddr_in*)sa)->sin_addr, ipStr, INET_ADDRSTRLEN);
                        ips.push_back({{"ip", ipStr}, {"version", "IPv4"}});
                    } else if (sa->sa_family == AF_INET6) {
                        inet_ntop(AF_INET6, &((sockaddr_in6*)sa)->sin6_addr, ipStr, INET6_ADDRSTRLEN);
                        ips.push_back({{"ip", ipStr}, {"version", "IPv6"}});
                    }
                    pUni = pUni->Next;
                }
                adapter["ip_addresses"] = ips;
                
                // Gateways
                nlohmann::json gateways = nlohmann::json::array();
                PIP_ADAPTER_GATEWAY_ADDRESS_LH pGw = pCurr->FirstGatewayAddress;
                while (pGw) {
                    char gwStr[INET6_ADDRSTRLEN] = {};
                    sockaddr* sa = pGw->Address.lpSockaddr;
                    if (sa->sa_family == AF_INET)
                        inet_ntop(AF_INET, &((sockaddr_in*)sa)->sin_addr, gwStr, INET_ADDRSTRLEN);
                    else if (sa->sa_family == AF_INET6)
                        inet_ntop(AF_INET6, &((sockaddr_in6*)sa)->sin6_addr, gwStr, INET6_ADDRSTRLEN);
                    if (gwStr[0]) gateways.push_back(gwStr);
                    pGw = pGw->Next;
                }
                if (!gateways.empty()) adapter["gateways"] = gateways;
                
                // DNS servers
                nlohmann::json dnsServers = nlohmann::json::array();
                PIP_ADAPTER_DNS_SERVER_ADDRESS pDns = pCurr->FirstDnsServerAddress;
                while (pDns) {
                    char dnsStr[INET6_ADDRSTRLEN] = {};
                    sockaddr* sa = pDns->Address.lpSockaddr;
                    if (sa->sa_family == AF_INET)
                        inet_ntop(AF_INET, &((sockaddr_in*)sa)->sin_addr, dnsStr, INET_ADDRSTRLEN);
                    else if (sa->sa_family == AF_INET6)
                        inet_ntop(AF_INET6, &((sockaddr_in6*)sa)->sin6_addr, dnsStr, INET6_ADDRSTRLEN);
                    if (dnsStr[0]) dnsServers.push_back(dnsStr);
                    pDns = pDns->Next;
                }
                if (!dnsServers.empty()) adapter["dns_servers"] = dnsServers;
                
                adapters.push_back(adapter);
            }
            pCurr = pCurr->Next;
        }
    }
    
    HeapFree(GetProcessHeap(), 0, pAddresses);
    return adapters;
}

// Build backward-compatible flat IP list from the new network_adapters structure
static nlohmann::json buildFlatIPList(const nlohmann::json& networkAdapters) {
    nlohmann::json ips = nlohmann::json::array();
    for (const auto& adapter : networkAdapters) {
        if (!adapter.contains("ip_addresses")) continue;
        for (const auto& ipEntry : adapter["ip_addresses"]) {
            ips.push_back(nlohmann::json{
                {"adapter", adapter.value("adapter", "")},
                {"ip", ipEntry.value("ip", "")},
                {"version", ipEntry.value("version", "")}
            });
        }
    }
    return ips;
}

// ─── Installed Software Collector ───────────────────────────────────────────

static void enumerateSoftware(HKEY rootKey, const wchar_t* subKeyPath,
                               std::vector<nlohmann::json>& apps,
                               std::set<std::string>& seen) {
    HKEY hUninstKey = nullptr;
    if (RegOpenKeyExW(rootKey, subKeyPath, 0, KEY_READ, &hUninstKey) != ERROR_SUCCESS) return;
    
    DWORD index = 0;
    wchar_t subKeyName[256];
    DWORD subKeyNameSize = sizeof(subKeyName) / sizeof(wchar_t);
    
    while (RegEnumKeyExW(hUninstKey, index, subKeyName, &subKeyNameSize,
                         nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
        HKEY appKey = nullptr;
        std::wstring appPath = std::wstring(subKeyPath) + L"\\" + subKeyName;
        
        if (RegOpenKeyExW(rootKey, appPath.c_str(), 0, KEY_READ, &appKey) == ERROR_SUCCESS) {
            std::string name = readRegistryString(appKey, L"DisplayName");
            
            if (!name.empty()) {
                // Skip system components
                DWORD systemComponent = readRegistryDWORD(appKey, L"SystemComponent");
                std::string parentKey = readRegistryString(appKey, L"ParentKeyName");
                
                if (systemComponent != 1 && parentKey.empty()) {
                    std::string version = readRegistryString(appKey, L"DisplayVersion");
                    std::string dedupeKey = name + "|" + version;
                    
                    if (seen.find(dedupeKey) == seen.end()) {
                        seen.insert(dedupeKey);
                        nlohmann::json app;
                        app["name"] = name;
                        if (!version.empty()) app["version"] = version;
                        
                        std::string publisher = readRegistryString(appKey, L"Publisher");
                        if (!publisher.empty()) app["publisher"] = publisher;
                        
                        std::string installDate = readRegistryString(appKey, L"InstallDate");
                        if (!installDate.empty()) app["install_date"] = installDate;
                        
                        std::string installLoc = readRegistryString(appKey, L"InstallLocation");
                        if (!installLoc.empty()) app["install_location"] = installLoc;
                        
                        DWORD sizeKB = readRegistryDWORD(appKey, L"EstimatedSize");
                        if (sizeKB > 0) app["size_mb"] = sizeKB / 1024;
                        
                        apps.push_back(app);
                    }
                }
            }
            RegCloseKey(appKey);
        }
        
        subKeyNameSize = sizeof(subKeyName) / sizeof(wchar_t);
        index++;
    }
    
    RegCloseKey(hUninstKey);
}

static nlohmann::json collectInstalledSoftware() {
    std::vector<nlohmann::json> apps;
    std::set<std::string> seen;
    
    // 64-bit apps
    enumerateSoftware(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall", apps, seen);
    // 32-bit apps on 64-bit system
    enumerateSoftware(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall", apps, seen);
    // Per-user apps
    enumerateSoftware(HKEY_CURRENT_USER,
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall", apps, seen);
    
    nlohmann::json result = nlohmann::json::array();
    for (const auto& app : apps) result.push_back(app);
    
    LOG_INFO("InventoryCollector: Collected {} installed software entries", apps.size());
    return result;
}

// ─── Running Services Collector ─────────────────────────────────────────────

static std::string getServiceStateName(DWORD state) {
    switch (state) {
        case SERVICE_STOPPED:          return "Stopped";
        case SERVICE_START_PENDING:    return "StartPending";
        case SERVICE_STOP_PENDING:     return "StopPending";
        case SERVICE_RUNNING:          return "Running";
        case SERVICE_CONTINUE_PENDING: return "ContinuePending";
        case SERVICE_PAUSE_PENDING:    return "PausePending";
        case SERVICE_PAUSED:           return "Paused";
        default:                       return "Unknown";
    }
}

static std::string getServiceStartMode(DWORD startType) {
    switch (startType) {
        case SERVICE_AUTO_START:   return "Auto";
        case SERVICE_DEMAND_START: return "Manual";
        case SERVICE_DISABLED:    return "Disabled";
        case SERVICE_BOOT_START:  return "Boot";
        case SERVICE_SYSTEM_START: return "System";
        default:                   return "Unknown";
    }
}

static nlohmann::json collectRunningServices() {
    nlohmann::json services = nlohmann::json::array();
    
    SC_HANDLE hSCM = OpenSCManagerW(NULL, NULL, SC_MANAGER_ENUMERATE_SERVICE);
    if (!hSCM) {
        LOG_WARN("InventoryCollector: Failed to open SCM. Error: {}", GetLastError());
        return services;
    }
    
    DWORD bytesNeeded = 0, serviceCount = 0, resumeHandle = 0;
    EnumServicesStatusExW(hSCM, SC_ENUM_PROCESS_INFO, SERVICE_WIN32,
        SERVICE_STATE_ALL, NULL, 0, &bytesNeeded, &serviceCount, &resumeHandle, NULL);
    
    if (bytesNeeded == 0) {
        CloseServiceHandle(hSCM);
        return services;
    }
    
    std::vector<BYTE> buffer(bytesNeeded);
    resumeHandle = 0;
    if (EnumServicesStatusExW(hSCM, SC_ENUM_PROCESS_INFO, SERVICE_WIN32,
            SERVICE_STATE_ALL, buffer.data(), (DWORD)buffer.size(),
            &bytesNeeded, &serviceCount, &resumeHandle, NULL)) {
        
        auto* pServices = (ENUM_SERVICE_STATUS_PROCESSW*)buffer.data();
        for (DWORD i = 0; i < serviceCount; i++) {
            nlohmann::json svc;
            
            if (pServices[i].lpServiceName)
                svc["name"] = wideToUtf8(pServices[i].lpServiceName);
            if (pServices[i].lpDisplayName)
                svc["display_name"] = wideToUtf8(pServices[i].lpDisplayName);
            
            svc["state"] = getServiceStateName(pServices[i].ServiceStatusProcess.dwCurrentState);
            
            // Get start mode via QueryServiceConfigW
            SC_HANDLE hSvc = OpenServiceW(hSCM, pServices[i].lpServiceName, SERVICE_QUERY_CONFIG);
            if (hSvc) {
                DWORD needed = 0;
                QueryServiceConfigW(hSvc, NULL, 0, &needed);
                if (needed > 0) {
                    std::vector<BYTE> cfgBuf(needed);
                    auto* cfg = (QUERY_SERVICE_CONFIGW*)cfgBuf.data();
                    if (QueryServiceConfigW(hSvc, cfg, needed, &needed)) {
                        svc["start_mode"] = getServiceStartMode(cfg->dwStartType);
                    }
                }
                CloseServiceHandle(hSvc);
            }
            
            services.push_back(svc);
        }
    }
    
    CloseServiceHandle(hSCM);
    LOG_INFO("InventoryCollector: Collected {} services", services.size());
    return services;
}

// ─── Main entry point ───────────────────────────────────────────────────────

nlohmann::json InventoryCollector::collectFullInventory() {
    OSInfoCollector osCollector;
    osCollector.initialize();
    nlohmann::json inv = osCollector.collect();
    
    inv["logical_disks"]       = collectDisks();
    
    // Extended network info (replaces old flat IP list)
    nlohmann::json networkAdapters = collectNetworkDetails();
    inv["network_adapters"]    = networkAdapters;
    inv["ip_addresses"]        = buildFlatIPList(networkAdapters); // backward compat
    
    // New collectors
    inv["cpu"]                 = collectCPUInfo();
    inv["memory"]              = collectRAMInfo();
    inv["installed_software"]  = collectInstalledSoftware();
    inv["services"]            = collectRunningServices();
    
    return inv;
}

} // namespace Endpoint
} // namespace ResolutePulse
