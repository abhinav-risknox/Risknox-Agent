#pragma once

#include <string>
#include <functional>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

namespace ResolutePulse {

class ServiceMain {
public:
    // Service name and display name
    static constexpr const char* SERVICE_NAME = "ResolutePulse";
    static constexpr const char* SERVICE_DISPLAY_NAME = "Resolute Pulse Log Agent";
    static constexpr const char* SERVICE_DESCRIPTION = 
        "Collects Windows Event Logs and forwards them to a central server.";
    
    // Set the main function to run
    static void setMainFunction(std::function<int()> mainFunc);
    
    // Install the service
    static bool installService(const std::string& exePath);
    
    // Uninstall the service
    static bool uninstallService();
    
    // Run as Windows Service (called when started by SCM)
    static int runAsService();
    
    // Request service stop (call from within main function)
    static void requestStop();
    
    // Check if stop was requested
    static bool isStopRequested();
    
private:
    // Windows Service entry points
    static void WINAPI serviceMain(DWORD argc, LPWSTR* argv);
    static void WINAPI serviceCtrlHandler(DWORD ctrlCode);
    
    static void reportServiceStatus(DWORD currentState, 
                                   DWORD exitCode,
                                   DWORD waitHint);
    
    static std::function<int()> mainFunction_;
};

} // namespace ResolutePulse
