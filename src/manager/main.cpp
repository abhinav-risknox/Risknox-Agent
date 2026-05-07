#include "manager/ca/CertificateAuthority.h"
#include "manager/db/PostgresClient.h"
#include "manager/server/ManagerServer.h"
#include "manager/registry/AgentRegistry.h"
#include "manager/registry/LicenseManager.h"
#include "manager/api/RestApi.h"
#include "utils/Logger.h"

#include <iostream>
#include <csignal>
#include <string>
#include <cstdlib>

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
    // Initialize logger
    Logger::initialize("info");

    LOG_INFO("===========================================");
    LOG_INFO("ResolutePulse Manager Server");
    LOG_INFO("===========================================");

    // Configuration defaults
    std::string dbConnString = "host=127.0.0.1 port=5432 dbname=risknox user=postgres password=abhi1243";

    // Append password from environment variable to avoid hardcoding
    const char* envDbPass = std::getenv("DB_PASSWORD");
    if (envDbPass) {
        dbConnString += " password=";
        dbConnString += envDbPass;
    }
    
    std::string caDir = "ca";
    int port = 1514;
    int apiPort = 8080;

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if ((arg == "--db" || arg == "-d") && i + 1 < argc) {
            dbConnString = argv[++i];
            // If the caller didn't include a password, still append from env
            if (dbConnString.find("password") == std::string::npos && envDbPass) {
                dbConnString += " password=";
                dbConnString += envDbPass;
            }
        } else if ((arg == "--ca-dir") && i + 1 < argc) {
            caDir = argv[++i];
        } else if ((arg == "--port" || arg == "-p") && i + 1 < argc) {
            port = std::stoi(argv[++i]);
        } else if ((arg == "--api-port") && i + 1 < argc) {
            apiPort = std::stoi(argv[++i]);
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "ResolutePulse Manager Server" << std::endl;
            std::cout << "Usage: " << argv[0] << " [options]" << std::endl;
            std::cout << "Options:" << std::endl;
            std::cout << "  --db, -d CONN      PostgreSQL connection string" << std::endl;
            std::cout << "                     (DB_PASSWORD env var appended if password= absent)" << std::endl;
            std::cout << "  --ca-dir DIR       CA directory (default: ca)" << std::endl;
            std::cout << "  --port, -p PORT    mTLS listen port (default: 1514)" << std::endl;
            std::cout << "  --api-port PORT    REST API port (default: 8080)" << std::endl;
            std::cout << "  --help, -h         Show this help" << std::endl;
            return 0;
        }
    }

    // Environment variable overrides
    const char* envDb = std::getenv("RPLS_DB_CONN");
    if (envDb) dbConnString = envDb;

    const char* envPort = std::getenv("RPLS_PORT");
    if (envPort) port = std::stoi(envPort);

    const char* envApiPort = std::getenv("RPLS_API_PORT");
    if (envApiPort) apiPort = std::stoi(envApiPort);

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

    // ─── Start REST API ───
    LOG_INFO("Starting REST API on port {}...", apiPort);
    RestApi restApi(db, server);
    g_restApi = &restApi;

    if (!restApi.start(apiPort)) {
        LOG_CRITICAL("Failed to start REST API");
        return 1;
    }

    LOG_INFO("Manager Server running.");
    LOG_INFO("  mTLS Agent port: {}", port);
    LOG_INFO("  REST API port:   {}", apiPort);
    LOG_INFO("  Command ingest:  127.0.0.1:1515");
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

