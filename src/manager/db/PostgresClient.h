#pragma once

#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <ctime>

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
// Agent command queue (offline delivery + audit)
// ─────────────────────────────────────────────────────────────

struct AgentCommand {
    int         id = 0;
    std::string agentId;
    std::string policyType;   // 'software_blocking' | 'web_blocking' | etc.
    std::string policyData;   // raw JSON string from JSONB column
    std::string status;
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

    // ── Agent command queue ──

    // Queue a command for offline delivery (inserted when agent is not connected)
    bool queueCommand(const std::string& agentId, const std::string& policyType,
                      const std::string& policyDataJson);

    // Fetch all pending commands for a specific agent (called on reconnect)
    std::vector<AgentCommand> fetchPendingCommands(const std::string& agentId);

    // Update command status: 'sent' | 'failed' | 'offline'
    bool updateCommandStatus(int commandId, const std::string& status,
                             const std::string& errorMessage = "");

    // ── Status reports ──

    // Store a status report from an agent (patch scan results, install outcomes, etc.)
    bool storeStatusReport(const std::string& agentId,
                           const std::string& reportType,
                           const std::string& reportDataJson);

    // Get last error message
    const std::string& getLastError() const { return lastError_; }

private:
    // Attempt to reconnect using the stored connection string
    bool reconnect();

    PGconn*     conn_       = nullptr;
    std::string lastError_;
    std::string connString_; // stored for auto-reconnect
};

} // namespace ResolutePulse
