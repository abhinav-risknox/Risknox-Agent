#pragma once

#include "manager/ca/CertificateAuthority.h"
#include "manager/db/PostgresClient.h"
#include "common/Protocol.h"

#include <string>

typedef struct ssl_st SSL;

namespace ResolutePulse {

class ManagerServer;  // forward declare

class AgentHandler {
public:
    AgentHandler(CertificateAuthority& ca, PostgresClient& db, ManagerServer* server = nullptr);
    ~AgentHandler() = default;

    // Handle a connected client - reads messages and dispatches
    // Returns true if connection should stay open, false if it should close
    bool handleConnection(SSL* ssl, const std::string& clientAddr, bool hasClientCert);

    // Returns the last successfully authenticated agent_id (empty until first heartbeat/license)
    const std::string& getLastAgentId() const { return lastAgentId_; }

    // Push a policy update to a connected agent
    bool pushPolicyUpdate(SSL* ssl, const std::string& agentId,
                          const std::string& policyType,
                          const nlohmann::json& policyData);

    // Push a MODULE_COMMAND to a connected agent
    // commandId is echoed back in the MODULE_COMMAND_RESULT for audit correlation.
    bool pushModuleCommand(SSL* ssl, const std::string& agentId,
                           const std::string& commandId,
                           const std::string& verb,
                           const nlohmann::json& params);

private:
    // Handle registration request (one-way TLS - no client cert)
    void handleRegistration(SSL* ssl, const std::string& clientAddr,
                            const std::string& payload);

    // Handle event batch from authenticated agent
    void handleEventBatch(SSL* ssl, const std::string& agentId,
                          const std::string& payload);

    // Handle heartbeat from authenticated agent
    void handleHeartbeat(SSL* ssl, const std::string& agentId,
                         const std::string& payload);

    // Handle license check from authenticated agent
    void handleLicenseCheck(SSL* ssl, const std::string& agentId,
                            const std::string& payload);

    // Handle status report from authenticated agent
    void handleStatusReport(SSL* ssl, const std::string& agentId,
                            const std::string& payload);

    // Read exactly N bytes from SSL
    bool sslReadExact(SSL* ssl, void* buffer, size_t length);

    // Send a complete message (header + payload) over SSL
    bool sslSendMessage(SSL* ssl, MessageType type, const std::string& jsonPayload);

    // Extract agent_id CN from peer certificate
    std::string extractAgentIdFromCert(SSL* ssl);

    // Validate public key format
    bool validatePublicKey(const std::string& publicKeyPem);

    // Determine certificate expiry based on license
    int calculateExpiryDays(const std::string& agentId);

    CertificateAuthority& ca_;
    PostgresClient&       db_;
    ManagerServer*        server_ = nullptr;  // optional, may be nullptr in tests
    std::string           lastAgentId_;  // populated after first authenticated message
};

} // namespace ResolutePulse

