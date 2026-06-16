#include "InventoryCollector.h"
#include "sysinfo/OSInfoCollector.h"
#include "utils/Logger.h"

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

namespace ResolutePulse {
namespace Endpoint {

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

static nlohmann::json collectIPs() {
    nlohmann::json ips = nlohmann::json::array();
    
    ULONG outBufLen = 15000;
    PIP_ADAPTER_ADDRESSES pAddresses = (IP_ADAPTER_ADDRESSES*)HeapAlloc(GetProcessHeap(), 0, outBufLen);
    
    if (pAddresses == NULL) return ips;
    
    ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    DWORD dwRetVal = GetAdaptersAddresses(AF_UNSPEC, flags, NULL, pAddresses, &outBufLen);
    
    if (dwRetVal == ERROR_BUFFER_OVERFLOW) {
        HeapFree(GetProcessHeap(), 0, pAddresses);
        pAddresses = (IP_ADAPTER_ADDRESSES*)HeapAlloc(GetProcessHeap(), 0, outBufLen);
        if (pAddresses == NULL) return ips;
        dwRetVal = GetAdaptersAddresses(AF_UNSPEC, flags, NULL, pAddresses, &outBufLen);
    }
    
    if (dwRetVal == NO_ERROR) {
        PIP_ADAPTER_ADDRESSES pCurrAddresses = pAddresses;
        while (pCurrAddresses) {
            if (pCurrAddresses->OperStatus == IfOperStatusUp && 
                pCurrAddresses->IfType != IF_TYPE_SOFTWARE_LOOPBACK) {
                
                PIP_ADAPTER_UNICAST_ADDRESS pUnicast = pCurrAddresses->FirstUnicastAddress;
                while (pUnicast != NULL) {
                    sockaddr* sa = pUnicast->Address.lpSockaddr;
                    char ipStr[INET6_ADDRSTRLEN] = {0};
                    
                    if (sa->sa_family == AF_INET) {
                        sockaddr_in* sa_in = (sockaddr_in*)sa;
                        inet_ntop(AF_INET, &(sa_in->sin_addr), ipStr, INET_ADDRSTRLEN);
                        ips.push_back(nlohmann::json{
                            {"adapter", pCurrAddresses->AdapterName},
                            {"ip", ipStr},
                            {"version", "IPv4"}
                        });
                    } else if (sa->sa_family == AF_INET6) {
                        sockaddr_in6* sa_in6 = (sockaddr_in6*)sa;
                        inet_ntop(AF_INET6, &(sa_in6->sin6_addr), ipStr, INET6_ADDRSTRLEN);
                        ips.push_back(nlohmann::json{
                            {"adapter", pCurrAddresses->AdapterName},
                            {"ip", ipStr},
                            {"version", "IPv6"}
                        });
                    }
                    pUnicast = pUnicast->Next;
                }
            }
            pCurrAddresses = pCurrAddresses->Next;
        }
    }
    
    HeapFree(GetProcessHeap(), 0, pAddresses);
    return ips;
}

nlohmann::json InventoryCollector::collectFullInventory() {
    OSInfoCollector osCollector;
    osCollector.initialize();
    nlohmann::json inv = osCollector.collect();
    
    inv["logical_disks"] = collectDisks();
    inv["ip_addresses"] = collectIPs();
    
    return inv;
}

} // namespace Endpoint
} // namespace ResolutePulse
