#include "ThreatNotifier.h"
#include "utils/Logger.h"

#include <Windows.h>
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

    STARTUPINFOA si = {};
    si.cb          = sizeof(si);
    si.dwFlags     = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi = {};
    BOOL ok = CreateProcessA(
        nullptr,
        const_cast<char*>(cmdStr.c_str()),
        nullptr, nullptr,
        FALSE,
        0,     // No CREATE_NO_WINDOW — WPF needs to create its own window
        nullptr, nullptr,
        &si, &pi
    );

    if (!ok) {
        LOG_ERROR("ThreatNotifier: Failed to launch ThreatNotification.exe: {}", GetLastError());
        return ThreatAction::Quarantine;
    }

    // Allow the WPF window to come to foreground
    AllowSetForegroundWindow(pi.dwProcessId);

    // Wait for the process to exit (max 120s — double the typical timeout)
    DWORD waitResult = WaitForSingleObject(pi.hProcess, 120000);

    ThreatAction result = ThreatAction::Quarantine;
    if (waitResult == WAIT_OBJECT_0) {
        DWORD exitCode = 0;
        GetExitCodeProcess(pi.hProcess, &exitCode);
        result = parseExitCode(exitCode);
        LOG_INFO("ThreatNotifier: WPF exit code = {} → {}", exitCode,
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
