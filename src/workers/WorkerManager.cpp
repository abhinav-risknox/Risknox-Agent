#include "WorkerManager.h"
#include "utils/Logger.h"

#include <filesystem>
#include <stdexcept>

namespace ResolutePulse {

WorkerManager::WorkerManager() {
    watchdogThread_ = std::thread(&WorkerManager::watchdogLoop, this);
}

WorkerManager::~WorkerManager() {
    stopAll();
}

// ─────────────────────────────────────────────────────────────────────────────
// Process spawning
// ─────────────────────────────────────────────────────────────────────────────

HANDLE WorkerManager::spawnProcess(const std::string& exe) {
    // Resolve path relative to the directory of the current executable
    char selfPath[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, selfPath, MAX_PATH);
    std::filesystem::path agentDir = std::filesystem::path(selfPath).parent_path();
    std::filesystem::path exePath  = agentDir / exe;

    std::string cmdLine = "\"" + exePath.string() + "\"";

    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    // Workers inherit no console window - they're silent background processes
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi = {};
    BOOL ok = CreateProcessA(
        nullptr,
        const_cast<char*>(cmdLine.c_str()),
        nullptr,  // process security
        nullptr,  // thread security
        FALSE,    // don't inherit handles
        CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP,
        nullptr,  // inherit environment
        nullptr,  // inherit CWD
        &si, &pi);

    if (!ok) {
        LOG_ERROR("WorkerManager: CreateProcess failed for '{}' err={}", exe, GetLastError());
        return INVALID_HANDLE_VALUE;
    }

    CloseHandle(pi.hThread);  // we don't need the thread handle
    LOG_INFO("WorkerManager: spawned '{}' PID={}", exe, pi.dwProcessId);
    return pi.hProcess;
}

// ─────────────────────────────────────────────────────────────────────────────
// spawnWorker
// ─────────────────────────────────────────────────────────────────────────────

bool WorkerManager::spawnWorker(const std::string& exe,
                                 const std::string& pipeName,
                                 bool persistent) {
    std::lock_guard<std::mutex> lk(mutex_);

    HANDLE h = spawnProcess(exe);
    if (h == INVALID_HANDLE_VALUE) return false;

    WorkerEntry entry;
    entry.hProcess   = h;
    entry.exe        = exe;
    entry.pipeName   = pipeName;
    entry.persistent = persistent;
    entry.startedAt  = std::chrono::steady_clock::now();

    workers_[pipeName] = std::move(entry);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// sendCommand - request/response pattern (for persistent workers)
// ─────────────────────────────────────────────────────────────────────────────

nlohmann::json WorkerManager::sendCommand(const std::string& pipeName,
                                            const nlohmann::json& cmd,
                                            DWORD timeoutMs) {
    PipeClient pipe;
    if (!pipe.connect(pipeName, 3000)) {
        LOG_ERROR("WorkerManager::sendCommand: cannot connect to pipe '{}': {}",
                  pipeName, pipe.getLastError());
        return { {"ok", false}, {"error", "pipe connect failed: " + pipe.getLastError()} };
    }

    if (!pipe.sendJson(cmd)) {
        return { {"ok", false}, {"error", "pipe write failed: " + pipe.getLastError()} };
    }

    nlohmann::json response;
    if (!pipe.recvJson(response, timeoutMs)) {
        return { {"ok", false}, {"error", "pipe read failed: " + pipe.getLastError()} };
    }

    return response;
}

// ─────────────────────────────────────────────────────────────────────────────
// streamEvents - for on-demand workers that emit multiple events then exit
// (used by rp-patch.exe and rp-antivirus.exe)
// ─────────────────────────────────────────────────────────────────────────────

void WorkerManager::streamEvents(const std::string& pipeName,
                                   const nlohmann::json& cmd,
                                   std::function<void(const nlohmann::json&)> callback,
                                   DWORD timeoutMs) {
    PipeClient pipe;
    if (!pipe.connect(pipeName, 5000)) {
        LOG_ERROR("WorkerManager::streamEvents: cannot connect to pipe '{}'", pipeName);
        callback({ {"type", "error"}, {"message", "pipe connect failed"} });
        return;
    }

    if (!pipe.sendJson(cmd)) {
        callback({ {"type", "error"}, {"message", "pipe write failed"} });
        return;
    }

    // Drain events until the worker closes the pipe (EOF) or timeout
    DWORD elapsed = 0;
    while (elapsed < timeoutMs) {
        nlohmann::json event;
        if (pipe.recvJson(event, 1000)) {
            callback(event);
            // Worker signals completion with {"type":"complete"}
            if (event.contains("type") && event["type"] == "complete") break;
        } else {
            // Timeout on single read - check if worker process exited
            bool workerGone = false;
            {
                std::lock_guard<std::mutex> lk(mutex_);
                auto it = workers_.find(pipeName);
                if (it != workers_.end()) {
                    DWORD code = 0;
                    GetExitCodeProcess(it->second.hProcess, &code);
                    if (code != STILL_ACTIVE) {
                        workerGone = true;
                        CloseHandle(it->second.hProcess);
                        workers_.erase(it);
                    }
                }
            }
            if (workerGone) break;
            elapsed += 1000;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// isRunning
// ─────────────────────────────────────────────────────────────────────────────

bool WorkerManager::isRunning(const std::string& pipeName) const {
    std::lock_guard<std::mutex> lk(mutex_);
    auto it = workers_.find(pipeName);
    if (it == workers_.end()) return false;
    DWORD code = 0;
    GetExitCodeProcess(it->second.hProcess, &code);
    return code == STILL_ACTIVE;
}

// ─────────────────────────────────────────────────────────────────────────────
// Watchdog - restarts crashed persistent workers
// ─────────────────────────────────────────────────────────────────────────────

void WorkerManager::watchdogLoop() {
    while (running_.load()) {
        // Check every 5 seconds
        std::this_thread::sleep_for(std::chrono::seconds(5));

        std::lock_guard<std::mutex> lk(mutex_);
        for (auto& [pipeName, entry] : workers_) {
            if (!entry.persistent) continue;
            if (entry.hProcess == INVALID_HANDLE_VALUE) continue;

            DWORD code = 0;
            GetExitCodeProcess(entry.hProcess, &code);
            if (code == STILL_ACTIVE) continue;

            // Worker crashed or exited unexpectedly
            auto elapsed = std::chrono::steady_clock::now() - entry.startedAt;
            auto elapsedSecs = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();

            if (elapsedSecs < 2) {
                // Crashed too fast - back off to avoid restart storm
                LOG_WARN("WorkerManager: '{}' exited in {}s - backing off restart",
                         entry.exe, elapsedSecs);
                std::this_thread::sleep_for(std::chrono::seconds(10));
            }

            LOG_WARN("WorkerManager: persistent worker '{}' exited (code={}) - restarting",
                     entry.exe, code);

            CloseHandle(entry.hProcess);
            HANDLE h = spawnProcess(entry.exe);
            if (h != INVALID_HANDLE_VALUE) {
                entry.hProcess  = h;
                entry.startedAt = std::chrono::steady_clock::now();
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// stopAll
// ─────────────────────────────────────────────────────────────────────────────

void WorkerManager::stopAll() {
    running_ = false;
    if (watchdogThread_.joinable()) watchdogThread_.join();

    std::lock_guard<std::mutex> lk(mutex_);
    for (auto& [pipeName, entry] : workers_) {
        if (entry.hProcess == INVALID_HANDLE_VALUE) continue;
        TerminateProcess(entry.hProcess, 0);
        WaitForSingleObject(entry.hProcess, 3000);
        CloseHandle(entry.hProcess);
        LOG_INFO("WorkerManager: stopped worker '{}'", entry.exe);
    }
    workers_.clear();
}

} // namespace ResolutePulse

