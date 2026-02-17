#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>

namespace ResolutePulse {

struct NetworkConnection {
    std::string protocol;      // TCP or UDP
    std::string localAddress;
    uint16_t localPort = 0;
    std::string remoteAddress;
    uint16_t remotePort = 0;
    std::string state;         // LISTENING, ESTABLISHED, etc.
    DWORD processId = 0;
    std::string processName;
    
    nlohmann::json toJson() const {
        nlohmann::json j;
        j["protocol"] = protocol;
        j["local_address"] = localAddress;
        j["local_port"] = localPort;
        if (!remoteAddress.empty()) j["remote_address"] = remoteAddress;
        if (remotePort > 0) j["remote_port"] = remotePort;
        if (!state.empty()) j["state"] = state;
        if (processId > 0) j["process_id"] = processId;
        if (!processName.empty()) j["process_name"] = processName;
        return j;
    }
};

class OpenPortsCollector {
public:
    OpenPortsCollector();
    ~OpenPortsCollector() = default;
    
    // Initialize the collector
    bool initialize();
    
    // Collect network connections
    nlohmann::json collect();
    
private:
    // Collect TCP connections
    void collectTcpConnections(std::vector<NetworkConnection>& connections);
    
    // Collect UDP connections
    void collectUdpConnections(std::vector<NetworkConnection>& connections);
    
    // Get process name from PID
    std::string getProcessName(DWORD processId);
    
    // Convert IP address to string
    std::string ipToString(DWORD ip);
    
    // Get TCP state string
    std::string getTcpState(DWORD state);
};

} // namespace ResolutePulse
