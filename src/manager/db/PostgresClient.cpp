#include "PostgresClient.h"
#include "utils/Logger.h"

#include <libpq-fe.h>
#include <sstream>

namespace ResolutePulse {

PostgresClient::PostgresClient() = default;

PostgresClient::~PostgresClient() {
    disconnect();
}

bool PostgresClient::connect(const std::string& connString) {
    std::lock_guard<std::recursive_mutex> lock(dbMutex_);
    connString_ = connString; // store for auto-reconnect
    conn_ = PQconnectdb(connString_.c_str());
    
    if (PQstatus(conn_) != CONNECTION_OK) {
        lastError_ = "PostgreSQL connection failed: " + std::string(PQerrorMessage(conn_));
        LOG_ERROR("{}", lastError_);
        PQfinish(conn_);
        conn_ = nullptr;
        return false;
    }
    
    LOG_INFO("Connected to PostgreSQL");
    return true;
}

void PostgresClient::disconnect() {
    std::lock_guard<std::recursive_mutex> lock(dbMutex_);
    if (conn_) {
        PQfinish(conn_);
        conn_ = nullptr;
        LOG_DEBUG("Disconnected from PostgreSQL");
    }
}

bool PostgresClient::isConnected() const {
    std::lock_guard<std::recursive_mutex> lock(dbMutex_);
    return conn_ != nullptr && PQstatus(conn_) == CONNECTION_OK;
}

bool PostgresClient::reconnect() {
    std::lock_guard<std::recursive_mutex> lock(dbMutex_);
    if (connString_.empty()) {
        lastError_ = "Cannot reconnect: no connection string stored";
        LOG_ERROR("{}", lastError_);
        return false;
    }
    LOG_INFO("PostgreSQL connection lost - attempting reconnect...");
    if (conn_) {
        PQfinish(conn_);
        conn_ = nullptr;
    }
    conn_ = PQconnectdb(connString_.c_str());
    if (PQstatus(conn_) != CONNECTION_OK) {
        lastError_ = "Reconnect failed: " + std::string(PQerrorMessage(conn_));
        LOG_ERROR("{}", lastError_);
        PQfinish(conn_);
        conn_ = nullptr;
        return false;
    }
    LOG_INFO("Reconnected to PostgreSQL successfully");
    return true;
}

// ─────────────────────────────────────────────────────────────
// Agent Operations
// ─────────────────────────────────────────────────────────────

bool PostgresClient::insertAgent(const AgentRecord& agent) {
    std::lock_guard<std::recursive_mutex> lock(dbMutex_);
    if (!isConnected() && !reconnect()) {
        lastError_ = "Not connected";
        return false;
    }

    const char* paramValues[6] = {
        agent.agentId.c_str(),
        agent.hostname.c_str(),
        agent.osType.c_str(),
        agent.osVersion.c_str(),
        agent.agentVersion.c_str(),
        agent.ipAddress.c_str()
    };

    PGresult* res = PQexecParams(conn_,
        "INSERT INTO agents (agent_id, hostname, os_type, os_version, agent_version, ip_address, status) "
        "VALUES ($1, $2, $3, $4, $5, $6, 'ACTIVE')",
        6, nullptr, paramValues, nullptr, nullptr, 0);

    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        lastError_ = "Insert agent failed: " + std::string(PQerrorMessage(conn_));
        LOG_ERROR("{}", lastError_);
        PQclear(res);
        return false;
    }

    PQclear(res);
    LOG_INFO("Inserted agent: {}", agent.agentId);
    return true;
}

std::optional<AgentRecord> PostgresClient::getAgent(const std::string& agentId) {
    std::lock_guard<std::recursive_mutex> lock(dbMutex_);
    if (!isConnected() && !reconnect()) return std::nullopt;

    const char* paramValues[1] = { agentId.c_str() };

    PGresult* res = PQexecParams(conn_,
        "SELECT id, agent_id, hostname, os_type, os_version, agent_version, "
        "status, cert_serial, registered_at::text, last_seen_at::text, ip_address "
        "FROM agents WHERE agent_id = $1",
        1, nullptr, paramValues, nullptr, nullptr, 0);

    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0) {
        PQclear(res);
        return std::nullopt;
    }

    AgentRecord agent;
    agent.id           = std::stoi(PQgetvalue(res, 0, 0));
    agent.agentId      = PQgetvalue(res, 0, 1);
    agent.hostname     = PQgetvalue(res, 0, 2);
    agent.osType       = PQgetvalue(res, 0, 3);
    agent.osVersion    = PQgetvalue(res, 0, 4) ? PQgetvalue(res, 0, 4) : "";
    agent.agentVersion = PQgetvalue(res, 0, 5) ? PQgetvalue(res, 0, 5) : "";
    agent.status       = PQgetvalue(res, 0, 6);
    agent.certSerial   = PQgetisnull(res, 0, 7) ? "" : PQgetvalue(res, 0, 7);
    agent.registeredAt = PQgetisnull(res, 0, 8) ? "" : PQgetvalue(res, 0, 8);
    agent.lastSeenAt   = PQgetisnull(res, 0, 9) ? "" : PQgetvalue(res, 0, 9);
    agent.ipAddress    = PQgetisnull(res, 0, 10) ? "" : PQgetvalue(res, 0, 10);

    PQclear(res);
    return agent;
}

bool PostgresClient::updateAgentStatus(const std::string& agentId, const std::string& status) {
    std::lock_guard<std::recursive_mutex> lock(dbMutex_);
    if (!isConnected() && !reconnect()) return false;

    const char* paramValues[2] = { status.c_str(), agentId.c_str() };

    PGresult* res = PQexecParams(conn_,
        "UPDATE agents SET status = $1 WHERE agent_id = $2",
        2, nullptr, paramValues, nullptr, nullptr, 0);

    bool ok = PQresultStatus(res) == PGRES_COMMAND_OK;
    if (!ok) {
        lastError_ = "Update agent status failed: " + std::string(PQerrorMessage(conn_));
        LOG_ERROR("{}", lastError_);
    }
    PQclear(res);
    return ok;
}

bool PostgresClient::updateAgentCertSerial(const std::string& agentId, const std::string& certSerial) {
    std::lock_guard<std::recursive_mutex> lock(dbMutex_);
    if (!isConnected() && !reconnect()) return false;

    const char* paramValues[2] = { certSerial.c_str(), agentId.c_str() };

    PGresult* res = PQexecParams(conn_,
        "UPDATE agents SET cert_serial = $1 WHERE agent_id = $2",
        2, nullptr, paramValues, nullptr, nullptr, 0);

    bool ok = PQresultStatus(res) == PGRES_COMMAND_OK;
    if (!ok) {
        lastError_ = "Update cert serial failed: " + std::string(PQerrorMessage(conn_));
    }
    PQclear(res);
    return ok;
}

bool PostgresClient::updateLastSeen(const std::string& agentId) {
    std::lock_guard<std::recursive_mutex> lock(dbMutex_);
    if (!isConnected() && !reconnect()) return false;

    const char* paramValues[1] = { agentId.c_str() };

    PGresult* res = PQexecParams(conn_,
        "UPDATE agents SET last_seen_at = NOW() WHERE agent_id = $1",
        1, nullptr, paramValues, nullptr, nullptr, 0);

    bool ok = PQresultStatus(res) == PGRES_COMMAND_OK;
    PQclear(res);
    return ok;
}

bool PostgresClient::agentExists(const std::string& agentId) {
    std::lock_guard<std::recursive_mutex> lock(dbMutex_);
    if (!isConnected() && !reconnect()) return false;

    const char* paramValues[1] = { agentId.c_str() };

    PGresult* res = PQexecParams(conn_,
        "SELECT 1 FROM agents WHERE agent_id = $1",
        1, nullptr, paramValues, nullptr, nullptr, 0);

    bool exists = (PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0);
    PQclear(res);
    return exists;
}

// ─────────────────────────────────────────────────────────────
// Certificate Operations
// ─────────────────────────────────────────────────────────────

bool PostgresClient::insertCertificate(const CertificateRecord& cert) {
    std::lock_guard<std::recursive_mutex> lock(dbMutex_);
    if (!isConnected() && !reconnect()) return false;

    const char* paramValues[4] = {
        cert.serialNumber.c_str(),
        cert.agentId.c_str(),
        cert.certificatePem.c_str(),
        cert.expiresAt.c_str()
    };

    PGresult* res = PQexecParams(conn_,
        "INSERT INTO certificates (serial_number, agent_id, certificate_pem, expires_at) "
        "VALUES ($1, $2, $3, $4::timestamptz)",
        4, nullptr, paramValues, nullptr, nullptr, 0);

    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        lastError_ = "Insert certificate failed: " + std::string(PQerrorMessage(conn_));
        LOG_ERROR("{}", lastError_);
        PQclear(res);
        return false;
    }

    PQclear(res);
    LOG_INFO("Inserted certificate serial={} for agent={}", cert.serialNumber, cert.agentId);
    return true;
}

std::optional<CertificateRecord> PostgresClient::getCertificate(const std::string& agentId) {
    std::lock_guard<std::recursive_mutex> lock(dbMutex_);
    if (!isConnected() && !reconnect()) return std::nullopt;

    const char* paramValues[1] = { agentId.c_str() };

    PGresult* res = PQexecParams(conn_,
        "SELECT id, serial_number, agent_id, certificate_pem, "
        "issued_at::text, expires_at::text, revoked, revoked_at::text, revoke_reason "
        "FROM certificates WHERE agent_id = $1 AND revoked = FALSE "
        "ORDER BY issued_at DESC LIMIT 1",
        1, nullptr, paramValues, nullptr, nullptr, 0);

    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0) {
        PQclear(res);
        return std::nullopt;
    }

    CertificateRecord cert;
    cert.id             = std::stoi(PQgetvalue(res, 0, 0));
    cert.serialNumber   = PQgetvalue(res, 0, 1);
    cert.agentId        = PQgetvalue(res, 0, 2);
    cert.certificatePem = PQgetvalue(res, 0, 3);
    cert.issuedAt       = PQgetisnull(res, 0, 4) ? "" : PQgetvalue(res, 0, 4);
    cert.expiresAt      = PQgetisnull(res, 0, 5) ? "" : PQgetvalue(res, 0, 5);
    cert.revoked        = std::string(PQgetvalue(res, 0, 6)) == "t";
    cert.revokedAt      = PQgetisnull(res, 0, 7) ? "" : PQgetvalue(res, 0, 7);
    cert.revokeReason   = PQgetisnull(res, 0, 8) ? "" : PQgetvalue(res, 0, 8);

    PQclear(res);
    return cert;
}

std::optional<CertificateRecord> PostgresClient::getCertificateBySerial(const std::string& serialNumber) {
    std::lock_guard<std::recursive_mutex> lock(dbMutex_);
    if (!isConnected() && !reconnect()) return std::nullopt;

    const char* paramValues[1] = { serialNumber.c_str() };

    PGresult* res = PQexecParams(conn_,
        "SELECT id, serial_number, agent_id, certificate_pem, "
        "issued_at::text, expires_at::text, revoked, revoked_at::text, revoke_reason "
        "FROM certificates WHERE serial_number = $1",
        1, nullptr, paramValues, nullptr, nullptr, 0);

    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0) {
        PQclear(res);
        return std::nullopt;
    }

    CertificateRecord cert;
    cert.id             = std::stoi(PQgetvalue(res, 0, 0));
    cert.serialNumber   = PQgetvalue(res, 0, 1);
    cert.agentId        = PQgetvalue(res, 0, 2);
    cert.certificatePem = PQgetvalue(res, 0, 3);
    cert.issuedAt       = PQgetisnull(res, 0, 4) ? "" : PQgetvalue(res, 0, 4);
    cert.expiresAt      = PQgetisnull(res, 0, 5) ? "" : PQgetvalue(res, 0, 5);
    cert.revoked        = std::string(PQgetvalue(res, 0, 6)) == "t";
    cert.revokedAt      = PQgetisnull(res, 0, 7) ? "" : PQgetvalue(res, 0, 7);
    cert.revokeReason   = PQgetisnull(res, 0, 8) ? "" : PQgetvalue(res, 0, 8);

    PQclear(res);
    return cert;
}

bool PostgresClient::revokeCertificate(const std::string& serialNumber, const std::string& reason) {
    std::lock_guard<std::recursive_mutex> lock(dbMutex_);
    if (!isConnected() && !reconnect()) return false;

    const char* paramValues[2] = { reason.c_str(), serialNumber.c_str() };

    PGresult* res = PQexecParams(conn_,
        "UPDATE certificates SET revoked = TRUE, revoked_at = NOW(), revoke_reason = $1 "
        "WHERE serial_number = $2",
        2, nullptr, paramValues, nullptr, nullptr, 0);

    bool ok = PQresultStatus(res) == PGRES_COMMAND_OK;
    if (!ok) {
        lastError_ = "Revoke certificate failed: " + std::string(PQerrorMessage(conn_));
        LOG_ERROR("{}", lastError_);
    } else {
        LOG_INFO("Revoked certificate: serial={}", serialNumber);
    }
    PQclear(res);
    return ok;
}

std::vector<std::string> PostgresClient::getRevokedSerials() {
    std::lock_guard<std::recursive_mutex> lock(dbMutex_);
    std::vector<std::string> serials;
    if (!isConnected() && !reconnect()) return serials;

    PGresult* res = PQexec(conn_,
        "SELECT serial_number FROM certificates WHERE revoked = TRUE");

    if (PQresultStatus(res) == PGRES_TUPLES_OK) {
        int rows = PQntuples(res);
        serials.reserve(rows);
        for (int i = 0; i < rows; i++) {
            serials.emplace_back(PQgetvalue(res, i, 0));
        }
    }

    PQclear(res);
    return serials;
}

// ─────────────────────────────────────────────────────────────
// License Operations
// ─────────────────────────────────────────────────────────────

std::optional<LicenseRecord> PostgresClient::getLicense(const std::string& agentId) {
    std::lock_guard<std::recursive_mutex> lock(dbMutex_);
    if (!isConnected() && !reconnect()) return std::nullopt;

    const char* paramValues[1] = { agentId.c_str() };

    PGresult* res = PQexecParams(conn_,
        "SELECT id, agent_id, license_key, license_type, "
        "valid_from::text, valid_until::text, max_agents "
        "FROM licenses WHERE agent_id = $1 AND valid_until > NOW() "
        "ORDER BY valid_until DESC LIMIT 1",
        1, nullptr, paramValues, nullptr, nullptr, 0);

    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0) {
        PQclear(res);
        return std::nullopt;
    }

    LicenseRecord license;
    license.id          = std::stoi(PQgetvalue(res, 0, 0));
    license.agentId     = PQgetvalue(res, 0, 1);
    license.licenseKey  = PQgetvalue(res, 0, 2);
    license.licenseType = PQgetvalue(res, 0, 3);
    license.validFrom   = PQgetisnull(res, 0, 4) ? "" : PQgetvalue(res, 0, 4);
    license.validUntil  = PQgetisnull(res, 0, 5) ? "" : PQgetvalue(res, 0, 5);
    license.maxAgents   = std::stoi(PQgetvalue(res, 0, 6));

    PQclear(res);
    return license;
}

bool PostgresClient::insertLicense(const LicenseRecord& license) {
    std::lock_guard<std::recursive_mutex> lock(dbMutex_);
    if (!isConnected() && !reconnect()) {
        lastError_ = "Not connected";
        return false;
    }

    const char* paramValues[5] = {
        license.agentId.c_str(),
        license.licenseKey.c_str(),
        license.licenseType.c_str(),
        license.validFrom.c_str(),
        license.validUntil.c_str()
    };

    PGresult* res = PQexecParams(conn_,
        "INSERT INTO licenses (agent_id, license_key, license_type, valid_from, valid_until, max_agents) "
        "VALUES ($1, $2, $3, $4::timestamp, $5::timestamp, 1)",
        5, nullptr, paramValues, nullptr, nullptr, 0);

    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        lastError_ = "Insert license failed: " + std::string(PQerrorMessage(conn_));
        LOG_ERROR("{}", lastError_);
        PQclear(res);
        return false;
    }

    PQclear(res);
    LOG_INFO("Inserted {} license for agent: {}", license.licenseType, license.agentId);
    return true;
}

// ─────────────────────────────────────────────────────────────
// Policy Commands (policy_commands table)
// ─────────────────────────────────────────────────────────────

bool PostgresClient::recordPolicyCommand(const std::string& agentId,
                                          const std::string& commandId,
                                          const std::string& policyType,
                                          const std::string& policyDataJson,
                                          bool pending) {
    std::lock_guard<std::recursive_mutex> lock(dbMutex_);
    if (!isConnected() && !reconnect()) return false;

    const char* paramValues[4] = {
        agentId.c_str(),
        commandId.c_str(),
        policyType.c_str(),
        policyDataJson.c_str()
    };

    // Online dispatch: status='sent', dispatched_at=NOW()
    // Offline queue:   status='pending', dispatched_at=NULL
    const char* sql = pending
        ? "INSERT INTO policy_commands "
          "  (agent_id, command_id, policy_type, policy_data, status) "
          "VALUES ($1, $2, $3, $4::jsonb, 'pending')"
        : "INSERT INTO policy_commands "
          "  (agent_id, command_id, policy_type, policy_data, status, dispatched_at) "
          "VALUES ($1, $2, $3, $4::jsonb, 'sent', NOW())";

    PGresult* res = PQexecParams(conn_, sql, 4, nullptr, paramValues, nullptr, nullptr, 0);

    bool ok = PQresultStatus(res) == PGRES_COMMAND_OK;
    if (!ok) {
        lastError_ = "recordPolicyCommand failed: " + std::string(PQerrorMessage(conn_));
        LOG_ERROR("{}", lastError_);
    } else {
        LOG_DEBUG("Recorded policy command: agent={} type={} commandId={} pending={}",
                  agentId, policyType, commandId, pending);
    }
    PQclear(res);
    return ok;
}

bool PostgresClient::ackPolicyCommand(const std::string& commandId,
                                       bool applied,
                                       const std::string& message) {
    std::lock_guard<std::recursive_mutex> lock(dbMutex_);
    if (!isConnected() && !reconnect()) return false;

    std::string ackStatus = applied ? "applied" : "failed";
    const char* paramValues[3] = {
        ackStatus.c_str(),
        message.c_str(),
        commandId.c_str()
    };

    PGresult* res = PQexecParams(conn_,
        "UPDATE policy_commands "
        "SET ack_status = $1, ack_message = $2, ack_at = NOW(), status = 'acked' "
        "WHERE command_id = $3",
        3, nullptr, paramValues, nullptr, nullptr, 0);

    bool ok = PQresultStatus(res) == PGRES_COMMAND_OK;
    if (!ok) {
        lastError_ = "ackPolicyCommand failed: " + std::string(PQerrorMessage(conn_));
        LOG_ERROR("{}", lastError_);
    } else {
        LOG_INFO("Policy ACK recorded: commandId={} status={}", commandId, ackStatus);
    }
    PQclear(res);
    return ok;
}

bool PostgresClient::markPolicyCommandDispatched(const std::string& commandId) {
    std::lock_guard<std::recursive_mutex> lock(dbMutex_);
    if (!isConnected() && !reconnect()) return false;

    const char* paramValues[1] = { commandId.c_str() };

    PGresult* res = PQexecParams(conn_,
        "UPDATE policy_commands "
        "SET status = 'sent', dispatched_at = NOW() "
        "WHERE command_id = $1",
        1, nullptr, paramValues, nullptr, nullptr, 0);

    bool ok = PQresultStatus(res) == PGRES_COMMAND_OK;
    if (!ok) {
        lastError_ = "markPolicyCommandDispatched failed: " + std::string(PQerrorMessage(conn_));
        LOG_ERROR("{}", lastError_);
    }
    PQclear(res);
    return ok;
}

std::vector<PolicyCommand> PostgresClient::fetchPendingPolicies(const std::string& agentId) {
    std::lock_guard<std::recursive_mutex> lock(dbMutex_);
    std::vector<PolicyCommand> commands;
    if (!isConnected() && !reconnect()) return commands;

    const char* paramValues[1] = { agentId.c_str() };

    PGresult* res = PQexecParams(conn_,
        "SELECT id, agent_id, command_id, policy_type, policy_data::text, status, created_at::text "
        "FROM policy_commands "
        "WHERE agent_id = $1 AND status = 'pending' "
        "ORDER BY created_at ASC",
        1, nullptr, paramValues, nullptr, nullptr, 0);

    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        lastError_ = "fetchPendingPolicies failed: " + std::string(PQerrorMessage(conn_));
        LOG_ERROR("{}", lastError_);
        PQclear(res);
        return commands;
    }

    int rows = PQntuples(res);
    commands.reserve(rows);
    for (int i = 0; i < rows; i++) {
        PolicyCommand cmd;
        cmd.id         = std::stoi(PQgetvalue(res, i, 0));
        cmd.agentId    = PQgetvalue(res, i, 1);
        cmd.commandId  = PQgetvalue(res, i, 2);
        cmd.policyType = PQgetvalue(res, i, 3);
        cmd.policyData = PQgetvalue(res, i, 4);
        cmd.status     = PQgetvalue(res, i, 5);
        cmd.createdAt  = PQgetisnull(res, i, 6) ? "" : PQgetvalue(res, i, 6);
        commands.push_back(std::move(cmd));
    }

    PQclear(res);
    LOG_DEBUG("Fetched {} pending policy commands for agent {}", commands.size(), agentId);
    return commands;
}

bool PostgresClient::updatePolicyCommandStatus(const std::string& commandId,
                                                const std::string& status,
                                                const std::string& errorMsg) {
    std::lock_guard<std::recursive_mutex> lock(dbMutex_);
    if (!isConnected() && !reconnect()) return false;

    const char* paramValues[3] = {
        status.c_str(),
        errorMsg.empty() ? nullptr : errorMsg.c_str(),
        commandId.c_str()
    };

    PGresult* res = PQexecParams(conn_,
        "UPDATE policy_commands "
        "SET status = $1, ack_message = COALESCE($2, ack_message) "
        "WHERE command_id = $3",
        3, nullptr, paramValues, nullptr, nullptr, 0);

    bool ok = PQresultStatus(res) == PGRES_COMMAND_OK;
    if (!ok) {
        lastError_ = "updatePolicyCommandStatus failed: " + std::string(PQerrorMessage(conn_));
        LOG_ERROR("{}", lastError_);
    }
    PQclear(res);
    return ok;
}

// ─────────────────────────────────────────────────────────────
// Module Commands (module_commands table)
// ─────────────────────────────────────────────────────────────

bool PostgresClient::recordModuleCommand(const std::string& agentId,
                                          const std::string& commandId,
                                          const std::string& verb,
                                          const std::string& paramsJson,
                                          bool pending) {
    std::lock_guard<std::recursive_mutex> lock(dbMutex_);
    if (!isConnected() && !reconnect()) return false;

    const char* paramValues[4] = {
        agentId.c_str(),
        commandId.c_str(),
        verb.c_str(),
        paramsJson.c_str()
    };

    const char* sql = pending
        ? "INSERT INTO module_commands "
          "  (agent_id, command_id, verb, params, status) "
          "VALUES ($1, $2, $3, $4::jsonb, 'pending')"
        : "INSERT INTO module_commands "
          "  (agent_id, command_id, verb, params, status, dispatched_at) "
          "VALUES ($1, $2, $3, $4::jsonb, 'sent', NOW())";

    PGresult* res = PQexecParams(conn_, sql, 4, nullptr, paramValues, nullptr, nullptr, 0);

    bool ok = PQresultStatus(res) == PGRES_COMMAND_OK;
    if (!ok) {
        lastError_ = "recordModuleCommand failed: " + std::string(PQerrorMessage(conn_));
        LOG_ERROR("{}", lastError_);
    } else {
        LOG_DEBUG("Recorded module command: agent={} verb={} commandId={} pending={}",
                  agentId, verb, commandId, pending);
    }
    PQclear(res);
    return ok;
}

bool PostgresClient::markModuleCommandDispatched(const std::string& commandId) {
    std::lock_guard<std::recursive_mutex> lock(dbMutex_);
    if (!isConnected() && !reconnect()) return false;

    const char* paramValues[1] = { commandId.c_str() };

    PGresult* res = PQexecParams(conn_,
        "UPDATE module_commands "
        "SET status = 'sent', dispatched_at = NOW() "
        "WHERE command_id = $1",
        1, nullptr, paramValues, nullptr, nullptr, 0);

    bool ok = PQresultStatus(res) == PGRES_COMMAND_OK;
    if (!ok) {
        lastError_ = "markModuleCommandDispatched failed: " + std::string(PQerrorMessage(conn_));
        LOG_ERROR("{}", lastError_);
    }
    PQclear(res);
    return ok;
}

bool PostgresClient::ackModuleCommand(const std::string& commandId,
                                       const std::string& ackStatus,
                                       const std::string& resultPayload) {
    std::lock_guard<std::recursive_mutex> lock(dbMutex_);
    if (!isConnected() && !reconnect()) return false;

    const char* paramValues[3] = {
        ackStatus.c_str(),
        resultPayload.c_str(),
        commandId.c_str()
    };

    PGresult* res = PQexecParams(conn_,
        "UPDATE module_commands "
        "SET ack_status = $1, result_payload = $2, ack_at = NOW(), status = 'acked' "
        "WHERE command_id = $3",
        3, nullptr, paramValues, nullptr, nullptr, 0);

    bool ok = PQresultStatus(res) == PGRES_COMMAND_OK;
    if (!ok) {
        lastError_ = "ackModuleCommand failed: " + std::string(PQerrorMessage(conn_));
        LOG_ERROR("{}", lastError_);
    } else {
        LOG_INFO("Command ACK recorded: commandId={} status={}", commandId, ackStatus);
    }
    PQclear(res);
    return ok;
}

std::vector<ModuleCommandRecord> PostgresClient::fetchPendingModuleCommands(const std::string& agentId) {
    std::lock_guard<std::recursive_mutex> lock(dbMutex_);
    std::vector<ModuleCommandRecord> commands;
    if (!isConnected() && !reconnect()) return commands;

    const char* paramValues[1] = { agentId.c_str() };

    PGresult* res = PQexecParams(conn_,
        "SELECT id, agent_id, command_id, verb, params::text, status, created_at::text "
        "FROM module_commands "
        "WHERE agent_id = $1 AND status = 'pending' "
        "ORDER BY created_at ASC",
        1, nullptr, paramValues, nullptr, nullptr, 0);

    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        lastError_ = "fetchPendingModuleCommands failed: " + std::string(PQerrorMessage(conn_));
        LOG_ERROR("{}", lastError_);
        PQclear(res);
        return commands;
    }

    int rows = PQntuples(res);
    commands.reserve(rows);
    for (int i = 0; i < rows; i++) {
        ModuleCommandRecord cmd;
        cmd.id        = std::stoi(PQgetvalue(res, i, 0));
        cmd.agentId   = PQgetvalue(res, i, 1);
        cmd.commandId = PQgetvalue(res, i, 2);
        cmd.verb      = PQgetvalue(res, i, 3);
        cmd.params    = PQgetvalue(res, i, 4);
        cmd.status    = PQgetvalue(res, i, 5);
        cmd.createdAt = PQgetisnull(res, i, 6) ? "" : PQgetvalue(res, i, 6);
        commands.push_back(std::move(cmd));
    }

    PQclear(res);
    LOG_DEBUG("Fetched {} pending module commands for agent {}", commands.size(), agentId);
    return commands;
}

bool PostgresClient::updateModuleCommandStatus(const std::string& commandId,
                                                const std::string& status,
                                                const std::string& errorMsg) {
    std::lock_guard<std::recursive_mutex> lock(dbMutex_);
    if (!isConnected() && !reconnect()) return false;

    const char* paramValues[3] = {
        status.c_str(),
        errorMsg.empty() ? nullptr : errorMsg.c_str(),
        commandId.c_str()
    };

    PGresult* res = PQexecParams(conn_,
        "UPDATE module_commands "
        "SET status = $1, result_payload = COALESCE($2, result_payload) "
        "WHERE command_id = $3",
        3, nullptr, paramValues, nullptr, nullptr, 0);

    bool ok = PQresultStatus(res) == PGRES_COMMAND_OK;
    if (!ok) {
        lastError_ = "updateModuleCommandStatus failed: " + std::string(PQerrorMessage(conn_));
        LOG_ERROR("{}", lastError_);
    }
    PQclear(res);
    return ok;
}

// ─────────────────────────────────────────────────────────────
// Status Reports (agent_status_reports table)
// ─────────────────────────────────────────────────────────────

bool PostgresClient::storeStatusReport(const std::string& agentId,
                                        const std::string& reportType,
                                        const std::string& reportDataJson) {
    std::lock_guard<std::recursive_mutex> lock(dbMutex_);
    if (!isConnected() && !reconnect()) return false;

    const char* paramValues[3] = {
        agentId.c_str(),
        reportType.c_str(),
        reportDataJson.c_str()
    };

    PGresult* res = PQexecParams(conn_,
        "INSERT INTO agent_status_reports (agent_id, report_type, report_data) "
        "VALUES ($1, $2, $3::jsonb)",
        3, nullptr, paramValues, nullptr, nullptr, 0);

    bool ok = PQresultStatus(res) == PGRES_COMMAND_OK;
    if (!ok) {
        lastError_ = "storeStatusReport failed: " + std::string(PQerrorMessage(conn_));
        LOG_ERROR("{}", lastError_);
    } else {
        LOG_INFO("Stored status report: agent={} type={}", agentId, reportType);
    }
    PQclear(res);
    return ok;
}

// ─────────────────────────────────────────────────────────────
// REST API Query Methods
// ─────────────────────────────────────────────────────────────

std::vector<AgentRecord> PostgresClient::listAgents() {
    std::lock_guard<std::recursive_mutex> lock(dbMutex_);
    std::vector<AgentRecord> agents;
    if (!isConnected() && !reconnect()) return agents;

    PGresult* res = PQexec(conn_,
        "SELECT id, agent_id, hostname, os_type, os_version, agent_version, "
        "status, cert_serial, registered_at::text, last_seen_at::text, ip_address "
        "FROM agents ORDER BY last_seen_at DESC NULLS LAST");

    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        lastError_ = "listAgents failed: " + std::string(PQerrorMessage(conn_));
        LOG_ERROR("{}", lastError_);
        PQclear(res);
        return agents;
    }

    int rows = PQntuples(res);
    agents.reserve(rows);
    for (int i = 0; i < rows; i++) {
        AgentRecord a;
        a.id           = std::stoi(PQgetvalue(res, i, 0));
        a.agentId      = PQgetvalue(res, i, 1);
        a.hostname     = PQgetvalue(res, i, 2);
        a.osType       = PQgetvalue(res, i, 3);
        a.osVersion    = PQgetisnull(res, i, 4) ? "" : PQgetvalue(res, i, 4);
        a.agentVersion = PQgetisnull(res, i, 5) ? "" : PQgetvalue(res, i, 5);
        a.status       = PQgetvalue(res, i, 6);
        a.certSerial   = PQgetisnull(res, i, 7) ? "" : PQgetvalue(res, i, 7);
        a.registeredAt = PQgetisnull(res, i, 8) ? "" : PQgetvalue(res, i, 8);
        a.lastSeenAt   = PQgetisnull(res, i, 9) ? "" : PQgetvalue(res, i, 9);
        a.ipAddress    = PQgetisnull(res, i, 10) ? "" : PQgetvalue(res, i, 10);
        agents.push_back(std::move(a));
    }

    PQclear(res);
    return agents;
}

std::vector<ModuleCommandRecord> PostgresClient::listModuleCommands(
    const std::string& agentId, int limit, int offset) {
    std::lock_guard<std::recursive_mutex> lock(dbMutex_);
    std::vector<ModuleCommandRecord> commands;
    if (!isConnected() && !reconnect()) return commands;

    PGresult* res;
    if (agentId.empty()) {
        std::string lim = std::to_string(limit);
        std::string off = std::to_string(offset);
        const char* paramValues[2] = { lim.c_str(), off.c_str() };
        res = PQexecParams(conn_,
            "SELECT id, agent_id, command_id, verb, params::text, status, created_at::text "
            "FROM module_commands ORDER BY created_at DESC LIMIT $1 OFFSET $2",
            2, nullptr, paramValues, nullptr, nullptr, 0);
    } else {
        std::string lim = std::to_string(limit);
        std::string off = std::to_string(offset);
        const char* paramValues[3] = { agentId.c_str(), lim.c_str(), off.c_str() };
        res = PQexecParams(conn_,
            "SELECT id, agent_id, command_id, verb, params::text, status, created_at::text "
            "FROM module_commands WHERE agent_id = $1 ORDER BY created_at DESC LIMIT $2 OFFSET $3",
            3, nullptr, paramValues, nullptr, nullptr, 0);
    }

    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        lastError_ = "listModuleCommands failed: " + std::string(PQerrorMessage(conn_));
        LOG_ERROR("{}", lastError_);
        PQclear(res);
        return commands;
    }

    int rows = PQntuples(res);
    commands.reserve(rows);
    for (int i = 0; i < rows; i++) {
        ModuleCommandRecord c;
        c.id        = std::stoi(PQgetvalue(res, i, 0));
        c.agentId   = PQgetvalue(res, i, 1);
        c.commandId = PQgetvalue(res, i, 2);
        c.verb      = PQgetvalue(res, i, 3);
        c.params    = PQgetvalue(res, i, 4);
        c.status    = PQgetvalue(res, i, 5);
        c.createdAt = PQgetisnull(res, i, 6) ? "" : PQgetvalue(res, i, 6);
        commands.push_back(std::move(c));
    }

    PQclear(res);
    return commands;
}

std::vector<PolicyCommand> PostgresClient::listPolicyCommands(
    const std::string& agentId, int limit, int offset) {
    std::lock_guard<std::recursive_mutex> lock(dbMutex_);
    std::vector<PolicyCommand> commands;
    if (!isConnected() && !reconnect()) return commands;

    PGresult* res;
    if (agentId.empty()) {
        std::string lim = std::to_string(limit);
        std::string off = std::to_string(offset);
        const char* paramValues[2] = { lim.c_str(), off.c_str() };
        res = PQexecParams(conn_,
            "SELECT id, agent_id, command_id, policy_type, policy_data::text, status, created_at::text "
            "FROM policy_commands ORDER BY created_at DESC LIMIT $1 OFFSET $2",
            2, nullptr, paramValues, nullptr, nullptr, 0);
    } else {
        std::string lim = std::to_string(limit);
        std::string off = std::to_string(offset);
        const char* paramValues[3] = { agentId.c_str(), lim.c_str(), off.c_str() };
        res = PQexecParams(conn_,
            "SELECT id, agent_id, command_id, policy_type, policy_data::text, status, created_at::text "
            "FROM policy_commands WHERE agent_id = $1 ORDER BY created_at DESC LIMIT $2 OFFSET $3",
            3, nullptr, paramValues, nullptr, nullptr, 0);
    }

    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        lastError_ = "listPolicyCommands failed: " + std::string(PQerrorMessage(conn_));
        LOG_ERROR("{}", lastError_);
        PQclear(res);
        return commands;
    }

    int rows = PQntuples(res);
    commands.reserve(rows);
    for (int i = 0; i < rows; i++) {
        PolicyCommand c;
        c.id         = std::stoi(PQgetvalue(res, i, 0));
        c.agentId    = PQgetvalue(res, i, 1);
        c.commandId  = PQgetvalue(res, i, 2);
        c.policyType = PQgetvalue(res, i, 3);
        c.policyData = PQgetvalue(res, i, 4);
        c.status     = PQgetvalue(res, i, 5);
        c.createdAt  = PQgetisnull(res, i, 6) ? "" : PQgetvalue(res, i, 6);
        commands.push_back(std::move(c));
    }

    PQclear(res);
    return commands;
}

std::optional<ModuleCommandRecord> PostgresClient::getModuleCommandByCommandId(
    const std::string& commandId) {
    std::lock_guard<std::recursive_mutex> lock(dbMutex_);
    if (!isConnected() && !reconnect()) return std::nullopt;

    const char* paramValues[1] = { commandId.c_str() };
    PGresult* res = PQexecParams(conn_,
        "SELECT id, agent_id, command_id, verb, params::text, status, created_at::text "
        "FROM module_commands WHERE command_id = $1",
        1, nullptr, paramValues, nullptr, nullptr, 0);

    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0) {
        PQclear(res);
        return std::nullopt;
    }

    ModuleCommandRecord c;
    c.id        = std::stoi(PQgetvalue(res, 0, 0));
    c.agentId   = PQgetvalue(res, 0, 1);
    c.commandId = PQgetvalue(res, 0, 2);
    c.verb      = PQgetvalue(res, 0, 3);
    c.params    = PQgetvalue(res, 0, 4);
    c.status    = PQgetvalue(res, 0, 5);
    c.createdAt = PQgetisnull(res, 0, 6) ? "" : PQgetvalue(res, 0, 6);

    PQclear(res);
    return c;
}

std::optional<PolicyCommand> PostgresClient::getPolicyCommandByCommandId(
    const std::string& commandId) {
    std::lock_guard<std::recursive_mutex> lock(dbMutex_);
    if (!isConnected() && !reconnect()) return std::nullopt;

    const char* paramValues[1] = { commandId.c_str() };
    PGresult* res = PQexecParams(conn_,
        "SELECT id, agent_id, command_id, policy_type, policy_data::text, status, created_at::text "
        "FROM policy_commands WHERE command_id = $1",
        1, nullptr, paramValues, nullptr, nullptr, 0);

    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0) {
        PQclear(res);
        return std::nullopt;
    }

    PolicyCommand c;
    c.id         = std::stoi(PQgetvalue(res, 0, 0));
    c.agentId    = PQgetvalue(res, 0, 1);
    c.commandId  = PQgetvalue(res, 0, 2);
    c.policyType = PQgetvalue(res, 0, 3);
    c.policyData = PQgetvalue(res, 0, 4);
    c.status     = PQgetvalue(res, 0, 5);
    c.createdAt  = PQgetisnull(res, 0, 6) ? "" : PQgetvalue(res, 0, 6);

    PQclear(res);
    return c;
}

std::vector<PostgresClient::StatusReportRecord> PostgresClient::getLatestStatusReports(
    const std::string& agentId, int limit) {
    std::lock_guard<std::recursive_mutex> lock(dbMutex_);
    std::vector<StatusReportRecord> reports;
    if (!isConnected() && !reconnect()) return reports;

    std::string lim = std::to_string(limit);
    const char* paramValues[2] = { agentId.c_str(), lim.c_str() };

    PGresult* res = PQexecParams(conn_,
        "SELECT agent_id, report_type, report_data::text, created_at::text "
        "FROM agent_status_reports WHERE agent_id = $1 "
        "ORDER BY created_at DESC LIMIT $2",
        2, nullptr, paramValues, nullptr, nullptr, 0);

    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        PQclear(res);
        return reports;
    }

    int rows = PQntuples(res);
    reports.reserve(rows);
    for (int i = 0; i < rows; i++) {
        StatusReportRecord r;
        r.agentId    = PQgetvalue(res, i, 0);
        r.reportType = PQgetvalue(res, i, 1);
        r.reportData = PQgetvalue(res, i, 2);
        r.createdAt  = PQgetisnull(res, i, 3) ? "" : PQgetvalue(res, i, 3);
        reports.push_back(std::move(r));
    }

    PQclear(res);
    return reports;
}

nlohmann::json PostgresClient::getAuditLog(const std::string& agentId,
                                             int limit, int offset) {
    std::lock_guard<std::recursive_mutex> lock(dbMutex_);
    nlohmann::json entries = nlohmann::json::array();
    if (!isConnected() && !reconnect()) return entries;

    // Unified audit view: combine both tables
    std::string sql;
    if (agentId.empty()) {
        sql = "SELECT * FROM ("
              "  SELECT 'policy' AS command_class, command_id, agent_id, "
              "    policy_type AS verb_or_type, status, "
              "    COALESCE(ack_status, '') AS ack_status, "
              "    COALESCE(ack_message, '') AS result_or_message, "
              "    created_at::text, COALESCE(dispatched_at::text, '') AS dispatched_at, "
              "    COALESCE(ack_at::text, '') AS ack_at "
              "  FROM policy_commands "
              "  UNION ALL "
              "  SELECT 'module', command_id, agent_id, "
              "    verb, status, "
              "    COALESCE(ack_status, ''), "
              "    COALESCE(result_payload, ''), "
              "    created_at::text, COALESCE(dispatched_at::text, ''), "
              "    COALESCE(ack_at::text, '') "
              "  FROM module_commands "
              ") AS combined "
              "ORDER BY created_at DESC "
              "LIMIT " + std::to_string(limit) + " OFFSET " + std::to_string(offset);
    } else {
        sql = "SELECT * FROM ("
              "  SELECT 'policy' AS command_class, command_id, agent_id, "
              "    policy_type AS verb_or_type, status, "
              "    COALESCE(ack_status, '') AS ack_status, "
              "    COALESCE(ack_message, '') AS result_or_message, "
              "    created_at::text, COALESCE(dispatched_at::text, '') AS dispatched_at, "
              "    COALESCE(ack_at::text, '') AS ack_at "
              "  FROM policy_commands WHERE agent_id = '" + agentId + "' "
              "  UNION ALL "
              "  SELECT 'module', command_id, agent_id, "
              "    verb, status, "
              "    COALESCE(ack_status, ''), "
              "    COALESCE(result_payload, ''), "
              "    created_at::text, COALESCE(dispatched_at::text, ''), "
              "    COALESCE(ack_at::text, '') "
              "  FROM module_commands WHERE agent_id = '" + agentId + "' "
              ") AS combined "
              "ORDER BY created_at DESC "
              "LIMIT " + std::to_string(limit) + " OFFSET " + std::to_string(offset);
    }

    PGresult* res = PQexec(conn_, sql.c_str());
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        lastError_ = "getAuditLog failed: " + std::string(PQerrorMessage(conn_));
        LOG_ERROR("{}", lastError_);
        PQclear(res);
        return entries;
    }

    int rows = PQntuples(res);
    for (int i = 0; i < rows; i++) {
        nlohmann::json entry;
        entry["command_class"]      = PQgetvalue(res, i, 0);
        entry["command_id"]         = PQgetvalue(res, i, 1);
        entry["agent_id"]           = PQgetvalue(res, i, 2);
        entry["verb_or_type"]       = PQgetvalue(res, i, 3);
        entry["status"]             = PQgetvalue(res, i, 4);
        entry["ack_status"]         = PQgetvalue(res, i, 5);
        entry["result_or_message"]  = PQgetvalue(res, i, 6);
        entry["created_at"]         = PQgetvalue(res, i, 7);
        entry["dispatched_at"]      = PQgetvalue(res, i, 8);
        entry["ack_at"]             = PQgetvalue(res, i, 9);
        entries.push_back(std::move(entry));
    }

    PQclear(res);
    return entries;
}

// ─────────────────────────────────────────────────────────────
// Operator Authentication
// ─────────────────────────────────────────────────────────────

bool PostgresClient::authenticateOperator(const std::string& username,
                                           const std::string& password) {
    std::lock_guard<std::recursive_mutex> lock(dbMutex_);
    if (!isConnected() && !reconnect()) return false;

    // Try the operators table first
    const char* paramValues[2] = { username.c_str(), password.c_str() };
    PGresult* res = PQexecParams(conn_,
        "SELECT 1 FROM operators WHERE username = $1 AND password_hash = crypt($2, password_hash)",
        2, nullptr, paramValues, nullptr, nullptr, 0);

    if (PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0) {
        PQclear(res);
        return true;
    }
    PQclear(res);

    // Fallback: hardcoded bootstrap admin (for initial setup before operators table exists)
    if (username == "admin" && password == "RiskNoX@2024") {
        LOG_WARN("Operator '{}' authenticated via bootstrap credentials. "
                 "Create operators table for production use.", username);
        return true;
    }

    return false;
}

bool PostgresClient::recordModuleCommandWithOperator(const std::string& agentId,
                                                      const std::string& commandId,
                                                      const std::string& verb,
                                                      const std::string& paramsJson,
                                                      const std::string& initiatedBy,
                                                      bool pending) {
    std::lock_guard<std::recursive_mutex> lock(dbMutex_);
    if (!isConnected() && !reconnect()) return false;

    const char* paramValues[5] = {
        agentId.c_str(),
        commandId.c_str(),
        verb.c_str(),
        paramsJson.c_str(),
        initiatedBy.c_str()
    };

    // Try INSERT with initiated_by column; if column doesn't exist, fall back
    const char* sql = pending
        ? "INSERT INTO module_commands "
          "  (agent_id, command_id, verb, params, status, initiated_by) "
          "VALUES ($1, $2, $3, $4::jsonb, 'pending', $5)"
        : "INSERT INTO module_commands "
          "  (agent_id, command_id, verb, params, status, dispatched_at, initiated_by) "
          "VALUES ($1, $2, $3, $4::jsonb, 'sent', NOW(), $5)";

    PGresult* res = PQexecParams(conn_, sql, 5, nullptr, paramValues, nullptr, nullptr, 0);

    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        // Column may not exist yet; fall back to standard record without operator
        PQclear(res);
        LOG_DEBUG("initiated_by column not found, falling back to standard insert");
        return recordModuleCommand(agentId, commandId, verb, paramsJson, pending);
    }

    PQclear(res);
    LOG_DEBUG("Recorded module command with operator: agent={} verb={} by={}",
              agentId, verb, initiatedBy);
    return true;
}

} // namespace ResolutePulse

nlohmann::json PostgresClient::getGlobalPolicySummary() {
    std::lock_guard<std::recursive_mutex> lock(dbMutex_);
    nlohmann::json summary;
    summary["web"] = nlohmann::json::array();
    summary["apps"] = nlohmann::json::array();

    if (!isConnected() && !reconnect()) return summary;

    // Get latest module_status report for every agent
    const char* sql = 
        "WITH latest_reports AS ("
        "    SELECT DISTINCT ON (agent_id) agent_id, report_data "
        "    FROM agent_status_reports "
        "    WHERE report_type = 'module_status' "
        "    ORDER BY agent_id, created_at DESC "
        ") "
        "SELECT agent_id, report_data FROM latest_reports";

    PGresult* res = PQexec(conn_, sql);
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        lastError_ = "getGlobalPolicySummary failed: " + std::string(PQerrorMessage(conn_));
        LOG_ERROR("{}", lastError_);
        PQclear(res);
        return summary;
    }

    std::map<std::string, std::set<std::string>> webBlocks; // url -> set of agentIds
    struct AppInfo {
        std::string name;
        std::set<std::string> agents;
        int totalKills = 0;
    };
    std::map<std::string, AppInfo> appBlocks; // exe -> AppInfo

    int rows = PQntuples(res);
    for (int i = 0; i < rows; i++) {
        std::string agentId = PQgetvalue(res, i, 0);
        std::string dataStr = PQgetvalue(res, i, 1);
        auto data = nlohmann::json::parse(dataStr, nullptr, false);

        if (data.is_object()) {
            // Web
            if (data.contains("web_blocking") && data["web_blocking"].contains("blockedUrls")) {
                for (const auto& u : data["web_blocking"]["blockedUrls"]) {
                    std::string url = u.value("url", "");
                    if (!url.empty()) webBlocks[url].insert(agentId);
                }
            }
            // Apps
            if (data.contains("software_blocking") && data["software_blocking"].contains("blockedApps")) {
                for (const auto& a : data["software_blocking"]["blockedApps"]) {
                    std::string exe = a.value("executable", "");
                    if (!exe.empty()) {
                        auto& info = appBlocks[exe];
                        info.name = a.value("name", exe);
                        info.agents.insert(agentId);
                        info.totalKills += a.value("kills", 0);
                    }
                }
            }
        }
    }
    PQclear(res);

    // Format output
    for (auto const& [url, agents] : webBlocks) {
        nlohmann::json item;
        item["url"] = url;
        item["agents"] = agents;
        item["agent_count"] = agents.size();
        summary["web"].push_back(item);
    }

    for (auto const& [exe, info] : appBlocks) {
        nlohmann::json item;
        item["executable"] = exe;
        item["name"] = info.name;
        item["agents"] = info.agents;
        item["agent_count"] = info.agents.size();
        item["total_kills"] = info.totalKills;
        summary["apps"].push_back(item);
    }

    return summary;
}
