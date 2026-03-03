#include "LicenseManager.h"
#include "utils/Logger.h"

namespace ResolutePulse {

LicenseManager::LicenseManager(PostgresClient& db) : db_(db) {}

std::optional<LicenseRecord> LicenseManager::getLicense(const std::string& agentId) {
    return db_.getLicense(agentId);
}

int LicenseManager::calculateExpiry(const std::string& agentId) {
    auto license = db_.getLicense(agentId);
    
    if (!license.has_value()) {
        LOG_INFO("No license found for agent {}, using trial period ({} days)",
                 agentId, TRIAL_DAYS);
        return TRIAL_DAYS;
    }

    // Determine days based on license type
    if (license->licenseType == "ENTERPRISE") {
        LOG_INFO("Enterprise license for agent {}: 365 day cert", agentId);
        return 365;
    } else if (license->licenseType == "STANDARD") {
        LOG_INFO("Standard license for agent {}: 180 day cert", agentId);
        return 180;
    }

    // TRIAL
    LOG_INFO("Trial license for agent {}: {} day cert", agentId, TRIAL_DAYS);
    return TRIAL_DAYS;
}

std::string LicenseManager::checkLicenseStatus(const std::string& agentId) {
    auto license = db_.getLicense(agentId);
    
    if (!license.has_value()) {
        return "NONE";
    }

    // getLicense() already filters for valid_until > NOW(),
    // so if we got a result, it's active
    return "ACTIVE";
}

} // namespace ResolutePulse
