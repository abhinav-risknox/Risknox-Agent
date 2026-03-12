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
    
    // Registration components
    std::unique_ptr<CertificateStore> certStore_;
    
    std::atomic<bool> running_{false};
    std::atomic<bool> stopRequested_{false};
    bool useRegistration_ = false;  // True when manager registration is configured
    
    // Management thread and status
    std::thread managementThread_;
    void managementLoop();
    
    // System info thread and configuration
    std::thread sysInfoThread_;
    std::chrono::hours sysInfoInterval_{24};
    bool collectSysInfoOnStartup_{true};
    
    // System info collection loop
    void sysInfoLoop();
};

} // namespace ResolutePulse
