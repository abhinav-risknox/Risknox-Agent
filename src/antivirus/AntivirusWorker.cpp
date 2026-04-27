#include "AntivirusWorker.h"
#include "ipc/PipeChannel.h"
#include "utils/Logger.h"

#include <windows.h>
#include <filesystem>
#include <string>
#include <regex>

namespace ResolutePulse {

AntivirusWorker::AntivirusWorker(const std::string& clamDir)
    : clamDir_(clamDir) {}

// ─────────────────────────────────────────────────────────────────────────────
// runScan - spawn clamscan.exe, read stdout line-by-line, send events to pipe
// ─────────────────────────────────────────────────────────────────────────────

void AntivirusWorker::runScan(const std::string& path, PipeServer& pipe) {
    namespace fs = std::filesystem;

    fs::path clamDir  = fs::path(clamDir_);
    fs::path clamscan = clamDir / "clamscan.exe";
    fs::path database = clamDir / "database";

    if (!fs::exists(clamscan)) {
        pipe.sendJson({ {"type","error"}, {"message","clamscan.exe not found"} });
        return;
    }

    // Build command line
    // --infected:   only print infected files
    // --recursive:  scan directories recursively
    // --no-summary: suppress summary (we emit our own complete event)
    std::string cmdLine =
        "\"" + clamscan.string() + "\""
        " --database=\"" + database.string() + "\""
        " --recursive"
        " \"" + path + "\"";

    // Set up anonymous pipe for clamscan stdout
    HANDLE hReadStdOut  = nullptr;
    HANDLE hWriteStdOut = nullptr;

    SECURITY_ATTRIBUTES sa = {};
    sa.nLength              = sizeof(sa);
    sa.bInheritHandle       = TRUE;  // child inherits the write end

    if (!CreatePipe(&hReadStdOut, &hWriteStdOut, &sa, 0)) {
        pipe.sendJson({ {"type","error"}, {"message","CreatePipe failed"} });
        return;
    }
    // Don't let the read end be inherited by the child
    SetHandleInformation(hReadStdOut, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si = {};
    si.cb         = sizeof(si);
    si.dwFlags    = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = hWriteStdOut;
    si.hStdError  = hWriteStdOut;  // send errors to stdout too
    si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi = {};
    BOOL ok = CreateProcessA(
        nullptr,
        const_cast<char*>(cmdLine.c_str()),
        nullptr, nullptr,
        TRUE,   // inherit handles (so child gets hWriteStdOut)
        CREATE_NO_WINDOW,
        nullptr, nullptr,
        &si, &pi);

    CloseHandle(hWriteStdOut);  // parent doesn't write to it

    if (!ok) {
        CloseHandle(hReadStdOut);
        pipe.sendJson({ {"type","error"}, {"message","CreateProcess failed"} });
        return;
    }

    // ── Stream stdout from clamscan line-by-line ──────────────────────────
    // clamscan output format:
    //   Scanning <path>
    //   <path>: <ThreatName> FOUND
    //   <path>: OK

    std::string lineBuf;
    char readBuf[4096];
    int  filesScanned = 0;
    int  threats      = 0;

    // Progress report frequency
    int progressInterval = 50;

    while (true) {
        DWORD bytesRead = 0;
        BOOL  success   = ReadFile(hReadStdOut, readBuf, sizeof(readBuf) - 1, &bytesRead, nullptr);
        if (!success || bytesRead == 0) break;  // EOF or error - clamscan exited

        readBuf[bytesRead] = '\0';
        lineBuf += readBuf;

        // Process complete lines
        size_t pos;
        while ((pos = lineBuf.find('\n')) != std::string::npos) {
            std::string line = lineBuf.substr(0, pos);
            lineBuf.erase(0, pos + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();

            if (line.find(": OK") != std::string::npos || line.find(": Empty file") != std::string::npos) {
                filesScanned++;
                if (filesScanned % progressInterval == 0) {
                    pipe.sendJson({ {"type","progress"}, {"filesScanned", filesScanned} });
                }
            } else if (line.find("FOUND") != std::string::npos) {
                // "<path>: <ThreatName> FOUND"
                auto colonPos = line.rfind(": ");
                std::string filePath = (colonPos != std::string::npos)
                                     ? line.substr(0, colonPos)
                                     : line;
                std::string threat   = "";
                if (colonPos != std::string::npos) {
                    std::string rest = line.substr(colonPos + 2);
                    auto foundPos = rest.rfind(" FOUND");
                    if (foundPos != std::string::npos) threat = rest.substr(0, foundPos);
                }
                threats++;
                filesScanned++;
                pipe.sendJson({
                    {"type",   "threat"},
                    {"file",   filePath},
                    {"threat", threat}
                });
                LOG_WARN("THREAT DETECTED: {} - {}", filePath, threat);
            }
        }
    }

    CloseHandle(hReadStdOut);
    WaitForSingleObject(pi.hProcess, 5000);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    // Emit final summary
    pipe.sendJson({
        {"type",         "complete"},
        {"filesScanned", filesScanned},
        {"threats",      threats}
    });

    LOG_INFO("AV scan complete: {} files scanned, {} threats", filesScanned, threats);
}

// ─────────────────────────────────────────────────────────────────────────────
// updateDefinitions - run freshclam to refresh virus signatures
// ─────────────────────────────────────────────────────────────────────────────

bool AntivirusWorker::updateDefinitions() {
    namespace fs = std::filesystem;
    fs::path freshclam = fs::path(clamDir_) / "freshclam.exe";
    fs::path database  = fs::path(clamDir_) / "database";

    if (!fs::exists(freshclam)) {
        LOG_WARN("freshclam.exe not found at {}", freshclam.string());
        return false;
    }

    std::string cmdLine =
        "\"" + freshclam.string() + "\""
        " --datadir=\"" + database.string() + "\""
        " --quiet";

    STARTUPINFOA si = {};
    si.cb          = sizeof(si);
    si.dwFlags     = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi = {};
    BOOL ok = CreateProcessA(
        nullptr, const_cast<char*>(cmdLine.c_str()),
        nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);

    if (!ok) {
        LOG_ERROR("Failed to start freshclam: err={}", GetLastError());
        return false;
    }

    // Wait up to 10 minutes
    DWORD result = WaitForSingleObject(pi.hProcess, 10 * 60 * 1000);
    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    if (result != WAIT_OBJECT_0 || exitCode != 0) {
        LOG_ERROR("freshclam exited with code {}", exitCode);
        return false;
    }

    LOG_INFO("freshclam: virus database updated successfully");
    return true;
}

} // namespace ResolutePulse


// ─────────────────────────────────────────────────────────────────────────────
// rp-antivirus.exe  - standalone entry point
// ─────────────────────────────────────────────────────────────────────────────

int main() {
    using namespace ResolutePulse;

    // Resolve clamav dir relative to this executable
    char selfPath[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, selfPath, MAX_PATH);
    std::filesystem::path agentDir = std::filesystem::path(selfPath).parent_path();
    // Resolve clamav dir: prefer bundled copy next to this exe,
    // fall back to a system-wide ClamAV installation.
    std::filesystem::path bundledClamDir = agentDir / "clamav";
    std::string clamDir;

    if (std::filesystem::exists(bundledClamDir / "clamscan.exe")) {
        clamDir = bundledClamDir.string();
        LOG_INFO("rp-antivirus: using bundled ClamAV at {}", clamDir);
    } else {
        clamDir = "C:\\Program Files\\ClamAV";
        LOG_WARN("rp-antivirus: bundled ClamAV not found, falling back to system path: {}", clamDir);
    }

    AntivirusWorker worker(clamDir);

    // One-shot: open pipe, receive command, run scan, exit
    PipeServer pipe;
    if (!pipe.listen("rp-antivirus")) {
        return 1;
    }

    nlohmann::json cmd;
    if (!pipe.recvJson(cmd, 10000)) {
        return 1;
    }

    std::string action = cmd.value("action", "quick_scan");
    std::string path   = cmd.value("path", agentDir.string());  // default: scan agent dir

    if (action == "update_definitions") {
        bool ok = worker.updateDefinitions();
        pipe.sendJson({
            {"type",    "complete"},
            {"success", ok}
        });
    } else {
        // quick_scan or full_scan
        worker.runScan(path, pipe);
    }

    pipe.close();
    return 0;
}

