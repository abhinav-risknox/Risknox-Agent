#include "ServiceMain.h"
#include "utils/Logger.h"

#include <Windows.h>
#include <atomic>

namespace ResolutePulse {

// Static members
std::function<int()> ServiceMain::mainFunction_;
static SERVICE_STATUS_HANDLE g_serviceStatusHandle = nullptr;
static SERVICE_STATUS g_serviceStatus = {0};
static std::atomic<bool> g_stopRequested{false};

void ServiceMain::setMainFunction(std::function<int()> mainFunc) {
    mainFunction_ = std::move(mainFunc);
}

void ServiceMain::requestStop() {
    g_stopRequested = true;
}

bool ServiceMain::isStopRequested() {
    return g_stopRequested.load();
}

bool ServiceMain::installService(const std::string& exePath) {
    SC_HANDLE schSCManager = OpenSCManager(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
    if (!schSCManager) {
        LOG_ERROR("OpenSCManager failed: {}", GetLastError());
        return false;
    }
    
    // Convert to wide string
    std::wstring wideExePath(exePath.begin(), exePath.end());
    std::wstring wideServiceName(SERVICE_NAME, SERVICE_NAME + strlen(SERVICE_NAME));
    std::wstring wideDisplayName(SERVICE_DISPLAY_NAME, SERVICE_DISPLAY_NAME + strlen(SERVICE_DISPLAY_NAME));
    
    SC_HANDLE schService = CreateServiceW(
        schSCManager,
        wideServiceName.c_str(),
        wideDisplayName.c_str(),
        SERVICE_ALL_ACCESS,
        SERVICE_WIN32_OWN_PROCESS,
        SERVICE_AUTO_START,
        SERVICE_ERROR_NORMAL,
        wideExePath.c_str(),
        nullptr,    // No load ordering group
        nullptr,    // No tag identifier
        nullptr,    // No dependencies
        nullptr,    // LocalSystem account
        nullptr     // No password
    );
    
    if (!schService) {
        DWORD error = GetLastError();
        CloseServiceHandle(schSCManager);
        
        if (error == ERROR_SERVICE_EXISTS) {
            LOG_WARN("Service already exists");
            return true;
        }
        
        LOG_ERROR("CreateService failed: {}", error);
        return false;
    }
    
    // Set service description
    std::wstring wideDesc(SERVICE_DESCRIPTION, SERVICE_DESCRIPTION + strlen(SERVICE_DESCRIPTION));
    SERVICE_DESCRIPTIONW desc = {const_cast<LPWSTR>(wideDesc.c_str())};
    ChangeServiceConfig2W(schService, SERVICE_CONFIG_DESCRIPTION, &desc);
    
    // Configure recovery options (restart on failure)
    SC_ACTION actions[3] = {
        {SC_ACTION_RESTART, 60000},   // Restart after 1 minute
        {SC_ACTION_RESTART, 60000},   // Restart after 1 minute
        {SC_ACTION_RESTART, 60000}    // Restart after 1 minute
    };
    SERVICE_FAILURE_ACTIONS failureActions = {0};
    failureActions.dwResetPeriod = 86400;  // Reset failure count after 1 day
    failureActions.lpRebootMsg = nullptr;
    failureActions.lpCommand = nullptr;
    failureActions.cActions = 3;
    failureActions.lpsaActions = actions;
    
    ChangeServiceConfig2(schService, SERVICE_CONFIG_FAILURE_ACTIONS, &failureActions);
    
    CloseServiceHandle(schService);
    CloseServiceHandle(schSCManager);
    
    LOG_INFO("Service installed successfully");
    return true;
}

bool ServiceMain::uninstallService() {
    SC_HANDLE schSCManager = OpenSCManager(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
    if (!schSCManager) {
        LOG_ERROR("OpenSCManager failed: {}", GetLastError());
        return false;
    }
    
    std::wstring wideServiceName(SERVICE_NAME, SERVICE_NAME + strlen(SERVICE_NAME));
    SC_HANDLE schService = OpenServiceW(schSCManager, wideServiceName.c_str(), 
                                         SERVICE_STOP | DELETE);
    if (!schService) {
        DWORD error = GetLastError();
        CloseServiceHandle(schSCManager);
        
        if (error == ERROR_SERVICE_DOES_NOT_EXIST) {
            LOG_WARN("Service does not exist");
            return true;
        }
        
        LOG_ERROR("OpenService failed: {}", error);
        return false;
    }
    
    // Try to stop the service first
    SERVICE_STATUS status;
    ControlService(schService, SERVICE_CONTROL_STOP, &status);
    
    // Wait for service to stop
    for (int i = 0; i < 30; ++i) {
        if (QueryServiceStatus(schService, &status)) {
            if (status.dwCurrentState == SERVICE_STOPPED) {
                break;
            }
        }
        Sleep(1000);
    }
    
    // Delete the service
    if (!DeleteService(schService)) {
        LOG_ERROR("DeleteService failed: {}", GetLastError());
        CloseServiceHandle(schService);
        CloseServiceHandle(schSCManager);
        return false;
    }
    
    CloseServiceHandle(schService);
    CloseServiceHandle(schSCManager);
    
    LOG_INFO("Service uninstalled successfully");
    return true;
}

int ServiceMain::runAsService() {
    std::wstring wideServiceName(SERVICE_NAME, SERVICE_NAME + strlen(SERVICE_NAME));
    
    SERVICE_TABLE_ENTRYW dispatchTable[] = {
        {const_cast<LPWSTR>(wideServiceName.c_str()), serviceMain},
        {nullptr, nullptr}
    };
    
    if (!StartServiceCtrlDispatcherW(dispatchTable)) {
        DWORD error = GetLastError();
        if (error == ERROR_FAILED_SERVICE_CONTROLLER_CONNECT) {
            // Not started as a service, run in console mode
            LOG_WARN("Not running as service, use --console for console mode");
            return 1;
        }
        LOG_ERROR("StartServiceCtrlDispatcher failed: {}", error);
        return 1;
    }
    
    return 0;
}

void WINAPI ServiceMain::serviceMain(DWORD argc, LPWSTR* argv) {
    (void)argc;
    (void)argv;
    
    std::wstring wideServiceName(SERVICE_NAME, SERVICE_NAME + strlen(SERVICE_NAME));
    
    g_serviceStatusHandle = RegisterServiceCtrlHandlerW(
        wideServiceName.c_str(),
        serviceCtrlHandler
    );
    
    if (!g_serviceStatusHandle) {
        LOG_ERROR("RegisterServiceCtrlHandler failed: {}", GetLastError());
        return;
    }
    
    g_serviceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_serviceStatus.dwServiceSpecificExitCode = 0;
    
    reportServiceStatus(SERVICE_START_PENDING, NO_ERROR, 3000);
    
    // Initialize logger for service mode (log to file)
    Logger::initialize("info", "ResolutePulse.log");
    
    reportServiceStatus(SERVICE_RUNNING, NO_ERROR, 0);
    
    LOG_INFO("Service started");
    
    // Run the main function
    int exitCode = 0;
    if (mainFunction_) {
        exitCode = mainFunction_();
    }
    
    LOG_INFO("Service stopped with exit code: {}", exitCode);
    
    reportServiceStatus(SERVICE_STOPPED, exitCode, 0);
}

void WINAPI ServiceMain::serviceCtrlHandler(DWORD ctrlCode) {
    switch (ctrlCode) {
        case SERVICE_CONTROL_STOP:
        case SERVICE_CONTROL_SHUTDOWN:
            reportServiceStatus(SERVICE_STOP_PENDING, NO_ERROR, 5000);
            g_stopRequested = true;
            LOG_INFO("Service stop requested");
            break;
            
        case SERVICE_CONTROL_INTERROGATE:
            // Just report current status
            break;
            
        default:
            break;
    }
    
    // Report current status
    SetServiceStatus(g_serviceStatusHandle, &g_serviceStatus);
}

void ServiceMain::reportServiceStatus(DWORD currentState, 
                                      DWORD exitCode,
                                      DWORD waitHint) {
    static DWORD checkPoint = 1;
    
    g_serviceStatus.dwCurrentState = currentState;
    g_serviceStatus.dwWin32ExitCode = exitCode;
    g_serviceStatus.dwWaitHint = waitHint;
    
    if (currentState == SERVICE_START_PENDING) {
        g_serviceStatus.dwControlsAccepted = 0;
    } else {
        g_serviceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
    }
    
    if (currentState == SERVICE_RUNNING || currentState == SERVICE_STOPPED) {
        g_serviceStatus.dwCheckPoint = 0;
    } else {
        g_serviceStatus.dwCheckPoint = checkPoint++;
    }
    
    SetServiceStatus(g_serviceStatusHandle, &g_serviceStatus);
}

} // namespace ResolutePulse
