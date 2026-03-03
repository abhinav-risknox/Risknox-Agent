#pragma once

#include "manager/db/PostgresClient.h"

#include <string>
#include <optional>

namespace ResolutePulse {

class LicenseManager {
public:
    LicenseManager(PostgresClient& db);
    ~LicenseManager() = default;

    // Get active license for an agent
    std::optional<LicenseRecord> getLicense(const std::string& agentId);

    // Calculate certificate expiry days based on license
    // Returns 7 (trial) if no license found
    int calculateExpiry(const std::string& agentId);

    // Check license status
    // Returns "ACTIVE", "EXPIRED", or "NONE"
    std::string checkLicenseStatus(const std::string& agentId);

    // Default trial period in days
    static constexpr int TRIAL_DAYS = 7;

private:
    PostgresClient& db_;
};

} // namespace ResolutePulse
