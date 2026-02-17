#include "OpenPortsCollector.h"
#include "utils/Logger.h"
#include <windows.h>
#include <tlhelp32.h>
#include <sstream>
#include <iomanip>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

namespace ResolutePulse {

OpenPortsCollector::OpenPortsCollector() = default;

bool OpenPortsCollector::initialize() {
    LOG_DEBUG("Initializing Open Ports Collector");
    return true;
}

nlohmann::json OpenPortsCollector::collect() {
    LOG_DEBUG("Collecting network connections");
    
    std::vector<NetworkConnection> listeningPorts;
    std::vector<NetworkConnection> establishedConnections;
    
    try {
        std::vector<NetworkConnection> allConnections;
        
        // Collect TCP connections
        collectTcpConnections(allConnections);
        
        // Collect UDP connections
        collectUdpConnections(allConnections);
        
        // Separate listening and established
        for (const auto& conn : allConnections) {
            if (conn.state == "LISTENING" || conn.state == "LISTEN") {
                listeningPorts.push_back(conn);
            } else if (conn.state == "ESTABLISHED") {
                establishedConnections.push_back(conn);
            }
        }
        
        LOG_INFO("Collected {} listening ports and {} established connections", 
                 listeningPorts.size(), establishedConnections.size());
    } catch (const std::exception& e) {
        LOG_ERROR("Error collecting network connections: {}", e.what());
    }
    
    // Build JSON result
    nlohmann::json result;
    
    nlohmann::json listeningArray = nlohmann::json::array();
    for (const auto& conn : listeningPorts) {
        listeningArray.push_back(conn.toJson());
    }
    result["listening_ports"] = listeningArray;
    
    nlohmann::json establishedArray = nlohmann::json::array();
    for (const auto& conn : establishedConnections) {
        establishedArray.push_back(conn.toJson());
    }
    result["established_connections"] = establishedArray;
    
    return result;
}

void OpenPortsCollector::collectTcpConnections(std::vector<NetworkConnection>& connections) {
    PMIB_TCPTABLE_OWNER_PID pTcpTable = nullptr;
    DWORD dwSize = 0;
    DWORD dwRetVal = 0;
    
    // Get size needed
    dwRetVal = GetExtendedTcpTable(nullptr, &dwSize, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0);
    
    if (dwRetVal == ERROR_INSUFFICIENT_BUFFER) {
        pTcpTable = (MIB_TCPTABLE_OWNER_PID*)malloc(dwSize);
        if (pTcpTable == nullptr) {
            return;
        }
    } else {
        return;
    }
    
    // Get actual data
    dwRetVal = GetExtendedTcpTable(pTcpTable, &dwSize, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0);
    
    if (dwRetVal == NO_ERROR) {
        for (DWORD i = 0; i < pTcpTable->dwNumEntries; i++) {
            NetworkConnection conn;
            conn.protocol = "TCP";
            
            MIB_TCPROW_OWNER_PID row = pTcpTable->table[i];
            
            conn.localAddress = ipToString(row.dwLocalAddr);
            conn.localPort = ntohs((u_short)row.dwLocalPort);
            
            conn.remoteAddress = ipToString(row.dwRemoteAddr);
            conn.remotePort = ntohs((u_short)row.dwRemotePort);
            
            conn.state = getTcpState(row.dwState);
            conn.processId = row.dwOwningPid;
            conn.processName = getProcessName(row.dwOwningPid);
            
            connections.push_back(conn);
        }
    }
    
    if (pTcpTable) {
        free(pTcpTable);
    }
}

void OpenPortsCollector::collectUdpConnections(std::vector<NetworkConnection>& connections) {
    PMIB_UDPTABLE_OWNER_PID pUdpTable = nullptr;
    DWORD dwSize = 0;
    DWORD dwRetVal = 0;
    
    // Get size needed
    dwRetVal = GetExtendedUdpTable(nullptr, &dwSize, FALSE, AF_INET, UDP_TABLE_OWNER_PID, 0);
    
    if (dwRetVal == ERROR_INSUFFICIENT_BUFFER) {
        pUdpTable = (MIB_UDPTABLE_OWNER_PID*)malloc(dwSize);
        if (pUdpTable == nullptr) {
            return;
        }
    } else {
        return;
    }
    
    // Get actual data
    dwRetVal = GetExtendedUdpTable(pUdpTable, &dwSize, FALSE, AF_INET, UDP_TABLE_OWNER_PID, 0);
    
    if (dwRetVal == NO_ERROR) {
        for (DWORD i = 0; i < pUdpTable->dwNumEntries; i++) {
            NetworkConnection conn;
            conn.protocol = "UDP";
            
            MIB_UDPROW_OWNER_PID row = pUdpTable->table[i];
            
            conn.localAddress = ipToString(row.dwLocalAddr);
            conn.localPort = ntohs((u_short)row.dwLocalPort);
            
            conn.state = "LISTEN";  // UDP doesn't have states like TCP
            conn.processId = row.dwOwningPid;
            conn.processName = getProcessName(row.dwOwningPid);
            
            connections.push_back(conn);
        }
    }
    
    if (pUdpTable) {
        free(pUdpTable);
    }
}

std::string OpenPortsCollector::getProcessName(DWORD processId) {
    if (processId == 0) {
        return "System Idle Process";
    }
    if (processId == 4) {
        return "System";
    }
    
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) {
        return "";
    }
    
    PROCESSENTRY32W pe32;
    pe32.dwSize = sizeof(PROCESSENTRY32W);
    
    std::string processName;
    
    if (Process32FirstW(hSnapshot, &pe32)) {
        do {
            if (pe32.th32ProcessID == processId) {
                // Convert wide string to UTF-8
                int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, pe32.szExeFile, -1, nullptr, 0, nullptr, nullptr);
                if (sizeNeeded > 0) {
                    processName.resize(sizeNeeded - 1);
                    WideCharToMultiByte(CP_UTF8, 0, pe32.szExeFile, -1, &processName[0], sizeNeeded, nullptr, nullptr);
                }
                break;
            }
        } while (Process32NextW(hSnapshot, &pe32));
    }
    
    CloseHandle(hSnapshot);
    return processName;
}

std::string OpenPortsCollector::ipToString(DWORD ip) {
    in_addr addr;
    addr.S_un.S_addr = ip;
    
    char* ipStr = inet_ntoa(addr);
    return ipStr ? std::string(ipStr) : "0.0.0.0";
}

std::string OpenPortsCollector::getTcpState(DWORD state) {
    switch (state) {
        case MIB_TCP_STATE_CLOSED:        return "CLOSED";
        case MIB_TCP_STATE_LISTEN:        return "LISTENING";
        case MIB_TCP_STATE_SYN_SENT:      return "SYN_SENT";
        case MIB_TCP_STATE_SYN_RCVD:      return "SYN_RECEIVED";
        case MIB_TCP_STATE_ESTAB:         return "ESTABLISHED";
        case MIB_TCP_STATE_FIN_WAIT1:     return "FIN_WAIT1";
        case MIB_TCP_STATE_FIN_WAIT2:     return "FIN_WAIT2";
        case MIB_TCP_STATE_CLOSE_WAIT:    return "CLOSE_WAIT";
        case MIB_TCP_STATE_CLOSING:       return "CLOSING";
        case MIB_TCP_STATE_LAST_ACK:      return "LAST_ACK";
        case MIB_TCP_STATE_TIME_WAIT:     return "TIME_WAIT";
        case MIB_TCP_STATE_DELETE_TCB:    return "DELETE_TCB";
        default:                          return "UNKNOWN";
    }
}

} // namespace ResolutePulse
