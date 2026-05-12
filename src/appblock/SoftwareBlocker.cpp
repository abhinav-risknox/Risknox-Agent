#include "SoftwareBlocker.h"
#include "utils/Logger.h"
#include "utils/PathUtils.h"

#include <Windows.h>
#include <TlHelp32.h>
#include <algorithm>
#include <fstream>
#include <filesystem>

namespace ResolutePulse {

SoftwareBlocker::SoftwareBlocker() = default;

SoftwareBlocker::~SoftwareBlocker() {
    stop();
}

std::string SoftwareBlocker::toLower(const std::string& s) const {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
}

std::string SoftwareBlocker::getCurrentTimestamp() const {
    time_t now = time(nullptr);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));
    return std::string(buf);
}

bool SoftwareBlocker::initialize(const AppBlockConfig& config) {
    config_ = config;

    if (!config_.configPath.empty()) {
        std::filesystem::path p(config_.configPath);
        if (p.has_parent_path()) {
            std::filesystem::create_directories(p.parent_path());
        }
    }

    loadBlockedApps();
    LOG_INFO("SoftwareBlocker initialized ({} blocked apps loaded)", blockedApps_.size());
    return true;
}

bool SoftwareBlocker::start() {
    if (running_.load()) return true;
    running_ = true;

    // Restart monitors and re-apply IFEO blocks for all active blocks
    for (const auto& app : blockedApps_) {
        if (app.status == "active") {
            // Re-apply IFEO block in case it was tampered with
            setIFEOBlock(app.executable);
            startProcessMonitor(app.executable);
        }
    }

    LOG_INFO("SoftwareBlocker started with {} active blocks", blockedApps_.size());
    return true;
}

void SoftwareBlocker::stop() {
    running_ = false;
    stopAllMonitors();
}

int SoftwareBlocker::terminateProcess(const std::string& executable) {
    LOG_DEBUG("SoftwareBlocker: Terminating processes: {}", executable);
    int killed = 0;
    std::string exeLower = toLower(executable);
    std::string exeNoExt = exeLower;
    if (exeNoExt.size() > 4 && exeNoExt.substr(exeNoExt.size() - 4) == ".exe") {
        exeNoExt = exeNoExt.substr(0, exeNoExt.size() - 4);
    }

    // Method 1: Toolhelp32 snapshot
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe = {};
        pe.dwSize = sizeof(pe);

        if (Process32FirstW(snapshot, &pe)) {
            do {
                // Convert wide process name to narrow for comparison
                char narrowName[MAX_PATH] = {};
                WideCharToMultiByte(CP_ACP, 0, pe.szExeFile, -1, narrowName, MAX_PATH, nullptr, nullptr);
                std::string procName = toLower(std::string(narrowName));

                if (procName == exeLower || procName == exeNoExt) {
                    HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                    if (hProcess) {
                        if (TerminateProcess(hProcess, 1)) {
                            LOG_DEBUG("  Killed PID: {}", pe.th32ProcessID);
                            killed++;
                        }
                        CloseHandle(hProcess);
                    }
                }
            } while (Process32NextW(snapshot, &pe));
        }
        CloseHandle(snapshot);
    }

    // Method 2: taskkill as fallback
    std::string cmd = "taskkill /F /IM " + executable + " /T";
    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};

    std::wstring wcmd(cmd.begin(), cmd.end());
    std::vector<wchar_t> cmdBuf(wcmd.begin(), wcmd.end());
    cmdBuf.push_back(L'\0');

    if (CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, FALSE,
                       CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 5000);
        DWORD exitCode = 0;
        GetExitCodeProcess(pi.hProcess, &exitCode);
        if (exitCode == 0) killed++;
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }

    LOG_DEBUG("SoftwareBlocker: Terminated {} instances of {}", killed, executable);
    return killed;
}

std::wstring SoftwareBlocker::getIFEODebuggerValue() const {
    // Resolve System32 dynamically via PathUtils (uses GetSystemDirectoryW)
    std::filesystem::path sysDir = PathUtils::getSystemDirectory();
    std::filesystem::path debuggerPath = sysDir / "rundll32.exe";

    // rundll32.exe with no valid args does nothing — app fails to launch
    return debuggerPath.wstring();
}

bool SoftwareBlocker::setIFEOBlock(const std::string& executable) {
    LOG_DEBUG("SoftwareBlocker: Applying IFEO block: {}", executable);

    // Registry path: HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\{exe}
    std::wstring subKey = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options\\";
    std::wstring wExe(executable.begin(), executable.end());
    subKey += wExe;

    HKEY key = nullptr;
    LONG result = RegCreateKeyExW(HKEY_LOCAL_MACHINE, subKey.c_str(), 0, nullptr,
                                   0, KEY_ALL_ACCESS, nullptr, &key, nullptr);
    if (result != ERROR_SUCCESS) {
        LOG_WARN("SoftwareBlocker: Failed to create IFEO key for {}: error {}", executable, result);
        return false;
    }

    // Build debugger value dynamically (no hardcoded paths)
    std::wstring debuggerValue = getIFEODebuggerValue();
    result = RegSetValueExW(key, L"Debugger", 0, REG_SZ,
                             reinterpret_cast<const BYTE*>(debuggerValue.c_str()),
                             static_cast<DWORD>((debuggerValue.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);

    if (result == ERROR_SUCCESS) {
        LOG_DEBUG("SoftwareBlocker: IFEO Debugger set for {}", executable);
        return true;
    }

    LOG_WARN("SoftwareBlocker: Failed to set IFEO Debugger for {}: error {}", executable, result);
    return false;
}

bool SoftwareBlocker::removeIFEOBlock(const std::string& executable) {
    LOG_DEBUG("SoftwareBlocker: Removing IFEO block: {}", executable);

    std::wstring subKey = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options\\";
    std::wstring wExe(executable.begin(), executable.end());
    subKey += wExe;

    HKEY key = nullptr;
    LONG result = RegOpenKeyExW(HKEY_LOCAL_MACHINE, subKey.c_str(), 0, KEY_ALL_ACCESS, &key);
    if (result != ERROR_SUCCESS) {
        LOG_DEBUG("SoftwareBlocker: No IFEO key for {} (already unblocked?)", executable);
        return false;
    }

    // Remove the Debugger value
    result = RegDeleteValueW(key, L"Debugger");
    bool removed = (result == ERROR_SUCCESS);
    if (removed) {
        LOG_DEBUG("SoftwareBlocker: IFEO Debugger removed for {}", executable);
    }

    // Check if key is now empty; if so, clean it up
    DWORD valueCount = 0;
    RegQueryInfoKeyW(key, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                      &valueCount, nullptr, nullptr, nullptr, nullptr);
    RegCloseKey(key);

    if (valueCount == 0) {
        RegDeleteKeyW(HKEY_LOCAL_MACHINE, subKey.c_str());
        LOG_DEBUG("SoftwareBlocker: IFEO key cleaned up for {}", executable);
    }

    return removed;
}

void SoftwareBlocker::monitorLoop(const std::string& executable) {
    std::string exeLower = toLower(executable);
    std::string exeNoExt = exeLower;
    if (exeNoExt.size() > 4 && exeNoExt.substr(exeNoExt.size() - 4) == ".exe") {
        exeNoExt = exeNoExt.substr(0, exeNoExt.size() - 4);
    }

    LOG_DEBUG("SoftwareBlocker: Monitor started for {}", executable);
    int totalKills = 0;

    while (running_.load()) {
        // Check if still blocked
        {
            std::lock_guard<std::mutex> lock(mutex_);
            bool stillBlocked = false;
            for (const auto& b : blockedApps_) {
                if (toLower(b.executable) == exeLower && b.status == "active") {
                    stillBlocked = true;
                    break;
                }
            }
            if (!stillBlocked) break;
        }

        // Scan for running processes
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W pe = {};
            pe.dwSize = sizeof(pe);

            if (Process32FirstW(snapshot, &pe)) {
                do {
                    char narrowName[MAX_PATH] = {};
                    WideCharToMultiByte(CP_ACP, 0, pe.szExeFile, -1,
                                         narrowName, MAX_PATH, nullptr, nullptr);
                    std::string procName = toLower(std::string(narrowName));

                    if (procName == exeLower || procName == exeNoExt) {
                        HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                        if (hProcess) {
                            if (TerminateProcess(hProcess, 1)) {
                                totalKills++;
                                LOG_DEBUG("SoftwareBlocker: Monitor killed {} (PID: {})",
                                          executable, pe.th32ProcessID);

                                // Fire event
                                if (eventCallback_) {
                                    nlohmann::json event;
                                    event["type"] = "software_blocked";
                                    event["executable"] = executable;
                                    event["pid"] = pe.th32ProcessID;
                                    event["timestamp"] = getCurrentTimestamp();
                                    eventCallback_(event);
                                }
                            }
                            CloseHandle(hProcess);
                        }
                    }
                } while (Process32NextW(snapshot, &pe));
            }
            CloseHandle(snapshot);
        }

        Sleep(config_.monitorIntervalMs);
    }

    LOG_DEBUG("SoftwareBlocker: Monitor stopped for {} (total kills: {})",
              executable, totalKills);
}

void SoftwareBlocker::startProcessMonitor(const std::string& executable) {
    std::string key = toLower(executable);
    // Don't start duplicate
    if (monitors_.find(key) != monitors_.end()) return;

    monitors_[key] = std::thread(&SoftwareBlocker::monitorLoop, this, executable);
}

void SoftwareBlocker::stopProcessMonitor(const std::string& executable) {
    std::string key = toLower(executable);
    auto it = monitors_.find(key);
    if (it != monitors_.end()) {
        if (it->second.joinable()) {
            it->second.detach(); // Let it exit on its own via the flag check
        }
        monitors_.erase(it);
    }
}

void SoftwareBlocker::stopAllMonitors() {
    for (auto& [key, thread] : monitors_) {
        if (thread.joinable()) {
            thread.detach();
        }
    }
    monitors_.clear();
}

nlohmann::json SoftwareBlocker::blockApplication(const std::string& name, const std::string& executable) {
    LOG_INFO("SoftwareBlocker: BLOCKING {} ({})", name, executable);

    std::lock_guard<std::mutex> lock(mutex_);

    // Check if already blocked
    for (const auto& b : blockedApps_) {
        if (toLower(b.executable) == toLower(executable)) {
            return {{"success", true}, {"message", "Already blocked"}};
        }
    }

    // Step 1: Apply IFEO registry block (prevents future launches immediately)
    bool ifeoOk = setIFEOBlock(executable);

    // Step 2: Kill running processes
    int killed = terminateProcess(executable);

    // Step 3: Start process monitor
    if (running_.load()) {
        startProcessMonitor(executable);
    }

    // Save to config
    BlockedApp app;
    app.name = name;
    app.executable = executable;
    app.blockedAt = getCurrentTimestamp();
    app.status = "active";
    app.kills = killed;
    app.ifeoApplied = ifeoOk;

    blockedApps_.push_back(app);
    saveBlockedApps();

    // Fire event
    if (eventCallback_) {
        nlohmann::json event;
        event["type"] = "application_blocked";
        event["name"] = name;
        event["executable"] = executable;
        event["kills"] = killed;
        event["ifeoApplied"] = ifeoOk;
        event["timestamp"] = getCurrentTimestamp();
        eventCallback_(event);
    }

    LOG_INFO("SoftwareBlocker: BLOCKED {} - kills={}, ifeo={}",
             executable, killed, ifeoOk);

    return {
        {"success", true},
        {"message", name + " blocked successfully"},
        {"details", {
            {"totalKills", killed},
            {"ifeoApplied", ifeoOk},
            {"monitorActive", running_.load()}
        }}
    };
}

nlohmann::json SoftwareBlocker::unblockApplication(const std::string& executable) {
    LOG_INFO("SoftwareBlocker: UNBLOCKING {}", executable);

    std::lock_guard<std::mutex> lock(mutex_);

    // Check if blocked
    std::string exeLower = toLower(executable);
    bool found = false;
    for (const auto& b : blockedApps_) {
        if (toLower(b.executable) == exeLower) { found = true; break; }
    }

    if (!found) {
        return {{"success", true}, {"message", "Not blocked"}};
    }

    // Step 1: Remove IFEO block
    bool ifeoRemoved = removeIFEOBlock(executable);

    // Step 2: Stop monitor
    stopProcessMonitor(executable);

    // Step 3: Remove from config
    blockedApps_.erase(
        std::remove_if(blockedApps_.begin(), blockedApps_.end(),
            [&exeLower, this](const BlockedApp& b) {
                return toLower(b.executable) == exeLower;
            }),
        blockedApps_.end());
    saveBlockedApps();

    // Fire event
    if (eventCallback_) {
        nlohmann::json event;
        event["type"] = "application_unblocked";
        event["executable"] = executable;
        event["timestamp"] = getCurrentTimestamp();
        eventCallback_(event);
    }

    LOG_INFO("SoftwareBlocker: UNBLOCKED {} - ifeo={}", executable, ifeoRemoved);

    return {
        {"success", true},
        {"message", executable + " unblocked successfully"},
        {"details", {
            {"ifeoRemoved", ifeoRemoved}
        }}
    };
}

std::vector<BlockedApp> SoftwareBlocker::getBlockedApplications() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return blockedApps_;
}

bool SoftwareBlocker::applyPolicy(const nlohmann::json& policy) {
    if (policy.contains("action")) {
        std::string action = policy["action"].get<std::string>();

        if (action == "block" && policy.contains("executable")) {
            std::string name = policy.value("name", policy["executable"].get<std::string>());
            blockApplication(name, policy["executable"].get<std::string>());
        } else if (action == "unblock" && policy.contains("executable")) {
            unblockApplication(policy["executable"].get<std::string>());
        } else if (action == "replace" && policy.contains("apps")) {
            // Full replacement: unblock all current, then block new set
            {
                std::lock_guard<std::mutex> lock(mutex_);
                for (const auto& app : blockedApps_) {
                    removeIFEOBlock(app.executable);
                    stopProcessMonitor(app.executable);
                }
                blockedApps_.clear();
            }

            for (const auto& appEntry : policy["apps"]) {
                std::string name = appEntry.value("name", "");
                std::string exe = appEntry.value("executable", "");
                if (!exe.empty()) {
                    blockApplication(name, exe);
                }
            }
        }
    }

    LOG_INFO("SoftwareBlocker: Policy applied");
    return true;
}

nlohmann::json SoftwareBlocker::getStatus() const {
    std::lock_guard<std::mutex> lock(mutex_);

    nlohmann::json status;
    status["enabled"] = config_.enabled;
    status["running"] = running_.load();
    status["blockedAppCount"] = blockedApps_.size();
    status["activeMonitors"] = monitors_.size();

    nlohmann::json apps = nlohmann::json::array();
    for (const auto& app : blockedApps_) {
        apps.push_back(app.toJson());
    }
    status["blockedApps"] = apps;
    return status;
}

void SoftwareBlocker::setEventCallback(BlockEventCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    eventCallback_ = std::move(callback);
}

bool SoftwareBlocker::loadBlockedApps() {
    try {
        if (config_.configPath.empty()) return true;
        if (!std::filesystem::exists(config_.configPath)) return true;

        std::ifstream in(config_.configPath);
        if (!in.is_open()) return false;

        nlohmann::json j = nlohmann::json::parse(in);
        blockedApps_.clear();

        for (const auto& item : j) {
            BlockedApp app;
            app.name = item.value("name", "");
            app.executable = item.value("executable", "");
            app.blockedAt = item.value("blockedAt", "");
            app.status = item.value("status", "active");
            app.kills = item.value("kills", 0);
            app.ifeoApplied = item.value("ifeoApplied", false);
            if (!app.executable.empty()) {
                blockedApps_.push_back(app);
            }
        }
        return true;
    } catch (const std::exception& e) {
        LOG_WARN("SoftwareBlocker: Failed to load blocked apps: {}", e.what());
        return false;
    }
}

bool SoftwareBlocker::saveBlockedApps() {
    try {
        if (config_.configPath.empty()) return true;

        nlohmann::json j = nlohmann::json::array();
        for (const auto& app : blockedApps_) {
            j.push_back(app.toJson());
        }

        std::ofstream out(config_.configPath);
        if (!out.is_open()) return false;
        out << j.dump(2);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("SoftwareBlocker: Failed to save blocked apps: {}", e.what());
        return false;
    }
}

} // namespace ResolutePulse

