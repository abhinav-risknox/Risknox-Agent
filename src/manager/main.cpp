#include "manager/ca/CertificateAuthority.h"
#include "manager/db/PostgresClient.h"
#include "manager/server/ManagerServer.h"
#include "manager/registry/AgentRegistry.h"
#include "manager/registry/LicenseManager.h"
#include "utils/Logger.h"

#include <iostream>
#include <csignal>
#include <string>
#include <cstdlib>

using namespace ResolutePulse;

static ManagerServer* g_server = nullptr;

void signalHandler(int signum) {
    std::cout << "\nShutdown signal received..." << std::endl;
    if (g_server) {
        g_server->stop();
    }
}

int main(int argc, char* argv[]) {
    // Initialize logger
    Logger::initialize("info");

    LOG_INFO("===========================================");
    LOG_INFO("ResolutePulse Manager Server");
    LOG_INFO("===========================================");

    // Configuration defaults
    std::string dbConnString = "host=127.0.0.1 port=5433 dbname=risknox user=postgres";

    // Append password from environment variable to avoid hardcoding
    const char* envDbPass = std::getenv("DB_PASSWORD");
    if (envDbPass) {
        dbConnString += " password=";
        dbConnString += envDbPass;
    }
    
    std::string caDir = "ca";
    int port = 1514;

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if ((arg == "--db" || arg == "-d") && i + 1 < argc) {
            dbConnString = argv[++i];
        } else if ((arg == "--ca-dir") && i + 1 < argc) {
            caDir = argv[++i];
        } else if ((arg == "--port" || arg == "-p") && i + 1 < argc) {
            port = std::stoi(argv[++i]);
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "ResolutePulse Manager Server" << std::endl;
            std::cout << "Usage: " << argv[0] << " [options]" << std::endl;
            std::cout << "Options:" << std::endl;
            std::cout << "  --db, -d CONN      PostgreSQL connection string" << std::endl;
            std::cout << "  --ca-dir DIR       CA directory (default: ca)" << std::endl;
            std::cout << "  --port, -p PORT    Listen port (default: 1514)" << std::endl;
            std::cout << "  --help, -h         Show this help" << std::endl;
            return 0;
        }
    }

    // Environment variable overrides
    const char* envDb = std::getenv("RPLS_DB_CONN");
    if (envDb) dbConnString = envDb;

    const char* envPort = std::getenv("RPLS_PORT");
    if (envPort) port = std::stoi(envPort);

    // Set up signal handlers
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    // ─── Initialize Certificate Authority ───
    LOG_INFO("Initializing Certificate Authority...");
    CertificateAuthority ca;
    if (!ca.initializeCA(caDir)) {
        LOG_CRITICAL("Failed to initialize Certificate Authority");
        return 1;
    }

    // ─── Connect to PostgreSQL ───
    LOG_INFO("Connecting to PostgreSQL...");
    PostgresClient db;
    if (!db.connect(dbConnString)) {
        LOG_CRITICAL("Failed to connect to PostgreSQL");
        return 1;
    }

    // ─── Initialize components ───
    AgentRegistry registry(db);
    LicenseManager licenseManager(db);

    // ─── Start Manager Server ───
    LOG_INFO("Starting Manager Server on port {}...", port);
    ManagerServer server;
    g_server = &server;

    if (!server.initialize(port, ca, db)) {
        LOG_CRITICAL("Failed to initialize Manager Server");
        return 1;
    }

    if (!server.start()) {
        LOG_CRITICAL("Failed to start Manager Server");
        return 1;
    }

    LOG_INFO("Manager Server running. Press Ctrl+C to stop.");

    // Wait for server to stop (signal handler will call stop())
    while (server.isRunning()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    // Cleanup
    g_server = nullptr;
    db.disconnect();

    LOG_INFO("Manager Server shut down cleanly");
    return 0;
}
