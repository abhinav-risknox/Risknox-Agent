#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace ResolutePulse {
namespace Endpoint {

struct PasswordPolicy {
    int minLength;
    int maxAgeDays;
    int minAgeDays;
    int historyLength;
    // Complexity isn't directly in USER_MODALS_INFO_0, but let's define it
};

class PasswordPolicyManager {
public:
    static bool getPolicy(PasswordPolicy& policy, std::string& errorMsg);
    static bool setPolicy(const PasswordPolicy& policy, std::string& errorMsg);
};

} // namespace Endpoint
} // namespace ResolutePulse
