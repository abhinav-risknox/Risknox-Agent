#include "manager/db/PostgresClient.h"
#include "utils/Logger.h"

#include <iostream>
#include <cassert>
#include <cstring>
#include <vector>
#include <string>

// ─────────────────────────────────────────────────────────────
// Mock libpq implementation
// ─────────────────────────────────────────────────────────────

// Simplified PGconn and PGresult for mocking
struct pg_conn {
    std::string errorMessage;
    int status;
};

struct pg_result {
    int status;
    std::vector<std::vector<std::string>> rows;
};

// Global state for controlled mocking
static pg_conn* g_mockConn = nullptr;
static int g_mockConnStatus = 0; // 0 = CONNECTION_OK, 1 = CONNECTION_BAD
static std::string g_mockErrorMessage = "";

static pg_result* g_mockResult = nullptr;
static std::string g_lastQuery = "";
static std::vector<std::string> g_lastParams = {};

extern "C" {
    typedef enum {
        CONNECTION_OK,
        CONNECTION_BAD,
        CONNECTION_STARTED,
        CONNECTION_MADE,
        CONNECTION_AWAITING_RESPONSE,
        CONNECTION_AUTH_OK,
        CONNECTION_SETENV,
        CONNECTION_SSL_STARTUP,
        CONNECTION_NEEDED,
        CONNECTION_CHECK_WRITABLE,
        CONNECTION_CONSUME,
        CONNECTION_GSS_STARTUP
    } ConnStatusType;

    typedef enum {
        PGRES_EMPTY_QUERY = 0,
        PGRES_COMMAND_OK,
        PGRES_TUPLES_OK,
        PGRES_COPY_OUT,
        PGRES_COPY_IN,
        PGRES_BAD_RESPONSE,
        PGRES_NONFATAL_ERROR,
        PGRES_FATAL_ERROR,
        PGRES_COPY_BOTH,
        PGRES_SINGLE_TUPLE,
        PGRES_PIPELINE_SYNC,
        PGRES_PIPELINE_ABORTED
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
        if (conn == g_mockConn) {
            delete g_mockConn;
            g_mockConn = nullptr;
        }
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
        return g_mockResult;
    }

    PGresult* PQexecParams(PGconn* conn, const char* command, int nParams,
                          const void* const * paramTypes, const char* const * paramValues,
                          const int* paramLengths, const int* paramFormats, int resultFormat) {
        g_lastQuery = command;
        g_lastParams.clear();
        for (int i = 0; i < nParams; ++i) {
            g_lastParams.push_back(paramValues[i] ? paramValues[i] : "NULL");
        }
        return g_mockResult;
    }

    ExecStatusType PQresultStatus(const PGresult* res) {
        if (!res) return PGRES_FATAL_ERROR;
        return (ExecStatusType)res->status;
    }

    void PQclear(PGresult* res) {
        // In this simple mock, we don't manage result memory strictly
    }

    int PQntuples(const PGresult* res) {
        if (!res) return 0;
        return (int)res->rows.size();
    }

    char* PQgetvalue(const PGresult* res, int tup_num, int field_num) {
        if (!res || tup_num >= (int)res->rows.size() || field_num >= (int)res->rows[tup_num].size()) {
            return (char*)"";
        }
        return (char*)res->rows[tup_num][field_num].c_str();
    }

    int PQgetisnull(const PGresult* res, int tup_num, int field_num) {
        if (!res || tup_num >= (int)res->rows.size() || field_num >= (int)res->rows[tup_num].size()) {
            return 1;
        }
        return 0; // Simple mock: all provided values are NOT null
    }
}

// ─────────────────────────────────────────────────────────────
// Helper to setup mock results
// ─────────────────────────────────────────────────────────────

void setupMockResult(int status, const std::vector<std::vector<std::string>>& rows = {}) {
    if (g_mockResult) delete g_mockResult;
    g_mockResult = new pg_result();
    g_mockResult->status = status;
    g_mockResult->rows = rows;
}

// ─────────────────────────────────────────────────────────────
// Tests
// ─────────────────────────────────────────────────────────────

void test_Connection() {
    std::cout << "Testing Connection..." << std::endl;
    ResolutePulse::PostgresClient client;

    // Test failed connection
    g_mockConnStatus = 1; // CONNECTION_BAD
    g_mockErrorMessage = "Database not found";
    assert(client.connect("host=localhost dbname=risknox user=postgres password=abhi1243") == false);
    assert(client.isConnected() == false);
    assert(client.getLastError().find("Database not found") != std::string::npos);

    // Test successful connection
    g_mockConnStatus = 0; // CONNECTION_OK
    assert(client.connect("host=localhost dbname=risknox user=postgres password=abhi1243") == true);
    assert(client.isConnected() == true);

    client.disconnect();
    assert(client.isConnected() == false);
    std::cout << "  OK" << std::endl;
}

void test_AgentOperations() {
    std::cout << "Testing Agent Operations..." << std::endl;
    ResolutePulse::PostgresClient client;
    g_mockConnStatus = 0;
    client.connect("host=localhost dbname=risknox user=postgres password=abhi1243");

    // Insert Agent
    ResolutePulse::AgentRecord agent;
    agent.agentId = "agent-001";
    agent.hostname = "test-host";
    agent.ipAddress = "127.0.0.1";
    
    setupMockResult(1); // PGRES_COMMAND_OK
    assert(client.insertAgent(agent) == true);
    assert(g_lastQuery.find("INSERT INTO agents") != std::string::npos);
    assert(g_lastParams[0] == "agent-001");

    // Get Agent
    setupMockResult(2, {{"1", "agent-001", "test-host", "windows", "10", "1.0", "ACTIVE", "ser-123", "2024-01-01", "2024-01-02", "127.0.0.1"}}); // PGRES_TUPLES_OK
    auto retrieved = client.getAgent("agent-001");
    assert(retrieved.has_value());
    assert(retrieved->agentId == "agent-001");
    assert(retrieved->status == "ACTIVE");

    // Agent Exists
    setupMockResult(2, {{"1"}});
    assert(client.agentExists("agent-001") == true);

    // Update Status
    setupMockResult(1);
    assert(client.updateAgentStatus("agent-001", "REVOKED") == true);
    assert(g_lastParams[0] == "REVOKED");

    std::cout << "  OK" << std::endl;
}

void test_CertificateOperations() {
    std::cout << "Testing Certificate Operations..." << std::endl;
    ResolutePulse::PostgresClient client;
    g_mockConnStatus = 0;
    client.connect("host=localhost dbname=risknox user=postgres password=abhi1243");

    // Insert Certificate
    ResolutePulse::CertificateRecord cert;
    cert.serialNumber = "SN-999";
    cert.agentId = "agent-001";
    cert.certificatePem = "BEGIN CERT...";
    cert.expiresAt = "2025-01-01";

    setupMockResult(1);
    assert(client.insertCertificate(cert) == true);
    assert(g_lastParams[0] == "SN-999");

    // Get Certificate
    setupMockResult(2, {{"1", "SN-999", "agent-001", "PEM-DATA", "2024-01-01", "2025-01-01", "f", "", ""}});
    auto retrieved = client.getCertificate("agent-001");
    assert(retrieved.has_value());
    assert(retrieved->serialNumber == "SN-999");

    // Revoke Certificate
    setupMockResult(1);
    assert(client.revokeCertificate("SN-999", "Testing") == true);
    assert(g_lastParams[1] == "SN-999");

    // Get Revoked Serials
    setupMockResult(2, {{"SN-001"}, {"SN-002"}});
    auto revoked = client.getRevokedSerials();
    assert(revoked.size() == 2);
    assert(revoked[0] == "SN-001");

    std::cout << "  OK" << std::endl;
}

void test_LicenseOperations() {
    std::cout << "Testing License Operations..." << std::endl;
    ResolutePulse::PostgresClient client;
    g_mockConnStatus = 0;
    client.connect("host=localhost dbname=risknox user=postgres password=abhi1243");

    setupMockResult(2, {{"1", "agent-001", "KEY-XYZ", "TRIAL", "2024-01-01", "2030-01-01", "5"}});
    auto license = client.getLicense("agent-001");
    assert(license.has_value());
    assert(license->licenseKey == "KEY-XYZ");
    assert(license->maxAgents == 5);

    std::cout << "  OK" << std::endl;
}

int main() {
    ResolutePulse::Logger::initialize("debug");

    try {
        test_Connection();
        test_AgentOperations();
        test_CertificateOperations();
        test_LicenseOperations();
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }

    if (g_mockResult) delete g_mockResult;
    if (g_mockConn) delete g_mockConn;

    std::cout << "\nALL POSTGRES CLIENT TESTS PASSED!" << std::endl;
    return 0;
}
