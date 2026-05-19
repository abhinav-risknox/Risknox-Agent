#pragma once

// ModuleController.h
// Handles MODULE_COMMAND verbs sent from the Manager and executes them
// against live agent subsystems.  All verbs return a ModuleCommandResult
// that is sent back to the Manager via MODULE_COMMAND_RESULT.
//
// Supported verbs:
//   collector_start   - Resume event collection + batch sender
//   collector_stop    - Pause event collection + batch sender
//   fim_start         - Resume FIM monitoring
//   fim_stop          - Pause FIM monitoring
//   worker_restart    - Stop + re-spawn all persistent worker subprocesses
//   status_request    - Force an immediate status flush and STATUS_REPORT
//   diagnostics       - Return live counters + last log lines
//   config_get        - Return the active JSON config or one section
//   config_push       - Accept a new JSON config section, persist, apply
//   agent_restart     - Schedule a graceful restart (sets flag for run() loop)
//   av_update         - Trigger on-demand freshclam update via rp-antivirus
//   av_version        - Query ClamAV DB metadata via rp-antivirus

#include "collector/EventCollector.h"
#include "sender/BatchSender.h"
#include "fim/FimMonitor.h"
#include "workers/WorkerManager.h"
#include "utils/Logger.h"
#include "utils/PathUtils.h"
#include "patch/PatchManager.h"
#include "common/Protocol.h"

#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <windows.h>

#include "logtailer/LogTailer.h"

namespace ResolutePulse {

class ModuleController {
public:
    // Callbacks wired by Agent after construction
    using StatusFlushFn  = std::function<void()>;          // Force a status.json flush
    using StatusReportFn = std::function<void()>;          // Send STATUS_REPORT via mTLS

    ModuleController() = default;

    // Wire the live subsystem pointers (any may be nullptr if not enabled)
    void setCollector(EventCollector* c)    { collector_   = c; }
    void setBatchSender(BatchSender* bs)    { batchSender_ = bs; }
    void setFimMonitor(FimMonitor* fim)     { fimMonitor_  = fim; }
    void setWorkerManager(WorkerManager* wm){ workerManager_ = wm; }
    void setLogTailer(LogTailer* lt)        { logTailer_   = lt; }

    // Callbacks
    void setStatusFlushCallback(StatusFlushFn fn)   { statusFlushCb_  = std::move(fn); }
    void setStatusReportCallback(StatusReportFn fn) { statusReportCb_ = std::move(fn); }

    // Set the agent config file path (for config_push)
    void setConfigPath(const std::string& p) { configPath_ = p; }
    // Set the agent ID (echoed in results)
    void setAgentId(const std::string& id)   { agentId_ = id; }

    // Set path to the persistent workers exe directory (for worker_restart)
    void setWorkerExeDir(const std::string& d) { workerExeDir_ = d; }

    // Whether an agent restart was requested via the 'agent_restart' verb
    bool restartRequested() const { return restartRequested_.load(); }
    void clearRestartRequest()    { restartRequested_ = false; }

    // Execute a MODULE_COMMAND and return the result.
    // This is called from the management loop on the management thread.
    ModuleCommandResult execute(const ModuleCommand& cmd) {
        LOG_INFO("ModuleController: verb={} commandId={}", cmd.verb, cmd.commandId);

        ModuleCommandResult result;
        result.commandId = cmd.commandId;
        result.agentId   = agentId_;
        result.verb      = cmd.verb;
        result.timestamp = currentTimestamp();

        if      (cmd.verb == "collector_start")  handleCollectorStart(result);
        else if (cmd.verb == "collector_stop")   handleCollectorStop(result);
        else if (cmd.verb == "fim_start")        handleFimStart(result);
        else if (cmd.verb == "fim_stop")         handleFimStop(result);
        else if (cmd.verb == "worker_restart")   handleWorkerRestart(result);
        else if (cmd.verb == "status_request")   handleStatusRequest(result);
        else if (cmd.verb == "diagnostics")      handleDiagnostics(result);
        else if (cmd.verb == "config_get")       handleConfigGet(cmd.params, result);
        else if (cmd.verb == "config_push")      handleConfigPush(cmd.params, result);
        else if (cmd.verb == "agent_restart")    handleAgentRestart(result);
        else if (cmd.verb == "av_update")        handleAvUpdate(result);
        else if (cmd.verb == "av_version")       handleAvVersion(result);
        else {
            result.status = "unsupported";
            result.output = "Unknown verb: " + cmd.verb;
            LOG_WARN("ModuleController: unknown verb '{}'", cmd.verb);
        }

        return result;
    }

private:
    // ── Verb handlers ────────────────────────────────────────────

    void handleCollectorStart(ModuleCommandResult& r) {
        if (!collector_ || !batchSender_) {
            r.status = "failed";
            r.output = "Event collector not initialised";
            return;
        }
        collector_->start();
        batchSender_->start();
        r.status = "success";
        r.output = "Event collection resumed";
        if (statusFlushCb_) statusFlushCb_();
        LOG_INFO("ModuleController: event collection started by Manager command");
    }

    void handleCollectorStop(ModuleCommandResult& r) {
        if (!collector_ || !batchSender_) {
            r.status = "failed";
            r.output = "Event collector not initialised";
            return;
        }
        collector_->stop();
        batchSender_->stop();
        r.status = "success";
        r.output = "Event collection paused";
        if (statusFlushCb_) statusFlushCb_();
        LOG_INFO("ModuleController: event collection stopped by Manager command");
    }

    void handleFimStart(ModuleCommandResult& r) {
        if (!fimMonitor_) {
            r.status = "failed";
            r.output = "FIM monitor not initialised";
            return;
        }
        fimMonitor_->start();
        r.status = "success";
        r.output = "FIM monitoring resumed";
        if (statusFlushCb_) statusFlushCb_();
        LOG_INFO("ModuleController: FIM started by Manager command");
    }

    void handleFimStop(ModuleCommandResult& r) {
        if (!fimMonitor_) {
            r.status = "failed";
            r.output = "FIM monitor not initialised";
            return;
        }
        fimMonitor_->stop();
        r.status = "success";
        r.output = "FIM monitoring paused";
        if (statusFlushCb_) statusFlushCb_();
        LOG_INFO("ModuleController: FIM stopped by Manager command");
    }

    void handleWorkerRestart(ModuleCommandResult& r) {
        if (!workerManager_) {
            r.status = "failed";
            r.output = "WorkerManager not initialised";
            return;
        }
        workerManager_->stopAll();
        // Re-spawn persistent workers from the configured exe directory
        std::string dir = workerExeDir_.empty() ? "" : (workerExeDir_ + "/");
        bool wb = workerManager_->spawnWorker(dir + "rp-webblock.exe",  "rp-webblock",  true);
        bool sb = workerManager_->spawnWorker(dir + "rp-softblock.exe", "rp-softblock", true);
        r.status = (wb && sb) ? "success" : "failed";
        r.output = "rp-webblock=" + std::string(wb ? "ok" : "fail")
                 + " rp-softblock=" + std::string(sb ? "ok" : "fail");
        if (statusFlushCb_) statusFlushCb_();
        LOG_INFO("ModuleController: workers restarted by Manager command");
    }

    void handleStatusRequest(ModuleCommandResult& r) {
        if (statusFlushCb_)  statusFlushCb_();
        if (statusReportCb_) statusReportCb_();
        r.status = "success";
        r.output = "Status report sent";
    }

    void handleDiagnostics(ModuleCommandResult& r) {
        nlohmann::json diag;
        if (collector_) {
            diag["eventsCollected"] = collector_->getEventsCollected();
            diag["eventsFiltered"]  = collector_->getEventsFiltered();
        }
        if (batchSender_) {
            diag["eventsSent"]   = batchSender_->getEventsSent();
            diag["batchesSent"]  = batchSender_->getBatchesSent();
            diag["senderRunning"]= batchSender_->isRunning();
        }
        if (workerManager_) {
            diag["workerWebblock"]  = workerManager_->isRunning("rp-webblock");
            diag["workerSoftblock"] = workerManager_->isRunning("rp-softblock");
        }
        if (fimMonitor_) {
            diag["fimActive"] = true;
        }
        r.status = "success";
        r.output = diag.dump();
    }

    bool readActiveConfig(nlohmann::json& fullConfig, std::string& activePath, std::string& error) {
        std::filesystem::path programDataConfig = PathUtils::getAgentDataDir() / "config.json";

        if (!std::filesystem::exists(programDataConfig)) {
            error = "config.json not found in ProgramData";
            return false;
        }

        try {
            std::ifstream in(programDataConfig);
            in >> fullConfig;
            activePath = programDataConfig.string();
            return true;
        } catch (const std::exception& e) {
            error = std::string("config read failed: ") + e.what();
            return false;
        }
    }

    void handleConfigGet(const nlohmann::json& params, ModuleCommandResult& r) {
        std::string section = params.value("section", "all");

        nlohmann::json fullConfig;
        std::string activePath;
        std::string error;
        if (!readActiveConfig(fullConfig, activePath, error)) {
            r.status = "failed";
            r.output = nlohmann::json({
                {"success", false},
                {"error", error}
            }).dump();
            return;
        }

        nlohmann::json payload = {
            {"success", true},
            {"section", section},
            {"configPath", activePath}
        };

        if (section == "all") {
            payload["config"] = fullConfig;
        } else {
            payload["config"] = fullConfig.value(section, nlohmann::json::object());
        }

        r.status = "success";
        r.output = payload.dump();
    }

    void handleConfigPush(const nlohmann::json& params, ModuleCommandResult& r) {
        // params expected: { "section": "patch_management" | "web_blocking" | ...,
        //                    "config":  { ... section-specific fields ... } }
        if (!params.contains("section") || !params.contains("config")) {
            r.status = "failed";
            r.output = "params must contain 'section' and 'config' keys";
            return;
        }

        std::string section = params["section"].get<std::string>();
        auto newConfig      = params["config"];

        nlohmann::json fullConfig;
        std::string activePath;
        std::string readError;
        if (!readActiveConfig(fullConfig, activePath, readError)) {
            r.status = "failed";
            r.output = nlohmann::json({
                {"success", false},
                {"error", readError}
            }).dump();
            return;
        }

        try {
            // Merge the pushed section (shallow merge: new keys override, extra keys kept)
            if (!fullConfig.contains(section)) {
                fullConfig[section] = nlohmann::json::object();
            }
            for (auto& [k, v] : newConfig.items()) {
                fullConfig[section][k] = v;
            }

            // Determine target path: Always persist to ProgramData for service compatibility
            std::filesystem::path targetPath = PathUtils::getAgentDataDir() / "config.json";
            std::filesystem::create_directories(targetPath.parent_path());
            
            // Write atomically
            std::string tmp = targetPath.string() + ".tmp";
            {
                std::ofstream out(tmp);
                out << fullConfig.dump(4);
            }
            std::filesystem::rename(tmp, targetPath);

            // Update our own reference so subsequent pushes merge against the latest
            configPath_ = targetPath.string();

            // Post-write hook: reconfigure LogTailer when log sources change
            if (section == "log_forwarding" && logTailer_ && newConfig.contains("logs")) {
                logTailer_->reconfigureFromJson(newConfig["logs"]);
            }

            r.status = "success";
            r.output = nlohmann::json({
                {"success", true},
                {"section", section},
                {"configPath", targetPath.string()},
                {"effectiveConfig", fullConfig[section]}
            }).dump();
            LOG_INFO("ModuleController: config section '{}' updated by Manager", section);
        } catch (const std::exception& e) {
            r.status = "failed";
            r.output = nlohmann::json({
                {"success", false},
                {"error", std::string("config_push failed: ") + e.what()}
            }).dump();
            LOG_ERROR("ModuleController: config_push failed: {}", e.what());
        }
    }

    void handleAgentRestart(ModuleCommandResult& r) {
        restartRequested_ = true;
        r.status = "success";
        r.output = "Agent restart scheduled";
        LOG_WARN("ModuleController: graceful restart requested by Manager");
    }

    void handleAvUpdate(ModuleCommandResult& r) {
        auto resp = runAntivirusAction({{"action", "update_definitions"}}, 10 * 60 * 1000);
        bool ok = resp.value("success", false);
        r.status = ok ? "success" : "failed";
        r.output = resp.dump();
    }

    void handleAvVersion(ModuleCommandResult& r) {
        auto resp = runAntivirusAction({{"action", "database_info"}}, 30000);
        bool ok = resp.value("success", false);
        r.status = ok ? "success" : "failed";
        r.output = resp.dump();
    }

    nlohmann::json runAntivirusAction(const nlohmann::json& actionCmd, DWORD timeoutMs) {
        if (!workerManager_) {
            return {
                {"type", "complete"},
                {"success", false},
                {"error", "WorkerManager not initialised"},
                {"action", actionCmd.value("action", "")}
            };
        }

        std::string exe = workerExeDir_.empty()
            ? "rp-antivirus.exe"
            : (workerExeDir_ + "/rp-antivirus.exe");

        if (!workerManager_->spawnWorker(exe, "rp-antivirus", /*persistent=*/false)) {
            return {
                {"type", "complete"},
                {"success", false},
                {"error", "Failed to spawn rp-antivirus.exe"},
                {"action", actionCmd.value("action", "")}
            };
        }

        nlohmann::json terminalEvent;
        bool gotTerminalEvent = false;

        workerManager_->streamEvents(
            "rp-antivirus",
            actionCmd,
            [&](const nlohmann::json& event) {
                std::string type = event.value("type", "");
                if (type == "complete" || type == "error") {
                    terminalEvent = event;
                    gotTerminalEvent = true;
                }
            },
            timeoutMs);

        if (!gotTerminalEvent) {
            return {
                {"type", "complete"},
                {"success", false},
                {"error", "Timeout waiting for rp-antivirus response"},
                {"action", actionCmd.value("action", "")}
            };
        }

        if (terminalEvent.value("type", "") == "error") {
            return {
                {"type", "complete"},
                {"success", false},
                {"error", terminalEvent.value("message", "worker error")},
                {"action", actionCmd.value("action", "")}
            };
        }

        return terminalEvent;
    }

    // ── Helpers ──────────────────────────────────────────────────

    static std::string currentTimestamp() {
        time_t now = time(nullptr);
        char buf[32];
        strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));
        return std::string(buf);
    }

    // Subsystem pointers (not owned)
    EventCollector*  collector_    = nullptr;
    BatchSender*     batchSender_  = nullptr;
    FimMonitor*      fimMonitor_   = nullptr;
    WorkerManager*   workerManager_= nullptr;
    LogTailer*       logTailer_    = nullptr;

    StatusFlushFn  statusFlushCb_;
    StatusReportFn statusReportCb_;

    std::string configPath_;
    std::string agentId_;
    std::string workerExeDir_;

    std::atomic<bool> restartRequested_{false};
};

} // namespace ResolutePulse
