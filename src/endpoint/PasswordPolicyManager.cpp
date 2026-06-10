#include "PasswordPolicyManager.h"
#include "utils/Logger.h"

#include <windows.h>
#include <lm.h>

#pragma comment(lib, "netapi32.lib")

namespace ResolutePulse {
namespace Endpoint {

static std::string getNetApiError(NET_API_STATUS status) {
    switch(status) {
        case NERR_Success: return "Success";
        case ERROR_ACCESS_DENIED: return "Access denied";
        case ERROR_INVALID_PARAMETER: return "Invalid parameter";
        default: return "Error code " + std::to_string(status);
    }
}

bool PasswordPolicyManager::getPolicy(PasswordPolicy& policy, std::string& errorMsg) {
    USER_MODALS_INFO_0* pBuf = nullptr;
    NET_API_STATUS nStatus = NetUserModalsGet(NULL, 0, (LPBYTE*)&pBuf);
    
    if (nStatus == NERR_Success) {
        policy.minLength = pBuf->usrmod0_min_passwd_len;
        policy.maxAgeDays = pBuf->usrmod0_max_passwd_age == TIMEQ_FOREVER ? 0 : pBuf->usrmod0_max_passwd_age / 86400;
        policy.minAgeDays = pBuf->usrmod0_min_passwd_age / 86400;
        policy.historyLength = pBuf->usrmod0_password_hist_len;
        NetApiBufferFree(pBuf);
        return true;
    }
    
    errorMsg = getNetApiError(nStatus);
    LOG_ERROR("PasswordPolicyManager: Failed to get policy: {}", errorMsg);
    return false;
}

bool PasswordPolicyManager::setPolicy(const PasswordPolicy& policy, std::string& errorMsg) {
    USER_MODALS_INFO_0* pBuf = nullptr;
    NET_API_STATUS nStatus = NetUserModalsGet(NULL, 0, (LPBYTE*)&pBuf);
    
    if (nStatus != NERR_Success) {
        errorMsg = getNetApiError(nStatus);
        LOG_ERROR("PasswordPolicyManager: Failed to get current policy for update: {}", errorMsg);
        return false;
    }
    
    if (policy.minLength >= 0) pBuf->usrmod0_min_passwd_len = policy.minLength;
    if (policy.maxAgeDays >= 0) pBuf->usrmod0_max_passwd_age = policy.maxAgeDays == 0 ? TIMEQ_FOREVER : policy.maxAgeDays * 86400;
    if (policy.minAgeDays >= 0) pBuf->usrmod0_min_passwd_age = policy.minAgeDays * 86400;
    if (policy.historyLength >= 0) pBuf->usrmod0_password_hist_len = policy.historyLength;
    
    DWORD dwError = 0;
    nStatus = NetUserModalsSet(NULL, 0, (LPBYTE)pBuf, &dwError);
    NetApiBufferFree(pBuf);
    
    if (nStatus == NERR_Success) {
        LOG_INFO("PasswordPolicyManager: Password policy updated successfully");
        return true;
    }
    
    errorMsg = getNetApiError(nStatus);
    LOG_ERROR("PasswordPolicyManager: Failed to set password policy: {}", errorMsg);
    return false;
}

} // namespace Endpoint
} // namespace ResolutePulse
