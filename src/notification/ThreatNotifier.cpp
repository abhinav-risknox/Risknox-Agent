#include "ThreatNotifier.h"
#include "utils/Logger.h"

#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <chrono>
#include <ctime>

namespace ResolutePulse {

// ─────────────────────────────────────────────────────────────────────────────
// notify — spawn Notification.ps1 in the active user session and read result
// ─────────────────────────────────────────────────────────────────────────────

ThreatAction ThreatNotifier::notify(const ThreatNotification& info) const {
    if (scriptPath_.empty() || !std::filesystem::exists(scriptPath_)) {
        LOG_WARN("ThreatNotifier: Notification.ps1 not found at '{}', defaulting to quarantine",
                 scriptPath_);
        return ThreatAction::Quarantine;
    }

    // Write result to a temp file so we don't need a stdout pipe.
    // Piping stdout causes PowerShell to detect non-console output and suppresses
    // the WinForms window. Using a result file matches ScanCompleteToast's spawn
    // pattern (no STARTF_USESTDHANDLES) which is known to show its window correctly.
    char tempDir[MAX_PATH] = {};
    GetTempPathA(MAX_PATH, tempDir);
    std::string resultFile = std::string(tempDir) + "rp_notifier_result.txt";

    // Remove any stale result file from a previous run
    DeleteFileA(resultFile.c_str());

    std::ostringstream cmd;
    cmd << "powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass"
        << " -File \"" << scriptPath_ << "\""
        << " -FileName \""        << info.fileName        << "\""
        << " -ThreatName \""      << info.threatName      << "\""
        << " -FilePath \""        << info.filePath        << "\""
        << " -SourceUrl \""       << info.sourceUrl       << "\""
        << " -AutoCloseSeconds "  << info.autoCloseSeconds
        << " -ResultFile \""      << resultFile           << "\"";

    std::string cmdStr = cmd.str();

    // No pipe handles — spawn exactly like ScanCompleteToast.ps1 so the WinForms
    // window is not suppressed by STARTF_USESTDHANDLES stdout redirection.
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
        CREATE_NO_WINDOW,
        nullptr, nullptr,
        &si, &pi
    );

    if (!ok) {
        LOG_ERROR("ThreatNotifier: Failed to launch Notification.ps1: {}", GetLastError());
        return ThreatAction::Quarantine;
    }

    // Allow the PowerShell process to bring its WinForms window to the foreground.
    AllowSetForegroundWindow(pi.dwProcessId);

    // Wait for the script to exit (max 60s — auto-close default is 10s)
    WaitForSingleObject(pi.hProcess, 60000);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    // Read the result from the temp file
    std::string output;
    std::ifstream rf(resultFile);
    if (rf.is_open()) {
        std::getline(rf, output);
        rf.close();
    }
    DeleteFileA(resultFile.c_str());

    // Trim trailing whitespace
    while (!output.empty() && (output.back() == '\r' || output.back() == '\n' || output.back() == ' '))
        output.pop_back();

    LOG_INFO("ThreatNotifier: User action = '{}'", output);
    return parseAction(output);
}

ThreatAction ThreatNotifier::parseAction(const std::string& output) const {
    if (output == "QUARANTINE") return ThreatAction::Quarantine;
    if (output == "IGNORE")     return ThreatAction::Ignore;
    if (output == "DETAILS")    return ThreatAction::Details;
    if (output == "DISMISSED")  return ThreatAction::Dismissed;
    return ThreatAction::Unknown;
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
