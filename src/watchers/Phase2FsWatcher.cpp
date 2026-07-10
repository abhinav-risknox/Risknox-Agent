#include "utils/Logger.h"

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <thread>

namespace fs = std::filesystem;

static fs::path programDataPath() {
    const char* pd = std::getenv("ProgramData");
    return fs::path(pd ? pd : "C:\\ProgramData") / "YourAgent";
}

static fs::path logPath() { return programDataPath() / "Logs" / "fswatcher.log"; }
static fs::path quarantinePath() { return programDataPath() / "Quarantine"; }
static fs::path ignoreLogPath() { return programDataPath() / "Logs" / "ignored.log"; }
static fs::path notificationExe() { return fs::current_path() / "notification_popup.exe"; }

static void ensureDirs() {
    std::error_code ec;
    fs::create_directories(logPath().parent_path(), ec);
    fs::create_directories(quarantinePath(), ec);
    fs::create_directories(ignoreLogPath().parent_path(), ec);
}

static void writeLog(const std::string& msg, const std::string& level = "INFO") {
    ensureDirs();
    std::ofstream ofs(logPath(), std::ios::app);
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    char ts[32] = {};
    std::tm tm{};
    gmtime_s(&tm, &now);
    std::strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm);
    std::string entry = "[" + std::string(ts) + "][" + level + "] " + msg;
    if (ofs) ofs << entry << "\n";
    std::cout << entry << std::endl;
    LOG_INFO("{}", entry);
}

static std::wstring toWide(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring out(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), out.data(), len);
    return out;
}

static std::string toUtf8(const std::wstring& s) {
    if (s.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string out(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), out.data(), len, nullptr, nullptr);
    return out;
}

static bool testMotW(const fs::path& filePath, std::string& zoneId, std::string& hostUrl) {
    zoneId.clear();
    hostUrl.clear();
    std::wstring ads = filePath.wstring() + L":Zone.Identifier";
    HANDLE h = CreateFileW(ads.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    std::string content;
    char buf[1024];
    DWORD read = 0;
    while (ReadFile(h, buf, sizeof(buf), &read, nullptr) && read > 0) {
        content.append(buf, buf + read);
    }
    CloseHandle(h);

    auto z = content.find("ZoneId=");
    if (z != std::string::npos) {
        auto end = content.find_first_of("\r\n", z);
        zoneId = content.substr(z + 7, end == std::string::npos ? std::string::npos : end - (z + 7));
    }
    auto hpos = content.find("HostUrl=");
    if (hpos != std::string::npos) {
        auto end = content.find_first_of("\r\n", hpos);
        hostUrl = content.substr(hpos + 8, end == std::string::npos ? std::string::npos : end - (hpos + 8));
    }
    return zoneId == "3" || zoneId == "4";
}

static bool isThreatByExtension(const fs::path& p) {
    static const std::set<std::wstring> risky = {
        L".exe", L".bat", L".vbs", L".msi", L".ps1", L".js", L".cmd", L".scr", L".dll"
    };
    auto ext = p.extension().wstring();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
    return risky.find(ext) != risky.end();
}

static bool invokeNotification(const fs::path& filePath, const std::wstring& threat, const std::wstring& sourceUrl, std::wstring& actionOut) {
    fs::path exe = notificationExe();
    if (!fs::exists(exe)) {
        actionOut = L"QUARANTINE";
        return false;
    }

    std::wstring cmd = L"\"" + exe.wstring() + L"\""
        L" --FileName=\"" + filePath.filename().wstring() + L"\""
        L" --ThreatName=\"" + threat + L"\""
        L" --FilePath=\"" + filePath.wstring() + L"\""
        L" --SourceUrl=\"" + sourceUrl + L"\""
        L" --AutoCloseSeconds=10";

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE readPipe = nullptr, writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &sa, 0)) {
        actionOut = L"QUARANTINE";
        return false;
    }
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = writePipe;
    si.hStdError = writePipe;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi{};
    std::wstring mutableCmd = cmd;
    BOOL ok = CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, TRUE,
                             CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(writePipe);
    if (!ok) {
        CloseHandle(readPipe);
        actionOut = L"QUARANTINE";
        return false;
    }

    std::string output;
    char buf[256];
    DWORD read = 0;
    while (ReadFile(readPipe, buf, sizeof(buf), &read, nullptr) && read > 0) {
        output.append(buf, buf + read);
    }
    CloseHandle(readPipe);
    WaitForSingleObject(pi.hProcess, 5000);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    auto trimmed = output;
    trimmed.erase(std::remove_if(trimmed.begin(), trimmed.end(), [](char c) { return c == '\r' || c == '\n'; }), trimmed.end());
    if (trimmed.empty()) trimmed = "QUARANTINE";
    actionOut = toWide(trimmed);
    return true;
}

static fs::path quarantineFile(const fs::path& filePath, const std::wstring& threatName) {
    ensureDirs();
    auto ts = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    char stamp[32] = {};
    std::tm tm{};
    gmtime_s(&tm, &ts);
    std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", &tm);

    fs::path dest = quarantinePath() / (std::string(stamp) + "_" + filePath.filename().string());
    std::error_code ec;
    fs::rename(filePath, dest, ec);
    if (ec) {
        fs::copy_file(filePath, dest, fs::copy_options::overwrite_existing, ec);
        if (!ec) fs::remove(filePath, ec);
    }
    std::ofstream meta(dest.string() + ".json");
    if (meta) {
        meta << "{";
        meta << "\"OriginalPath\":\"" << toUtf8(filePath.wstring()) << "\",";
        meta << "\"QuarantinedAt\":\"";
        meta << stamp << "\",";
        meta << "\"ThreatName\":\"" << toUtf8(threatName) << "\",";
        meta << "\"QuarantinedTo\":\"" << toUtf8(dest.wstring()) << "\",";
        meta << "\"FileHash\":\"\"";
        meta << "}";
    }
    writeLog("Quarantined: " + filePath.filename().string() + " -> " + dest.string(), "WARN");
    return dest;
}

static void handleFile(const fs::path& filePath) {
    auto ext = filePath.extension().string();
    if (ext == ".crdownload" || ext == ".part" || ext == ".tmp" || ext == ".download") return;

    std::ifstream test(filePath, std::ios::binary);
    if (!test.good()) return;
    test.close();

    writeLog("New file detected: " + filePath.filename().string());

    std::string zoneId, hostUrl;
    if (!testMotW(filePath, zoneId, hostUrl)) {
        writeLog("Skipped (no MotW): " + filePath.filename().string(), "INFO");
        return;
    }

    writeLog("MotW Zone " + zoneId + " confirmed - scanning: " + filePath.filename().string());

    if (!isThreatByExtension(filePath)) {
        writeLog("CLEAN: " + filePath.filename().string(), "OK");
        return;
    }

    writeLog("THREAT DETECTED: " + filePath.filename().string() + " | Win.Test.MockThreat-1", "THREAT");

    std::wstring action;
    std::wstring sourceUrl = hostUrl.empty() ? L"Unknown" : toWide(hostUrl);
    invokeNotification(filePath, L"Win.Test.MockThreat-1", sourceUrl, action);
    writeLog("User action: " + toUtf8(action), "INFO");

    if (action == L"QUARANTINE") {
        quarantineFile(filePath, L"Win.Test.MockThreat-1");
    } else if (action == L"IGNORE") {
        writeLog("IGNORED by admin: " + filePath.filename().string() + " | Win.Test.MockThreat-1", "IGNORE");
        std::ofstream ofs(ignoreLogPath(), std::ios::app);
        if (ofs) {
            ofs << std::chrono::system_clock::to_time_t(std::chrono::system_clock::now())
                << " | " << filePath.string() << " | Win.Test.MockThreat-1\n";
        }
    } else if (action == L"DETAILS") {
        writeLog("Details requested for: " + filePath.filename().string(), "INFO");
        quarantineFile(filePath, L"Win.Test.MockThreat-1");
        ShellExecuteW(nullptr, L"open", quarantinePath().wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    } else if (action == L"DISMISSED") {
        writeLog("Notification dismissed - auto-quarantining: " + filePath.filename().string(), "WARN");
        quarantineFile(filePath, L"Win.Test.MockThreat-1");
    } else {
        writeLog("Unknown action (" + toUtf8(action) + ") - auto-quarantining: " + filePath.filename().string(), "WARN");
        quarantineFile(filePath, L"Win.Test.MockThreat-1");
    }
}

int main() {
    const fs::path watchPath = fs::path(std::getenv("USERPROFILE") ? std::getenv("USERPROFILE") : "C:\\Users") / "Downloads";
    ensureDirs();
    writeLog("=== YourAgent FSWatcher Starting ===");
    writeLog("Watching    : " + watchPath.string());
    writeLog("Quarantine  : " + quarantinePath().string());
    writeLog("Notification: " + notificationExe().string());
    writeLog("Poll every  : 2s");

    std::map<std::string, bool> processed;
    if (fs::exists(watchPath)) {
        for (auto& entry : fs::directory_iterator(watchPath, fs::directory_options::skip_permission_denied)) {
            if (entry.is_regular_file()) processed[entry.path().string()] = true;
        }
    }
    writeLog("Seeded " + std::to_string(processed.size()) + " existing files");

    try {
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            if (!fs::exists(watchPath)) continue;

            for (auto& entry : fs::directory_iterator(watchPath, fs::directory_options::skip_permission_denied)) {
                if (!entry.is_regular_file()) continue;
                auto full = entry.path().string();
                if (processed.find(full) == processed.end()) {
                    processed[full] = true;
                    handleFile(entry.path());
                }
            }
        }
    } catch (...) {
        writeLog("=== FSWatcher stopped ===", "WARN");
    }

    return 0;
}
