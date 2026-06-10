#include "UserManager.h"
#include "utils/Logger.h"

#include <windows.h>
#include <lm.h>
#include <locale>
#include <codecvt>

#pragma comment(lib, "netapi32.lib")

namespace ResolutePulse {
namespace Endpoint {

// Helper string conversions
static std::wstring utf8_to_wstring(const std::string& str) {
    if (str.empty()) return std::wstring();
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
    return wstrTo;
}

static std::string wstring_to_utf8(const std::wstring& wstr) {
    if (wstr.empty()) return std::string();
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
}

static std::string getNetApiError(NET_API_STATUS status) {
    switch(status) {
        case NERR_Success: return "Success";
        case NERR_UserExists: return "User already exists";
        case NERR_UserNotFound: return "User not found";
        case NERR_GroupNotFound: return "Group not found";
        case ERROR_ACCESS_DENIED: return "Access denied";
        case ERROR_INVALID_PARAMETER: return "Invalid parameter";
        case NERR_PasswordTooShort: return "Password does not meet length/complexity requirements";
        default: return "Error code " + std::to_string(status);
    }
}

bool UserManager::createUser(const std::string& username, const std::string& password, const std::string& fullName, std::string& errorMsg) {
    USER_INFO_1 ui;
    DWORD dwError = 0;
    
    std::wstring wUsername = utf8_to_wstring(username);
    std::wstring wPassword = utf8_to_wstring(password);
    std::wstring wFullName = utf8_to_wstring(fullName);

    ui.usri1_name = (LPWSTR)wUsername.c_str();
    ui.usri1_password = (LPWSTR)wPassword.c_str();
    ui.usri1_priv = USER_PRIV_USER;
    ui.usri1_home_dir = NULL;
    ui.usri1_comment = (LPWSTR)wFullName.c_str();
    ui.usri1_flags = UF_SCRIPT;
    ui.usri1_script_path = NULL;

    NET_API_STATUS nStatus = NetUserAdd(NULL, 1, (LPBYTE)&ui, &dwError);

    if (nStatus == NERR_Success) {
        LOG_INFO("UserManager: Created user {}", username);
        return true;
    }
    
    errorMsg = getNetApiError(nStatus);
    LOG_ERROR("UserManager: Failed to create user {}: {}", username, errorMsg);
    return false;
}

bool UserManager::deleteUser(const std::string& username, std::string& errorMsg) {
    std::wstring wUsername = utf8_to_wstring(username);
    NET_API_STATUS nStatus = NetUserDel(NULL, wUsername.c_str());

    if (nStatus == NERR_Success) {
        LOG_INFO("UserManager: Deleted user {}", username);
        return true;
    }
    
    errorMsg = getNetApiError(nStatus);
    LOG_ERROR("UserManager: Failed to delete user {}: {}", username, errorMsg);
    return false;
}

static bool modifyUserFlags(const std::string& username, DWORD flagsToSet, DWORD flagsToClear, std::string& errorMsg) {
    std::wstring wUsername = utf8_to_wstring(username);
    USER_INFO_1008 ui1008;
    DWORD dwError = 0;
    
    USER_INFO_1* pUI = nullptr;
    NET_API_STATUS nStatus = NetUserGetInfo(NULL, wUsername.c_str(), 1, (LPBYTE*)&pUI);
    if (nStatus != NERR_Success) {
        errorMsg = getNetApiError(nStatus);
        return false;
    }
    
    DWORD flags = pUI->usri1_flags;
    flags |= flagsToSet;
    flags &= ~flagsToClear;
    
    ui1008.usri1008_flags = flags;
    nStatus = NetUserSetInfo(NULL, wUsername.c_str(), 1008, (LPBYTE)&ui1008, &dwError);
    NetApiBufferFree(pUI);
    
    if (nStatus == NERR_Success) return true;
    errorMsg = getNetApiError(nStatus);
    return false;
}

bool UserManager::disableUser(const std::string& username, std::string& errorMsg) {
    bool ok = modifyUserFlags(username, UF_ACCOUNTDISABLE, 0, errorMsg);
    if (ok) LOG_INFO("UserManager: Disabled user {}", username);
    else LOG_ERROR("UserManager: Failed to disable user {}: {}", username, errorMsg);
    return ok;
}

bool UserManager::enableUser(const std::string& username, std::string& errorMsg) {
    bool ok = modifyUserFlags(username, 0, UF_ACCOUNTDISABLE, errorMsg);
    if (ok) LOG_INFO("UserManager: Enabled user {}", username);
    else LOG_ERROR("UserManager: Failed to enable user {}: {}", username, errorMsg);
    return ok;
}

bool UserManager::changePassword(const std::string& username, const std::string& newPassword, std::string& errorMsg) {
    std::wstring wUsername = utf8_to_wstring(username);
    std::wstring wPassword = utf8_to_wstring(newPassword);
    
    USER_INFO_1003 ui1003;
    ui1003.usri1003_password = (LPWSTR)wPassword.c_str();
    
    DWORD dwError = 0;
    NET_API_STATUS nStatus = NetUserSetInfo(NULL, wUsername.c_str(), 1003, (LPBYTE)&ui1003, &dwError);
    
    if (nStatus == NERR_Success) {
        LOG_INFO("UserManager: Password changed for user {}", username);
        return true;
    }
    
    errorMsg = getNetApiError(nStatus);
    LOG_ERROR("UserManager: Failed to change password for user {}: {}", username, errorMsg);
    return false;
}

std::vector<UserInfo> UserManager::listUsers(std::string& errorMsg) {
    std::vector<UserInfo> users;
    LPUSER_INFO_1 pBuf = NULL;
    LPUSER_INFO_1 pTmpBuf;
    DWORD dwLevel = 1;
    DWORD dwPrefMaxLen = MAX_PREFERRED_LENGTH;
    DWORD dwEntriesRead = 0;
    DWORD dwTotalEntries = 0;
    DWORD dwResumeHandle = 0;
    NET_API_STATUS nStatus;

    do {
        nStatus = NetUserEnum(NULL, dwLevel, FILTER_NORMAL_ACCOUNT, (LPBYTE*)&pBuf,
                              dwPrefMaxLen, &dwEntriesRead, &dwTotalEntries, &dwResumeHandle);
        
        if ((nStatus == NERR_Success) || (nStatus == ERROR_MORE_DATA)) {
            if ((pTmpBuf = pBuf) != NULL) {
                for (DWORD i = 0; i < dwEntriesRead; i++) {
                    UserInfo u;
                    if (pTmpBuf->usri1_name != NULL) u.username = wstring_to_utf8(pTmpBuf->usri1_name);
                    if (pTmpBuf->usri1_comment != NULL) u.fullName = wstring_to_utf8(pTmpBuf->usri1_comment);
                    u.isEnabled = ((pTmpBuf->usri1_flags & UF_ACCOUNTDISABLE) == 0);
                    u.isLocked = ((pTmpBuf->usri1_flags & UF_LOCKOUT) != 0);
                    u.passwordRequired = ((pTmpBuf->usri1_flags & UF_PASSWD_NOTREQD) == 0);
                    u.passwordExpires = ((pTmpBuf->usri1_flags & UF_DONT_EXPIRE_PASSWD) == 0);
                    users.push_back(u);
                    pTmpBuf++;
                }
            }
        } else {
            errorMsg = getNetApiError(nStatus);
            break;
        }

        if (pBuf != NULL) {
            NetApiBufferFree(pBuf);
            pBuf = NULL;
        }
    } while (nStatus == ERROR_MORE_DATA);

    if (pBuf != NULL) NetApiBufferFree(pBuf);
    return users;
}

std::vector<std::string> UserManager::listGroups(std::string& errorMsg) {
    std::vector<std::string> groups;
    LPLOCALGROUP_INFO_0 pBuf = NULL;
    LPLOCALGROUP_INFO_0 pTmpBuf;
    DWORD dwLevel = 0;
    DWORD dwPrefMaxLen = MAX_PREFERRED_LENGTH;
    DWORD dwEntriesRead = 0;
    DWORD dwTotalEntries = 0;
    DWORD_PTR dwResumeHandle = 0;
    NET_API_STATUS nStatus;

    do {
        nStatus = NetLocalGroupEnum(NULL, dwLevel, (LPBYTE*)&pBuf,
                              dwPrefMaxLen, &dwEntriesRead, &dwTotalEntries, &dwResumeHandle);
        
        if ((nStatus == NERR_Success) || (nStatus == ERROR_MORE_DATA)) {
            if ((pTmpBuf = pBuf) != NULL) {
                for (DWORD i = 0; i < dwEntriesRead; i++) {
                    if (pTmpBuf->lgrpi0_name != NULL) {
                        groups.push_back(wstring_to_utf8(pTmpBuf->lgrpi0_name));
                    }
                    pTmpBuf++;
                }
            }
        } else {
            errorMsg = getNetApiError(nStatus);
            break;
        }

        if (pBuf != NULL) {
            NetApiBufferFree(pBuf);
            pBuf = NULL;
        }
    } while (nStatus == ERROR_MORE_DATA);

    if (pBuf != NULL) NetApiBufferFree(pBuf);
    return groups;
}

bool UserManager::addUserToGroup(const std::string& username, const std::string& groupname, std::string& errorMsg) {
    std::wstring wUsername = utf8_to_wstring(username);
    std::wstring wGroupname = utf8_to_wstring(groupname);
    
    LOCALGROUP_MEMBERS_INFO_3 member;
    member.lgrmi3_domainandname = (LPWSTR)wUsername.c_str();
    
    NET_API_STATUS nStatus = NetLocalGroupAddMembers(NULL, wGroupname.c_str(), 3, (LPBYTE)&member, 1);
    
    if (nStatus == NERR_Success || nStatus == ERROR_MEMBER_IN_ALIAS) {
        LOG_INFO("UserManager: Added user {} to group {}", username, groupname);
        return true;
    }
    
    errorMsg = getNetApiError(nStatus);
    LOG_ERROR("UserManager: Failed to add user {} to group {}: {}", username, groupname, errorMsg);
    return false;
}

bool UserManager::removeUserFromGroup(const std::string& username, const std::string& groupname, std::string& errorMsg) {
    std::wstring wUsername = utf8_to_wstring(username);
    std::wstring wGroupname = utf8_to_wstring(groupname);
    
    LOCALGROUP_MEMBERS_INFO_3 member;
    member.lgrmi3_domainandname = (LPWSTR)wUsername.c_str();
    
    NET_API_STATUS nStatus = NetLocalGroupDelMembers(NULL, wGroupname.c_str(), 3, (LPBYTE)&member, 1);
    
    if (nStatus == NERR_Success || nStatus == ERROR_MEMBER_NOT_IN_ALIAS) {
        LOG_INFO("UserManager: Removed user {} from group {}", username, groupname);
        return true;
    }
    
    errorMsg = getNetApiError(nStatus);
    LOG_ERROR("UserManager: Failed to remove user {} from group {}: {}", username, groupname, errorMsg);
    return false;
}

} // namespace Endpoint
} // namespace ResolutePulse
