#include "RestApi.h"

#include <nlohmann/json.hpp>
#include <random>
#include <sstream>
#include <iomanip>
#include <ctime>

namespace ResolutePulse {

// ─────────────────────────────────────────────────────────────────────────────
// Construction / Destruction
// ─────────────────────────────────────────────────────────────────────────────

RestApi::RestApi(PostgresClient& db, ManagerServer& server)
    : db_(db), server_(server) {}

RestApi::~RestApi() { stop(); }

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

bool RestApi::start(int port) {
    if (running_.load()) return true;

    port_ = port;
    httpServer_ = std::make_unique<httplib::Server>();

    registerRoutes();

    running_ = true;
    httpThread_ = std::thread([this]() {
        LOG_INFO("REST API listening on 0.0.0.0:{}", port_);
        httpServer_->listen("0.0.0.0", port_);
        running_ = false;
        LOG_INFO("REST API stopped");
    });

    return true;
}

void RestApi::stop() {
    if (httpServer_) httpServer_->stop();
    if (httpThread_.joinable()) httpThread_.join();
    running_ = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Route Registration
// ─────────────────────────────────────────────────────────────────────────────

void RestApi::registerRoutes() {
    using Req = httplib::Request;
    using Res = httplib::Response;

    // ── CORS preflight handler ──
    httpServer_->Options(".*", [this](const Req&, Res& res) {
        addCorsHeaders(res);
        res.status = 204;
    });

    // ── Auth ──
    httpServer_->Post("/api/auth/login", [this](const Req& req, Res& res) {
        addCorsHeaders(res);
        handleLogin(req, res);
    });

    // ── Agents ──
    httpServer_->Get("/api/agents", [this](const Req& req, Res& res) {
        addCorsHeaders(res);
        if (!authenticate(req, res)) return;
        handleGetAgents(req, res);
    });

    httpServer_->Get(R"(/api/agents/([^/]+))", [this](const Req& req, Res& res) {
        addCorsHeaders(res);
        if (!authenticate(req, res)) return;
        handleGetAgent(req, res);
    });

    httpServer_->Get(R"(/api/agents/([^/]+)/status)", [this](const Req& req, Res& res) {
        addCorsHeaders(res);
        if (!authenticate(req, res)) return;
        handleGetAgentStatus(req, res);
    });

    // ── Module Commands ──
    httpServer_->Post(R"(/api/agents/([^/]+)/module-command)", [this](const Req& req, Res& res) {
        addCorsHeaders(res);
        if (!authenticate(req, res)) return;
        handlePostModuleCommand(req, res);
    });

    httpServer_->Get("/api/commands/module", [this](const Req& req, Res& res) {
        addCorsHeaders(res);
        if (!authenticate(req, res)) return;
        handleGetModuleCommands(req, res);
    });

    // ── Policy Commands ──
    httpServer_->Post(R"(/api/agents/([^/]+)/policy)", [this](const Req& req, Res& res) {
        addCorsHeaders(res);
        if (!authenticate(req, res)) return;
        handlePostPolicyCommand(req, res);
    });

    httpServer_->Get("/api/commands/policy", [this](const Req& req, Res& res) {
        addCorsHeaders(res);
        if (!authenticate(req, res)) return;
        handleGetPolicyCommands(req, res);
    });

    // ── Unified command lookup ──
    httpServer_->Get(R"(/api/commands/([^/]+))", [this](const Req& req, Res& res) {
        addCorsHeaders(res);
        if (!authenticate(req, res)) return;
        handleGetCommandById(req, res);
    });

    // ── Audit log ──
    httpServer_->Get("/api/audit-log", [this](const Req& req, Res& res) {
        addCorsHeaders(res);
        if (!authenticate(req, res)) return;
        handleGetAuditLog(req, res);
    });

    // ── Health check (no auth required) ──
    httpServer_->Get("/api/health", [this](const Req& req, Res& res) {
        addCorsHeaders(res);
        handleHealthCheck(req, res);
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// CORS
// ─────────────────────────────────────────────────────────────────────────────

void RestApi::addCorsHeaders(httplib::Response& res) {
    res.set_header("Access-Control-Allow-Origin",  "*");
    res.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
    res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
}

// ─────────────────────────────────────────────────────────────────────────────
// Authentication
// ─────────────────────────────────────────────────────────────────────────────

bool RestApi::authenticate(const httplib::Request& req, httplib::Response& res) {
    auto it = req.headers.find("Authorization");
    if (it == req.headers.end()) {
        res.status = 401;
        res.set_content(R"({"error":"Missing Authorization header"})", "application/json");
        return false;
    }

    std::string header = it->second;
    if (header.substr(0, 7) != "Bearer ") {
        res.status = 401;
        res.set_content(R"({"error":"Invalid Authorization format, expected: Bearer <token>"})", "application/json");
        return false;
    }

    std::string token = header.substr(7);

    std::lock_guard<std::mutex> lk(tokenMutex_);
    auto tit = tokens_.find(token);
    if (tit == tokens_.end()) {
        res.status = 401;
        res.set_content(R"({"error":"Invalid token"})", "application/json");
        return false;
    }
    if (std::chrono::system_clock::now() > tit->second.expiresAt) {
        tokens_.erase(tit);
        res.status = 401;
        res.set_content(R"({"error":"Token expired"})", "application/json");
        return false;
    }

    return true;
}

void RestApi::handleLogin(const httplib::Request& req, httplib::Response& res) {
    try {
        auto body = nlohmann::json::parse(req.body);
        std::string username = body.value("username", "");
        std::string password = body.value("password", "");

        // Validate credentials against the operators table in the DB.
        // Falls back to a hardcoded admin account if no operators table
        // exists yet (bootstrap scenario).
        bool valid = db_.authenticateOperator(username, password);

        if (!valid) {
            res.status = 401;
            res.set_content(R"({"error":"Invalid credentials"})", "application/json");
            return;
        }

        std::string token = generateToken();
        {
            std::lock_guard<std::mutex> lk(tokenMutex_);
            tokens_[token] = {
                username,
                std::chrono::system_clock::now() + std::chrono::hours(8)
            };
        }

        nlohmann::json resp;
        resp["token"]      = token;
        resp["username"]   = username;
        resp["expires_in"] = 8 * 3600; // 8 hours in seconds

        res.set_content(resp.dump(), "application/json");
        LOG_INFO("REST API: login successful for user '{}'", username);

    } catch (const std::exception& e) {
        res.status = 400;
        nlohmann::json err;
        err["error"] = std::string("Invalid request: ") + e.what();
        res.set_content(err.dump(), "application/json");
    }
}

std::string RestApi::generateToken() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> dist(0, 0xFFFFFFFF);

    std::ostringstream ss;
    for (int i = 0; i < 8; ++i) {
        ss << std::hex << std::setfill('0') << std::setw(8) << dist(gen);
    }
    return ss.str();
}

// ─────────────────────────────────────────────────────────────────────────────
// Agent Endpoints
// ─────────────────────────────────────────────────────────────────────────────

void RestApi::handleGetAgents(const httplib::Request&, httplib::Response& res) {
    auto agents = db_.listAgents();

    nlohmann::json arr = nlohmann::json::array();
    for (const auto& a : agents) {
        nlohmann::json j;
        j["agent_id"]       = a.agentId;
        j["hostname"]       = a.hostname;
        j["os_type"]        = a.osType;
        j["os_version"]     = a.osVersion;
        j["agent_version"]  = a.agentVersion;
        j["status"]         = a.status;
        j["ip_address"]     = a.ipAddress;
        j["registered_at"]  = a.registeredAt;
        j["last_seen_at"]   = a.lastSeenAt;
        j["online"]         = server_.isAgentOnline(a.agentId);
        arr.push_back(std::move(j));
    }

    nlohmann::json resp;
    resp["agents"] = arr;
    resp["count"]  = arr.size();
    res.set_content(resp.dump(), "application/json");
}

void RestApi::handleGetAgent(const httplib::Request& req, httplib::Response& res) {
    std::string agentId = req.matches[1];
    auto agent = db_.getAgent(agentId);

    if (!agent) {
        res.status = 404;
        res.set_content(R"({"error":"Agent not found"})", "application/json");
        return;
    }

    nlohmann::json j;
    j["agent_id"]       = agent->agentId;
    j["hostname"]       = agent->hostname;
    j["os_type"]        = agent->osType;
    j["os_version"]     = agent->osVersion;
    j["agent_version"]  = agent->agentVersion;
    j["status"]         = agent->status;
    j["cert_serial"]    = agent->certSerial;
    j["ip_address"]     = agent->ipAddress;
    j["registered_at"]  = agent->registeredAt;
    j["last_seen_at"]   = agent->lastSeenAt;
    j["online"]         = server_.isAgentOnline(agentId);

    // Include license info
    auto license = db_.getLicense(agentId);
    if (license) {
        nlohmann::json lic;
        lic["type"]        = license->licenseType;
        lic["valid_from"]  = license->validFrom;
        lic["valid_until"] = license->validUntil;
        j["license"] = lic;
    }

    res.set_content(j.dump(), "application/json");
}

void RestApi::handleGetAgentStatus(const httplib::Request& req, httplib::Response& res) {
    std::string agentId = req.matches[1];

    auto reports = db_.getLatestStatusReports(agentId, 20);

    nlohmann::json arr = nlohmann::json::array();
    for (const auto& r : reports) {
        nlohmann::json j;
        j["report_type"] = r.reportType;
        j["report_data"] = nlohmann::json::parse(r.reportData, nullptr, false);
        j["created_at"]  = r.createdAt;
        arr.push_back(std::move(j));
    }

    nlohmann::json resp;
    resp["agent_id"] = agentId;
    resp["online"]   = server_.isAgentOnline(agentId);
    resp["reports"]  = arr;
    res.set_content(resp.dump(), "application/json");
}

// ─────────────────────────────────────────────────────────────────────────────
// Module Command Endpoints
// ─────────────────────────────────────────────────────────────────────────────

void RestApi::handlePostModuleCommand(const httplib::Request& req, httplib::Response& res) {
    try {
        std::string agentId = req.matches[1];
        auto body = nlohmann::json::parse(req.body);

        std::string verb = body.at("verb").get<std::string>();
        auto params = body.value("params", nlohmann::json::object());

        // Generate a unique commandId
        std::string commandId = agentId + "-" + verb + "-" +
            std::to_string(time(nullptr)) + "-" +
            generateToken().substr(0, 8);

        // Resolve operator identity from token
        std::string operatorName = "unknown";
        {
            auto it = req.headers.find("Authorization");
            if (it != req.headers.end() && it->second.size() > 7) {
                std::string token = it->second.substr(7);
                std::lock_guard<std::mutex> lk(tokenMutex_);
                auto tit = tokens_.find(token);
                if (tit != tokens_.end()) operatorName = tit->second.username;
            }
        }

        // Record in DB with operator identity (write-ahead audit)
        db_.recordModuleCommandWithOperator(agentId, commandId, verb,
                                             params.dump(), operatorName, true);

        // Attempt immediate dispatch via the ManagerServer
        server_.dispatchModuleCommandFromApi(agentId, commandId, verb, params);

        nlohmann::json resp;
        resp["command_id"]   = commandId;
        resp["agent_id"]     = agentId;
        resp["verb"]         = verb;
        resp["status"]       = "queued";
        resp["initiated_by"] = operatorName;
        res.set_content(resp.dump(), "application/json");

        LOG_INFO("REST API: module command queued: verb={} agent={} by={}",
                 verb, agentId, operatorName);

    } catch (const std::exception& e) {
        res.status = 400;
        nlohmann::json err;
        err["error"] = std::string("Invalid request: ") + e.what();
        res.set_content(err.dump(), "application/json");
    }
}

void RestApi::handleGetModuleCommands(const httplib::Request& req, httplib::Response& res) {
    std::string agentId = req.get_param_value("agent_id");
    int limit  = 50;
    int offset = 0;
    try { limit  = std::stoi(req.get_param_value("limit")); } catch (...) {}
    try { offset = std::stoi(req.get_param_value("offset")); } catch (...) {}

    auto commands = db_.listModuleCommands(agentId, limit, offset);

    nlohmann::json arr = nlohmann::json::array();
    for (const auto& c : commands) {
        nlohmann::json j;
        j["id"]             = c.id;
        j["agent_id"]       = c.agentId;
        j["command_id"]     = c.commandId;
        j["verb"]           = c.verb;
        j["params"]         = nlohmann::json::parse(c.params, nullptr, false);
        j["status"]         = c.status;
        j["ack_status"]     = c.ackStatus;
        j["result_payload"] = c.resultPayload;
        j["created_at"]     = c.createdAt;
        arr.push_back(std::move(j));
    }

    nlohmann::json resp;
    resp["commands"] = arr;
    resp["count"]    = arr.size();
    res.set_content(resp.dump(), "application/json");
}

// ─────────────────────────────────────────────────────────────────────────────
// Policy Command Endpoints
// ─────────────────────────────────────────────────────────────────────────────

void RestApi::handlePostPolicyCommand(const httplib::Request& req, httplib::Response& res) {
    try {
        std::string agentId = req.matches[1];
        auto body = nlohmann::json::parse(req.body);

        std::string policyType = body.at("policy_type").get<std::string>();
        auto policyData = body.at("policy_data");

        // Resolve operator
        std::string operatorName = "unknown";
        {
            auto it = req.headers.find("Authorization");
            if (it != req.headers.end() && it->second.size() > 7) {
                std::string token = it->second.substr(7);
                std::lock_guard<std::mutex> lk(tokenMutex_);
                auto tit = tokens_.find(token);
                if (tit != tokens_.end()) operatorName = tit->second.username;
            }
        }

        // Dispatch via ManagerServer's existing dispatchCommand path
        // (which generates a commandId and records to DB)
        std::string commandId = server_.dispatchPolicyFromApi(agentId, policyType, policyData, operatorName);

        nlohmann::json resp;
        resp["command_id"]   = commandId;
        resp["agent_id"]     = agentId;
        resp["policy_type"]  = policyType;
        resp["status"]       = "queued";
        resp["initiated_by"] = operatorName;
        res.set_content(resp.dump(), "application/json");

        LOG_INFO("REST API: policy command queued: type={} agent={} by={}",
                 policyType, agentId, operatorName);

    } catch (const std::exception& e) {
        res.status = 400;
        nlohmann::json err;
        err["error"] = std::string("Invalid request: ") + e.what();
        res.set_content(err.dump(), "application/json");
    }
}

void RestApi::handleGetPolicyCommands(const httplib::Request& req, httplib::Response& res) {
    std::string agentId = req.get_param_value("agent_id");
    int limit  = 50;
    int offset = 0;
    try { limit  = std::stoi(req.get_param_value("limit")); } catch (...) {}
    try { offset = std::stoi(req.get_param_value("offset")); } catch (...) {}

    auto commands = db_.listPolicyCommands(agentId, limit, offset);

    nlohmann::json arr = nlohmann::json::array();
    for (const auto& c : commands) {
        nlohmann::json j;
        j["id"]           = c.id;
        j["agent_id"]     = c.agentId;
        j["command_id"]   = c.commandId;
        j["policy_type"]  = c.policyType;
        j["policy_data"]  = nlohmann::json::parse(c.policyData, nullptr, false);
        j["status"]       = c.status;
        j["created_at"]   = c.createdAt;
        arr.push_back(std::move(j));
    }

    nlohmann::json resp;
    resp["commands"] = arr;
    resp["count"]    = arr.size();
    res.set_content(resp.dump(), "application/json");
}

// ─────────────────────────────────────────────────────────────────────────────
// Unified Command Lookup
// ─────────────────────────────────────────────────────────────────────────────

void RestApi::handleGetCommandById(const httplib::Request& req, httplib::Response& res) {
    std::string commandId = req.matches[1];

    // Try module_commands first, then policy_commands
    auto mcmd = db_.getModuleCommandByCommandId(commandId);
    if (mcmd) {
        nlohmann::json j;
        j["type"]           = "module";
        j["id"]             = mcmd->id;
        j["agent_id"]       = mcmd->agentId;
        j["command_id"]     = mcmd->commandId;
        j["verb"]           = mcmd->verb;
        j["params"]         = nlohmann::json::parse(mcmd->params, nullptr, false);
        j["status"]         = mcmd->status;
        j["created_at"]     = mcmd->createdAt;
        res.set_content(j.dump(), "application/json");
        return;
    }

    auto pcmd = db_.getPolicyCommandByCommandId(commandId);
    if (pcmd) {
        nlohmann::json j;
        j["type"]           = "policy";
        j["id"]             = pcmd->id;
        j["agent_id"]       = pcmd->agentId;
        j["command_id"]     = pcmd->commandId;
        j["policy_type"]    = pcmd->policyType;
        j["policy_data"]    = nlohmann::json::parse(pcmd->policyData, nullptr, false);
        j["status"]         = pcmd->status;
        j["created_at"]     = pcmd->createdAt;
        res.set_content(j.dump(), "application/json");
        return;
    }

    res.status = 404;
    res.set_content(R"({"error":"Command not found"})", "application/json");
}

// ─────────────────────────────────────────────────────────────────────────────
// Audit Log
// ─────────────────────────────────────────────────────────────────────────────

void RestApi::handleGetAuditLog(const httplib::Request& req, httplib::Response& res) {
    std::string agentId = req.get_param_value("agent_id");
    int limit  = 100;
    int offset = 0;
    try { limit  = std::stoi(req.get_param_value("limit")); } catch (...) {}
    try { offset = std::stoi(req.get_param_value("offset")); } catch (...) {}

    auto entries = db_.getAuditLog(agentId, limit, offset);

    nlohmann::json resp;
    resp["entries"] = entries;
    resp["count"]   = entries.size();
    res.set_content(resp.dump(), "application/json");
}

// ─────────────────────────────────────────────────────────────────────────────
// Health Check
// ─────────────────────────────────────────────────────────────────────────────

void RestApi::handleHealthCheck(const httplib::Request&, httplib::Response& res) {
    nlohmann::json j;
    j["status"]     = "healthy";
    j["db_connected"] = db_.isConnected();
    j["server_running"] = server_.isRunning();

    time_t now = time(nullptr);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));
    j["timestamp"] = buf;

    res.set_content(j.dump(), "application/json");
}

} // namespace ResolutePulse
