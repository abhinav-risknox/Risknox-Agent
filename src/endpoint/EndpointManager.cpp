#include "EndpointManager.h"
#include "UserManager.h"
#include "PasswordPolicyManager.h"
#include "SessionManager.h"
#include "InventoryCollector.h"
#include "utils/Logger.h"

namespace ResolutePulse {
namespace Endpoint {

bool EndpointManager::handleCommand(const std::string& verb, const nlohmann::json& params, nlohmann::json& result, std::string& errorMsg) {
    if (verb == "user_list") {
        auto users = UserManager::listUsers(errorMsg);
        if (!errorMsg.empty()) return false;
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& u : users) {
            arr.push_back({
                {"username", u.username},
                {"full_name", u.fullName},
                {"is_enabled", u.isEnabled},
                {"is_locked", u.isLocked},
                {"password_required", u.passwordRequired},
                {"password_expires", u.passwordExpires}
            });
        }
        result = arr;
        return true;
    } else if (verb == "user_create") {
        std::string username = params.value("username", "");
        std::string password = params.value("password", "");
        std::string fullName = params.value("full_name", "");
        if (username.empty() || password.empty()) {
            errorMsg = "username and password are required";
            return false;
        }
        return UserManager::createUser(username, password, fullName, errorMsg);
    } else if (verb == "user_delete") {
        std::string username = params.value("username", "");
        return UserManager::deleteUser(username, errorMsg);
    } else if (verb == "user_enable") {
        std::string username = params.value("username", "");
        return UserManager::enableUser(username, errorMsg);
    } else if (verb == "user_disable") {
        std::string username = params.value("username", "");
        return UserManager::disableUser(username, errorMsg);
    } else if (verb == "user_password_change") {
        std::string username = params.value("username", "");
        std::string password = params.value("password", "");
        return UserManager::changePassword(username, password, errorMsg);
    } else if (verb == "group_list") {
        auto groups = UserManager::listGroups(errorMsg);
        if (!errorMsg.empty()) return false;
        result = groups;
        return true;
    } else if (verb == "group_add_user") {
        std::string username = params.value("username", "");
        std::string groupname = params.value("groupname", "");
        return UserManager::addUserToGroup(username, groupname, errorMsg);
    } else if (verb == "group_remove_user") {
        std::string username = params.value("username", "");
        std::string groupname = params.value("groupname", "");
        return UserManager::removeUserFromGroup(username, groupname, errorMsg);
    } else if (verb == "password_policy_get") {
        PasswordPolicy policy;
        if (PasswordPolicyManager::getPolicy(policy, errorMsg)) {
            result = {
                {"min_length", policy.minLength},
                {"max_age_days", policy.maxAgeDays},
                {"min_age_days", policy.minAgeDays},
                {"history_length", policy.historyLength}
            };
            return true;
        }
        return false;
    } else if (verb == "password_policy_set") {
        PasswordPolicy policy;
        policy.minLength = params.value("min_length", -1);
        policy.maxAgeDays = params.value("max_age_days", -1);
        policy.minAgeDays = params.value("min_age_days", -1);
        policy.historyLength = params.value("history_length", -1);
        return PasswordPolicyManager::setPolicy(policy, errorMsg);
    } else if (verb == "session_list") {
        auto sessions = SessionManager::listSessions(errorMsg);
        if (!errorMsg.empty()) return false;
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& s : sessions) {
            arr.push_back({
                {"session_id", s.sessionId},
                {"station_name", s.stationName},
                {"username", s.username},
                {"state", s.stateName}
            });
        }
        result = arr;
        return true;
    } else if (verb == "session_logoff") {
        int sessionId = params.value("session_id", -1);
        return SessionManager::logoffSession(sessionId, errorMsg);
    } else if (verb == "session_disconnect") {
        int sessionId = params.value("session_id", -1);
        return SessionManager::disconnectSession(sessionId, errorMsg);
    } else if (verb == "inventory_collect") {
        result = InventoryCollector::collectFullInventory();
        return true;
    }
    
    errorMsg = "Unknown verb: " + verb;
    return false;
}

} // namespace Endpoint
} // namespace ResolutePulse
