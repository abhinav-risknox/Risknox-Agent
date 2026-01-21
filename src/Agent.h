#pragma once

#include "config/ConfigManager.h"
#include "collector/ChannelDiscovery.h"
#include "collector/EventCollector.h"
#include "queue/EventQueue.h"
#include "buffer/EventBuffer.h"
#include "network/HttpSender.h"
#include "network/OpenSearchSender.h"
#include "sender/BatchSender.h"
#include "fim/FimMonitor.h"

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
    
    std::unique_ptr<EventQueue> queue_;
    std::unique_ptr<EventBuffer> buffer_;
    std::unique_ptr<HttpSender> sender_;
    std::unique_ptr<OpenSearchSender> openSearchSender_;
    std::unique_ptr<EventCollector> collector_;
    std::unique_ptr<BatchSender> batchSender_;
    std::unique_ptr<FimMonitor> fimMonitor_;
    
    std::atomic<bool> running_{false};
    std::atomic<bool> stopRequested_{false};
};

} // namespace ResolutePulse
