#include "LicenseManager.h"
#include "utils/Logger.h"

namespace ResolutePulse {

LicenseManager::LicenseManager(PostgresClient& db) : db_(db) {}

std::optional<LicenseRecord> LicenseManager::getLicense(const std::string& agentId) {
    return db_.getLicense(agentId);
}

int LicenseManager::calculateExpiry(const std::string& agentId) {
    // Certs are always 365 days — identity only.
    // License expiry is enforced via heartbeat, not cert duration.
    (void)agentId;
    return CERT_VALIDITY_DAYS;
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
