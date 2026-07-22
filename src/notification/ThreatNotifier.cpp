#include "ThreatNotifier.h"
#include "utils/Logger.h"
#include "utils/SpawnInUserSession.h"

#include <Windows.h>
#include <WtsApi32.h>
#include <UserEnv.h>
#include <filesystem>
#include <chrono>
#include <ctime>
#include <sstream>

namespace ResolutePulse {

// ─────────────────────────────────────────────────────────────────────────────
// notify — launch ThreatNotification.exe and read exit code
// ─────────────────────────────────────────────────────────────────────────────

ThreatAction ThreatNotifier::notify(const ThreatNotification& info) const {
    if (exePath_.empty() || !std::filesystem::exists(exePath_)) {
        LOG_WARN("ThreatNotifier: WPF exe not found at '{}', defaulting to quarantine", exePath_);
        return ThreatAction::Quarantine;
    }

    // Build CLI: ThreatNotification.exe --file "x" --threat "y" --path "z" ...
    std::ostringstream cmd;
    cmd << "\"" << exePath_ << "\""
        << " --file \""     << info.fileName        << "\""
        << " --threat \""   << info.threatName      << "\""
        << " --path \""     << info.filePath        << "\""
        << " --timeout "    << info.autoCloseSeconds;

    if (!info.sourceUrl.empty())
        cmd << " --source \"" << info.sourceUrl << "\"";
    if (!info.severity.empty())
        cmd << " --severity " << info.severity;
    if (!info.hash.empty())
        cmd << " --hash \"" << info.hash << "\"";

    std::string cmdStr = cmd.str();
    LOG_DEBUG("ThreatNotifier: Launching WPF: {}", cmdStr);

    // Spawn in the active user session — works from both console and Service mode.
    // WaitMs is driven by WaitForSingleObject below, so we pass 0 here and keep
    // our own handle via the fallback path.  For the service path SpawnInUserSession
    // does the wait internally only if waitMs > 0, so we duplicate the handle trick:
    // instead just use a raw approach that returns the handle.

    // ── find active session ──────────────────────────────────────────────────
    DWORD sessionId = WTSGetActiveConsoleSessionId();
    HANDLE hUserToken = nullptr;
    if (sessionId != 0xFFFFFFFF) {
        hUserToken = TryWTSToken(sessionId);
        if (!hUserToken) {
            hUserToken = TryExplorerToken(sessionId);
        }
    }
    bool hasToken = (hUserToken != nullptr);

    LPVOID pEnv = nullptr;
    if (hasToken) CreateEnvironmentBlock(&pEnv, hUserToken, FALSE);

    STARTUPINFOA si = {};
    si.cb           = sizeof(si);
    si.dwFlags      = STARTF_USESHOWWINDOW;
    si.wShowWindow  = SW_SHOW;
    si.lpDesktop    = const_cast<LPSTR>("winsta0\\default");

    PROCESS_INFORMATION pi = {};
    BOOL ok = FALSE;

    if (hasToken) {
        ok = CreateProcessAsUserA(
            hUserToken, nullptr,
            const_cast<char*>(cmdStr.c_str()),
            nullptr, nullptr, FALSE,
            CREATE_UNICODE_ENVIRONMENT,
            pEnv, nullptr, &si, &pi
        );
    } else {
        // Console / dev mode — no WTS session, plain spawn
        ok = CreateProcessA(nullptr,
            const_cast<char*>(cmdStr.c_str()),
            nullptr, nullptr, FALSE, 0,
            nullptr, nullptr, &si, &pi
        );
    }

    if (pEnv)      DestroyEnvironmentBlock(pEnv);
    if (hUserToken) CloseHandle(hUserToken);

    if (!ok) {
        LOG_ERROR("ThreatNotifier: Failed to launch ThreatNotification.exe: {}", GetLastError());
        return ThreatAction::Quarantine;
    }

    AllowSetForegroundWindow(pi.dwProcessId);

    // Wait for the user to respond (max 120s — double the typical timeout)
    DWORD waitResult = WaitForSingleObject(pi.hProcess, 120000);

    ThreatAction result = ThreatAction::Quarantine;
    if (waitResult == WAIT_OBJECT_0) {
        DWORD exitCode = 0;
        GetExitCodeProcess(pi.hProcess, &exitCode);
        result = parseExitCode(exitCode);
        LOG_INFO("ThreatNotifier: WPF exit code = {} \u2192 {}", exitCode,
                 exitCode == 0 ? "QUARANTINE" :
                 exitCode == 1 ? "IGNORE" :
                 exitCode == 2 ? "DETAILS" :
                 exitCode == 3 ? "DISMISSED" :
                 exitCode == 4 ? "AUTO_QUARANTINE" : "UNKNOWN");
    } else {
        LOG_WARN("ThreatNotifier: WPF process timed out, defaulting to quarantine");
    }

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// parseExitCode — map WPF exit codes to ThreatAction
// ─────────────────────────────────────────────────────────────────────────────

ThreatAction ThreatNotifier::parseExitCode(unsigned long exitCode) const {
    switch (exitCode) {
        case 0:  return ThreatAction::Quarantine;
        case 1:  return ThreatAction::Ignore;
        case 2:  return ThreatAction::Details;
        case 3:  return ThreatAction::Dismissed;
        case 4:  return ThreatAction::AutoQuarantine;
        default: return ThreatAction::Unknown;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// quarantine — move the file to quarantine directory with a timestamp prefix
// ─────────────────────────────────────────────────────────────────────────────

bool ThreatNotifier::quarantine(const std::string& filePath,
                                 const std::string& threatName,
                                 const std::string& quarantineDir) const {
    namespace fs = std::filesystem;

    try {
        fs::create_directories(quarantineDir);

        // Build destination: <quarantineDir>/<timestamp>_<filename>
        std::time_t now = std::time(nullptr);
        char ts[32] = {};
        std::tm tmUtc{};
        gmtime_s(&tmUtc, &now);
        std::strftime(ts, sizeof(ts), "%Y%m%d_%H%M%S", &tmUtc);

        std::string fileName = fs::path(filePath).filename().string();
        fs::path dest = fs::path(quarantineDir) / (std::string(ts) + "_" + fileName);

        fs::rename(filePath, dest);

        LOG_WARN("ThreatNotifier: Quarantined '{}' → '{}' (threat: {})",
                 filePath, dest.string(), threatName);
        return true;

    } catch (const std::exception& e) {
        LOG_ERROR("ThreatNotifier: Quarantine failed for '{}': {}", filePath, e.what());
        return false;
    }
}

} // namespace ResolutePulse
