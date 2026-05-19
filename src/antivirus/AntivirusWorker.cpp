#include "AntivirusWorker.h"
#include "ipc/PipeChannel.h"
#include "utils/Logger.h"
#include "utils/PathUtils.h"

#include <windows.h>
#include <filesystem>
#include <string>
#include <regex>
#include <vector>
#include <algorithm>
#include <chrono>
#include <fstream>

namespace ResolutePulse {

AntivirusWorker::AntivirusWorker(const std::string& binDir, const std::string& dbDir)
    : binDir_(binDir), dbDir_(dbDir) {}

// ─────────────────────────────────────────────────────────────────────────────
// runScan - spawn clamscan.exe, read stdout line-by-line, send events to pipe
// ─────────────────────────────────────────────────────────────────────────────

void AntivirusWorker::runScan(const std::string& path, PipeServer& pipe) {
    namespace fs = std::filesystem;

    fs::path binDir  = fs::path(binDir_);
    fs::path clamscan = binDir / "clamscan.exe";
    fs::path database = fs::path(dbDir_);
    fs::path bundledDatabase = binDir / "database";
    fs::path cvdCertsDir = binDir / "certs";
    fs::path logDir = PathUtils::getAgentDataDir() / "antivirus";
    fs::path scanLog = logDir / "clamscan.log";

    if (!fs::exists(clamscan)) {
        pipe.sendJson({ {"type","error"}, {"message","clamscan.exe not found"} });
        return;
    }

    if (!fs::exists(database) && fs::exists(bundledDatabase)) {
        LOG_WARN("AV database not found at {}, falling back to bundled database at {}",
                 database.string(), bundledDatabase.string());
        database = bundledDatabase;
    }

    if (!fs::exists(database)) {
        pipe.sendJson({
            {"type", "error"},
            {"message", "ClamAV database directory not found"},
            {"databaseDir", database.string()}
        });
        return;
    }

    if (!fs::exists(cvdCertsDir)) {
        pipe.sendJson({
            {"type", "error"},
            {"message", "ClamAV CVD certs directory not found"},
            {"cvdCertsDir", cvdCertsDir.string()}
        });
        return;
    }

    std::error_code logEc;
    fs::create_directories(logDir, logEc);
    if (logEc) {
        pipe.sendJson({
            {"type", "error"},
            {"message", "Failed to create ClamAV log directory"},
            {"logDir", logDir.string()},
            {"error", logEc.message()}
        });
        return;
    }

    // Build command line
    // --recursive:  scan directories recursively
    std::string cmdLine =
        "\"" + clamscan.string() + "\""
        " --database=\"" + database.string() + "\""
        " --cvdcertsdir=\"" + cvdCertsDir.string() + "\""
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
    std::vector<std::string> diagnosticLines;
    std::ofstream scanLogStream(scanLog.string(), std::ios::app);
    if (scanLogStream.is_open()) {
        auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        char ts[32] = {};
        std::tm tmUtc{};
        gmtime_s(&tmUtc, &now);
        std::strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", &tmUtc);
        scanLogStream << "=== Risknox ClamAV scan started "
                      << ts
                      << " path=\"" << path
                      << "\" database=\"" << database.string()
                      << "\" ===\n";
    } else {
        LOG_WARN("Unable to open ClamAV scan log at {}", scanLog.string());
    }

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
            if (scanLogStream.is_open()) {
                scanLogStream << line << "\n";
            }

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
            } else if (!line.empty()
                       && line.rfind("Scanning ", 0) != 0
                       && line.rfind("Loading: ", 0) != 0) {
                diagnosticLines.push_back(line);
                LOG_WARN("clamscan: {}", line);
            }
        }
    }

    CloseHandle(hReadStdOut);
    WaitForSingleObject(pi.hProcess, 5000);
    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    if (exitCode != 0 && exitCode != 1) {
        if (scanLogStream.is_open()) {
            scanLogStream << "=== Risknox ClamAV scan failed exitCode="
                          << exitCode
                          << " filesScanned=" << filesScanned
                          << " threats=" << threats
                          << " ===\n";
        }
        nlohmann::json error = {
            {"type", "error"},
            {"message", "clamscan failed"},
            {"exitCode", exitCode},
            {"filesScanned", filesScanned},
            {"threats", threats},
            {"databaseDir", database.string()},
            {"cvdCertsDir", cvdCertsDir.string()},
            {"logFile", scanLog.string()},
            {"scanPath", path}
        };
        if (!diagnosticLines.empty()) {
            error["details"] = diagnosticLines;
        }
        pipe.sendJson(error);
        LOG_ERROR("AV scan failed: exitCode={} path={} database={}",
                  exitCode, path, database.string());
        return;
    }

    // Emit final summary
    if (scanLogStream.is_open()) {
        scanLogStream << "=== Risknox ClamAV scan complete filesScanned="
                      << filesScanned
                      << " threats=" << threats
                      << " ===\n";
    }

    pipe.sendJson({
        {"type",         "complete"},
        {"filesScanned", filesScanned},
        {"threats",      threats},
        {"databaseDir",  database.string()},
        {"cvdCertsDir",  cvdCertsDir.string()},
        {"logFile",      scanLog.string()},
        {"scanPath",     path}
    });

    LOG_INFO("AV scan complete: {} files scanned, {} threats", filesScanned, threats);
}

// ─────────────────────────────────────────────────────────────────────────────
// updateDefinitions - run freshclam to refresh virus signatures
// ─────────────────────────────────────────────────────────────────────────────

bool AntivirusWorker::updateDefinitions() {
    namespace fs = std::filesystem;
    fs::path freshclam = fs::path(binDir_) / "freshclam.exe";
    fs::path database  = fs::path(dbDir_);
    fs::path cvdCertsDir = fs::path(binDir_) / "certs";
    fs::create_directories(database);

    if (!fs::exists(freshclam)) {
        LOG_WARN("freshclam.exe not found at {}", freshclam.string());
        return false;
    }

    if (!fs::exists(cvdCertsDir)) {
        LOG_WARN("ClamAV CVD certs directory not found at {}", cvdCertsDir.string());
        return false;
    }

    std::string cmdLine =
        "\"" + freshclam.string() + "\""
        " --datadir=\"" + database.string() + "\""
        " --cvdcertsdir=\"" + cvdCertsDir.string() + "\""
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

nlohmann::json AntivirusWorker::getDatabaseInfo() const {
    namespace fs = std::filesystem;

    auto toIsoUtc = [](std::time_t ts) -> std::string {
        if (ts <= 0) return "";
        std::tm tmUtc{};
        gmtime_s(&tmUtc, &ts);
        char buf[32] = {};
        std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tmUtc);
        return std::string(buf);
    };

    auto toEpochSeconds = [](fs::file_time_type ftime) -> std::int64_t {
        const auto sysNow = std::chrono::system_clock::now();
        const auto fsNow = fs::file_time_type::clock::now();
        const auto sysTp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            ftime - fsNow + sysNow);
        return static_cast<std::int64_t>(std::chrono::system_clock::to_time_t(sysTp));
    };

    fs::path dbDir = fs::path(dbDir_);
    fs::path bundledDbDir = fs::path(binDir_) / "database";

    if (!fs::exists(dbDir) && fs::exists(bundledDbDir)) {
        dbDir = bundledDbDir;
    }

    nlohmann::json out;
    out["databaseDir"] = dbDir.string();
    out["files"] = nlohmann::json::array();
    out["filesFound"] = 0;
    out["filesExpected"] = 6;
    out["totalSizeBytes"] = 0;
    out["totalSizeMb"] = 0.0;
    out["latestModifiedEpoch"] = 0;
    out["latestModified"] = "";

    if (!fs::exists(dbDir)) {
        out["error"] = "database directory not found";
        return out;
    }

    const std::vector<std::string> names = {"main", "daily", "bytecode"};
    const std::vector<std::string> exts = {".cld", ".cvd"};

    std::uintmax_t totalSizeBytes = 0;
    std::int64_t latestEpoch = 0;

    for (const auto& name : names) {
        for (const auto& ext : exts) {
            fs::path p = dbDir / (name + ext);
            if (!fs::exists(p)) {
                continue;
            }

            std::error_code ec;
            std::uintmax_t sizeBytes = fs::file_size(p, ec);
            if (ec) sizeBytes = 0;

            auto lastWrite = fs::last_write_time(p, ec);
            std::int64_t mtimeEpoch = 0;
            if (!ec) {
                mtimeEpoch = toEpochSeconds(lastWrite);
            }

            nlohmann::json file;
            file["name"] = p.filename().string();
            file["path"] = p.string();
            file["sizeBytes"] = sizeBytes;
            file["sizeMb"] = static_cast<double>(sizeBytes) / (1024.0 * 1024.0);
            file["lastModifiedEpoch"] = mtimeEpoch;
            file["lastModified"] = toIsoUtc(static_cast<std::time_t>(mtimeEpoch));

            out["files"].push_back(file);
            totalSizeBytes += sizeBytes;
            latestEpoch = std::max(latestEpoch, mtimeEpoch);
        }
    }

    out["filesFound"] = out["files"].size();
    out["totalSizeBytes"] = totalSizeBytes;
    out["totalSizeMb"] = static_cast<double>(totalSizeBytes) / (1024.0 * 1024.0);
    out["latestModifiedEpoch"] = latestEpoch;
    out["latestModified"] = toIsoUtc(static_cast<std::time_t>(latestEpoch));

    return out;
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
    // Resolve binDir: prefer bundled copy next to this exe
    std::filesystem::path agentDir = PathUtils::getExecutableDir();
    std::filesystem::path bundledClamDir = agentDir / "clamav";
    std::string binDir;

    if (std::filesystem::exists(bundledClamDir / "clamscan.exe")) {
        binDir = bundledClamDir.string();
        LOG_INFO("rp-antivirus: using bundled ClamAV at {}", binDir);
    } else {
        // Fallback to standard system paths
        std::filesystem::path pfClam = PathUtils::getProgramFilesPath() / "ClamAV";
        std::filesystem::path pf86Clam = std::filesystem::path(std::getenv("ProgramFiles(x86)") ? std::getenv("ProgramFiles(x86)") : "C:\\Program Files (x86)") / "ClamAV";

        if (std::filesystem::exists(pfClam / "clamscan.exe")) {
            binDir = pfClam.string();
        } else if (std::filesystem::exists(pf86Clam / "clamscan.exe")) {
            binDir = pf86Clam.string();
        } else {
            binDir = pfClam.string(); // Last resort fallback
        }
        LOG_WARN("rp-antivirus: bundled ClamAV not found, using system path: {}", binDir);
    }

    // Resolve dbDir: Always prefer ProgramData for writability (required for freshclam)
    std::filesystem::path dataDir = PathUtils::getAgentDataDir() / "antivirus" / "database";
    std::filesystem::create_directories(dataDir);
    std::string dbDir = dataDir.string();
    LOG_INFO("rp-antivirus: using database directory at {}", dbDir);

    AntivirusWorker worker(binDir, dbDir);

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
            {"action",  "update_definitions"},
            {"success", ok}
        });
    } else if (action == "database_info") {
        auto info = worker.getDatabaseInfo();
        bool ok = !info.contains("error");
        pipe.sendJson({
            {"type",     "complete"},
            {"action",   "database_info"},
            {"success",  ok},
            {"database", info}
        });
    } else {
        // quick_scan or full_scan
        worker.runScan(path, pipe);
    }

    pipe.close();
    return 0;
}

