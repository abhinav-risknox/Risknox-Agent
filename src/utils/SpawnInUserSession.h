#pragma once
// SpawnInUserSession.h
// Spawns a GUI process in the active interactive user session so it is visible
// even when the calling process runs as a Windows Service (Session 0).
//
// Token acquisition strategy (in priority order):
//   1. WTSQueryUserToken           — needs SE_TCB_PRIVILEGE (LocalSystem with TCB)
//   2. Duplicate explorer.exe token — works without SE_TCB_PRIVILEGE (most services)
//   3. Plain CreateProcessA        — fallback for console/dev mode (no session switch)
//
// Usage:
//   SpawnInUserSession("\"C:\\path\\app.exe\" --arg value", /*waitMs=*/0);

#include <Windows.h>
#include <WtsApi32.h>
#include <UserEnv.h>
#include <TlHelp32.h>
#include <string>
#include "utils/Logger.h"

#pragma comment(lib, "WtsApi32.lib")
#pragma comment(lib, "UserEnv.lib")

// ── Internal helpers ─────────────────────────────────────────────────────────

/// Try to get the user token via WTSQueryUserToken (needs SE_TCB_PRIVILEGE).
static HANDLE TryWTSToken(DWORD sessionId) {
    HANDLE hTok = nullptr;
    if (WTSQueryUserToken(sessionId, &hTok))
        return hTok;
    return nullptr;
}

/// Steal a primary token from explorer.exe running in the target session.
/// This works without SE_TCB_PRIVILEGE and is the standard approach used
/// by security products (Defender, CrowdStrike, etc.) for UI spawning.
static HANDLE TryExplorerToken(DWORD sessionId) {
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE)
        return nullptr;

    HANDLE hToken = nullptr;
    PROCESSENTRY32 pe = { sizeof(pe) };

    if (Process32First(hSnap, &pe)) {
        do {
            if (_stricmp(pe.szExeFile, "explorer.exe") != 0)
                continue;

            // Check this explorer is in the right session
            HANDLE hProc = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pe.th32ProcessID);
            if (!hProc) continue;

            DWORD procSession = 0;
            if (!ProcessIdToSessionId(pe.th32ProcessID, &procSession) ||
                procSession != sessionId) {
                CloseHandle(hProc);
                continue;
            }

            // Open the process token and duplicate it as a primary token
            HANDLE hRawTok = nullptr;
            if (OpenProcessToken(hProc, TOKEN_DUPLICATE | TOKEN_ASSIGN_PRIMARY |
                                        TOKEN_QUERY | TOKEN_ADJUST_PRIVILEGES |
                                        TOKEN_ADJUST_DEFAULT, &hRawTok)) {
                SECURITY_ATTRIBUTES sa = { sizeof(sa) };
                if (!DuplicateTokenEx(hRawTok,
                        TOKEN_ALL_ACCESS, &sa,
                        SecurityImpersonation, TokenPrimary,
                        &hToken)) {
                    hToken = nullptr;
                }
                CloseHandle(hRawTok);
            }
            CloseHandle(hProc);

            if (hToken) break; // got one — stop searching
        } while (Process32Next(hSnap, &pe));
    }

    CloseHandle(hSnap);
    return hToken;
}

// ── Public API ───────────────────────────────────────────────────────────────

/// Launch exeCmdLine in the session of the currently-logged-on console user.
/// Returns true if the process was created successfully.
inline bool SpawnInUserSession(const std::string& exeCmdLine, DWORD waitMs = 0)
{
    // Helper: plain spawn without session switching (dev/console mode)
    auto plainSpawn = [&](const char* reason) -> bool {
        LOG_DEBUG("SpawnInUserSession: using plain CreateProcessA ({})", reason);
        STARTUPINFOA si = {};
        si.cb          = sizeof(si);
        si.dwFlags     = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_SHOW;
        PROCESS_INFORMATION pi = {};
        std::string cmd = exeCmdLine;
        if (!CreateProcessA(nullptr, cmd.data(), nullptr, nullptr,
                            FALSE, 0, nullptr, nullptr, &si, &pi)) {
            LOG_ERROR("SpawnInUserSession: CreateProcessA failed: {}", GetLastError());
            return false;
        }
        if (waitMs) WaitForSingleObject(pi.hProcess, waitMs);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return true;
    };

    // ── 1. Find the active console session ──────────────────────────────────
    DWORD sessionId = WTSGetActiveConsoleSessionId();
    if (sessionId == 0xFFFFFFFF) {
        // No interactive session (headless / no user logged in)
        return plainSpawn("no active console session");
    }

    // ── 2. Obtain a user token ────────────────────────────────────────────────
    //   Try WTSQueryUserToken first (works if we have SE_TCB_PRIVILEGE).
    //   Fall back to cloning explorer.exe token (works for standard services).
    HANDLE hUserToken = TryWTSToken(sessionId);
    if (!hUserToken) {
        DWORD err = GetLastError();
        LOG_DEBUG("SpawnInUserSession: WTSQueryUserToken failed ({}), "
                  "trying explorer.exe token", err);

        hUserToken = TryExplorerToken(sessionId);
        if (!hUserToken) {
            LOG_WARN("SpawnInUserSession: explorer.exe token unavailable "
                     "(session {}), falling back to CreateProcessA — "
                     "window may not be visible to user", sessionId);
            return plainSpawn("no user token available");
        }
        LOG_DEBUG("SpawnInUserSession: using explorer.exe token for session {}", sessionId);
    }

    // ── 3. Build the user environment block ─────────────────────────────────
    LPVOID pEnv = nullptr;
    CreateEnvironmentBlock(&pEnv, hUserToken, FALSE); // best-effort; NULL is OK

    // ── 4. Launch in the user's interactive desktop ──────────────────────────
    STARTUPINFOA si = {};
    si.cb          = sizeof(si);
    si.dwFlags     = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_SHOW;
    si.lpDesktop   = const_cast<LPSTR>("winsta0\\default");

    PROCESS_INFORMATION pi = {};
    std::string cmd = exeCmdLine;
    DWORD flags = CREATE_UNICODE_ENVIRONMENT | CREATE_NEW_CONSOLE;

    BOOL ok = CreateProcessAsUserA(
        hUserToken,
        nullptr, cmd.data(),
        nullptr, nullptr, FALSE,
        flags, pEnv, nullptr,
        &si, &pi
    );

    if (pEnv) DestroyEnvironmentBlock(pEnv);
    CloseHandle(hUserToken);

    if (!ok) {
        DWORD err = GetLastError();
        LOG_ERROR("SpawnInUserSession: CreateProcessAsUserA failed: {} "
                  "(session {})", err, sessionId);
        return false;
    }

    if (waitMs) WaitForSingleObject(pi.hProcess, waitMs);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return true;
}


