#include "Agent.h"
#include "utils/Logger.h"
#include "service/ServiceMain.h"
#include "fim/FimEvent.h"

#include <nlohmann/json.hpp>
#include <fstream>

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
    
    // ─── Registration & Certificate Check ───
    // Check if manager registration is configured in config.json
    {
        std::ifstream cfgFile(configPath);
        if (cfgFile) {
            try {
                nlohmann::json cfgJson;
                cfgFile >> cfgJson;
                if (cfgJson.contains("manager")) {
                    useRegistration_ = true;
                    LOG_INFO("Manager registration enabled");
                }
            } catch (...) {}
        }
    }
    
    if (useRegistration_) {
        if (!performRegistration()) {
            LOG_ERROR("Registration failed");
            return false;
        }
    }
    
    // Initialize components
    queue_ = std::make_unique<EventQueue>(config.getBufferConfig().max_events);
    
    buffer_ = std::make_unique<EventBuffer>();
    if (!buffer_->initialize(config.getBufferConfig().db_path)) {
        LOG_ERROR("Failed to initialize event buffer");
        return false;
    }
    
    // 1. Initialize Telemetry Sender (Always TCP to Fluent Bit)
    auto tcpSender = std::make_unique<TcpSender>();
    if (!tcpSender->initialize(
            config.getFluentBitHost(),
            config.getFluentBitPort())) {
        LOG_ERROR("Failed to initialize TCP sender for events");
        return false;
    }
    telemetrySender_ = std::move(tcpSender);

    // 2. Initialize Management Sender (mTLS if registered)
    if (useRegistration_ && certStore_ && certStore_->exists()) {
        auto tlsSender = std::make_unique<TlsSender>();
        // Read manager config
        std::ifstream cfgFile(configPath);
        nlohmann::json cfgJson;
        cfgFile >> cfgJson;
        auto& mgr = cfgJson["manager"];
        std::string mgrHost = mgr.value("host", "localhost");
        int mgrPort = mgr.value("port", 1514);
        
        if (!tlsSender->initialize(mgrHost, mgrPort,
                certStore_->getAgentCertPath(),
                certStore_->getAgentKeyPath(),
                certStore_->getCACertPath())) {
            LOG_ERROR("Failed to initialize TLS sender for management");
            return false;
        }
        managementSender_ = std::move(tlsSender);
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
        *telemetrySender_,
        *buffer_,
        config.getAgentId(),
        config.getBufferConfig().max_events / 100,  // Batch size ~1% of max
        config.getBufferConfig().flush_interval_sec
    );
    
    // Initialize FIM if enabled
    const auto& fimCfg = config.getFimConfig();
    if (fimCfg.enabled && !fimCfg.directories.empty()) {
        LOG_INFO("Initializing File Integrity Monitoring...");
        
        FimConfig fimConfig;
        fimConfig.directories = fimCfg.directories;
        fimConfig.excludePatterns = fimCfg.exclude_patterns;
        fimConfig.maxFileSizeMb = fimCfg.max_file_size_mb;
        fimConfig.hashFiles = true;
        
        fimMonitor_ = std::make_unique<FimMonitor>();
        if (!fimMonitor_->initialize(fimConfig, fimCfg.db_path)) {
            LOG_WARN("Failed to initialize FIM - continuing without FIM");
            fimMonitor_.reset();
        } else {
            // FIM events go to the same queue as event log events
            fimMonitor_->setEventCallback([this, &config](const FimEvent& fimEvent) {
                // Convert FimEvent to Event for the queue
                Event event;
                event.channel = "FIM";
                event.eventId = 0;  // FIM events don't have Windows Event IDs
                event.timestamp = fimEvent.timestamp;
                event.data = fimEvent.toJson().dump();  // Store FIM data as JSON in data field
                
                queue_->push(std::move(event));
                LOG_DEBUG("FIM event queued: {} {}", 
                         changeTypeToString(fimEvent.changeType), fimEvent.path);
            });
        }
    }
    
    // Initialize System Info if enabled
    const auto& sysInfoCfg = config.getSysInfoConfig();
    if (sysInfoCfg.enabled) {
        LOG_INFO("Initializing System Information Collection...");
        
        sysInfoCollector_ = std::make_unique<SystemInfoCollector>();
        SystemInfoCollectionConfig collectionCfg;
        collectionCfg.collectOS = true;
        collectionCfg.collectApps = true;
        collectionCfg.collectPorts = true;
        
        if (!sysInfoCollector_->initialize(collectionCfg)) {
            LOG_WARN("Failed to initialize System Info Collector - continuing without system info");
            sysInfoCollector_.reset();
        } else {
            collectSysInfoOnStartup_ = sysInfoCfg.collect_on_startup;
            sysInfoInterval_ = std::chrono::hours(sysInfoCfg.collection_interval_hours);
        }
    }
    
    LOG_INFO("Agent initialized successfully");
    LOG_INFO("  Agent ID: {}", config.getAgentId());
    if (useRegistration_) {
        LOG_INFO("  Mode: Registered (mTLS)");
        if (certStore_) {
            LOG_INFO("  Certificate expires in {} days", certStore_->daysUntilExpiry());
        }
    } else {
        LOG_INFO("  Fluent Bit: {}:{}", config.getFluentBitHost(), config.getFluentBitPort());
    }
    LOG_INFO("  Channels: {}", discovery.available.size());
    if (fimMonitor_) {
        LOG_INFO("  FIM: enabled ({} directories)", fimCfg.directories.size());
    }
    if (sysInfoCollector_) {
        LOG_INFO("  System Info: enabled (startup={}, interval={}h)", 
                 collectSysInfoOnStartup_, sysInfoInterval_.count());
    }
    
    return true;
}

bool Agent::performRegistration() {
    LOG_INFO("Checking agent registration status...");
    
    auto& config = ConfigManager::instance();
    std::string certsDir = "certs";
    
    certStore_ = std::make_unique<CertificateStore>();
    certStore_->setCertsDir(certsDir);
    
    // Check if we already have a valid certificate
    if (certStore_->exists()) {
        if (certStore_->load() && certStore_->isValid()) {
            int daysLeft = certStore_->daysUntilExpiry();
            LOG_INFO("Valid certificate found ({} days until expiry)", daysLeft);
            
            if (daysLeft > 1) {
                return true;  // Certificate is good
            }
            LOG_WARN("Certificate expiring soon, will re-register");
        } else {
            LOG_WARN("Certificate expired or invalid, will re-register");
        }
    } else {
        LOG_INFO("No certificate found, registering with manager...");
    }
    
    // Need to register
    RegistrationClient regClient;
    
    // Generate key pair if not exists
    std::string keyPath = certsDir + "/agent.key";
    if (!std::filesystem::exists(keyPath)) {
        if (!regClient.generateKeyPair(certsDir)) {
            LOG_ERROR("Failed to generate key pair");
            return false;
        }
    } else {
        // Load existing public key for re-registration
        // For simplicity, regenerate the key pair
        if (!regClient.generateKeyPair(certsDir)) {
            LOG_ERROR("Failed to generate key pair");
            return false;
        }
    }
    
    // Get hostname
    char hostname[256] = {};
    gethostname(hostname, sizeof(hostname));
    
    // Get OS version
    std::string osVersion = "Windows";
    
    // Read manager settings from config
    // Re-read config.json for manager section
    std::string mgrHost = "localhost";
    int mgrPort = 1514;
    
    // TODO: Read from config properly. For now use defaults.
    
    if (!regClient.registerWithManager(
            mgrHost, mgrPort,
            config.getAgentId(),
            std::string(hostname),
            "windows",
            osVersion,
            "1.0.0",
            *certStore_)) {
        LOG_ERROR("Registration with manager failed: {}", regClient.getLastError());
        return false;
    }
    
    // Reload the certificate store
    if (!certStore_->load()) {
        LOG_ERROR("Failed to load certificates after registration");
        return false;
    }
    
    LOG_INFO("Agent registered and certificates loaded");
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
    
    // Start management thread if registered
    if (managementSender_) {
        managementThread_ = std::thread(&Agent::managementLoop, this);
        LOG_INFO("Management loop started");
    }
    
    // Start FIM if enabled
    if (fimMonitor_) {
        fimMonitor_->start();
        LOG_INFO("FIM monitoring started");
    }
    
    // Collect system info once at startup if enabled
    if (sysInfoCollector_ && collectSysInfoOnStartup_) {
        LOG_INFO("Collecting system information at startup...");
        try {
            auto sysInfoData = sysInfoCollector_->collectAll();
            
            // Convert to Event and queue it
            Event event;
            event.channel = "SystemInfo";
            event.eventId = 0;
            event.timestamp = sysInfoData.timestamp;
            event.data = sysInfoData.toJson().dump();
            
            queue_->push(std::move(event));
            LOG_INFO("System information collected and queued");
        } catch (const std::exception& e) {
            LOG_ERROR("Error collecting system info at startup: {}", e.what());
        }
    }
    
    // Start periodic system info collection thread if enabled
    if (sysInfoCollector_ && sysInfoInterval_.count() > 0) {
        sysInfoThread_ = std::thread(&Agent::sysInfoLoop, this);
        LOG_INFO("System info periodic collection started ({}h interval)", sysInfoInterval_.count());
    }
    
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
    
    // Stop management thread if running
    if (managementThread_.joinable()) {
        managementThread_.join();
    }
    
    // Stop system info thread if running
    if (sysInfoThread_.joinable()) {
        sysInfoThread_.join();
    }
    
    // Stop components in order
    if (fimMonitor_) {
        fimMonitor_->stop();
    }
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

void Agent::sysInfoLoop() {
    LOG_DEBUG("System info collection thread started");
    
    while (!shouldStop()) {
        // Wait for the interval or until stop is requested
        auto waitStart = std::chrono::steady_clock::now();
        auto waitEnd = waitStart + sysInfoInterval_;
        
        while (std::chrono::steady_clock::now() < waitEnd && !shouldStop()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        
        if (shouldStop()) {
            break;
        }
        
        // Collect system info
        LOG_INFO("Periodic system information collection triggered");
        try {
            auto sysInfoData = sysInfoCollector_->collectAll();
            
            // Convert to Event and queue it
            Event event;
            event.channel = "SystemInfo";
            event.eventId = 0;
            event.timestamp = sysInfoData.timestamp;
            event.data = sysInfoData.toJson().dump();
            
            queue_->push(std::move(event));
            LOG_INFO("System information collected and queued");
        } catch (const std::exception& e) {
            LOG_ERROR("Error during periodic system info collection: {}", e.what());
        }
    }
    
    LOG_DEBUG("System info collection thread stopped");
}

void Agent::managementLoop() {
    LOG_DEBUG("Management loop background thread started");
    auto& config = ConfigManager::instance();
    std::string agentId = config.getAgentId();
    
    while (!shouldStop()) {
        if (managementSender_) {
            LOG_DEBUG("Sending management heartbeat...");
            
            uint64_t collected = collector_ ? collector_->getEventsCollected() : 0;
            uint64_t sent = batchSender_ ? batchSender_->getEventsSent() : 0;
            
            SendResult result = managementSender_->sendHeartbeat(agentId, collected, sent);
            
            if (result == SendResult::Success) {
                LOG_DEBUG("Heartbeat acknowledged by manager");
            } else if (result == SendResult::AuthError) {
                LOG_CRITICAL("License validation failed or mTLS authentication error: {}", managementSender_->getLastError());
                LOG_CRITICAL("Suspending telemetry collection due to license enforcement.");
                
                if (collector_) collector_->stop();
                if (batchSender_) batchSender_->stop();
                
                // We keep the loop running to check for license updates/restoration,
                // but we'll log more frequently or just wait.
            } else {
                LOG_WARN("Manager heartbeat failed: {}", managementSender_->getLastError());
            }
        }
        
        // Wait for interval (60s) or stop
        auto nextRun = std::chrono::steady_clock::now() + std::chrono::seconds(60);
        while (std::chrono::steady_clock::now() < nextRun && !shouldStop()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
    
    LOG_DEBUG("Management loop background thread stopped");
}

} // namespace ResolutePulse
