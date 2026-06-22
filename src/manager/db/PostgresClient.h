#pragma once

#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <ctime>

#include <nlohmann/json.hpp>
#include <mutex>

// Forward declare libpq types to avoid including libpq-fe.h in the header
struct pg_conn;
typedef struct pg_conn PGconn;

namespace ResolutePulse {

// ─────────────────────────────────────────────────────────────
// Data structures for DB rows
// ─────────────────────────────────────────────────────────────

struct AgentRecord {
    int         id = 0;
    std::string agentId;
    std::string hostname;
    std::string osType;
    std::string osVersion;
    std::string agentVersion;
    std::string status;       // ACTIVE, INACTIVE, REVOKED, PENDING
    std::string certSerial;
    std::string registeredAt;
    std::string lastSeenAt;
    std::string ipAddress;
};

struct CertificateRecord {
    int         id = 0;
    std::string serialNumber;
    std::string agentId;
    std::string certificatePem;
    std::string issuedAt;
    std::string expiresAt;
    bool        revoked = false;
    std::string revokedAt;
    std::string revokeReason;
};

struct LicenseRecord {
    int         id = 0;
    std::string agentId;
    std::string licenseKey;
    std::string licenseType;   // TRIAL, STANDARD, ENTERPRISE
    std::string validFrom;
    std::string validUntil;
    int         maxAgents = 1;
};

// ─────────────────────────────────────────────────────────────
// policy_commands table row
// ─────────────────────────────────────────────────────────────

struct PolicyCommand {
    int         id = 0;
    std::string agentId;
    std::string commandId;    // correlation ID for ACK matching
    std::string policyType;   // 'antivirus' | 'patch_management' | 'web_blocking' | 'software_blocking'
    std::string policyData;   // raw JSON string from JSONB column
    std::string status;       // pending | sent | acked | failed
    std::string createdAt;
};

// ─────────────────────────────────────────────────────────────
// module_commands table row
// ─────────────────────────────────────────────────────────────

struct ModuleCommandRecord {
    int         id = 0;
    std::string agentId;
    std::string commandId;    // correlation ID for MODULE_COMMAND_RESULT matching
    std::string verb;         // 'av_version' | 'av_update' | 'diagnostics' | 'agent_restart' | etc.
    std::string params;       // raw JSON string from JSONB column
    std::string status;       // pending | sent | acked | failed
    std::string ackStatus;
    std::string resultPayload;
    std::string createdAt;
};

// ─────────────────────────────────────────────────────────────
// PostgresClient
// ─────────────────────────────────────────────────────────────

class PostgresClient {
public:
    PostgresClient();
    ~PostgresClient();

    // Connect to PostgreSQL
    // @param connString - "host=localhost dbname=risknox user=postgres password=..."
    bool connect(const std::string& connString);

    // Disconnect from PostgreSQL
    void disconnect();

    // Check if connected
    bool isConnected() const;

    // ── Agent operations ──

    // Insert a new agent record
    bool insertAgent(const AgentRecord& agent);

    // Get an agent by agent_id
    std::optional<AgentRecord> getAgent(const std::string& agentId);

    // Update agent status
    bool updateAgentStatus(const std::string& agentId, const std::string& status);

    // Update agent cert_serial
    bool updateAgentCertSerial(const std::string& agentId, const std::string& certSerial);

    // Update last_seen_at timestamp
    bool updateLastSeen(const std::string& agentId);

    // Check if agent exists
    bool agentExists(const std::string& agentId);

    // Remove an agent
    bool removeAgent(const std::string& agentId);

    // ── Certificate operations ──

    // Insert a new certificate record
    bool insertCertificate(const CertificateRecord& cert);

    // Get active (non-revoked) certificate for an agent
    std::optional<CertificateRecord> getCertificate(const std::string& agentId);

    // Get certificate by serial number
    std::optional<CertificateRecord> getCertificateBySerial(const std::string& serialNumber);

    // Revoke a certificate
    bool revokeCertificate(const std::string& serialNumber, const std::string& reason);

    // Get all revoked certificate serial numbers (for CRL generation)
    std::vector<std::string> getRevokedSerials();

    // ── License operations ──

    // Get active license for an agent
    std::optional<LicenseRecord> getLicense(const std::string& agentId);

    // Insert a new license record
    bool insertLicense(const LicenseRecord& license);

    // ── Policy commands (policy_commands table) ──────────────────────────────

    // Insert a policy command row.
    // pending=false  → status='sent', dispatched_at=NOW() (online dispatch)
    // pending=true   → status='pending'                   (offline queue)
    bool recordPolicyCommand(const std::string& agentId,
                             const std::string& commandId,
                             const std::string& policyType,
                             const std::string& policyDataJson,
                             bool pending = false);

    // Mark dispatched_at when a pending policy command is sent live.
    bool markPolicyCommandDispatched(const std::string& commandId);

    // Close the audit loop when POLICY_UPDATE_ACK is received.
    bool ackPolicyCommand(const std::string& commandId,
                          bool applied,
                          const std::string& message);

    // Fetch pending policy rows for offline drain (called on agent reconnect).
    std::vector<PolicyCommand> fetchPendingPolicies(const std::string& agentId);

    // Mark a pending policy command as 'sent' or 'failed' after drain attempt.
    bool updatePolicyCommandStatus(const std::string& commandId,
                                   const std::string& status,
                                   const std::string& errorMsg = "");

    // ── Module commands (module_commands table) ──────────────────────────────

    // Insert a module command row.
    // pending=false  → status='sent', dispatched_at=NOW() (online dispatch)
    // pending=true   → status='pending'                   (offline queue)
    bool recordModuleCommand(const std::string& agentId,
                             const std::string& commandId,
                             const std::string& verb,
                             const std::string& paramsJson,
                             bool pending = false);

    // Record dispatched_at when a pending module command is sent live.
    bool markModuleCommandDispatched(const std::string& commandId);

    // Close the audit loop when MODULE_COMMAND_RESULT is received.
    bool ackModuleCommand(const std::string& commandId,
                          const std::string& ackStatus,
                          const std::string& resultPayload);

    // Fetch pending module command rows for offline drain.
    std::vector<ModuleCommandRecord> fetchPendingModuleCommands(const std::string& agentId);

    // Mark a pending module command as 'sent' or 'failed' after drain attempt.
    bool updateModuleCommandStatus(const std::string& commandId,
                                   const std::string& status,
                                   const std::string& errorMsg = "");

    // ── Status reports ──────────────────────────────────────────────────────

    // Store a status report from an agent (av_scan, patch_scan, patch_install, etc.)
    bool storeStatusReport(const std::string& agentId,
                           const std::string& reportType,
                           const std::string& reportDataJson);

    // ── REST API query methods ──────────────────────────────────────────────

    // List all agents (for dashboard)
    std::vector<AgentRecord> listAgents();

    // List module commands with optional agent filter and pagination
    std::vector<ModuleCommandRecord> listModuleCommands(const std::string& agentId = "",
                                                         int limit = 50, int offset = 0);

    // List policy commands with optional agent filter and pagination
    std::vector<PolicyCommand> listPolicyCommands(const std::string& agentId = "",
                                                    int limit = 50, int offset = 0);

    // Get a single module command by command_id
    std::optional<ModuleCommandRecord> getModuleCommandByCommandId(const std::string& commandId);

    // Get a single policy command by command_id
    std::optional<PolicyCommand> getPolicyCommandByCommandId(const std::string& commandId);

    // Status report record (returned by getLatestStatusReports)
    struct StatusReportRecord {
        std::string agentId;
        std::string reportType;
        std::string reportData;
        std::string createdAt;
    };

    // Get latest N status reports for an agent
    std::vector<StatusReportRecord> getLatestStatusReports(const std::string& agentId, int limit = 5);

    // Get unified audit log (both policy + module commands, newest first)
    nlohmann::json getAuditLog(const std::string& agentId = "", int limit = 100, int offset = 0);

    // Get global policy summary for all agents
    nlohmann::json getGlobalPolicySummary();

    // ── Operator authentication ─────────────────────────────────────────────

    // Authenticate an operator (falls back to hardcoded admin if no operators table)
    bool authenticateOperator(const std::string& username, const std::string& password);

    // Record a module command with operator identity
    bool recordModuleCommandWithOperator(const std::string& agentId,
                                          const std::string& commandId,
                                          const std::string& verb,
                                          const std::string& paramsJson,
                                          const std::string& initiatedBy,
                                          bool pending = false);

    // Get last error message
    const std::string& getLastError() const { return lastError_; }

private:
    // Attempt to reconnect using the stored connection string
    bool reconnect();

    PGconn*           conn_       = nullptr;
    std::string       lastError_;
    std::string       connString_; // stored for auto-reconnect
    mutable std::recursive_mutex dbMutex_;    // Protects conn_ for multi-threaded access
};

} // namespace ResolutePulse
