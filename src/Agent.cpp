#include "Agent.h"
#include "utils/Logger.h"
#include "service/ServiceMain.h"

#include <thread>
#include <chrono>

namespace ResolutePulse {

Agent::Agent() = default;

Agent::~Agent() {
    stop();
}

bool Agent::initialize(const std::string& configPath) {
    LOG_INFO("Initializing Resolute Pulse Agent...");
    
    // Load configuration
    auto& config = ConfigManager::instance();
    if (!config.load(configPath)) {
        LOG_ERROR("Failed to load configuration");
        return false;
    }
    
    // Set log level from config
    Logger::setLevel(config.getLogLevel());
    
    // Discover available channels
    LOG_INFO("Discovering event log channels...");
    auto discovery = ChannelDiscovery::discover(
        config.getEventChannels(),
        config.getCriticalChannels()
    );
    
    // Check for missing critical channels
    if (!discovery.missing_critical.empty()) {
        LOG_CRITICAL("Critical channels are missing:");
        for (const auto& channel : discovery.missing_critical) {
            LOG_CRITICAL("  - {}", channel);
        }
        LOG_CRITICAL("Agent cannot start without critical channels");
        return false;
    }
    
    // Log missing optional channels
    if (!discovery.missing_optional.empty()) {
        LOG_WARN("The following optional channels are not available:");
        for (const auto& channel : discovery.missing_optional) {
            LOG_WARN("  - {}", channel);
        }
    }
    
    // Initialize components
    queue_ = std::make_unique<EventQueue>(config.getBufferConfig().max_events);
    
    buffer_ = std::make_unique<EventBuffer>();
    if (!buffer_->initialize(config.getBufferConfig().db_path)) {
        LOG_ERROR("Failed to initialize event buffer");
        return false;
    }
    
    sender_ = std::make_unique<HttpSender>();
    if (!sender_->initialize(
            config.getManagerHttpUrl(),
            config.getAuthToken(),
            config.getTlsConfig())) {
        LOG_ERROR("Failed to initialize HTTP sender");
        return false;
    }
    
    collector_ = std::make_unique<EventCollector>();
    
    // Convert filters from config format to collector format
    std::map<std::string, std::set<int>> filters;
    for (const auto& [channel, ids] : config.getEventFilters()) {
        filters[channel] = ids;
    }
    
    if (!collector_->initialize(discovery.available, filters)) {
        LOG_ERROR("Failed to initialize event collector");
        return false;
    }
    
    // Set up event callback
    collector_->setEventCallback([this](Event&& event) {
        queue_->push(std::move(event));
    });
    
    // Initialize batch sender
    batchSender_ = std::make_unique<BatchSender>(
        *queue_,
        *sender_,
        *buffer_,
        config.getAgentId(),
        config.getBufferConfig().max_events / 100,  // Batch size ~1% of max
        config.getBufferConfig().flush_interval_sec
    );
    
    LOG_INFO("Agent initialized successfully");
    LOG_INFO("  Agent ID: {}", config.getAgentId());
    LOG_INFO("  Server: {}", config.getManagerHttpUrl());
    LOG_INFO("  Channels: {}", discovery.available.size());
    
    return true;
}

int Agent::run() {
    if (running_.load()) {
        LOG_WARN("Agent already running");
        return 1;
    }
    
    LOG_INFO("Starting agent...");
    running_ = true;
    stopRequested_ = false;
    
    // Start components
    if (!collector_->start()) {
        LOG_ERROR("Failed to start event collector");
        running_ = false;
        return 1;
    }
    
    batchSender_->start();
    
    LOG_INFO("Agent running. Press Ctrl+C to stop (console mode).");
    
    // Main loop - just wait for stop signal
    while (!shouldStop()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        // Periodic status log (every 60 seconds)
        static auto lastStatus = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - lastStatus).count() >= 60) {
            LOG_INFO("Status: Collected={}, Filtered={}, Sent={}, Queued={}, Buffered={}",
                     collector_->getEventsCollected(),
                     collector_->getEventsFiltered(),
                     batchSender_->getEventsSent(),
                     queue_->size(),
                     buffer_->getEventCount());
            lastStatus = now;
        }
    }
    
    LOG_INFO("Stopping agent...");
    
    // Stop components in order
    collector_->stop();
    batchSender_->stop();
    
    // Final status
    LOG_INFO("Final status:");
    LOG_INFO("  Events collected: {}", collector_->getEventsCollected());
    LOG_INFO("  Events filtered: {}", collector_->getEventsFiltered());
    LOG_INFO("  Batches sent: {}", batchSender_->getBatchesSent());
    LOG_INFO("  Events sent: {}", batchSender_->getEventsSent());
    LOG_INFO("  Events buffered: {}", buffer_->getEventCount());
    
    running_ = false;
    LOG_INFO("Agent stopped");
    
    return 0;
}

void Agent::stop() {
    stopRequested_ = true;
}

bool Agent::shouldStop() const {
    // Check our own stop flag
    if (stopRequested_.load()) {
        return true;
    }
    
    // Check service stop request
    if (ServiceMain::isStopRequested()) {
        return true;
    }
    
    return false;
}

} // namespace ResolutePulse
