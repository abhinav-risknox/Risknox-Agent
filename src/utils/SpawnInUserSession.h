#pragma once
// SpawnInUserSession.h
// Spawns a GUI process in the active interactive user session so it is visible
// even when the calling process runs as a Windows Service (Session 0).
//
// Usage:
//   SpawnInUserSession("\"C:\\path\\app.exe\" --arg value", /*waitMs=*/0);
//
// Returns true if CreateProcessAsUser succeeded.

#include <Windows.h>
#include <WtsApi32.h>
#include <UserEnv.h>
#include <string>
#include "utils/Logger.h"

#pragma comment(lib, "WtsApi32.lib")
#pragma comment(lib, "UserEnv.lib")

/// Launch exeCmdLine in the session of the currently-logged-on console user.
/// If the caller IS the interactive user (e.g. console mode), falls back to
/// plain CreateProcessA so it still works during development.
inline bool SpawnInUserSession(const std::string& exeCmdLine, DWORD waitMs = 0)
{
    // ── 1. Find the active console session ──────────────────────────────────
    DWORD sessionId = WTSGetActiveConsoleSessionId();
    if (sessionId == 0xFFFFFFFF) {
        // No interactive session — try plain spawn (console / debug mode)
        STARTUPINFOA si = {};
        si.cb          = sizeof(si);
        si.dwFlags     = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_SHOW;
        PROCESS_INFORMATION pi = {};
        std::string cmd = exeCmdLine; // need mutable copy
        if (!CreateProcessA(nullptr, cmd.data(), nullptr, nullptr,
                            FALSE, 0, nullptr, nullptr, &si, &pi)) {
            LOG_ERROR("SpawnInUserSession: plain CreateProcessA failed: {}", GetLastError());
            return false;
        }
        if (waitMs) WaitForSingleObject(pi.hProcess, waitMs);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return true;
    }

    // ── 2. Obtain the token of the logged-on user ────────────────────────────
    HANDLE hUserToken = nullptr;
    if (!WTSQueryUserToken(sessionId, &hUserToken)) {
        DWORD err = GetLastError();
        LOG_WARN("SpawnInUserSession: WTSQueryUserToken failed ({}), falling back to CreateProcessA", err);
        // Fallback: plain spawn (works in console mode / non-service contexts)
        STARTUPINFOA si = {};
        si.cb          = sizeof(si);
        si.dwFlags     = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_SHOW;
        PROCESS_INFORMATION pi = {};
        std::string cmd = exeCmdLine;
        if (!CreateProcessA(nullptr, cmd.data(), nullptr, nullptr,
                            FALSE, 0, nullptr, nullptr, &si, &pi)) {
            LOG_ERROR("SpawnInUserSession: fallback CreateProcessA failed: {}", GetLastError());
            return false;
        }
        if (waitMs) WaitForSingleObject(pi.hProcess, waitMs);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return true;
    }

    // ── 3. Build the user environment block ─────────────────────────────────
    LPVOID pEnv = nullptr;
    CreateEnvironmentBlock(&pEnv, hUserToken, FALSE); // best-effort; NULL is OK

    // ── 4. Launch in the user session ───────────────────────────────────────
    STARTUPINFOA si = {};
    si.cb           = sizeof(si);
    si.dwFlags      = STARTF_USESHOWWINDOW;
    si.wShowWindow  = SW_SHOW;
    si.lpDesktop    = const_cast<LPSTR>("winsta0\\default"); // interactive desktop

    PROCESS_INFORMATION pi = {};
    std::string cmd = exeCmdLine; // CreateProcessAsUser needs mutable buffer
    DWORD dwCreationFlags = CREATE_UNICODE_ENVIRONMENT | CREATE_NEW_CONSOLE;

    BOOL ok = CreateProcessAsUserA(
        hUserToken,
        nullptr,
        cmd.data(),
        nullptr, nullptr,
        FALSE,
        dwCreationFlags,
        pEnv,
        nullptr,
        &si, &pi
    );

    if (pEnv) DestroyEnvironmentBlock(pEnv);
    CloseHandle(hUserToken);

    if (!ok) {
        DWORD err = GetLastError();
        LOG_ERROR("SpawnInUserSession: CreateProcessAsUser failed: {}", err);
        return false;
    }

    if (waitMs) WaitForSingleObject(pi.hProcess, waitMs);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return true;
}
