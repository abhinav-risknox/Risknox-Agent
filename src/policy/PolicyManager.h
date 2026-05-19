#pragma once

#include "workers/WorkerManager.h"
#include "utils/Logger.h"

#include <nlohmann/json.hpp>
#include <string>
#include <functional>

namespace ResolutePulse {

/**
 * PolicyManager dispatches incoming policy commands from the Manager
 * to the appropriate module (PatchManager, WebBlocker, SoftwareBlocker).
 */
class PolicyManager {
public:
    using StatusReportCallback = std::function<void(const std::string& reportType,
                                                     const nlohmann::json& data)>;

    PolicyManager() = default;
    ~PolicyManager() = default;

    void setWorkerManager(WorkerManager* wm) { workerManager_ = wm; }
    void setStatusReportCallback(StatusReportCallback cb) { statusCallback_ = std::move(cb); }

    /**
     * Handle an incoming policy update from the Manager.
     * Dispatches to the appropriate subprocess worker via Named Pipe IPC.
     * @param policyType One of: "patch", "web_blocking", "software_blocking", "antivirus"
     * @param policyData JSON policy payload
     * @return true if policy was applied successfully
     */
    bool handlePolicyUpdate(const std::string& policyType,
                            const nlohmann::json& policyData) {
        LOG_INFO("PolicyManager: Received policy update type={}", policyType);

        if (!workerManager_) {
            LOG_ERROR("PolicyManager: WorkerManager not set");
            return false;
        }

        bool result = false;

        if (policyType == "web_blocking") {
            // Persistent worker: request/response
            auto resp = workerManager_->sendCommand("rp-webblock", policyData);
            result = resp.value("ok", false);
            if (!result) {
                LOG_WARN("PolicyManager: rp-webblock returned error: {}",
                         resp.value("error", "unknown"));
            }
        } else if (policyType == "software_blocking") {
            // Persistent worker: request/response
            auto resp = workerManager_->sendCommand("rp-softblock", policyData);
            result = resp.value("ok", false);
            if (!result) {
                LOG_WARN("PolicyManager: rp-softblock returned error: {}",
                         resp.value("error", "unknown"));
            }
        } else if (policyType == "patch") {
            // On-demand worker: spawn, stream events, worker exits when done
            workerManager_->spawnWorker("rp-patch.exe", "rp-patch", /*persistent=*/false);
            // PipeClient::connect() in streamEvents retries every 100ms with 5s timeout
            workerManager_->streamEvents("rp-patch", policyData,
                [this](const nlohmann::json& event) {
                    std::string type = event.value("type", "");
                    LOG_INFO("PolicyManager [rp-patch]: {}", event.dump());
                    if (statusCallback_ && type == "complete") {
                        statusCallback_("patch_scan", event);
                    }
                });
            result = true;
        } else if (policyType == "antivirus") {
            // On-demand worker: spawn, stream events, worker exits when done
            workerManager_->spawnWorker("rp-antivirus.exe", "rp-antivirus", /*persistent=*/false);
            // PipeClient::connect() in streamEvents retries every 100ms with 5s timeout
            workerManager_->streamEvents("rp-antivirus", policyData,
                [this](const nlohmann::json& event) {
                    std::string type = event.value("type", "");
                    LOG_INFO("PolicyManager [rp-antivirus]: {}", event.dump());
                    if (statusCallback_ && (type == "complete" || type == "threat")) {
                        statusCallback_("av_scan", event);
                    }
                });
            result = true;
        } else if (policyType == "status_request") {
            sendStatusReport();
            result = true;
        } else {
            LOG_WARN("PolicyManager: Unknown policy type: {}", policyType);
        }

        return result;
    }

    /**
     * Request status from each subprocess worker and send via callback.
     */
    void sendStatusReport(nlohmann::json baseData = nlohmann::json::object()) {
        LOG_INFO("PolicyManager: Generating full module status report...");
        nlohmann::json report = baseData;
        report["timestamp"] = getCurrentTimestamp();

        if (workerManager_) {
            // Web Blocking
            auto webResp = workerManager_->sendCommand("rp-webblock",
                {{"action", "get_status"}}, 3000);
            if (webResp.value("ok", false)) {
                report["web_blocking"] = webResp.value("status", nlohmann::json{});
                LOG_DEBUG("PolicyManager: Web blocking status retrieved");
            } else {
                LOG_WARN("PolicyManager: Failed to get web blocking status: {}", 
                         webResp.value("error", "unknown"));
            }

            // Software Blocking
            auto sbResp = workerManager_->sendCommand("rp-softblock",
                {{"action", "get_status"}}, 3000);
            if (sbResp.value("ok", false)) {
                report["software_blocking"] = sbResp.value("status", nlohmann::json{});
                LOG_DEBUG("PolicyManager: Software blocking status retrieved");
            } else {
                LOG_WARN("PolicyManager: Failed to get software blocking status: {}", 
                         sbResp.value("error", "unknown"));
            }
        } else {
            LOG_ERROR("PolicyManager: WorkerManager is null during status report");
        }

        if (statusCallback_) {
            LOG_INFO("PolicyManager: Dispatching module_status report via callback");
            statusCallback_("module_status", report);
        } else {
            LOG_WARN("PolicyManager: No status report callback registered");
        }
    }

private:
    std::string getCurrentTimestamp() const {
        time_t now = time(nullptr);
        char buf[64];
        strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));
        return std::string(buf);
    }

    WorkerManager* workerManager_ = nullptr;
    StatusReportCallback statusCallback_;
};

} // namespace ResolutePulse
