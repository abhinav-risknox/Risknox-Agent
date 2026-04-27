#include "AgentHandler.h"
#include "ManagerServer.h"
#include "utils/Logger.h"

#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/bio.h>

#include <nlohmann/json.hpp>

namespace ResolutePulse {

AgentHandler::AgentHandler(CertificateAuthority& ca, PostgresClient& db, ManagerServer* server)
    : ca_(ca), db_(db), server_(server) {}

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

        case MessageType::STATUS_REPORT: {
            if (!hasClientCert) {
                LOG_WARN("Status report from unauthenticated client {}", clientAddr);
                return false;
            }
            std::string agentId = extractAgentIdFromCert(ssl);
            handleStatusReport(ssl, agentId, payload);
            break;
        }

        case MessageType::POLICY_UPDATE_ACK: {
            if (!hasClientCert) {
                LOG_WARN("Policy ack from unauthenticated client {}", clientAddr);
                return false;
            }
            std::string agentId = extractAgentIdFromCert(ssl);
            LOG_INFO("Policy update acknowledged by agent: {}", agentId);
            break;
        }

        case MessageType::MODULE_COMMAND_RESULT: {
            if (!hasClientCert) {
                LOG_WARN("MODULE_COMMAND_RESULT from unauthenticated client {}", clientAddr);
                return false;
            }
            std::string agentId = extractAgentIdFromCert(ssl);
            try {
                auto j      = nlohmann::json::parse(payload);
                auto result = j.get<ModuleCommandResult>();
                LOG_INFO("MODULE_COMMAND_RESULT: agent={} verb={} commandId={} status={}",
                         agentId, result.verb, result.commandId, result.status);
                // Persist the ACK outcome to the audit log
                db_.updateCommandAck(result.commandId, result.status, result.output);
            } catch (const std::exception& e) {
                LOG_ERROR("Failed to parse MODULE_COMMAND_RESULT from {}: {}", agentId, e.what());
            }
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

    // Check for existing agent - allow re-registration if cert expired or agent inactive
    if (db_.agentExists(request.agentId)) {
        auto existingAgent = db_.getAgent(request.agentId);
        bool allowReReg = false;

        if (existingAgent.has_value()) {
            // Allow re-registration if agent is INACTIVE
            if (existingAgent->status == "INACTIVE" || existingAgent->status == "EXPIRED") {
                LOG_INFO("Agent {} is {}, allowing re-registration", request.agentId, existingAgent->status);
                allowReReg = true;
            }

            // Allow re-registration if their certificate is revoked or missing
            if (!allowReReg) {
                auto certRecord = db_.getCertificate(request.agentId);
                if (!certRecord.has_value() || certRecord->revoked) {
                    LOG_INFO("Agent {} certificate is revoked/missing, allowing re-registration", request.agentId);
                    allowReReg = true;
                }
            }
        }

        if (!allowReReg) {
            LOG_WARN("Duplicate registration attempt (active cert): {}", request.agentId);

            RegisterReject reject;
            reject.status = "rejected";
            reject.agentId = request.agentId;
            reject.reason = "Agent already registered with valid certificate";
            reject.errorCode = 409;
            sslSendMessage(ssl, MessageType::REGISTER_REJECT,
                           nlohmann::json(reject).dump());
            return;
        }

        // Revoke old certificate before re-issuing
        auto oldCert = db_.getCertificate(request.agentId);
        if (oldCert.has_value() && !oldCert->revoked) {
            LOG_INFO("Revoking old certificate for re-registering agent: {}", request.agentId);
            ca_.revokeCertificate(oldCert->serialNumber);
            db_.revokeCertificate(oldCert->serialNumber, "Re-registration");
        }

        // Update existing agent record instead of inserting
        db_.updateAgentStatus(request.agentId, "ACTIVE");
        LOG_INFO("Agent {} re-registered successfully", request.agentId);
    } else {

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
    } // end else (new registration)

    // Calculate certificate expiry - always 365 days (identity only, decoupled from license)
    int expiryDays = calculateExpiryDays(request.agentId);

    // Derive isTrial from license record, not cert duration
    auto agentLicense = db_.getLicense(request.agentId);
    bool isTrial = !agentLicense.has_value() || agentLicense->licenseType == "TRIAL";

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

    // Track the agent ID so ManagerServer can register this session
    if (lastAgentId_.empty()) lastAgentId_ = agentId;

    // Send heartbeat ack - heartbeat is purely a health signal.
    // License enforcement is via the dedicated LICENSE_CHECK path only.
    HeartbeatAck ack;
    ack.agentId = agentId;
    ack.licenseValid = true;  // Not checked here; LICENSE_CHECK handles this
    ack.licenseMessage = "OK";
    ack.configChanged = false;

    time_t now = time(nullptr);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));
    ack.timestamp = buf;

    sslSendMessage(ssl, MessageType::HEARTBEAT_ACK,
                   nlohmann::json(ack).dump());

    // Drain any pending offline commands for this agent
    auto pending = db_.fetchPendingCommands(agentId);
    for (auto& cmd : pending) {
        LOG_INFO("Delivering offline command id={} type={} to agent {}",
                 cmd.id, cmd.policyType, agentId);
        auto policyData = nlohmann::json::parse(cmd.policyData, nullptr, false);
        if (policyData.is_discarded()) {
            db_.updateCommandStatus(cmd.id, "failed", "Invalid JSON in policy_data");
            continue;
        }
        bool ok = pushPolicyUpdate(ssl, agentId, cmd.policyType, policyData);
        db_.updateCommandStatus(cmd.id, ok ? "sent" : "failed",
                                 ok ? "" : "SSL write error during offline drain");
    }
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
        result.licenseType = "NONE";
        result.licenseExpiry = "";
    } else {
        result.licenseType = license->licenseType;
        result.licenseExpiry = license->validUntil;

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
    // Always issue 365-day identity certs.
    // License enforcement is via heartbeat response only.
    (void)agentId;  // unused - cert duration no longer depends on license
    return 365;
}

void AgentHandler::handleStatusReport(SSL* ssl, const std::string& agentId,
                                       const std::string& payload) {
    LOG_INFO("Status report from agent: {}", agentId);
    db_.updateLastSeen(agentId);

    try {
        auto j = nlohmann::json::parse(payload);
        auto report = j.get<StatusReport>();

        LOG_INFO("Status report type={} from agent {}", report.reportType, agentId);
        LOG_DEBUG("Status data: {}", report.reportData);

        // Store status report in DB for dashboard queries
        db_.storeStatusReport(agentId, report.reportType, report.reportData);

    } catch (const std::exception& e) {
        LOG_ERROR("Failed to parse status report from {}: {}", agentId, e.what());
    }

    // Send acknowledgment
    StatusReportAck ack;
    ack.agentId = agentId;
    ack.received = true;

    time_t now = time(nullptr);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));
    ack.timestamp = buf;

    sslSendMessage(ssl, MessageType::STATUS_REPORT_ACK,
                   nlohmann::json(ack).dump());
}

bool AgentHandler::pushPolicyUpdate(SSL* ssl, const std::string& agentId,
                                     const std::string& policyType,
                                     const nlohmann::json& policyData) {
    LOG_INFO("Pushing policy update to agent {}: type={}", agentId, policyType);

    PolicyUpdate update;
    update.agentId = agentId;
    update.policyType = policyType;
    update.policyData = policyData.dump();
    update.policyVersion = "1";

    time_t now = time(nullptr);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));
    update.timestamp = buf;

    return sslSendMessage(ssl, MessageType::POLICY_UPDATE,
                          nlohmann::json(update).dump());
}

bool AgentHandler::pushModuleCommand(SSL* ssl, const std::string& agentId,
                                      const std::string& commandId,
                                      const std::string& verb,
                                      const nlohmann::json& params) {
    LOG_INFO("Pushing MODULE_COMMAND to agent {}: verb={} commandId={}",
             agentId, verb, commandId);

    ModuleCommand cmd;
    cmd.commandId = commandId;
    cmd.agentId   = agentId;
    cmd.verb      = verb;
    cmd.params    = params;

    time_t now = time(nullptr);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));
    cmd.timestamp = buf;

    return sslSendMessage(ssl, MessageType::MODULE_COMMAND,
                          nlohmann::json(cmd).dump());
}

} // namespace ResolutePulse

