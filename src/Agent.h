#pragma once

#include "config/ConfigManager.h"
#include "collector/ChannelDiscovery.h"
#include "collector/EventCollector.h"
#include "queue/EventQueue.h"
#include "buffer/EventBuffer.h"
#include "network/TcpSender.h"
#include "sender/BatchSender.h"
#include "fim/FimMonitor.h"
#include "sysinfo/SystemInfoCollector.h"
#include "agent/registration/CertificateStore.h"
#include "agent/registration/RegistrationClient.h"
#include "agent/network/TlsSender.h"
#include "policy/PolicyManager.h"
#include "policy/ModuleController.h"
#include "workers/WorkerManager.h"
#include "logtailer/LogTailer.h"
#include "usb/UsbMonitor.h"

#include <memory>
#include <atomic>
#include <string>

namespace ResolutePulse {

class Agent {
public:
    Agent();
    ~Agent();
    
    // Initialize the agent
    // @param configPath - Path to config.json
    // @return true on success
    bool initialize(const std::string& configPath);
    
    // Run the agent (blocks until stop() is called)
    // @return exit code
    int run();
    
    // Stop the agent
    void stop();
    
    // Check if running
    bool isRunning() const { return running_.load(); }
    
private:
    // Check for stop signal (console or service)
    bool shouldStop() const;
    
    // Registration flow
    bool performRegistration();
    
    std::unique_ptr<EventQueue> queue_;
    std::unique_ptr<EventBuffer> buffer_;
    std::unique_ptr<SenderInterface> managementSender_; // Management stream (to Manager via mTLS)
    std::unique_ptr<SenderInterface> telemetrySender_;  // Telemetry stream (to Fluent Bit via TCP)
    std::unique_ptr<EventCollector> collector_;
    std::unique_ptr<BatchSender> batchSender_;
    std::unique_ptr<FimMonitor> fimMonitor_;
    std::unique_ptr<SystemInfoCollector> sysInfoCollector_;
    
    // Policy dispatch + subprocess management
    std::unique_ptr<WorkerManager>  workerManager_;
    std::unique_ptr<PolicyManager>  policyManager_;
    std::unique_ptr<ModuleController> moduleController_;
    std::unique_ptr<LogTailer>      logTailer_;
    std::unique_ptr<UsbMonitor>     usbMonitor_;
    
    // Registration components
    std::unique_ptr<CertificateStore> certStore_;
    
    std::atomic<bool> running_{false};
    std::atomic<bool> stopRequested_{false};
    bool useRegistration_ = false;  // True when manager registration is configured
    bool licenseSuspended_ = false; // True when license invalid, collectors stopped
    
    // Agent lifecycle phase (for GUI display)
    std::string agentPhase_ = "initializing";
    
    // Cached config path for writeStatusFile to read manager host/port
    std::string configPath_;

    // Set when a MODULE_COMMAND 'agent_restart' is received
    std::atomic<bool> restartRequested_{false};
    
    // License/status info for GUI (written to status.json)
    std::string licenseMessage_ = "Checking...";
    std::string licenseType_;
    std::string licenseExpiry_;
    std::string lastError_;
    
    // Write status.json for GUI to read
    void writeStatusFile();
    
    // Management thread and status
    std::thread managementThread_;
    void managementLoop();
    

    
    // System info thread and configuration
    std::thread sysInfoThread_;
    std::chrono::hours sysInfoInterval_{24};
    bool collectSysInfoOnStartup_{true};
    
    // System info collection loop
    void sysInfoLoop();

    // Antivirus scheduled scan thread and configuration
    std::thread avScanThread_;
    std::chrono::hours avScanInterval_{24};
    std::chrono::hours avUpdateInterval_{12};

    // Antivirus scheduled scan loop
    void avScanLoop();
    
    // Helper to resolve config dir path
    std::string resolveConfigDir() const;
};

} // namespace ResolutePulse
