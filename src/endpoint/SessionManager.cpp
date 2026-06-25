#include "SessionManager.h"
#include "utils/Logger.h"

#include <windows.h>
#include <wtsapi32.h>
#include <userenv.h>

#pragma comment(lib, "wtsapi32.lib")
#pragma comment(lib, "userenv.lib")

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
            
            // Get session timing info
            WTSINFO* pWtsInfo = NULL;
            if (WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, info.sessionId, WTSSessionInfo, (LPWSTR*)&pWtsInfo, &bytes)) {
                if (pWtsInfo) {
                    // Convert LARGE_INTEGER file times to UNIX epoch
                    auto filetimeToEpoch = [](const LARGE_INTEGER& li) -> uint64_t {
                        if (li.QuadPart == 0) return 0;
                        return (li.QuadPart - 116444736000000000ULL) / 10000000ULL;
                    };
                    info.logonTime = filetimeToEpoch(pWtsInfo->LogonTime);
                    uint64_t current = filetimeToEpoch(pWtsInfo->CurrentTime);
                    uint64_t lastInput = filetimeToEpoch(pWtsInfo->LastInputTime);
                    info.idleTime = (current > lastInput) ? (current - lastInput) : 0;
                    WTSFreeMemory(pWtsInfo);
                } else {
                    info.logonTime = 0;
                    info.idleTime = 0;
                }
            } else {
                info.logonTime = 0;
                info.idleTime = 0;
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

bool SessionManager::lockWorkstation(std::string& errorMsg) {
    // The agent runs as a Windows service in session 0.
    // LockWorkStation() only works in the interactive session.
    // We use CreateProcessAsUser to launch the lock command in the
    // active console session.

    DWORD sessionId = WTSGetActiveConsoleSessionId();
    if (sessionId == 0xFFFFFFFF) {
        errorMsg = "No active console session found";
        LOG_ERROR("SessionManager: {}", errorMsg);
        return false;
    }

    HANDLE hToken = NULL;
    if (!WTSQueryUserToken(sessionId, &hToken)) {
        errorMsg = "Failed to query user token for session " + std::to_string(sessionId) +
                   ". Error code: " + std::to_string(GetLastError());
        LOG_ERROR("SessionManager: {}", errorMsg);
        return false;
    }

    HANDLE hDupToken = NULL;
    if (!DuplicateTokenEx(hToken, MAXIMUM_ALLOWED, NULL, SecurityIdentification, TokenPrimary, &hDupToken)) {
        DWORD err = GetLastError();
        CloseHandle(hToken);
        errorMsg = "Failed to duplicate token. Error code: " + std::to_string(err);
        LOG_ERROR("SessionManager: {}", errorMsg);
        return false;
    }
    CloseHandle(hToken);

    LPVOID pEnv = NULL;
    CreateEnvironmentBlock(&pEnv, hDupToken, FALSE);

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.lpDesktop = (LPWSTR)L"winsta0\\default";
    PROCESS_INFORMATION pi = {};

    // Launch rundll32 to call LockWorkStation in the user's session
    wchar_t cmdLine[] = L"rundll32.exe user32.dll,LockWorkStation";

    BOOL ok = CreateProcessAsUserW(
        hDupToken,
        NULL,
        cmdLine,
        NULL, NULL,
        FALSE,
        CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW,
        pEnv,
        NULL,
        &si, &pi
    );

    DWORD lastErr = GetLastError();

    if (pEnv) DestroyEnvironmentBlock(pEnv);
    CloseHandle(hDupToken);

    if (ok) {
        // Wait briefly for the process to finish
        WaitForSingleObject(pi.hProcess, 5000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        LOG_INFO("SessionManager: Workstation locked successfully (session {})", sessionId);
        return true;
    }

    if (pi.hProcess) CloseHandle(pi.hProcess);
    if (pi.hThread) CloseHandle(pi.hThread);

    errorMsg = "Failed to launch lock process. Error code: " + std::to_string(lastErr);
    LOG_ERROR("SessionManager: {}", errorMsg);
    return false;
}

} // namespace Endpoint
} // namespace ResolutePulse

