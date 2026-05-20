#pragma once

#include "workers/WorkerManager.h"
#include "utils/Logger.h"

#include <nlohmann/json.hpp>
#include <string>
#include <functional>
#include <thread>

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
            // On-demand worker: run on a background thread so the management
            // loop is never blocked (patch scans can take several minutes).
            // The thread self-destructs when streamEvents() finishes.
            auto wm = workerManager_;
            auto cb = statusCallback_;
            std::thread([wm, policyData, cb]() {
                wm->spawnWorker("rp-patch.exe", "rp-patch", /*persistent=*/false);
                wm->streamEvents("rp-patch", policyData,
                    [cb](const nlohmann::json& event) {
                        std::string type = event.value("type", "");
                        LOG_INFO("PolicyManager [rp-patch]: {}", event.dump());
                        if (cb && type == "complete") {
                            cb("patch_scan", event);
                        }
                    });
            }).detach();
            result = true;
        } else if (policyType == "antivirus") {
            // On-demand worker: run on a background thread so the management
            // loop is never blocked (AV scans can take several minutes).
            auto wm = workerManager_;
            auto cb = statusCallback_;
            std::thread([wm, policyData, cb]() {
                wm->spawnWorker("rp-antivirus.exe", "rp-antivirus", /*persistent=*/false);
                wm->streamEvents("rp-antivirus", policyData,
                    [cb](const nlohmann::json& event) {
                        std::string type = event.value("type", "");
                        LOG_INFO("PolicyManager [rp-antivirus]: {}", event.dump());
                        if (cb && (type == "complete" || type == "threat")) {
                            cb("av_scan", event);
                        }
                    });
            }).detach();
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
            // Query both workers in parallel (halves worst-case from 6s to 3s)
            nlohmann::json webResp, sbResp;
            auto wm = workerManager_;
            std::thread t1([&webResp, wm]() {
                webResp = wm->sendCommand("rp-webblock",
                    {{"action", "get_status"}}, 3000);
            });
            std::thread t2([&sbResp, wm]() {
                sbResp = wm->sendCommand("rp-softblock",
                    {{"action", "get_status"}}, 3000);
            });
            t1.join();
            t2.join();

            if (webResp.value("ok", false)) {
                report["web_blocking"] = webResp.value("status", nlohmann::json{});
                LOG_DEBUG("PolicyManager: Web blocking status retrieved");
            } else {
                LOG_WARN("PolicyManager: Failed to get web blocking status: {}", 
                         webResp.value("error", "unknown"));
            }

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
