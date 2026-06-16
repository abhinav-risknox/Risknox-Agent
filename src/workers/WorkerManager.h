#pragma once

// WorkerManager.h - Manages spawning, monitoring, and communicating with
// worker child processes (rp-softblock.exe, rp-webblock.exe, etc.)
//
// Persistent workers: started at agent startup, restarted on crash.
// On-demand workers: spawned per-job, exit when done.

#include "ipc/PipeChannel.h"

#include <string>
#include <map>
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <nlohmann/json.hpp>

#ifdef _WIN32
#include <windows.h>
#endif

namespace ResolutePulse {

class WorkerManager {
public:
    WorkerManager();
    ~WorkerManager();

    // Spawn a worker process.
    // @param exe         Path to the worker executable (relative to agent dir)
    // @param pipeName    Named pipe identifier (without \\.\pipe\ prefix)
    // @param persistent  If true, worker is restarted on crash. If false, spawned
    //                    on-demand and removed from registry when it exits.
    bool spawnWorker(const std::string& exe,
                     const std::string& pipeName,
                     bool persistent = false);

    // Overload that passes extra CLI arguments to the worker process.
    // Used to pass unique pipe names: --pipe rp-antivirus-42
    bool spawnWorker(const std::string& exe,
                     const std::string& pipeName,
                     const std::string& extraArgs,
                     bool persistent = false);

    // Send a JSON command to a worker and wait for a response.
    // For on-demand workers, the worker exits after responding.
    // @param pipeName   The pipe name registered with spawnWorker
    // @param cmd        The command JSON to send
    // @param timeoutMs  How long to wait for the response
    // @return Response JSON, or {"ok":false,"error":"..."} on failure
    nlohmann::json sendCommand(const std::string& pipeName,
                                const nlohmann::json& cmd,
                                DWORD timeoutMs = 30000);

    // Stream events from an on-demand worker until it exits.
    // Calls `callback` for each JSON event received from the worker.
    void streamEvents(const std::string& pipeName,
                      const nlohmann::json& cmd,
                      std::function<void(const nlohmann::json&)> callback,
                      DWORD timeoutMs = 300000);

    // Stop all workers cleanly and wait for processes to exit.
    void stopAll();

    // Is a worker currently running for this pipe?
    bool isRunning(const std::string& pipeName) const;

private:
    struct WorkerEntry {
        HANDLE      hProcess    = INVALID_HANDLE_VALUE;
        std::string exe;
        std::string pipeName;
        bool        persistent  = false;
        std::chrono::steady_clock::time_point startedAt;
    };

    std::map<std::string, WorkerEntry> workers_;
    mutable std::mutex                 mutex_;
    std::condition_variable            watchdogCv_;
    std::atomic<bool>                  running_{true};
    std::thread                        watchdogThread_;

    // Watchdog: periodically checks persistent workers, restarts crashed ones
    void watchdogLoop();

    // Spawn the process and return its HANDLE (or INVALID_HANDLE_VALUE on fail)
    HANDLE spawnProcess(const std::string& exe, const std::string& extraArgs = "");
};

} // namespace ResolutePulse

