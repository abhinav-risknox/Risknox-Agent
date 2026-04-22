#pragma once

#include "patch/PatchManager.h"
#include "webblock/WebBlocker.h"
#include "appblock/SoftwareBlocker.h"
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

    void setPatchManager(PatchManager* pm) { patchManager_ = pm; }
    void setWebBlocker(WebBlocker* wb) { webBlocker_ = wb; }
    void setSoftwareBlocker(SoftwareBlocker* sb) { softwareBlocker_ = sb; }
    void setStatusReportCallback(StatusReportCallback cb) { statusCallback_ = std::move(cb); }

    /**
     * Handle an incoming policy update from the Manager.
     * @param policyType One of: "patch", "web_blocking", "software_blocking"
     * @param policyData JSON policy payload
     * @return true if policy was applied successfully
     */
    bool handlePolicyUpdate(const std::string& policyType,
                            const nlohmann::json& policyData) {
        LOG_INFO("PolicyManager: Received policy update type={}", policyType);

        bool result = false;

        if (policyType == "patch" && patchManager_) {
            result = patchManager_->applyPolicy(policyData);
        } else if (policyType == "web_blocking" && webBlocker_) {
            result = webBlocker_->applyPolicy(policyData);
        } else if (policyType == "software_blocking" && softwareBlocker_) {
            result = softwareBlocker_->applyPolicy(policyData);
        } else if (policyType == "status_request") {
            // Manager is requesting current status
            sendStatusReport();
            result = true;
        } else {
            LOG_WARN("PolicyManager: Unknown policy type: {}", policyType);
        }

        return result;
    }

    /**
     * Collect status from all modules and send via callback.
     */
    void sendStatusReport() {
        nlohmann::json report;
        report["timestamp"] = getCurrentTimestamp();

        if (patchManager_) {
            report["patch_management"] = patchManager_->getStatus();
        }
        if (webBlocker_) {
            report["web_blocking"] = webBlocker_->getStatus();
        }
        if (softwareBlocker_) {
            report["software_blocking"] = softwareBlocker_->getStatus();
        }

        if (statusCallback_) {
            statusCallback_("module_status", report);
        }
    }

    /**
     * Get combined status JSON for all modules.
     */
    nlohmann::json getFullStatus() const {
        nlohmann::json status;

        if (patchManager_) {
            status["patch_management"] = patchManager_->getStatus();
        }
        if (webBlocker_) {
            status["web_blocking"] = webBlocker_->getStatus();
        }
        if (softwareBlocker_) {
            status["software_blocking"] = softwareBlocker_->getStatus();
        }

        return status;
    }

private:
    std::string getCurrentTimestamp() const {
        time_t now = time(nullptr);
        char buf[64];
        strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));
        return std::string(buf);
    }

    PatchManager* patchManager_ = nullptr;
    WebBlocker* webBlocker_ = nullptr;
    SoftwareBlocker* softwareBlocker_ = nullptr;
    StatusReportCallback statusCallback_;
};

} // namespace ResolutePulse
