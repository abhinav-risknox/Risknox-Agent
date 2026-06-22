// =============================================================================
// test_license_manager.cpp - Unit tests for LicenseManager
//
// Uses mock libpq stubs - no database needed.
// =============================================================================

#include "manager/registry/LicenseManager.h"
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
static std::string g_lastQuery = "";
static std::vector<std::string> g_lastParams = {};

// Controllable license mock
static bool g_licenseShouldReturn = false;
static std::string g_licenseType = "TRIAL";

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

        // getLicense query
        if (query.find("licenses") != std::string::npos) {
            static pg_result licResult;
            if (g_licenseShouldReturn) {
                licResult.status = 2; // PGRES_TUPLES_OK
                licResult.rows = {{"1", "test-agent", "KEY-123", g_licenseType,
                                   "2024-01-01", "2030-12-31", "10"}};
            } else {
                licResult.status = 2; // PGRES_TUPLES_OK
                licResult.rows.clear(); // no license
            }
            return &licResult;
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
        if (!res || tup_num >= (int)res->rows.size() || field_num >= (int)res->rows[tup_num].size()) {
            return 1;
        }
        return 0; 
    }

    char* PQcmdTuples(PGresult* res) {
        return (char*)"1";
    }
}

// =============================================================================
// Tests
// =============================================================================

void test_NoLicense_TrialExpiry() {
    std::cout << "\n========== TEST 1: No License → Trial (7 days) ==========\n";
    PostgresClient db;
    g_mockConnStatus = 0;
    db.connect("mock");

    LicenseManager lm(db);
    g_licenseShouldReturn = false;

    int days = lm.calculateExpiry("agent-no-lic");
    assert(days == 7 && "Must return 7 (TRIAL_DAYS) when no license");

    std::cout << "[PASS] No license returns " << days << " days (trial)\n";
}

void test_EnterpriseLicense_365() {
    std::cout << "\n========== TEST 2: Enterprise License → 365 days ==========\n";
    PostgresClient db;
    g_mockConnStatus = 0;
    db.connect("mock");

    LicenseManager lm(db);
    g_licenseShouldReturn = true;
    g_licenseType = "ENTERPRISE";

    int days = lm.calculateExpiry("agent-enterprise");
    assert(days == 365 && "Must return 365 for ENTERPRISE license");

    std::cout << "[PASS] Enterprise license returns " << days << " days\n";
}

void test_StandardLicense_180() {
    std::cout << "\n========== TEST 3: Standard License → 180 days ==========\n";
    PostgresClient db;
    g_mockConnStatus = 0;
    db.connect("mock");

    LicenseManager lm(db);
    g_licenseShouldReturn = true;
    g_licenseType = "STANDARD";

    int days = lm.calculateExpiry("agent-standard");
    assert(days == 180 && "Must return 180 for STANDARD license");

    std::cout << "[PASS] Standard license returns " << days << " days\n";
}

void test_TrialLicense_7() {
    std::cout << "\n========== TEST 4: Trial License → 7 days ==========\n";
    PostgresClient db;
    g_mockConnStatus = 0;
    db.connect("mock");

    LicenseManager lm(db);
    g_licenseShouldReturn = true;
    g_licenseType = "TRIAL";

    int days = lm.calculateExpiry("agent-trial");
    assert(days == 7 && "Must return 7 for TRIAL license");

    std::cout << "[PASS] Trial license returns " << days << " days\n";
}

void test_CheckStatus_None() {
    std::cout << "\n========== TEST 5: Check Status - No License ==========\n";
    PostgresClient db;
    g_mockConnStatus = 0;
    db.connect("mock");

    LicenseManager lm(db);
    g_licenseShouldReturn = false;

    std::string status = lm.checkLicenseStatus("agent-no-lic");
    assert(status == "NONE" && "Must return 'NONE' when no license");

    std::cout << "[PASS] checkLicenseStatus() returns '" << status << "'\n";
}

void test_CheckStatus_Active() {
    std::cout << "\n========== TEST 6: Check Status - Active ==========\n";
    PostgresClient db;
    g_mockConnStatus = 0;
    db.connect("mock");

    LicenseManager lm(db);
    g_licenseShouldReturn = true;
    g_licenseType = "ENTERPRISE";

    std::string status = lm.checkLicenseStatus("agent-active");
    assert(status == "ACTIVE" && "Must return 'ACTIVE' when license exists");

    std::cout << "[PASS] checkLicenseStatus() returns '" << status << "'\n";
}

void test_GetLicense_Passthrough() {
    std::cout << "\n========== TEST 7: Get License - Passthrough ==========\n";
    PostgresClient db;
    g_mockConnStatus = 0;
    db.connect("mock");

    LicenseManager lm(db);

    // No license
    g_licenseShouldReturn = false;
    auto noLic = lm.getLicense("agent-none");
    assert(!noLic.has_value() && "Must return nullopt when no license");

    // With license
    g_licenseShouldReturn = true;
    g_licenseType = "STANDARD";
    auto lic = lm.getLicense("agent-has-lic");
    assert(lic.has_value() && "Must return license when one exists");
    assert(lic->licenseType == "STANDARD" && "License type must match");
    assert(lic->licenseKey == "KEY-123" && "License key must match mock");
    assert(lic->maxAgents == 10 && "Max agents must match mock");

    std::cout << "[PASS] getLicense() correctly delegates to DB\n";
}

int main() {
    Logger::initialize("debug");

    try {
        test_NoLicense_TrialExpiry();
        test_EnterpriseLicense_365();
        test_StandardLicense_180();
        test_TrialLicense_7();
        test_CheckStatus_None();
        test_CheckStatus_Active();
        test_GetLicense_Passthrough();
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }

    if (g_mockConn) { delete g_mockConn; g_mockConn = nullptr; }

    std::cout << "\n=== ALL LICENSE MANAGER TESTS PASSED ===\n\n";
    return 0;
}

