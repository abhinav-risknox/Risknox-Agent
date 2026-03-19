#include "AgentHandler.h"
#include "utils/Logger.h"

#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/bio.h>

#include <nlohmann/json.hpp>

namespace ResolutePulse {

AgentHandler::AgentHandler(CertificateAuthority& ca, PostgresClient& db)
    : ca_(ca), db_(db) {}

bool AgentHandler::handleConnection(SSL* ssl, const std::string& clientAddr, bool hasClientCert) {
    // Read message header
    uint8_t headerBuf[MESSAGE_HEADER_SIZE];
    if (!sslReadExact(ssl, headerBuf, MESSAGE_HEADER_SIZE)) {
        return false;
    }

    MessageHeader header;
    if (!deserializeHeader(headerBuf, header)) {
        return false;
    }

    // Read payload
    if (header.payloadLength == 0 || header.payloadLength > 1024 * 1024) {
        return false;
    }

    std::string payload(header.payloadLength, '\0');
    if (!sslReadExact(ssl, &payload[0], header.payloadLength)) {
        return false;
    }

    auto msgType = static_cast<MessageType>(header.type);

    switch (msgType) {
        case MessageType::REGISTER_REQUEST:
            handleRegistration(ssl, clientAddr, payload);
            break;

        case MessageType::EVENT_BATCH: {
            LOG_WARN("Received EVENT_BATCH over management channel from {}. This is not supported.", clientAddr);
            break;
        }

        case MessageType::HEARTBEAT: {
            if (!hasClientCert) {
                LOG_WARN("Heartbeat from unauthenticated client {}", clientAddr);
                return false;
            }
            std::string agentId = extractAgentIdFromCert(ssl);
            handleHeartbeat(ssl, agentId, payload);
            break;
        }

        case MessageType::LICENSE_CHECK: {
            if (!hasClientCert) {
                LOG_WARN("License check from unauthenticated client {}", clientAddr);
                return false;
            }
            std::string agentId = extractAgentIdFromCert(ssl);
            handleLicenseCheck(ssl, agentId, payload);
            break;
        }

        default:
            LOG_WARN("Unknown message type 0x{:02X} from {}", header.type, clientAddr);
            return false;
    }

    return true;
}

void AgentHandler::handleRegistration(SSL* ssl, const std::string& clientAddr,
                                       const std::string& payload) {
    LOG_INFO("Processing registration request from {}", clientAddr);

    // Parse request
    RegisterRequest request;
    try {
        auto j = nlohmann::json::parse(payload);
        request = j.get<RegisterRequest>();
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to parse registration request: {}", e.what());

        RegisterReject reject;
        reject.status = "rejected";
        reject.reason = "Invalid request format";
        reject.errorCode = 400;
        sslSendMessage(ssl, MessageType::REGISTER_REJECT,
                       nlohmann::json(reject).dump());
        return;
    }

    LOG_INFO("Registration request: agent_id={}, hostname={}, os={}",
             request.agentId, request.hostname, request.osType);

    // Validate public key
    if (!validatePublicKey(request.publicKeyPem)) {
        LOG_ERROR("Invalid public key from agent: {}", request.agentId);

        RegisterReject reject;
        reject.status = "rejected";
        reject.agentId = request.agentId;
        reject.reason = "Invalid public key format";
        reject.errorCode = 401;
        sslSendMessage(ssl, MessageType::REGISTER_REJECT,
                       nlohmann::json(reject).dump());
        return;
    }

    // Check for duplicate registration
    if (db_.agentExists(request.agentId)) {
        LOG_WARN("Duplicate registration attempt: {}", request.agentId);

        RegisterReject reject;
        reject.status = "rejected";
        reject.agentId = request.agentId;
        reject.reason = "Agent already registered";
        reject.errorCode = 409;
        sslSendMessage(ssl, MessageType::REGISTER_REJECT,
                       nlohmann::json(reject).dump());
        return;
    }

    // Insert agent record
    AgentRecord agent;
    agent.agentId      = request.agentId;
    agent.hostname     = request.hostname;
    agent.osType       = request.osType;
    agent.osVersion    = request.osVersion;
    agent.agentVersion = request.agentVersion;
    agent.ipAddress    = clientAddr;

    if (!db_.insertAgent(agent)) {
        LOG_ERROR("Failed to insert agent record: {}", request.agentId);

        RegisterReject reject;
        reject.status = "rejected";
        reject.agentId = request.agentId;
        reject.reason = "Internal server error";
        reject.errorCode = 500;
        sslSendMessage(ssl, MessageType::REGISTER_REJECT,
                       nlohmann::json(reject).dump());
        return;
    }

    // Calculate certificate expiry based on license
    int expiryDays = calculateExpiryDays(request.agentId);
    bool isTrial = (expiryDays <= 7);

    // Issue certificate
    auto issued = ca_.issueCertificate(request.agentId, request.publicKeyPem, expiryDays);
    if (issued.certificatePem.empty()) {
        LOG_ERROR("Failed to issue certificate for: {}", request.agentId);

        RegisterReject reject;
        reject.status = "rejected";
        reject.agentId = request.agentId;
        reject.reason = "Certificate issuance failed";
        reject.errorCode = 500;
        sslSendMessage(ssl, MessageType::REGISTER_REJECT,
                       nlohmann::json(reject).dump());
        return;
    }

    // Store certificate in DB
    CertificateRecord certRecord;
    certRecord.serialNumber   = issued.serialNumber;
    certRecord.agentId        = request.agentId;
    certRecord.certificatePem = issued.certificatePem;
    certRecord.expiresAt      = issued.expiresAt;

    if (!db_.insertCertificate(certRecord)) {
        LOG_ERROR("Failed to store certificate for: {}", request.agentId);
    }

    // Update agent with cert serial
    db_.updateAgentCertSerial(request.agentId, issued.serialNumber);

    // If no license exists, create a default TRIAL license
    if (!db_.getLicense(request.agentId).has_value()) {
        LicenseRecord lr;
        lr.agentId = request.agentId;
        lr.licenseKey = "TRIAL-" + request.agentId.substr(0, 8);
        lr.licenseType = "TRIAL";
        
        // Use current time and +7 days
        time_t now = time(nullptr);
        char buf[64];
        strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));
        lr.validFrom = buf;
        
        time_t future = now + (7 * 24 * 60 * 60);
        strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", gmtime(&future));
        lr.validUntil = buf;
        
        db_.insertLicense(lr);
    }

    // Send acceptance response
    RegisterAccept accept;
    accept.status         = "authorized";
    accept.agentId        = request.agentId;
    accept.certificatePem = issued.certificatePem;
    accept.caCertPem      = ca_.getCACertPem();
    accept.expiresAt      = issued.expiresAt;
    accept.trial          = isTrial;

    sslSendMessage(ssl, MessageType::REGISTER_ACCEPT,
                   nlohmann::json(accept).dump());

    LOG_INFO("Agent registered successfully: {} (serial={}, trial={})",
             request.agentId, issued.serialNumber, isTrial);
}

// Event batching handled by telemetry stream exclusively now

void AgentHandler::handleHeartbeat(SSL* ssl, const std::string& agentId,
                                    const std::string& payload) {
    LOG_DEBUG("Heartbeat from agent: {}", agentId);
    db_.updateLastSeen(agentId);

    // Send heartbeat ack
    HeartbeatAck ack;
    ack.agentId = agentId;

    // Get license status
    auto license = db_.getLicense(agentId);
    if (!license) {
        LOG_WARN("No license found for agent: {}", agentId);
        ack.licenseValid = false;
        ack.licenseMessage = "No active license found";
    } else {
        // Check expiry
        time_t now = time(nullptr);
        // Simple string comparison for ISO8601 (works if formats are identical)
        char nowBuf[64];
        strftime(nowBuf, sizeof(nowBuf), "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));
        std::string nowStr = nowBuf;

        if (license->validUntil < nowStr) {
            LOG_WARN("License expired for agent: {} (Expired at {})", agentId, license->validUntil);
            ack.licenseValid = false;
            ack.licenseMessage = "License expired";
        } else {
            ack.licenseValid = true;
            ack.licenseMessage = "License active";
        }
    }

    // Get current time for ack
    time_t now = time(nullptr);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));
    ack.timestamp = buf;
    ack.configChanged = false;

    sslSendMessage(ssl, MessageType::HEARTBEAT_ACK,
                   nlohmann::json(ack).dump());
}

void AgentHandler::handleLicenseCheck(SSL* ssl, const std::string& agentId,
                                      const std::string& payload) {
    LOG_DEBUG("License check from agent: {}", agentId);
    db_.updateLastSeen(agentId);

    LicenseCheckResult result;
    result.agentId = agentId;

    auto license = db_.getLicense(agentId);
    if (!license) {
        LOG_WARN("No license found for agent: {}", agentId);
        result.licenseValid = false;
        result.licenseMessage = "No active license found";
    } else {
        time_t now = time(nullptr);
        char nowBuf[64];
        strftime(nowBuf, sizeof(nowBuf), "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));
        std::string nowStr = nowBuf;

        if (license->validUntil < nowStr) {
            LOG_WARN("License expired for agent: {} (Expired at {})", agentId, license->validUntil);
            result.licenseValid = false;
            result.licenseMessage = "License expired";
        } else {
            result.licenseValid = true;
            result.licenseMessage = "License active";
        }
    }

    time_t now = time(nullptr);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));
    result.timestamp = buf;

    sslSendMessage(ssl, MessageType::LICENSE_CHECK_RESULT,
                   nlohmann::json(result).dump());
}

// ─────────────────────────────────────────────────────────────
// Helper methods
// ─────────────────────────────────────────────────────────────

bool AgentHandler::sslReadExact(SSL* ssl, void* buffer, size_t length) {
    size_t total = 0;
    auto* buf = static_cast<char*>(buffer);

    while (total < length) {
        int n = SSL_read(ssl, buf + total, static_cast<int>(length - total));
        if (n <= 0) {
            return false;
        }
        total += n;
    }
    return true;
}

bool AgentHandler::sslSendMessage(SSL* ssl, MessageType type, const std::string& jsonPayload) {
    MessageHeader header;
    header.type = static_cast<uint8_t>(type);
    header.payloadLength = static_cast<uint32_t>(jsonPayload.size());

    uint8_t headerBuf[MESSAGE_HEADER_SIZE];
    serializeHeader(header, headerBuf);

    // Send header
    if (SSL_write(ssl, headerBuf, MESSAGE_HEADER_SIZE) <= 0) {
        LOG_ERROR("Failed to send message header");
        return false;
    }

    // Send payload
    if (SSL_write(ssl, jsonPayload.c_str(), static_cast<int>(jsonPayload.size())) <= 0) {
        LOG_ERROR("Failed to send message payload");
        return false;
    }

    return true;
}

std::string AgentHandler::extractAgentIdFromCert(SSL* ssl) {
    X509* cert = SSL_get_peer_certificate(ssl);
    if (!cert) return "";

    X509_NAME* subject = X509_get_subject_name(cert);
    char cn[256] = {};
    X509_NAME_get_text_by_NID(subject, NID_commonName, cn, sizeof(cn));

    X509_free(cert);
    return std::string(cn);
}

bool AgentHandler::validatePublicKey(const std::string& publicKeyPem) {
    BIO* bio = BIO_new_mem_buf(publicKeyPem.data(), static_cast<int>(publicKeyPem.size()));
    if (!bio) return false;

    EVP_PKEY* key = PEM_read_bio_PUBKEY(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);

    if (!key) return false;

    EVP_PKEY_free(key);
    return true;
}

int AgentHandler::calculateExpiryDays(const std::string& agentId) {
    auto license = db_.getLicense(agentId);
    if (license.has_value()) {
        // Parse valid_until and calculate days remaining
        // For simplicity, use a fixed value based on license type
        if (license->licenseType == "ENTERPRISE") {
            return 365;
        } else if (license->licenseType == "STANDARD") {
            return 180;
        }
    }
    // No license or TRIAL → 7-day trial
    return 7;
}

} // namespace ResolutePulse
