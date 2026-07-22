#include "manager/ca/CertificateAuthority.h"
#include "manager/db/PostgresClient.h"
#include "manager/server/ManagerServer.h"
#include "manager/registry/AgentRegistry.h"
#include "manager/registry/LicenseManager.h"
#include "manager/api/RestApi.h"
#include "manager/config/ManagerConfig.h"
#include "utils/Logger.h"

#include <iostream>
#include <csignal>
#include <string>
#include <cstdlib>
#include <thread>
#include <chrono>

using namespace ResolutePulse;

static ManagerServer* g_server = nullptr;
static RestApi* g_restApi = nullptr;

void signalHandler(int signum) {
    std::cout << "\nShutdown signal received..." << std::endl;
    if (g_restApi) {
        g_restApi->stop();
    }
    if (g_server) {
        g_server->stop();
    }
}

int main(int argc, char* argv[]) {
    // Load configuration (defaults -> JSON config file -> env vars -> CLI flags)
    ManagerConfig configMgr;
    if (!configMgr.load(argc, argv)) {
        // Returned false (e.g. --help was displayed or invalid args)
        return 0;
    }

    const auto& config = configMgr.get();

    // Initialize logger
    Logger::initialize(config.logLevel);

    LOG_INFO("===========================================");
    LOG_INFO("ResolutePulse Manager Server");
    LOG_INFO("===========================================");

    // Set up signal handlers
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    // ─── Initialize Certificate Authority ───
    LOG_INFO("Initializing Certificate Authority...");
    CertificateAuthority ca;
    if (!ca.initializeCA(config.caDir)) {
        LOG_CRITICAL("Failed to initialize Certificate Authority");
        return 1;
    }

    // ─── Connect to PostgreSQL ───
    LOG_INFO("Connecting to PostgreSQL...");
    PostgresClient db;
    if (!db.connect(config.dbConnString)) {
        LOG_CRITICAL("Failed to connect to PostgreSQL");
        return 1;
    }

    // ─── Initialize components ───
    AgentRegistry registry(db);
    LicenseManager licenseManager(db);

    // ─── Start Manager Server ───
    LOG_INFO("Starting Manager Server on agent port {} (command port {})...",
             config.ports.agentPort, config.ports.commandPort);
    ManagerServer server;
    g_server = &server;

    if (!server.initialize(config.ports.agentPort, ca, db, config.ports.commandPort)) {
        LOG_CRITICAL("Failed to initialize Manager Server");
        return 1;
    }

    if (!server.start()) {
        LOG_CRITICAL("Failed to start Manager Server");
        return 1;
    }

    // ─── Start REST API ───
    LOG_INFO("Starting REST API on port {}...", config.ports.apiPort);
    RestApi restApi(db, server);
    g_restApi = &restApi;

    if (!restApi.start(config.ports.apiPort)) {
        LOG_CRITICAL("Failed to start REST API");
        return 1;
    }

    LOG_INFO("Manager Server running.");
    LOG_INFO("  mTLS Agent port: {}", config.ports.agentPort);
    LOG_INFO("  REST API port:   {}", config.ports.apiPort);
    LOG_INFO("  Command ingest:  127.0.0.1:{}", config.ports.commandPort);
    LOG_INFO("Press Ctrl+C to stop.");

    // Wait for server to stop (signal handler will call stop())
    while (server.isRunning()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    // Cleanup
    g_restApi = nullptr;
    g_server = nullptr;
    restApi.stop();
    db.disconnect();

    LOG_INFO("Manager Server shut down cleanly");
    return 0;
}
