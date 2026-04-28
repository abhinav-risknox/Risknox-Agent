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
    if (conn_) {
        PQfinish(conn_);
        conn_ = nullptr;
        LOG_DEBUG("Disconnected from PostgreSQL");
    }
}

bool PostgresClient::isConnected() const {
    return conn_ != nullptr && PQstatus(conn_) == CONNECTION_OK;
}

bool PostgresClient::reconnect() {
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

} // namespace ResolutePulse
