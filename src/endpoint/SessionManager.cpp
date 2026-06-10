#include "SessionManager.h"
#include "utils/Logger.h"

#include <windows.h>
#include <wtsapi32.h>

#pragma comment(lib, "wtsapi32.lib")

namespace ResolutePulse {
namespace Endpoint {

static std::string getStateName(WTS_CONNECTSTATE_CLASS state) {
    switch (state) {
        case WTSActive: return "Active";
        case WTSConnected: return "Connected";
        case WTSConnectQuery: return "ConnectQuery";
        case WTSShadow: return "Shadow";
        case WTSDisconnected: return "Disconnected";
        case WTSIdle: return "Idle";
        case WTSListen: return "Listen";
        case WTSReset: return "Reset";
        case WTSDown: return "Down";
        case WTSInit: return "Init";
        default: return "Unknown";
    }
}

static std::string wstring_to_utf8(const std::wstring& wstr) {
    if (wstr.empty()) return std::string();
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
}

std::vector<SessionInfo> SessionManager::listSessions(std::string& errorMsg) {
    std::vector<SessionInfo> sessions;
    PWTS_SESSION_INFOW pSessionInfo = NULL;
    DWORD count = 0;

    if (WTSEnumerateSessionsW(WTS_CURRENT_SERVER_HANDLE, 0, 1, &pSessionInfo, &count)) {
        for (DWORD i = 0; i < count; i++) {
            SessionInfo info;
            info.sessionId = pSessionInfo[i].SessionId;
            info.state = pSessionInfo[i].State;
            info.stateName = getStateName(pSessionInfo[i].State);
            if (pSessionInfo[i].pWinStationName) {
                info.stationName = wstring_to_utf8(pSessionInfo[i].pWinStationName);
            }
            
            // Get username for session
            LPWSTR pUsername = NULL;
            DWORD bytes = 0;
            if (WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, info.sessionId, WTSUserName, &pUsername, &bytes)) {
                if (pUsername) {
                    info.username = wstring_to_utf8(pUsername);
                    WTSFreeMemory(pUsername);
                }
            }
            
            sessions.push_back(info);
        }
        WTSFreeMemory(pSessionInfo);
    } else {
        errorMsg = "Failed to enumerate sessions. Error code: " + std::to_string(GetLastError());
        LOG_ERROR("SessionManager: {}", errorMsg);
    }

    return sessions;
}

bool SessionManager::logoffSession(int sessionId, std::string& errorMsg) {
    if (WTSLogoffSession(WTS_CURRENT_SERVER_HANDLE, sessionId, FALSE)) {
        LOG_INFO("SessionManager: Logged off session {}", sessionId);
        return true;
    }
    errorMsg = "Failed to log off session " + std::to_string(sessionId) + ". Error code: " + std::to_string(GetLastError());
    LOG_ERROR("SessionManager: {}", errorMsg);
    return false;
}

bool SessionManager::disconnectSession(int sessionId, std::string& errorMsg) {
    if (WTSDisconnectSession(WTS_CURRENT_SERVER_HANDLE, sessionId, FALSE)) {
        LOG_INFO("SessionManager: Disconnected session {}", sessionId);
        return true;
    }
    errorMsg = "Failed to disconnect session " + std::to_string(sessionId) + ". Error code: " + std::to_string(GetLastError());
    LOG_ERROR("SessionManager: {}", errorMsg);
    return false;
}

} // namespace Endpoint
} // namespace ResolutePulse
