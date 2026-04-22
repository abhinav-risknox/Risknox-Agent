// =============================================================================
// test_agent_registry.cpp - Unit tests for AgentRegistry (cache + DB layer)
//
// Uses mock libpq stubs - no database needed.
// =============================================================================

#include "manager/registry/AgentRegistry.h"
#include "manager/db/PostgresClient.h"
#include "utils/Logger.h"

#include <iostream>
#include <cassert>
#include <cstring>
#include <string>
#include <vector>

using namespace ResolutePulse;

// =============================================================================
// Mock libpq implementation
// =============================================================================

struct pg_conn {
    std::string errorMessage;
    int status;
};

struct pg_result {
    int status;
    std::vector<std::vector<std::string>> rows;
};

static pg_conn* g_mockConn = nullptr;
static int g_mockConnStatus = 0;
static std::string g_mockErrorMessage = "";

// Controllable mock result - set before each operation
static pg_result* g_mockResult = nullptr;
static std::string g_lastQuery = "";
static std::vector<std::string> g_lastParams = {};

// Track inserts for agentExists
static std::vector<std::string> g_insertedAgents;

// Mock result to return for getAgent DB fallback
static bool g_dbGetAgentShouldReturn = false;
static AgentRecord g_dbGetAgentRecord;

extern "C" {
    typedef enum {
        CONNECTION_OK, CONNECTION_BAD, CONNECTION_STARTED, CONNECTION_MADE,
        CONNECTION_AWAITING_RESPONSE, CONNECTION_AUTH_OK, CONNECTION_SETENV,
        CONNECTION_SSL_STARTUP, CONNECTION_NEEDED, CONNECTION_CHECK_WRITABLE,
        CONNECTION_CONSUME, CONNECTION_GSS_STARTUP
    } ConnStatusType;

    typedef enum {
        PGRES_EMPTY_QUERY = 0, PGRES_COMMAND_OK, PGRES_TUPLES_OK,
        PGRES_COPY_OUT, PGRES_COPY_IN, PGRES_BAD_RESPONSE,
        PGRES_NONFATAL_ERROR, PGRES_FATAL_ERROR, PGRES_COPY_BOTH,
        PGRES_SINGLE_TUPLE, PGRES_PIPELINE_SYNC, PGRES_PIPELINE_ABORTED
    } ExecStatusType;

    typedef struct pg_conn PGconn;
    typedef struct pg_result PGresult;

    PGconn* PQconnectdb(const char* conninfo) {
        g_mockConn = new PGconn();
        g_mockConn->status = g_mockConnStatus;
        g_mockConn->errorMessage = g_mockErrorMessage;
        return g_mockConn;
    }

    void PQfinish(PGconn* conn) {
        if (conn == g_mockConn) { delete g_mockConn; g_mockConn = nullptr; }
    }

    ConnStatusType PQstatus(const PGconn* conn) {
        if (!conn) return CONNECTION_BAD;
        return (ConnStatusType)conn->status;
    }

    char* PQerrorMessage(const PGconn* conn) {
        if (!conn) return (char*)"No connection";
        return (char*)conn->errorMessage.c_str();
    }

    PGresult* PQexec(PGconn* conn, const char* query) {
        g_lastQuery = query;
        static pg_result defaultResult;
        defaultResult.status = 1;
        defaultResult.rows.clear();
        return &defaultResult;
    }

    PGresult* PQexecParams(PGconn* conn, const char* command, int nParams,
                          const void* const * paramTypes, const char* const * paramValues,
                          const int* paramLengths, const int* paramFormats, int resultFormat) {
        g_lastQuery = command;
        g_lastParams.clear();
        for (int i = 0; i < nParams; ++i) {
            g_lastParams.push_back(paramValues[i] ? paramValues[i] : "NULL");
        }

        std::string query(command);

        // INSERT INTO agents
        if (query.find("INSERT INTO agents") != std::string::npos) {
            if (nParams > 0) g_insertedAgents.push_back(paramValues[0]);
            static pg_result insertResult;
            insertResult.status = 1; // PGRES_COMMAND_OK
            insertResult.rows.clear();
            return &insertResult;
        }

        // SELECT 1 FROM agents (agentExists)
        if (query.find("SELECT 1 FROM agents") != std::string::npos) {
            static pg_result existsResult;
            bool found = false;
            if (nParams > 0) {
                for (auto& id : g_insertedAgents) {
                    if (id == paramValues[0]) { found = true; break; }
                }
            }
            existsResult.status = 2; // PGRES_TUPLES_OK
            existsResult.rows = found ? std::vector<std::vector<std::string>>{{"1"}} : std::vector<std::vector<std::string>>{};
            return &existsResult;
        }

        // SELECT ... FROM agents WHERE agent_id = $1 (getAgent)
        if (query.find("FROM agents WHERE agent_id") != std::string::npos) {
            static pg_result getResult;
            if (g_dbGetAgentShouldReturn) {
                getResult.status = 2; // PGRES_TUPLES_OK
                getResult.rows = {{
                    std::to_string(g_dbGetAgentRecord.id),
                    g_dbGetAgentRecord.agentId,
                    g_dbGetAgentRecord.hostname,
                    g_dbGetAgentRecord.osType,
                    g_dbGetAgentRecord.osVersion,
                    g_dbGetAgentRecord.agentVersion,
                    g_dbGetAgentRecord.status,
                    g_dbGetAgentRecord.certSerial,
                    g_dbGetAgentRecord.registeredAt,
                    g_dbGetAgentRecord.lastSeenAt,
                    g_dbGetAgentRecord.ipAddress
                }};
            } else {
                getResult.status = 2; // PGRES_TUPLES_OK
                getResult.rows.clear(); // no rows
            }
            return &getResult;
        }

        // UPDATE agents
        if (query.find("UPDATE agents") != std::string::npos) {
            static pg_result updateResult;
            updateResult.status = 1; // PGRES_COMMAND_OK
            updateResult.rows.clear();
            return &updateResult;
        }

        static pg_result defaultResult;
        defaultResult.status = 1;
        defaultResult.rows.clear();
        return &defaultResult;
    }

    ExecStatusType PQresultStatus(const PGresult* res) {
        if (!res) return PGRES_FATAL_ERROR;
        return (ExecStatusType)res->status;
    }
    void PQclear(PGresult* res) {}
    int PQntuples(const PGresult* res) {
        if (!res) return 0;
        return (int)res->rows.size();
    }
    char* PQgetvalue(const PGresult* res, int tup_num, int field_num) {
        if (!res || tup_num >= (int)res->rows.size() || field_num >= (int)res->rows[tup_num].size())
            return (char*)"";
        return (char*)res->rows[tup_num][field_num].c_str();
    }
    int PQgetisnull(const PGresult* res, int tup_num, int field_num) {
        if (!res || tup_num >= (int)res->rows.size() || field_num >= (int)res->rows[tup_num].size())
            return 1;
        return 0;
    }
}

// =============================================================================
// Tests
// =============================================================================

void test_RegisterAgent() {
    std::cout << "\n========== TEST 1: Register Agent ==========\n";
    PostgresClient db;
    g_mockConnStatus = 0;
    db.connect("mock");

    AgentRegistry registry(db);

    AgentRecord agent;
    agent.agentId = "agent-reg-001";
    agent.hostname = "host-001";
    agent.osType = "Windows";
    agent.status = "ACTIVE";

    bool ok = registry.registerAgent(agent);
    assert(ok && "registerAgent() must return true");
    assert(g_lastQuery.find("INSERT INTO agents") != std::string::npos &&
           "Must insert into agents table");

    // Agent should now be in cache
    auto cached = registry.getAgent("agent-reg-001");
    assert(cached.has_value() && "Agent must be in cache after registration");
    assert(cached->hostname == "host-001" && "Cached hostname must match");

    std::cout << "[PASS] Register agent succeeds and populates cache\n";
}

void test_GetAgent_CacheHit() {
    std::cout << "\n========== TEST 2: Get Agent - Cache Hit ==========\n";
    PostgresClient db;
    g_mockConnStatus = 0;
    db.connect("mock");

    AgentRegistry registry(db);

    AgentRecord agent;
    agent.agentId = "agent-cache-hit";
    agent.hostname = "cached-host";
    agent.osType = "Linux";
    agent.status = "ACTIVE";

    registry.registerAgent(agent);

    // Clear DB mock so any DB call would return nothing
    g_dbGetAgentShouldReturn = false;

    auto cached = registry.getAgent("agent-cache-hit");
    assert(cached.has_value() && "Must return agent from cache");
    assert(cached->hostname == "cached-host" && "Must return cached data");

    std::cout << "[PASS] getAgent() returns from cache without DB call\n";
}

void test_GetAgent_CacheMiss_DBHit() {
    std::cout << "\n========== TEST 3: Get Agent - Cache Miss / DB Hit ==========\n";
    PostgresClient db;
    g_mockConnStatus = 0;
    db.connect("mock");

    AgentRegistry registry(db);

    // Don't register - agent not in cache. Set DB to return a record.
    g_dbGetAgentShouldReturn = true;
    g_dbGetAgentRecord = {};
    g_dbGetAgentRecord.id = 42;
    g_dbGetAgentRecord.agentId = "agent-db-hit";
    g_dbGetAgentRecord.hostname = "db-host";
    g_dbGetAgentRecord.osType = "macOS";
    g_dbGetAgentRecord.status = "ACTIVE";

    auto result = registry.getAgent("agent-db-hit");
    assert(result.has_value() && "Must return agent from DB");
    assert(result->hostname == "db-host" && "Must match DB record");

    // Second call should hit cache
    g_dbGetAgentShouldReturn = false;
    auto cached = registry.getAgent("agent-db-hit");
    assert(cached.has_value() && "Must return from cache on second call");
    assert(cached->hostname == "db-host" && "Cached data must match");

    std::cout << "[PASS] Cache miss falls through to DB, then caches result\n";
}

void test_GetAgent_NotFound() {
    std::cout << "\n========== TEST 4: Get Agent - Not Found ==========\n";
    PostgresClient db;
    g_mockConnStatus = 0;
    db.connect("mock");

    AgentRegistry registry(db);
    g_dbGetAgentShouldReturn = false;

    auto result = registry.getAgent("nonexistent-agent");
    assert(!result.has_value() && "Must return nullopt for unknown agent");

    std::cout << "[PASS] getAgent() returns nullopt for unknown agent\n";
}

void test_UpdateStatus() {
    std::cout << "\n========== TEST 5: Update Status ==========\n";
    PostgresClient db;
    g_mockConnStatus = 0;
    db.connect("mock");

    AgentRegistry registry(db);

    AgentRecord agent;
    agent.agentId = "agent-status-001";
    agent.hostname = "status-host";
    agent.status = "ACTIVE";
    registry.registerAgent(agent);

    bool ok = registry.updateStatus("agent-status-001", "REVOKED");
    assert(ok && "updateStatus() must succeed");
    assert(g_lastParams[0] == "REVOKED" && "Must pass new status to DB");

    // Cache should be updated
    auto updated = registry.getAgent("agent-status-001");
    assert(updated.has_value() && "Agent must still be in cache");
    assert(updated->status == "REVOKED" && "Cached status must be updated");

    std::cout << "[PASS] updateStatus() updates DB and cache\n";
}

void test_IsRegistered_Cache() {
    std::cout << "\n========== TEST 6: Is Registered - Cache ==========\n";
    PostgresClient db;
    g_mockConnStatus = 0;
    db.connect("mock");

    AgentRegistry registry(db);

    AgentRecord agent;
    agent.agentId = "agent-is-reg-cache";
    agent.status = "ACTIVE";
    registry.registerAgent(agent);

    assert(registry.isRegistered("agent-is-reg-cache") && "Must be registered (cache)");
    assert(!registry.isRegistered("unknown-agent-xyz") && "Must NOT be registered");

    std::cout << "[PASS] isRegistered() checks cache correctly\n";
}

void test_IsRegistered_DBFallback() {
    std::cout << "\n========== TEST 7: Is Registered - DB Fallback ==========\n";
    PostgresClient db;
    g_mockConnStatus = 0;
    db.connect("mock");

    AgentRegistry registry(db);

    // Pre-insert into mock DB tracking
    g_insertedAgents.push_back("agent-in-db-only");

    // Not in cache, but agentExists should hit DB
    assert(registry.isRegistered("agent-in-db-only") &&
           "Must return true from DB fallback");

    std::cout << "[PASS] isRegistered() falls back to DB when not in cache\n";
}

void test_ActiveAgentCount() {
    std::cout << "\n========== TEST 8: Active Agent Count ==========\n";
    PostgresClient db;
    g_mockConnStatus = 0;
    db.connect("mock");

    AgentRegistry registry(db);

    AgentRecord a1; a1.agentId = "count-001"; a1.status = "ACTIVE";
    AgentRecord a2; a2.agentId = "count-002"; a2.status = "ACTIVE";
    AgentRecord a3; a3.agentId = "count-003"; a3.status = "REVOKED";
    AgentRecord a4; a4.agentId = "count-004"; a4.status = "INACTIVE";

    registry.registerAgent(a1);
    registry.registerAgent(a2);
    registry.registerAgent(a3);
    registry.registerAgent(a4);

    size_t count = registry.getActiveAgentCount();
    assert(count == 2 && "Only ACTIVE agents should be counted");

    std::cout << "[PASS] getActiveAgentCount() returns " << count << " (expected 2)\n";
}

void test_RefreshCache() {
    std::cout << "\n========== TEST 9: Refresh Cache ==========\n";
    PostgresClient db;
    g_mockConnStatus = 0;
    db.connect("mock");

    AgentRegistry registry(db);

    AgentRecord agent;
    agent.agentId = "agent-refresh";
    agent.status = "ACTIVE";
    registry.registerAgent(agent);

    assert(registry.getActiveAgentCount() == 1 && "Should have 1 active agent");

    registry.refreshCache();

    assert(registry.getActiveAgentCount() == 0 && "Cache must be empty after refresh");

    std::cout << "[PASS] refreshCache() clears the cache\n";
}

int main() {
    Logger::initialize("debug");
    g_insertedAgents.clear();

    try {
        test_RegisterAgent();
        test_GetAgent_CacheHit();
        test_GetAgent_CacheMiss_DBHit();
        test_GetAgent_NotFound();
        test_UpdateStatus();
        test_IsRegistered_Cache();
        test_IsRegistered_DBFallback();
        test_ActiveAgentCount();
        test_RefreshCache();
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }

    if (g_mockConn) { delete g_mockConn; g_mockConn = nullptr; }

    std::cout << "\n=== ALL AGENT REGISTRY TESTS PASSED ===\n\n";
    return 0;
}

