#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace ResolutePulse {
namespace Endpoint {

struct SessionInfo {
    int sessionId;
    std::string stationName;
    std::string username;
    int state;
    std::string stateName;
};

class SessionManager {
public:
    static std::vector<SessionInfo> listSessions(std::string& errorMsg);
    static bool logoffSession(int sessionId, std::string& errorMsg);
    static bool disconnectSession(int sessionId, std::string& errorMsg);
    static bool lockWorkstation(std::string& errorMsg);
};

} // namespace Endpoint
} // namespace ResolutePulse
