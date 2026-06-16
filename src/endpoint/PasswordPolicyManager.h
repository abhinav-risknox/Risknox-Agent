#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace ResolutePulse {
namespace Endpoint {

struct PasswordPolicy {
    int minLength = 0;
    int maxAgeDays = 0;
    int minAgeDays = 0;
    int historyLength = 0;
    // Complexity isn't directly in USER_MODALS_INFO_0, but let's define it
};

class PasswordPolicyManager {
public:
    static bool getPolicy(PasswordPolicy& policy, std::string& errorMsg);
    static bool setPolicy(const PasswordPolicy& policy, std::string& errorMsg);
};

} // namespace Endpoint
} // namespace ResolutePulse
