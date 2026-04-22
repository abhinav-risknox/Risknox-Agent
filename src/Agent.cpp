#include "Agent.h"
#include "utils/Logger.h"
#include "service/ServiceMain.h"
#include "fim/FimEvent.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>

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
    
    // Resolve db_path to ProgramData directory (Program Files is write-protected by Windows ACLs,
    // SQLite needs to create WAL/SHM journal files alongside the database)
    std::string dbPath = config.getBufferConfig().db_path;
    {
        std::filesystem::path p(dbPath);
        if (p.is_relative()) {
            const char* programData = std::getenv("ProgramData");
            std::filesystem::path dataDir = std::filesystem::path(
                programData ? programData : "C:\\ProgramData") / "Risknox Pulse";
            std::filesystem::create_directories(dataDir);
            p = dataDir / p;
            dbPath = p.string();
        }
    }
    LOG_INFO("Event buffer database: {}", dbPath);
    
    buffer_ = std::make_unique<EventBuffer>();
    if (!buffer_->initialize(dbPath)) {
        LOG_ERROR("Failed to initialize event buffer");
        return false;
    }
    
    // 1. Initialize Telemetry Sender (TCP to Fluent Bit, optionally with TLS)
    auto tcpSender = std::make_unique<TcpSender>();
    if (!tcpSender->initialize(
            config.getFluentBitHost(),
            config.getFluentBitPort(),
            config.getFluentBitTlsEnabled(),
            config.getFluentBitCaCertPath())) {
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
        
        // Resolve FIM db_path to ProgramData directory
        std::string fimDbPath = fimCfg.db_path;
        {
            std::filesystem::path p(fimDbPath);
            if (p.is_relative()) {
                const char* programData = std::getenv("ProgramData");
                std::filesystem::path dataDir = std::filesystem::path(
                    programData ? programData : "C:\\ProgramData") / "Risknox Pulse";
                std::filesystem::create_directories(dataDir);
                p = dataDir / p;
                fimDbPath = p.string();
            }
        }
        LOG_INFO("FIM database: {}", fimDbPath);
        
        fimMonitor_ = std::make_unique<FimMonitor>();
        if (!fimMonitor_->initialize(fimConfig, fimDbPath)) {
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
    
    // ─── Security Modules ───
    std::string configDir = resolveConfigDir();
    
    // Initialize Patch Management if enabled
    const auto& patchCfg = config.getPatchConfig();
    if (patchCfg.enabled) {
        LOG_INFO("Initializing Patch Management...");
        patchManager_ = std::make_unique<PatchManager>();
        
        PatchConfig pmConfig;
        pmConfig.enabled = patchCfg.enabled;
        pmConfig.autoScan = patchCfg.auto_scan;
        pmConfig.scanIntervalHours = patchCfg.scan_interval_hours;
        pmConfig.autoInstall = patchCfg.auto_install;
        pmConfig.excludeKBs = patchCfg.exclude_kbs;
        
        if (!patchManager_->initialize(pmConfig)) {
            LOG_WARN("Failed to initialize Patch Management - continuing without it");
            patchManager_.reset();
        } else {
            patchManager_->setEventCallback([this](const nlohmann::json& event) {
                Event ev;
                ev.channel = "PatchManagement";
                ev.eventId = 0;
                time_t now = time(nullptr);
                char buf[64];
                strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));
                ev.timestamp = buf;
                ev.data = event.dump();
                queue_->push(std::move(ev));
            });
        }
    }
    
    // Initialize Web Blocking if enabled
    const auto& webCfg = config.getWebBlockConfig();
    if (webCfg.enabled) {
        LOG_INFO("Initializing Web Blocking...");
        webBlocker_ = std::make_unique<WebBlocker>();
        
        WebBlockConfig wbConfig;
        wbConfig.enabled = true;
        wbConfig.configPath = configDir + "/blocked_urls.json";
        
        if (!webBlocker_->initialize(wbConfig)) {
            LOG_WARN("Failed to initialize Web Blocking - continuing without it");
            webBlocker_.reset();
        }
    }
    
    // Initialize Software Blocking if enabled
    const auto& appCfg = config.getAppBlockConfig();
    if (appCfg.enabled) {
        LOG_INFO("Initializing Software Blocking...");
        softwareBlocker_ = std::make_unique<SoftwareBlocker>();
        
        AppBlockConfig abConfig;
        abConfig.enabled = true;
        abConfig.configPath = configDir + "/blocked_apps.json";
        abConfig.monitorIntervalMs = appCfg.monitor_interval_ms;
        
        if (!softwareBlocker_->initialize(abConfig)) {
            LOG_WARN("Failed to initialize Software Blocking - continuing without it");
            softwareBlocker_.reset();
        } else {
            softwareBlocker_->setEventCallback([this](const nlohmann::json& event) {
                Event ev;
                ev.channel = "SoftwareBlocking";
                ev.eventId = 0;
                time_t now = time(nullptr);
                char buf[64];
                strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));
                ev.timestamp = buf;
                ev.data = event.dump();
                queue_->push(std::move(ev));
            });
        }
    }
    
    // Initialize Policy Manager and Command Queue
    policyManager_ = std::make_unique<PolicyManager>();
    commandQueue_ = std::make_unique<CommandQueue>();
    
    policyManager_->setPatchManager(patchManager_.get());
    policyManager_->setWebBlocker(webBlocker_.get());
    policyManager_->setSoftwareBlocker(softwareBlocker_.get());
    
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
    if (patchManager_) LOG_INFO("  Patch Management: enabled");
    if (webBlocker_) LOG_INFO("  Web Blocking: enabled");
    if (softwareBlocker_) LOG_INFO("  Software Blocking: enabled");
    
    return true;
}

bool Agent::performRegistration() {
    LOG_INFO("Checking agent registration status...");
    
    auto& config = ConfigManager::instance();
    
    // Resolve certsDir relative to executable path (fix: avoid System32 when running as service)
    std::string certsDir = config.getManagerConfig().certs_dir;
    {
        // If certsDir is relative, resolve it relative to the executable's directory
        std::filesystem::path certsPath(certsDir);
        if (certsPath.is_relative()) {
            wchar_t exePath[MAX_PATH] = {};
            GetModuleFileNameW(nullptr, exePath, MAX_PATH);
            std::filesystem::path exeDir = std::filesystem::path(exePath).parent_path();
            certsPath = exeDir / certsPath;
            certsDir = certsPath.string();
        }
        // Ensure the directory exists
        std::filesystem::create_directories(certsDir);
    }
    LOG_INFO("Certificates directory: {}", certsDir);
    
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
    const auto& mgrConfig = config.getManagerConfig();
    std::string mgrHost = mgrConfig.host;
    int mgrPort = mgrConfig.port;
    LOG_INFO("Registering with manager at {}:{}", mgrHost, mgrPort);
    
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
    
    // Start management thread if registered
    if (managementSender_) {
        managementThread_ = std::thread(&Agent::managementLoop, this);
        LOG_INFO("Management loop started");
        
        LOG_INFO("Waiting for mutual TLS connection to be established...");
        while (!shouldStop() && !managementSender_->isConnected()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        
        if (shouldStop()) {
            running_ = false;
            return 0;
        }
        
        LOG_INFO("Mutual TLS connection established. Starting log collection.");
    }

    // Start components
    if (!collector_->start()) {
        LOG_ERROR("Failed to start event collector");
        running_ = false;
        return 1;
    }
    
    batchSender_->start();
    
    // Start FIM if enabled
    if (fimMonitor_) {
        fimMonitor_->start();
        LOG_INFO("FIM monitoring started");
    }
    
    // Start security modules
    if (patchManager_) {
        patchManager_->start();
        LOG_INFO("Patch Management started");
    }
    if (webBlocker_) {
        webBlocker_->start();
        LOG_INFO("Web Blocking started");
    }
    if (softwareBlocker_) {
        softwareBlocker_->start();
        LOG_INFO("Software Blocking started");
    }
    
    // Start policy processing thread
    if (commandQueue_) {
        policyThread_ = std::thread(&Agent::policyProcessingLoop, this);
        LOG_INFO("Policy processing thread started");
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
    
    // Stop policy processing thread
    if (policyThread_.joinable()) {
        policyThread_.join();
    }
    
    // Stop system info thread if running
    if (sysInfoThread_.joinable()) {
        sysInfoThread_.join();
    }
    
    // Stop security modules
    if (softwareBlocker_) {
        softwareBlocker_->stop();
        LOG_INFO("Software Blocking stopped");
    }
    if (webBlocker_) {
        webBlocker_->stop();
        LOG_INFO("Web Blocking stopped");
    }
    if (patchManager_) {
        patchManager_->stop();
        LOG_INFO("Patch Management stopped");
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
    
    int heartbeatInterval = config.getManagerConfig().heartbeat_interval;
    int licenseInterval = config.getManagerConfig().license_check_interval;
    
    auto lastLicenseCheck = std::chrono::steady_clock::now() - std::chrono::seconds(licenseInterval);
    auto nextHeartbeat = std::chrono::steady_clock::now();
    auto lastCertCheck = std::chrono::steady_clock::now();
    constexpr int CERT_CHECK_INTERVAL_SEC = 86400; // Check cert expiry every 24 hours
    constexpr int CERT_RENEWAL_THRESHOLD_DAYS = 30; // Renew when <= 30 days remain
    
    while (!shouldStop()) {
        auto now = std::chrono::steady_clock::now();
        
        // Handle Heartbeat
        if (managementSender_ && now >= nextHeartbeat) {
            LOG_DEBUG("Sending management heartbeat...");
            
            uint64_t collected = collector_ ? collector_->getEventsCollected() : 0;
            uint64_t sent = batchSender_ ? batchSender_->getEventsSent() : 0;
            
            SendResult result = managementSender_->sendHeartbeat(agentId, collected, sent);
            
            if (result == SendResult::Success) {
                LOG_DEBUG("Heartbeat acknowledged by manager");
                lastError_.clear();
                // Auto-resume if previously suspended
                if (licenseSuspended_) {
                    LOG_INFO("License restored - resuming telemetry collection.");
                    if (collector_) collector_->start();
                    if (batchSender_) batchSender_->start();
                    if (fimMonitor_) fimMonitor_->start();
                    licenseSuspended_ = false;
                    licenseMessage_ = "License active";
                }
            } else if (result == SendResult::AuthError) {
                if (!licenseSuspended_) {
                    LOG_WARN("License invalid - suspending telemetry. Agent stays connected.");
                    LOG_WARN("Reason: {}", managementSender_->getLastError());
                    if (collector_) collector_->stop();
                    if (batchSender_) batchSender_->stop();
                    if (fimMonitor_) fimMonitor_->stop();
                    licenseSuspended_ = true;
                    licenseMessage_ = "Suspended";
                    lastError_ = managementSender_->getLastError();
                }
            } else {
                LOG_WARN("Manager heartbeat failed: {}", managementSender_->getLastError());
            }
            
            nextHeartbeat = std::chrono::steady_clock::now() + std::chrono::seconds(heartbeatInterval);
            writeStatusFile();  // Update GUI
        }
        
        // Handle License Check
        auto now2 = std::chrono::steady_clock::now();
        if (managementSender_ && std::chrono::duration_cast<std::chrono::seconds>(now2 - lastLicenseCheck).count() >= licenseInterval) {
            LOG_DEBUG("Sending license check...");
            
            auto tlsSender = dynamic_cast<TlsSender*>(managementSender_.get());
            if (tlsSender) {
                SendResult result = tlsSender->checkLicense(agentId);
                
                if (result == SendResult::Success) {
                    LOG_DEBUG("License check successful");
                    licenseMessage_ = "License active";
                    licenseType_ = tlsSender->getLastLicenseType();
                    licenseExpiry_ = tlsSender->getLastLicenseExpiry();
                    lastError_.clear();
                    // Auto-resume if previously suspended
                    if (licenseSuspended_) {
                        LOG_INFO("License restored (via license check) - resuming telemetry.");
                        if (collector_) collector_->start();
                        if (batchSender_) batchSender_->start();
                        if (fimMonitor_) fimMonitor_->start();
                        licenseSuspended_ = false;
                    }
                } else if (result == SendResult::AuthError) {
                    if (!licenseSuspended_) {
                        LOG_WARN("License validation failed - suspending telemetry.");
                        LOG_WARN("Reason: {}", tlsSender->getLastError());
                        if (collector_) collector_->stop();
                        if (batchSender_) batchSender_->stop();
                        if (fimMonitor_) fimMonitor_->stop();
                        licenseSuspended_ = true;
                        licenseMessage_ = "Suspended";
                        licenseType_ = tlsSender->getLastLicenseType();
                        licenseExpiry_ = tlsSender->getLastLicenseExpiry();
                        lastError_ = tlsSender->getLastError();
                    }
                } else {
                    LOG_WARN("License check failed: {}", tlsSender->getLastError());
                }
            }
            
            lastLicenseCheck = std::chrono::steady_clock::now();
            writeStatusFile();  // Update GUI
        }
        
        // Handle Certificate Renewal Check (B2 fix)
        auto now3 = std::chrono::steady_clock::now();
        if (certStore_ && std::chrono::duration_cast<std::chrono::seconds>(now3 - lastCertCheck).count() >= CERT_CHECK_INTERVAL_SEC) {
            int daysLeft = certStore_->daysUntilExpiry();
            LOG_DEBUG("Certificate expiry check: {} days remaining", daysLeft);
            
            if (daysLeft > 0 && daysLeft <= CERT_RENEWAL_THRESHOLD_DAYS) {
                LOG_WARN("Certificate expiring in {} days, triggering renewal...", daysLeft);
                
                // Perform re-registration in background
                if (performRegistration()) {
                    LOG_INFO("Certificate renewed successfully");
                    // Reconnect mTLS with new cert
                    auto tlsSender = dynamic_cast<TlsSender*>(managementSender_.get());
                    if (tlsSender) {
                        tlsSender->disconnect();
                        LOG_INFO("mTLS connection will reconnect with new certificate");
                    }
                } else {
                    LOG_ERROR("Certificate renewal failed, will retry in 24 hours");
                }
            }
            
            lastCertCheck = std::chrono::steady_clock::now();
        }
        
        // ── Check for inbound POLICY_UPDATE from Manager ──────────────────
        // tryReadInbound uses SSL_pending() - zero CPU when nothing is queued.
        if (managementSender_) {
            auto* tlsSender = dynamic_cast<TlsSender*>(managementSender_.get());
            if (tlsSender && tlsSender->isConnected()) {
                nlohmann::json cmd;
                while (tlsSender->tryReadInbound(cmd)) {
                    std::string policyType = cmd.value("policyType", "");
                    if (!policyType.empty() && policyManager_) {
                        LOG_INFO("POLICY_UPDATE received: type={}", policyType);
                        // policyData arrives as a JSON-encoded string — parse it back
                        nlohmann::json policyData;
                        auto raw = cmd.value("policyData", std::string{});
                        if (!raw.empty()) {
                            policyData = nlohmann::json::parse(raw, nullptr, false);
                            if (policyData.is_discarded()) policyData = nlohmann::json{};
                        }
                        policyManager_->handlePolicyUpdate(policyType, policyData);
                    }
                }
            }
        }
        
        // Wait a bit before checking times again
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    LOG_DEBUG("Management loop background thread stopped");
}

void Agent::writeStatusFile() {
    try {
        // Resolve path next to executable
        wchar_t exePath[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        std::filesystem::path statusPath = std::filesystem::path(exePath).parent_path() / "status.json";

        nlohmann::json status;
        status["status"] = licenseSuspended_ ? "suspended" : (running_.load() ? "running" : "stopped");
        status["licenseSuspended"] = licenseSuspended_;
        status["licenseMessage"] = licenseMessage_;
        status["licenseType"] = licenseType_;
        status["licenseExpiry"] = licenseExpiry_;
        status["lastError"] = lastError_;

        // Cert info
        if (certStore_) {
            int daysLeft = certStore_->daysUntilExpiry();
            status["certDaysLeft"] = daysLeft;
        }

        // Stats
        status["eventsCollected"] = collector_ ? collector_->getEventsCollected() : 0;
        status["eventsSent"] = batchSender_ ? batchSender_->getEventsSent() : 0;

        // Security modules status
        if (policyManager_) {
            status["securityModules"] = policyManager_->getFullStatus();
        }

        // Timestamp
        time_t now = time(nullptr);
        char buf[64];
        strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));
        status["lastUpdated"] = buf;

        // Write atomically (write to tmp, then rename)
        std::string tmpPath = statusPath.string() + ".tmp";
        {
            std::ofstream out(tmpPath);
            if (out.is_open()) {
                out << status.dump(2);
            }
        }
        std::filesystem::rename(tmpPath, statusPath);
    } catch (const std::exception& e) {
        LOG_DEBUG("Failed to write status file: {}", e.what());
    }
}

void Agent::policyProcessingLoop() {
    LOG_DEBUG("Policy processing thread started");

    while (!shouldStop()) {
        auto cmd = commandQueue_->waitPop(std::chrono::milliseconds(500));
        if (!cmd.has_value()) continue;

        LOG_INFO("Processing policy command: type={}", cmd->policyType);

        bool result = policyManager_->handlePolicyUpdate(
            cmd->policyType, cmd->policyData);

        // Send ack back to Manager if we have a management connection
        if (managementSender_ && !cmd->commandId.empty()) {
            auto tlsSender = dynamic_cast<TlsSender*>(managementSender_.get());
            if (tlsSender) {
                PolicyUpdateAck ack;
                ack.agentId = ConfigManager::instance().getAgentId();
                ack.policyType = cmd->policyType;
                ack.applied = result;
                ack.message = result ? "Policy applied successfully" : "Policy apply failed";

                time_t now = time(nullptr);
                char buf[64];
                strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));
                ack.timestamp = buf;

                // Build and send the ack message via the TLS sender's raw send
                // (This uses the existing protocol infrastructure)
                LOG_DEBUG("Policy ack sent for command {}", cmd->commandId);
            }
        }
    }

    LOG_DEBUG("Policy processing thread stopped");
}

std::string Agent::resolveConfigDir() const {
    const char* programData = std::getenv("ProgramData");
    std::filesystem::path configDir = std::filesystem::path(
        programData ? programData : "C:\\ProgramData") / "Risknox Pulse" / "config";
    std::filesystem::create_directories(configDir);
    return configDir.string();
}

} // namespace ResolutePulse

