#include "Agent.h"
#include "service/ServiceMain.h"
#include "utils/Logger.h"

#include <Windows.h>
#include <iostream>
#include <csignal>
#include <filesystem>

using namespace ResolutePulse;

// Global agent pointer for signal handler
static Agent* g_agent = nullptr;

// One-shot guard — SCM can re-send STOP/SHUTDOWN multiple times during slow shutdown
static std::atomic<bool> g_shutdownOnce{false};

// Console Ctrl handler
BOOL WINAPI consoleCtrlHandler(DWORD ctrlType) {
    switch (ctrlType) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            if (!g_shutdownOnce.exchange(true)) {
                std::cout << "\nShutdown signal received..." << std::endl;
                if (g_agent) {
                    g_agent->stop();
                }
            }
            return TRUE;
    }
    return FALSE;
}

void printUsage(const char* exeName) {
    std::cout << "Resolute Pulse - Windows Log Collection Agent" << std::endl;
    std::cout << "Usage: " << exeName << " [options]" << std::endl;
    std::cout << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  --console     Run in console mode (default if not started by SCM)" << std::endl;
    std::cout << "  --install     Install as Windows Service" << std::endl;
    std::cout << "  --uninstall   Uninstall Windows Service" << std::endl;
    std::cout << "  --config PATH Path to config.json (default: config.json)" << std::endl;
    std::cout << "  --help        Show this help message" << std::endl;
}

std::string getExePath() {
    char buffer[MAX_PATH];
    GetModuleFileNameA(nullptr, buffer, MAX_PATH);
    return std::string(buffer);
}

std::string getConfigPath() {
    // Config must live in ProgramData - the only writable, service-safe location.
    // No fallback to EXE directory (Program Files is read-only and would ignore config_push updates).
    const char* programData = std::getenv("ProgramData");
    if (programData) {
        std::filesystem::path p = std::filesystem::path(programData) / "Risknox Pulse" / "config.json";
        if (std::filesystem::exists(p)) {
            return p.string();
        }
    }

    std::cerr << "FATAL: config.json not found in %ProgramData%\\Risknox Pulse\\" << std::endl;
    return "";
}

int runConsoleMode(const std::string& configPath) {
    // Initialize logger for console mode
    Logger::initialize("info");
    
    LOG_INFO("===========================================");
    LOG_INFO("Resolute Pulse - Windows Log Collection Agent");
    LOG_INFO("===========================================");
    LOG_INFO("Running in console mode");
    LOG_INFO("Config: {}", configPath);
    
    // Set up console Ctrl handler
    SetConsoleCtrlHandler(consoleCtrlHandler, TRUE);
    
    // Create and run agent
    Agent agent;
    g_agent = &agent;
    
    if (!agent.initialize(configPath)) {
        LOG_CRITICAL("Failed to initialize agent");
        return 1;
    }
    
    int exitCode = agent.run();
    
    g_agent = nullptr;
    return exitCode;
}

int main(int argc, char* argv[]) {
    std::string exePath = getExePath();
    std::string configPath = "";
    bool consoleMode = false;
    bool installMode = false;
    bool uninstallMode = false;
    
    // Parse arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return 0;
        }
        else if (arg == "--console" || arg == "-c") {
            consoleMode = true;
        }
        else if (arg == "--install") {
            installMode = true;
        }
        else if (arg == "--uninstall") {
            uninstallMode = true;
        }
        else if ((arg == "--config" || arg == "-f") && i + 1 < argc) {
            configPath = argv[++i];
        }
        else {
            std::cerr << "Unknown argument: " << arg << std::endl;
            printUsage(argv[0]);
            return 1;
        }
    }

    if (!installMode && !uninstallMode && configPath.empty()) {
        configPath = getConfigPath();
        if (configPath.empty()) return 1;
    }
    
    // Handle install/uninstall
    if (installMode) {
        Logger::initialize("info");
        if (ServiceMain::installService(exePath)) {
            std::cout << "Service installed successfully." << std::endl;
            std::cout << "Start with: net start ResolutePulse" << std::endl;
            return 0;
        } else {
            std::cerr << "Failed to install service." << std::endl;
            return 1;
        }
    }
    
    if (uninstallMode) {
        Logger::initialize("info");
        if (ServiceMain::uninstallService()) {
            std::cout << "Service uninstalled successfully." << std::endl;
            return 0;
        } else {
            std::cerr << "Failed to uninstall service." << std::endl;
            return 1;
        }
    }

    // Run mode
    if (consoleMode) {
        return runConsoleMode(configPath);
    } else {
        // Try service mode first
        
        // Set up the main function for service
        ServiceMain::setMainFunction([configPath]() -> int {
            Agent agent;
            g_agent = &agent;
            
            if (!agent.initialize(configPath)) {
                return 1;
            }
            
            int exitCode = agent.run();
            g_agent = nullptr;
            return exitCode;
        });

        int result = ServiceMain::runAsService();
        if (result != 0) {
            // Might have failed because not started by SCM
            // User should use --console explicitly
            std::cerr << "Use --console to run in console mode, or start as a service." << std::endl;
            printUsage(argv[0]);
        }
        return result;
    }
}
