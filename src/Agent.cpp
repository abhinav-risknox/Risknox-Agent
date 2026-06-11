#include "Agent.h"
#include "utils/Logger.h"
#include "utils/PathUtils.h"
#include "utils/MotwChecker.h"
#include "service/ServiceMain.h"
#include "fim/FimEvent.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>

#include <thread>
#include <chrono>
#include <algorithm>

namespace ResolutePulse {

Agent::Agent() = default;

Agent::~Agent() {
    stop();
}

bool Agent::initialize(const std::string& configPath) {
    LOG_INFO("Initializing Resolute Pulse Agent...");
    configPath_ = configPath;
    agentPhase_ = "initializing";
    writeStatusFile();  // Immediate feedback to GUI
    
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
        agentPhase_ = "registering";
        writeStatusFile();
        if (!performRegistration()) {
            LOG_ERROR("Registration failed");
            return false;
        }
    }
    
    // Initialize components
    queue_ = std::make_unique<EventQueue>(config.getBufferConfig().max_events);
    
    // Initialize log tailer (reads new lines from ClamAV logs into the event queue)
    logTailer_ = std::make_unique<LogTailer>(*queue_);
    {
        // Set base directory to the agent's executable directory
        logTailer_->setBaseDir(PathUtils::getExecutableDir().string());

        // Persist offsets to ProgramData so they survive restarts
        std::filesystem::path dataDir = PathUtils::getAgentDataDir();
        std::filesystem::create_directories(dataDir);
        logTailer_->setOffsetPath((dataDir / "logtailer_offsets.json").string());
    }

    // If config already has log_forwarding.logs, activate them on startup
    {
        std::ifstream lfCfgFile(configPath);
        if (lfCfgFile) {
            try {
                nlohmann::json lfCfgJson;
                lfCfgFile >> lfCfgJson;
                if (lfCfgJson.contains("log_forwarding") &&
                    lfCfgJson["log_forwarding"].contains("logs")) {
                    logTailer_->reconfigureFromJson(lfCfgJson["log_forwarding"]["logs"]);
                    LOG_INFO("LogTailer: loaded config from config.json");
                }
            } catch (...) {}
        }
    }
    
    // Resolve db_path to ProgramData directory (Program Files is write-protected by Windows ACLs,
    // SQLite needs to create WAL/SHM journal files alongside the database)
    std::string dbPath = config.getBufferConfig().db_path;
    {
        std::filesystem::path p(dbPath);
        if (p.is_relative()) {
            std::filesystem::path dataDir = PathUtils::getAgentDataDir();
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

        // Auto-discover all users' Downloads folders and add to FIM watch list
        {
            const auto& dlCfg = config.getDownloadScanConfig();
            if (dlCfg.enabled) {
                // Enumerate C:\Users\* to find all user profile directories
                std::filesystem::path usersRoot = "C:\\Users";
                std::error_code ec;
                for (const auto& entry : std::filesystem::directory_iterator(usersRoot, ec)) {
                    if (!entry.is_directory(ec)) continue;
                    std::string name = entry.path().filename().string();
                    // Skip system pseudo-profiles
                    if (name == "Public" || name == "Default" ||
                        name == "Default User" || name == "All Users") continue;
                    std::filesystem::path dlPath = entry.path() / "Downloads";
                    if (std::filesystem::exists(dlPath, ec)) {
                        std::string dlStr = dlPath.string();
                        // Only add if not already in the list
                        if (std::find(fimConfig.directories.begin(),
                                      fimConfig.directories.end(), dlStr)
                            == fimConfig.directories.end()) {
                            fimConfig.directories.push_back(dlStr);
                            LOG_INFO("Download scan: watching {}", dlStr);
                        }
                    }
                }
            }
        }
        
        // Resolve FIM db_path to ProgramData directory
        std::string fimDbPath = fimCfg.db_path;
        {
            std::filesystem::path p(fimDbPath);
            if (p.is_relative()) {
                std::filesystem::path dataDir = PathUtils::getAgentDataDir();
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
            const auto& dlCfg = config.getDownloadScanConfig();
            fimMonitor_->setEventCallback([this, dlCfg](const FimEvent& fimEvent) {
                // Convert FimEvent to Event for the queue
                Event event;
                event.channel = "FIM";
                event.eventId = 0;
                event.timestamp = fimEvent.timestamp;
                event.data = fimEvent.toJson().dump();
                event.sourceType = "winevent";

                queue_->push(std::move(event));
                LOG_DEBUG("FIM event queued: {} {}",
                         changeTypeToString(fimEvent.changeType), fimEvent.path);

                // ── New-file AV trigger ───────────────────────────────────────
                // Trigger on Created OR Modified (browsers rename .crdownload → final file,
                // which USN Journal reports as Modified, not Created).
                // MotW check is deferred to the debounce loop so Zone.Identifier has
                // time to be written by the browser before we check it.
                if (dlCfg.enabled &&
                    (fimEvent.changeType == FimChangeType::Created ||
                     fimEvent.changeType == FimChangeType::Modified)) {

                    // Only act on files inside a Downloads-like directory
                    std::string lowerPath = fimEvent.path;
                    std::transform(lowerPath.begin(), lowerPath.end(),
                                   lowerPath.begin(), ::tolower);
                    bool inDownloads = lowerPath.find("\\downloads\\") != std::string::npos;

                    if (inDownloads) {
                        // Extension filter (empty list = scan all)
                        bool extOk = dlCfg.scan_extensions.empty();
                        if (!extOk) {
                            for (const auto& ext : dlCfg.scan_extensions) {
                                if (lowerPath.size() >= ext.size() &&
                                    lowerPath.compare(lowerPath.size() - ext.size(),
                                                      ext.size(), ext) == 0) {
                                    extOk = true;
                                    break;
                                }
                            }
                        }

                        if (extOk) {
                            // Queue for debounce — MotW check happens there after 5s
                            // so Zone.Identifier is guaranteed to exist by then
                            LOG_INFO("Download detected, queuing for scan: {}", fimEvent.path);
                            std::lock_guard<std::mutex> lock(avScanQueueMutex_);
                            avScanQueue_.push_back(fimEvent.path);
                        }
                    }
                }
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
    
    // ─── Security Worker Subprocesses ───
    // Initialize WorkerManager - the broker for all worker subprocess IPC.
    workerManager_ = std::make_unique<WorkerManager>();

    // Spawn persistent workers (they run for the lifetime of the Agent and
    // are automatically restarted by the WorkerManager watchdog if they crash).
    const auto& webCfg = config.getWebBlockConfig();
    if (webCfg.enabled) {
        LOG_INFO("Spawning rp-webblock.exe subprocess...");
        if (workerManager_->spawnWorker("rp-webblock.exe", "rp-webblock", /*persistent=*/true)) {
            LOG_INFO("rp-webblock.exe spawned successfully");
        } else {
            LOG_WARN("Failed to spawn rp-webblock.exe - web blocking unavailable");
        }
    }

    const auto& appCfg = config.getAppBlockConfig();
    if (appCfg.enabled) {
        LOG_INFO("Spawning rp-softblock.exe subprocess...");
        if (workerManager_->spawnWorker("rp-softblock.exe", "rp-softblock", /*persistent=*/true)) {
            LOG_INFO("rp-softblock.exe spawned successfully");
        } else {
            LOG_WARN("Failed to spawn rp-softblock.exe - software blocking unavailable");
        }
    }
    // Note: rp-patch.exe and rp-antivirus.exe are on-demand workers spawned
    // per-request by PolicyManager, not persistent background processes.

    // ── USB Auto-Scan ─────────────────────────────────────────────────────────
    const auto& usbCfg = config.getUsbScanConfig();
    if (usbCfg.enabled && config.getAntivirusConfig().enabled) {
        LOG_INFO("Initializing USB monitor (auto-scan on insertion)...");
        usbMonitor_ = std::make_unique<UsbMonitor>();
        usbMonitor_->setScanDelaySeconds(usbCfg.scan_delay_seconds);
        usbMonitor_->setArrivalCallback([this](const UsbDriveInfo& drive) {
            LOG_INFO("USB inserted: {} ({}) — queuing AV scan", drive.driveLetter, drive.volumeName);
            // Trigger an immediate AV scan on the USB drive
            nlohmann::json policy;
            policy["action"] = "quick_scan";
            policy["path"]   = drive.driveLetter + "\\";
            if (policyManager_) {
                policyManager_->handlePolicyUpdate("antivirus", policy);
            }
        });
        usbMonitor_->setRemovalCallback([](const std::string& driveLetter) {
            LOG_INFO("USB removed: {}", driveLetter);
        });
        usbMonitor_->start();
        LOG_INFO("USB monitor started");
    }

    // Setup PolicyManager - dispatches policies to workers via Named Pipe IPC
    policyManager_ = std::make_unique<PolicyManager>();
    policyManager_->setWorkerManager(workerManager_.get());

    // Wire status reports to flow back through the mTLS tunnel.
    // IMPORTANT: the statusCallback fires from background threads (avScanLoop,
    // sysInfoLoop, PolicyManager's streamEvents thread). We must NOT call
    // sendStatusReport() directly from those threads — it is not SSL-safe.
    // Instead, enqueue and let the management loop thread drain the queue.
    if (managementSender_) {
        std::string agentId = config.getAgentId();
        policyManager_->setStatusReportCallback(
            [this, agentId](const std::string& reportType, const nlohmann::json& data) {
                auto* tls = dynamic_cast<TlsSender*>(managementSender_.get());
                if (tls) {
                    tls->enqueueStatusReport(agentId, reportType, data);
                }
            });
    }

    // ── ModuleController ─────────────────────────────────────────
    // Controls all agent modules via MODULE_COMMAND verbs from the Manager.
    moduleController_ = std::make_unique<ModuleController>();
    moduleController_->setCollector(collector_.get());
    moduleController_->setBatchSender(batchSender_.get());
    moduleController_->setFimMonitor(fimMonitor_.get());
    moduleController_->setWorkerManager(workerManager_.get());
    moduleController_->setLogTailer(logTailer_.get());
    moduleController_->setConfigPath(configPath_);
    moduleController_->setAgentId(config.getAgentId());

    // Resolve exe dir (same directory as the agent itself)
    {
        wchar_t exePath[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        std::string exeDir = std::filesystem::path(exePath).parent_path().string();
        moduleController_->setWorkerExeDir(exeDir);
    }

    // Status-flush callback: force an immediate status.json write
    moduleController_->setStatusFlushCallback([this]() {
        writeStatusFile();
    });

    // Status-report callback: push a STATUS_REPORT over mTLS immediately
    if (managementSender_) {
        std::string agentId = config.getAgentId();
        moduleController_->setStatusReportCallback([this, agentId]() {
            LOG_INFO("Agent: status_request received, triggering full policy status refresh");
            if (policyManager_) {
                nlohmann::json statusData;
                statusData["phase"]           = agentPhase_;
                statusData["licenseSuspended"] = licenseSuspended_;
                statusData["eventsCollected"]  = collector_ ? collector_->getEventsCollected() : 0;
                statusData["eventsSent"]       = batchSender_ ? batchSender_->getEventsSent() : 0;
                
                policyManager_->sendStatusReport(statusData);
            }
        });
    }
    
    agentPhase_ = "initialized";
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
    if (webCfg.enabled) LOG_INFO("  Web Blocking: rp-webblock.exe (subprocess)");
    if (appCfg.enabled) LOG_INFO("  Software Blocking: rp-softblock.exe (subprocess)");
    const auto& patchCfg2 = config.getPatchConfig();
    if (patchCfg2.enabled) LOG_INFO("  Patch Management: rp-patch.exe (on-demand subprocess)");
    
    writeStatusFile();  // Write enriched status after full init
    return true;
}

bool Agent::performRegistration() {
    LOG_INFO("Checking agent registration status...");
    
    auto& config = ConfigManager::instance();
    
    // Resolve certsDir to ProgramData directory (fix: avoid System32/Program Files when running as service)
    std::string certsDir = config.getManagerConfig().certs_dir;
    {
        // Always resolve relative certsDir relative to ProgramData
        std::filesystem::path certsPath(certsDir);
        if (certsPath.is_relative()) {
            certsPath = PathUtils::getAgentDataDir() / certsPath;
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
    agentPhase_ = "starting";
    
    // Start management thread if registered
    if (managementSender_) {
        agentPhase_ = "connecting";
        writeStatusFile();
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

    // Start download-scan debounce thread if enabled
    {
        const auto& dlCfg2 = ConfigManager::instance().getDownloadScanConfig();
        if (dlCfg2.enabled && ConfigManager::instance().getAntivirusConfig().enabled) {
            avScanDebounceStop_ = false;
            int debounceSeconds = dlCfg2.debounce_seconds;
            avScanDebounceThread_ = std::thread(&Agent::avScanDebounceLoop, this, debounceSeconds);
            LOG_INFO("Download scan watcher started (debounce={}s, motw_only={})",
                     debounceSeconds, dlCfg2.motw_only);
        }
    }
    
    // Workers are already running as subprocesses (spawned during initialize())
    LOG_INFO("Security worker subprocesses active");

    // Start log tailer if sources were configured
    if (logTailer_) {
        logTailer_->start();
        LOG_INFO("Log tailer started");
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
            event.sourceType = "winevent";
            
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

    // Start scheduled antivirus scan thread if enabled
    {
        const auto& avCfg = ConfigManager::instance().getAntivirusConfig();
        if (avCfg.enabled && avCfg.auto_scan) {
            avScanInterval_  = std::chrono::hours(avCfg.scan_interval_hours);
            avUpdateInterval_= std::chrono::hours(avCfg.update_interval_hours);
            avScanThread_ = std::thread(&Agent::avScanLoop, this);
            LOG_INFO("AV scheduled scan started (interval={}h, updateDefs={})",
                     avCfg.scan_interval_hours, avCfg.auto_update_definitions);
        }
    }
    
    agentPhase_ = licenseSuspended_ ? "suspended" : "operational";
    writeStatusFile();
    LOG_INFO("Agent running. Press Ctrl+C to stop (console mode).");
    
    // Main loop - just wait for stop signal
    auto lastStatusWrite = std::chrono::steady_clock::now();
    auto lastStatusLog   = std::chrono::steady_clock::now();
    while (!shouldStop()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        // Check if ModuleController received an 'agent_restart' verb
        if (moduleController_ && moduleController_->restartRequested()) {
            LOG_WARN("Agent restart requested by Manager MODULE_COMMAND");
            moduleController_->clearRestartRequest();
            restartRequested_ = true;
            break;
        }
        
        auto now = std::chrono::steady_clock::now();
        
        // Periodic status file update (every 5 seconds for GUI)
        if (std::chrono::duration_cast<std::chrono::seconds>(now - lastStatusWrite).count() >= 5) {
            agentPhase_ = licenseSuspended_ ? "suspended" : "operational";
            writeStatusFile();
            lastStatusWrite = now;
        }
        
        // Periodic status log (every 60 seconds)
        if (std::chrono::duration_cast<std::chrono::seconds>(now - lastStatusLog).count() >= 60) {
            LOG_INFO("Status: Collected={}, Filtered={}, Sent={}, Queued={}, Buffered={}",
                     collector_->getEventsCollected(),
                     collector_->getEventsFiltered(),
                     batchSender_->getEventsSent(),
                     queue_->size(),
                     buffer_->getEventCount());
            lastStatusLog = now;
        }
    }
    
    agentPhase_ = "stopping";
    writeStatusFile();
    LOG_INFO("Stopping agent...");

    // Kill worker subprocesses first so long-running on-demand jobs do not
    // keep management or scheduled scan threads blocked during shutdown.
    if (workerManager_) {
        workerManager_->stopAll();
        LOG_INFO("Worker subprocesses stopped");
    }
    
    // Stop management thread if running
    if (managementThread_.joinable()) {
        managementThread_.join();
    }
    

    
    // Stop system info thread if running
    if (sysInfoThread_.joinable()) {
        sysInfoThread_.join();
    }

    // Stop AV scan thread if running
    if (avScanThread_.joinable()) {
        avScanThread_.join();
    }
    
    // Stop components in order
    if (logTailer_) {
        logTailer_->stop();
    }
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

    // Stop USB monitor
    if (usbMonitor_) {
        usbMonitor_->stop();
    }

    // Stop download-scan debounce thread
    avScanDebounceStop_ = true;
    if (avScanDebounceThread_.joinable()) {
        avScanDebounceThread_.join();
    }
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

void Agent::avScanDebounceLoop(int debounceSeconds) {
    LOG_DEBUG("AV scan debounce loop started ({}s)", debounceSeconds);

    while (!avScanDebounceStop_.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(debounceSeconds));

        if (avScanDebounceStop_.load()) break;

        std::vector<std::string> filesToScan;
        {
            std::lock_guard<std::mutex> lock(avScanQueueMutex_);
            filesToScan.swap(avScanQueue_);
        }

        if (filesToScan.empty()) continue;

        const auto& dlCfg = ConfigManager::instance().getDownloadScanConfig();
        LOG_INFO("AV debounce fired: checking {} file(s)", filesToScan.size());

        // Deduplicate paths
        std::sort(filesToScan.begin(), filesToScan.end());
        filesToScan.erase(std::unique(filesToScan.begin(), filesToScan.end()),
                          filesToScan.end());

        // Scan each file individually via rp-antivirus.exe
        for (const auto& filePath : filesToScan) {
            if (avScanDebounceStop_.load()) break;

            // MotW check here — Zone.Identifier is now written by browser after 5s debounce
            if (dlCfg.motw_only) {
                auto motw = checkMotw(filePath);
                if (!motw.hasMotw) {
                    LOG_INFO("Skipping (no MotW): {}", filePath);
                    continue;
                }
                LOG_INFO("MotW confirmed (Zone {}): {}", motw.zoneId, filePath);
            }

            LOG_INFO("Scanning downloaded file: {}", filePath);
            nlohmann::json policy;
            policy["action"] = "quick_scan";
            policy["path"]   = filePath;
            if (policyManager_) {
                policyManager_->handlePolicyUpdate("antivirus", policy);
            }
        }
    }

    LOG_DEBUG("AV scan debounce loop stopped");
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
            event.sourceType = "winevent";
            
            queue_->push(std::move(event));
            LOG_INFO("System information collected and queued");
        } catch (const std::exception& e) {
            LOG_ERROR("Error during periodic system info collection: {}", e.what());
        }
    }
    
    LOG_DEBUG("System info collection thread stopped");
}

void Agent::avScanLoop() {
    auto& cfg = ConfigManager::instance();
    const auto& avCfg = cfg.getAntivirusConfig();

    LOG_INFO("AV scheduled scan thread started (interval={}h, paths={})",
             avScanInterval_.count(), avCfg.scan_paths.size());

    // Update definitions once at startup if configured
    if (avCfg.auto_update_definitions && policyManager_) {
        LOG_INFO("AV: running initial definition update...");
        policyManager_->handlePolicyUpdate("antivirus",
            nlohmann::json{ {"action", "update_definitions"} });
    }

    auto lastDefUpdate = std::chrono::steady_clock::now();

    while (!shouldStop()) {
        // ── Wait for scan interval, wake every second to check stop ──
        auto scanDue = std::chrono::steady_clock::now() + avScanInterval_;
        while (std::chrono::steady_clock::now() < scanDue && !shouldStop()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        if (shouldStop()) break;

        // ── Optional: refresh definitions before scan ──
        if (avCfg.auto_update_definitions) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::hours>(now - lastDefUpdate);
            if (elapsed >= avUpdateInterval_) {
                LOG_INFO("AV: updating virus definitions (scheduled)");
                if (policyManager_) {
                    policyManager_->handlePolicyUpdate("antivirus",
                        nlohmann::json{ {"action", "update_definitions"} });
                }
                lastDefUpdate = now;
            }
        }

        // ── Run a scan for each configured path ──
        for (const auto& path : avCfg.scan_paths) {
            if (shouldStop()) break;
            LOG_INFO("AV: starting scheduled quick_scan of '{}'", path);
            if (policyManager_) {
                policyManager_->handlePolicyUpdate("antivirus",
                    nlohmann::json{ {"action", "quick_scan"}, {"path", path} });
            }
        }
    }

    LOG_DEBUG("AV scheduled scan thread stopped");
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

                // Flush any queued STATUS_REPORTs (av_scan, patch_scan, module_status).
                // These are enqueued by background threads; we send them here on the
                // SSL-owning thread to avoid concurrent SSL write races.
                auto* tls = dynamic_cast<TlsSender*>(managementSender_.get());
                if (tls) {
                    int flushed = tls->flushPendingStatusReports();
                    if (flushed > 0) {
                        LOG_DEBUG("Flushed {} pending status report(s)", flushed);
                    }
                }

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
        
        // ── Check for inbound POLICY_UPDATE or MODULE_COMMAND from Manager ──
        // tryReadInbound now uses select() to check both SSL buffer and TCP socket,
        // so commands pushed by the Manager are discovered instantly.
        if (managementSender_) {
            auto* tlsSender = dynamic_cast<TlsSender*>(managementSender_.get());
            if (tlsSender && tlsSender->isConnected()) {
                nlohmann::json cmd;
                while (tlsSender->tryReadInbound(cmd)) {
                    std::string msgType = cmd.value("_msgType", "policy_update");

                    if (msgType == "module_command" && moduleController_) {
                        // ── MODULE_COMMAND: delegate to ModuleController ──
                        try {
                            ModuleCommand mc = cmd.get<ModuleCommand>();
                            LOG_INFO("MODULE_COMMAND received: verb={} commandId={}",
                                     mc.verb, mc.commandId);
                            auto result = moduleController_->execute(mc);
                            tlsSender->sendModuleCommandResult(agentId, result);
                        } catch (const std::exception& e) {
                            LOG_ERROR("Failed to parse MODULE_COMMAND: {}", e.what());
                        }

                    } else {
                        // ── POLICY_UPDATE: existing path ──
                        std::string policyType = cmd.value("policyType", "");
                        std::string commandId  = cmd.value("commandId", "");
                        if (!policyType.empty() && policyManager_) {
                            LOG_INFO("POLICY_UPDATE received: type={} commandId={}", policyType, commandId);

                            // ACK immediately so the Manager knows we received it.
                            // Long-running policies (patch, antivirus) execute
                            // asynchronously; their result arrives via STATUS_REPORT.
                            tlsSender->sendPolicyAck(
                                agentId, commandId, policyType, true,
                                "Policy received, executing");

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
        }
        
        // Sleep on the mTLS socket via select(): zero CPU while idle,
        // instant wake when the Manager pushes a command.
        // Falls back to a plain 1s sleep when not connected (no socket to wait on).
        {
            auto* tls = dynamic_cast<TlsSender*>(managementSender_.get());
            if (tls && tls->isConnected()) {
                tls->waitForDataOrTimeout(1000);
            } else {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }
    }
    
    LOG_DEBUG("Management loop background thread stopped");
}

void Agent::writeStatusFile() {
    try {
        // Resolve path in ProgramData
        std::filesystem::path statusPath = PathUtils::getAgentDataDir() / "status.json";
        std::filesystem::create_directories(statusPath.parent_path());

        nlohmann::json status;
        status["status"] = licenseSuspended_ ? "suspended" : (running_.load() ? "running" : "stopped");
        status["phase"] = agentPhase_;
        status["lastError"] = lastError_;

        // ── Connection info ──
        {
            nlohmann::json conn;
            if (managementSender_) {
                conn["mTLS"] = managementSender_->isConnected() ? "connected" : "disconnected";
            } else {
                conn["mTLS"] = "not_configured";
            }
            conn["telemetry"] = telemetrySender_ ? (telemetrySender_->isConnected() ? "connected" : "disconnected") : "not_configured";

            // Read manager host:port from cached config
            if (useRegistration_ && !configPath_.empty()) {
                try {
                    std::ifstream cfgFile(configPath_);
                    if (cfgFile) {
                        nlohmann::json cfgJson;
                        cfgFile >> cfgJson;
                        if (cfgJson.contains("manager")) {
                            auto& mgr = cfgJson["manager"];
                            conn["managerHost"] = mgr.value("host", "localhost") + ":" + std::to_string(mgr.value("port", 1514));
                        }
                    }
                } catch (...) {}
            }
            status["connection"] = conn;
        }

        // ── Component statuses ──
        {
            nlohmann::json components;

            // Event Collector
            if (collector_) {
                nlohmann::json ec;
                ec["status"] = "running";
                ec["eventsCollected"] = collector_->getEventsCollected();
                ec["eventsFiltered"] = collector_->getEventsFiltered();
                components["eventCollector"] = ec;
            }

            // Batch Sender
            if (batchSender_) {
                nlohmann::json bs;
                bs["status"] = batchSender_->isRunning() ? "running" : "stopped";
                bs["eventsSent"] = batchSender_->getEventsSent();
                bs["batchesSent"] = batchSender_->getBatchesSent();
                bs["eventsBuffered"] = buffer_ ? buffer_->getEventCount() : 0;
                components["batchSender"] = bs;
            }

            // FIM
            if (fimMonitor_) {
                nlohmann::json fim;
                fim["status"] = "running";
                components["fim"] = fim;
            }

            // System Info
            if (sysInfoCollector_) {
                nlohmann::json si;
                si["status"] = "idle";
                si["intervalHours"] = sysInfoInterval_.count();
                components["sysInfo"] = si;
            }

            status["components"] = components;
        }

        // ── Worker subprocess statuses ──
        {
            nlohmann::json workers;
            auto addWorker = [&](const std::string& name) {
                nlohmann::json w;
                if (workerManager_ && workerManager_->isRunning(name)) {
                    w["status"] = "running";
                } else {
                    w["status"] = "idle";
                }
                workers[name] = w;
            };

            auto& config = ConfigManager::instance();
            if (config.getWebBlockConfig().enabled)  addWorker("rp-webblock");
            if (config.getAppBlockConfig().enabled)  addWorker("rp-softblock");
            if (config.getPatchConfig().enabled)      addWorker("rp-patch");
            // antivirus is always shown as on-demand if workers exist
            addWorker("rp-antivirus");

            status["workers"] = workers;
        }

        // ── License info (structured) ──
        {
            nlohmann::json lic;
            lic["suspended"] = licenseSuspended_;
            lic["message"] = licenseMessage_;
            lic["type"] = licenseType_;
            lic["expiry"] = licenseExpiry_;
            status["license"] = lic;
        }

        // ── Cert info ──
        if (certStore_) {
            nlohmann::json cert;
            int daysLeft = certStore_->daysUntilExpiry();
            cert["daysLeft"] = daysLeft;
            cert["renewable"] = (daysLeft > 0 && daysLeft <= 30);
            status["cert"] = cert;
        }

        // ── Legacy fields (backward compat with existing GUI) ──
        status["licenseSuspended"] = licenseSuspended_;
        status["licenseMessage"] = licenseMessage_;
        status["licenseType"] = licenseType_;
        status["licenseExpiry"] = licenseExpiry_;
        status["eventsCollected"] = collector_ ? collector_->getEventsCollected() : 0;
        status["eventsSent"] = batchSender_ ? batchSender_->getEventsSent() : 0;
        if (certStore_) {
            status["certDaysLeft"] = certStore_->daysUntilExpiry();
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



std::string Agent::resolveConfigDir() const {
    std::filesystem::path configDir = PathUtils::getAgentDataDir() / "config";
    std::filesystem::create_directories(configDir);
    return configDir.string();
}

} // namespace ResolutePulse

