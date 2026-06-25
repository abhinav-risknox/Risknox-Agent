#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace ResolutePulse {
namespace Endpoint {

struct UserInfo {
    std::string username;
    std::string fullName;
    std::string description;
    bool isEnabled;
    bool isLocked;
    bool passwordExpires;
    bool passwordRequired;
    uint64_t lastLogon;
};

class UserManager {
public:
    static bool createUser(const std::string& username, const std::string& password, const std::string& fullName, std::string& errorMsg);
    static bool deleteUser(const std::string& username, std::string& errorMsg);
    static bool disableUser(const std::string& username, std::string& errorMsg);
    static bool enableUser(const std::string& username, std::string& errorMsg);
    static bool unlockUser(const std::string& username, std::string& errorMsg);
    static bool changePassword(const std::string& username, const std::string& newPassword, std::string& errorMsg);
    
    static std::vector<UserInfo> listUsers(std::string& errorMsg);
    static std::vector<std::string> listGroups(std::string& errorMsg);
    
    static bool addUserToGroup(const std::string& username, const std::string& groupname, std::string& errorMsg);
    static bool removeUserFromGroup(const std::string& username, const std::string& groupname, std::string& errorMsg);
};

} // namespace Endpoint
} // namespace ResolutePulse
