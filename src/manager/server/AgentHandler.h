#pragma once

#include "manager/ca/CertificateAuthority.h"
#include "manager/db/PostgresClient.h"
#include "common/Protocol.h"

#include <string>

typedef struct ssl_st SSL;

namespace ResolutePulse {

class AgentHandler {
public:
    AgentHandler(CertificateAuthority& ca, PostgresClient& db);
    ~AgentHandler() = default;

    // Handle a connected client — reads messages and dispatches
    void handleConnection(SSL* ssl, const std::string& clientAddr, bool hasClientCert);

private:
    // Handle registration request (one-way TLS — no client cert)
    void handleRegistration(SSL* ssl, const std::string& clientAddr,
                            const std::string& payload);

    // Handle event batch from authenticated agent
    void handleEventBatch(SSL* ssl, const std::string& agentId,
                          const std::string& payload);

    // Handle heartbeat from authenticated agent
    void handleHeartbeat(SSL* ssl, const std::string& agentId,
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
};

} // namespace ResolutePulse
