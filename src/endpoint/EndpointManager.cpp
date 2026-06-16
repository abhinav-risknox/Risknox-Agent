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
        if (UserManager::createUser(username, password, fullName, errorMsg)) {
            result = {{"success", true}, {"message", "User created successfully"}};
            return true;
        }
        return false;
    } else if (verb == "user_delete") {
        std::string username = params.value("username", "");
        if (username.empty()) { errorMsg = "username is required"; return false; }
        if (UserManager::deleteUser(username, errorMsg)) {
            result = {{"success", true}, {"message", "User deleted successfully"}};
            return true;
        }
        return false;
    } else if (verb == "user_enable") {
        std::string username = params.value("username", "");
        if (username.empty()) { errorMsg = "username is required"; return false; }
        if (UserManager::enableUser(username, errorMsg)) {
            result = {{"success", true}, {"message", "User enabled successfully"}};
            return true;
        }
        return false;
    } else if (verb == "user_disable") {
        std::string username = params.value("username", "");
        if (username.empty()) { errorMsg = "username is required"; return false; }
        if (UserManager::disableUser(username, errorMsg)) {
            result = {{"success", true}, {"message", "User disabled successfully"}};
            return true;
        }
        return false;
    } else if (verb == "user_password_change") {
        std::string username = params.value("username", "");
        std::string password = params.value("password", "");
        if (username.empty() || password.empty()) { errorMsg = "username and password are required"; return false; }
        if (UserManager::changePassword(username, password, errorMsg)) {
            result = {{"success", true}, {"message", "Password changed successfully"}};
            return true;
        }
        return false;
    } else if (verb == "group_list") {
        auto groups = UserManager::listGroups(errorMsg);
        if (!errorMsg.empty()) return false;
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& g : groups) {
            arr.push_back({{"groupname", g}});
        }
        result = arr;
        return true;
    } else if (verb == "group_add_user") {
        std::string username = params.value("username", "");
        std::string groupname = params.value("groupname", "");
        if (username.empty() || groupname.empty()) { errorMsg = "username and groupname are required"; return false; }
        if (UserManager::addUserToGroup(username, groupname, errorMsg)) {
            result = {{"success", true}, {"message", "User added to group successfully"}};
            return true;
        }
        return false;
    } else if (verb == "group_remove_user") {
        std::string username = params.value("username", "");
        std::string groupname = params.value("groupname", "");
        if (username.empty() || groupname.empty()) { errorMsg = "username and groupname are required"; return false; }
        if (UserManager::removeUserFromGroup(username, groupname, errorMsg)) {
            result = {{"success", true}, {"message", "User removed from group successfully"}};
            return true;
        }
        return false;
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
        if (PasswordPolicyManager::setPolicy(policy, errorMsg)) {
            result = {{"success", true}, {"message", "Password policy set successfully"}};
            return true;
        }
        return false;
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
        if (sessionId == -1) { errorMsg = "session_id is required"; return false; }
        if (SessionManager::logoffSession(sessionId, errorMsg)) {
            result = {{"success", true}, {"message", "Session logged off successfully"}};
            return true;
        }
        return false;
    } else if (verb == "session_disconnect") {
        int sessionId = params.value("session_id", -1);
        if (sessionId == -1) { errorMsg = "session_id is required"; return false; }
        if (SessionManager::disconnectSession(sessionId, errorMsg)) {
            result = {{"success", true}, {"message", "Session disconnected successfully"}};
            return true;
        }
        return false;
    } else if (verb == "inventory_collect") {
        result = InventoryCollector::collectFullInventory();
        return true;
    }
    
    errorMsg = "Unknown verb: " + verb;
    return false;
}

} // namespace Endpoint
} // namespace ResolutePulse
