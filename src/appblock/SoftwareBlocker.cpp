#include "SoftwareBlocker.h"
#include "utils/Logger.h"

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

    // Restart monitors for all active blocks
    for (const auto& app : blockedApps_) {
        if (app.status == "active") {
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

bool SoftwareBlocker::blockWithRegistry(const std::string& executable) {
    LOG_DEBUG("SoftwareBlocker: Applying registry block: {}", executable);
    int successCount = 0;

    // Three registry locations (same as Python reference)
    struct RegLocation {
        HKEY hive;
        const wchar_t* basePath;
        const wchar_t* disallowPath;
        const char* name;
    };

    RegLocation locations[] = {
        { HKEY_LOCAL_MACHINE,
          L"SOFTWARE\\Policies\\Microsoft\\Windows\\Explorer",
          L"SOFTWARE\\Policies\\Microsoft\\Windows\\Explorer\\DisallowRun",
          "Group Policy" },
        { HKEY_LOCAL_MACHINE,
          L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer",
          L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer\\DisallowRun",
          "HKLM Standard" },
        { HKEY_CURRENT_USER,
          L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer",
          L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer\\DisallowRun",
          "HKCU" },
    };

    // Convert executable to wide string
    std::wstring wExe(executable.begin(), executable.end());

    for (const auto& loc : locations) {
        HKEY key = nullptr;
        LONG result;

        // Enable DisallowRun
        result = RegCreateKeyExW(loc.hive, loc.basePath, 0, nullptr,
                                  0, KEY_ALL_ACCESS, nullptr, &key, nullptr);
        if (result == ERROR_SUCCESS) {
            DWORD val = 1;
            RegSetValueExW(key, L"DisallowRun", 0, REG_DWORD,
                           reinterpret_cast<const BYTE*>(&val), sizeof(val));
            RegCloseKey(key);
        } else {
            LOG_WARN("SoftwareBlocker: Failed to open {} base key: {}",
                     loc.name, result);
            continue;
        }

        // Add executable to DisallowRun list
        result = RegCreateKeyExW(loc.hive, loc.disallowPath, 0, nullptr,
                                  0, KEY_ALL_ACCESS, nullptr, &key, nullptr);
        if (result != ERROR_SUCCESS) {
            LOG_WARN("SoftwareBlocker: Failed to open {} disallow key: {}",
                     loc.name, result);
            continue;
        }

        // Check if already present
        bool alreadyPresent = false;
        DWORD idx = 0;
        wchar_t valueName[256];
        DWORD valueNameLen;
        BYTE valueData[512];
        DWORD valueDataLen;
        DWORD valueType;

        int nextIndex = 1;
        while (true) {
            valueNameLen = 256;
            valueDataLen = 512;
            result = RegEnumValueW(key, idx, valueName, &valueNameLen,
                                    nullptr, &valueType, valueData, &valueDataLen);
            if (result != ERROR_SUCCESS) break;

            if (valueType == REG_SZ) {
                std::wstring existing(reinterpret_cast<wchar_t*>(valueData));
                std::string existingNarrow;
                existingNarrow.resize(existing.size());
                WideCharToMultiByte(CP_ACP, 0, existing.c_str(), -1,
                                     &existingNarrow[0], (int)existingNarrow.size() + 1,
                                     nullptr, nullptr);
                existingNarrow.resize(strlen(existingNarrow.c_str()));

                if (toLower(existingNarrow) == toLower(executable)) {
                    alreadyPresent = true;
                    break;
                }
            }

            // Track the next available index
            try {
                int n = std::stoi(std::wstring(valueName, valueNameLen));
                if (n >= nextIndex) nextIndex = n + 1;
            } catch (...) {}

            idx++;
        }

        if (!alreadyPresent) {
            std::wstring indexStr = std::to_wstring(nextIndex);
            result = RegSetValueExW(key, indexStr.c_str(), 0, REG_SZ,
                                     reinterpret_cast<const BYTE*>(wExe.c_str()),
                                     static_cast<DWORD>((wExe.size() + 1) * sizeof(wchar_t)));
            if (result == ERROR_SUCCESS) {
                LOG_DEBUG("SoftwareBlocker: {} - added at index {}", loc.name, nextIndex);
                successCount++;
            }
        } else {
            successCount++; // Already there counts as success
        }

        RegCloseKey(key);
    }

    LOG_DEBUG("SoftwareBlocker: Registry block: {}/3 locations succeeded", successCount);
    return successCount > 0;
}

bool SoftwareBlocker::removeRegistryBlock(const std::string& executable) {
    LOG_DEBUG("SoftwareBlocker: Removing registry block: {}", executable);
    int removed = 0;

    struct RegLocation {
        HKEY hive;
        const wchar_t* disallowPath;
        const char* name;
    };

    RegLocation locations[] = {
        { HKEY_LOCAL_MACHINE,
          L"SOFTWARE\\Policies\\Microsoft\\Windows\\Explorer\\DisallowRun",
          "Group Policy" },
        { HKEY_LOCAL_MACHINE,
          L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer\\DisallowRun",
          "HKLM" },
        { HKEY_CURRENT_USER,
          L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer\\DisallowRun",
          "HKCU" },
    };

    for (const auto& loc : locations) {
        HKEY key = nullptr;
        LONG result = RegOpenKeyExW(loc.hive, loc.disallowPath, 0, KEY_ALL_ACCESS, &key);
        if (result != ERROR_SUCCESS) continue;

        // Find and remove matching values
        std::vector<std::wstring> toDelete;
        DWORD idx = 0;
        wchar_t valueName[256];
        DWORD valueNameLen;
        BYTE valueData[512];
        DWORD valueDataLen;
        DWORD valueType;

        while (true) {
            valueNameLen = 256;
            valueDataLen = 512;
            result = RegEnumValueW(key, idx, valueName, &valueNameLen,
                                    nullptr, &valueType, valueData, &valueDataLen);
            if (result != ERROR_SUCCESS) break;

            if (valueType == REG_SZ) {
                std::wstring existing(reinterpret_cast<wchar_t*>(valueData));
                std::string existingNarrow;
                existingNarrow.resize(existing.size());
                WideCharToMultiByte(CP_ACP, 0, existing.c_str(), -1,
                                     &existingNarrow[0], (int)existingNarrow.size() + 1,
                                     nullptr, nullptr);
                existingNarrow.resize(strlen(existingNarrow.c_str()));

                if (toLower(existingNarrow) == toLower(executable)) {
                    toDelete.push_back(std::wstring(valueName, valueNameLen));
                }
            }
            idx++;
        }

        for (const auto& vn : toDelete) {
            if (RegDeleteValueW(key, vn.c_str()) == ERROR_SUCCESS) {
                removed++;
                LOG_DEBUG("SoftwareBlocker: Removed from {} [{}]", loc.name,
                          std::string(vn.begin(), vn.end()));
            }
        }

        RegCloseKey(key);
    }

    LOG_DEBUG("SoftwareBlocker: Removed {} registry entries", removed);
    return removed > 0;
}

void SoftwareBlocker::updateGroupPolicy() {
    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};

    wchar_t cmd[] = L"gpupdate /force";
    if (CreateProcessW(nullptr, cmd, nullptr, nullptr, FALSE,
                       CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 30000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        LOG_DEBUG("SoftwareBlocker: Group Policy updated");
    }
}

bool SoftwareBlocker::restartExplorer() {
    LOG_DEBUG("SoftwareBlocker: Restarting Explorer...");

    // Kill explorer
    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};

    wchar_t killCmd[] = L"taskkill /f /im explorer.exe";
    if (CreateProcessW(nullptr, killCmd, nullptr, nullptr, FALSE,
                       CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 5000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }

    Sleep(2000);

    // Restart explorer
    STARTUPINFOW si2 = {};
    si2.cb = sizeof(si2);
    PROCESS_INFORMATION pi2 = {};

    wchar_t explorerPath[] = L"explorer.exe";
    if (CreateProcessW(nullptr, explorerPath, nullptr, nullptr, FALSE,
                       0, nullptr, nullptr, &si2, &pi2)) {
        CloseHandle(pi2.hProcess);
        CloseHandle(pi2.hThread);
        Sleep(1000);
        LOG_DEBUG("SoftwareBlocker: Explorer restarted");
        return true;
    }

    LOG_WARN("SoftwareBlocker: Failed to restart Explorer");
    return false;
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

    // Step 1: Kill running processes
    int killed = terminateProcess(executable);

    // Step 2: Apply registry blocking
    bool registryOk = blockWithRegistry(executable);

    // Step 3: Update Group Policy
    updateGroupPolicy();

    // Step 4: Restart Explorer
    bool explorerRestarted = restartExplorer();

    // Step 5: Start process monitor
    if (running_.load()) {
        startProcessMonitor(executable);
    }

    // Step 6: Kill again (catch anything that restarted)
    Sleep(500);
    int killed2 = terminateProcess(executable);

    // Save to config
    BlockedApp app;
    app.name = name;
    app.executable = executable;
    app.blockedAt = getCurrentTimestamp();
    app.status = "active";
    app.kills = killed + killed2;
    app.registryApplied = registryOk;
    app.explorerRestarted = explorerRestarted;

    blockedApps_.push_back(app);
    saveBlockedApps();

    // Fire event
    if (eventCallback_) {
        nlohmann::json event;
        event["type"] = "application_blocked";
        event["name"] = name;
        event["executable"] = executable;
        event["kills"] = killed + killed2;
        event["registryApplied"] = registryOk;
        event["timestamp"] = getCurrentTimestamp();
        eventCallback_(event);
    }

    LOG_INFO("SoftwareBlocker: BLOCKED {} - kills={}, registry={}, explorer={}",
             executable, killed + killed2, registryOk, explorerRestarted);

    return {
        {"success", true},
        {"message", name + " blocked successfully"},
        {"details", {
            {"totalKills", killed + killed2},
            {"registryApplied", registryOk},
            {"explorerRestarted", explorerRestarted},
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

    // Step 1: Remove registry block
    bool registryRemoved = removeRegistryBlock(executable);

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

    // Step 4: Update Group Policy
    updateGroupPolicy();

    // Step 5: Restart Explorer
    bool explorerRestarted = restartExplorer();

    // Fire event
    if (eventCallback_) {
        nlohmann::json event;
        event["type"] = "application_unblocked";
        event["executable"] = executable;
        event["timestamp"] = getCurrentTimestamp();
        eventCallback_(event);
    }

    LOG_INFO("SoftwareBlocker: UNBLOCKED {} - registry={}, explorer={}",
             executable, registryRemoved, explorerRestarted);

    return {
        {"success", true},
        {"message", executable + " unblocked successfully"},
        {"details", {
            {"registryRemoved", registryRemoved},
            {"explorerRestarted", explorerRestarted}
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
                    removeRegistryBlock(app.executable);
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
            app.registryApplied = item.value("registryApplied", false);
            app.explorerRestarted = item.value("explorerRestarted", false);
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

