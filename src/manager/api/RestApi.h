#pragma once

// RestApi.h
// Lightweight HTTP REST API for the Manager dashboard.
// Runs on a configurable port (default 8080) and provides endpoints
// for agent listing, module/policy command dispatch, command history,
// audit logging, and health status.
//
// All endpoints return JSON.  CORS headers are added automatically.
// Authentication is via Bearer token (see /api/auth/login).

#include "manager/db/PostgresClient.h"
#include "manager/server/ManagerServer.h"
#include "utils/Logger.h"

#include <nlohmann/json.hpp>
#define CPPHTTPLIB_OPENSSL_SUPPORT
#include <httplib/httplib.h>
#include <string>
#include <thread>
#include <atomic>
#include <functional>
#include <unordered_map>
#include <mutex>
#include <chrono>

namespace ResolutePulse {

class RestApi {
public:
    RestApi(PostgresClient& db, ManagerServer& server);
    ~RestApi();

    // Start the HTTP server on the given port (blocks in a background thread)
    bool start(int port = 8080);

    // Stop the HTTP server
    void stop();

    bool isRunning() const { return running_.load(); }

private:
    // ── Route registration ──
    void registerRoutes();

    // ── Middleware ──
    void addCorsHeaders(httplib::Response& res);
    bool authenticate(const httplib::Request& req, httplib::Response& res);

    // ── Auth endpoints ──
    void handleLogin(const httplib::Request& req, httplib::Response& res);

    // ── Agent endpoints ──
    void handleGetAgents(const httplib::Request& req, httplib::Response& res);
    void handleGetAgent(const httplib::Request& req, httplib::Response& res);
    void handleGetAgentStatus(const httplib::Request& req, httplib::Response& res);

    // ── Module command endpoints ──
    void handlePostModuleCommand(const httplib::Request& req, httplib::Response& res);
    void handleGetModuleCommands(const httplib::Request& req, httplib::Response& res);

    // ── Policy command endpoints ──
    void handlePostPolicyCommand(const httplib::Request& req, httplib::Response& res);
    void handleGetPolicyCommands(const httplib::Request& req, httplib::Response& res);

    // ── Unified command lookup ──
    void handleGetCommandById(const httplib::Request& req, httplib::Response& res);

    // ── Audit log ──
    void handleGetAuditLog(const httplib::Request& req, httplib::Response& res);

    // ── Health ──
    void handleHealthCheck(const httplib::Request& req, httplib::Response& res);

    // ── Internals ──
    PostgresClient&  db_;
    ManagerServer&   server_;

    std::unique_ptr<httplib::Server> httpServer_;
    std::thread                      httpThread_;
    std::atomic<bool>                running_{false};
    int                              port_ = 8080;

    // Simple token store  { token -> { username, expires_at } }
    struct TokenInfo {
        std::string username;
        std::chrono::system_clock::time_point expiresAt;
    };
    std::unordered_map<std::string, TokenInfo> tokens_;
    std::mutex                                  tokenMutex_;

    // Generate a random hex token
    static std::string generateToken();
};

} // namespace ResolutePulse
