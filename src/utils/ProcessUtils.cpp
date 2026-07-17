#include "ProcessUtils.h"
#include "Logger.h"

#include <WtsApi32.h>
#include <UserEnv.h>

#pragma comment(lib, "Wtsapi32.lib")
#pragma comment(lib, "Userenv.lib")

namespace ResolutePulse {
namespace ProcessUtils {

bool launchInteractiveProcess(const std::string& cmdLine, PROCESS_INFORMATION* outPi) {
    DWORD sessionId = WTSGetActiveConsoleSessionId();
    if (sessionId == 0xFFFFFFFF) {
        LOG_WARN("ProcessUtils: No active console session found to launch UI.");
        return false;
    }

    HANDLE hToken = NULL;
    if (!WTSQueryUserToken(sessionId, &hToken)) {
        LOG_ERROR("ProcessUtils: Failed to query user token for session {}. Error: {}", sessionId, GetLastError());
        return false;
    }

    HANDLE hDupToken = NULL;
    if (!DuplicateTokenEx(hToken, MAXIMUM_ALLOWED, NULL, SecurityIdentification, TokenPrimary, &hDupToken)) {
        LOG_ERROR("ProcessUtils: Failed to duplicate token. Error: {}", GetLastError());
        CloseHandle(hToken);
        return false;
    }
    CloseHandle(hToken);

    LPVOID pEnv = NULL;
    CreateEnvironmentBlock(&pEnv, hDupToken, FALSE);

    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    si.lpDesktop = const_cast<char*>("winsta0\\default");
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_SHOW;

    PROCESS_INFORMATION pi = {};

    // Copy string because CreateProcessA may modify it
    std::string mutableCmd = cmdLine;

    BOOL ok = CreateProcessAsUserA(
        hDupToken,
        nullptr,
        mutableCmd.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_UNICODE_ENVIRONMENT,
        pEnv,
        nullptr,
        &si,
        &pi
    );

    if (pEnv) {
        DestroyEnvironmentBlock(pEnv);
    }
    CloseHandle(hDupToken);

    if (!ok) {
        LOG_ERROR("ProcessUtils: CreateProcessAsUserA failed. Error: {}", GetLastError());
        return false;
    }

    if (outPi) {
        *outPi = pi;
    } else {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }

    return true;
}

} // namespace ProcessUtils
} // namespace ResolutePulse
