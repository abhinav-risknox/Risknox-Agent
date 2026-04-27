#include "TlsSender.h"
#include "common/Protocol.h"
#include "utils/Logger.h"

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/pem.h>

#ifdef _WIN32
#include <ws2tcpip.h>
#endif

#include <nlohmann/json.hpp>
#include <sstream>
#include <thread>
#include <chrono>

namespace ResolutePulse {

TlsSender::TlsSender() = default;

TlsSender::~TlsSender() {
    disconnect();
    if (sslCtx_) SSL_CTX_free(sslCtx_);
#ifdef _WIN32
    if (wsaInitialized_) WSACleanup();
#endif
}

bool TlsSender::initialize(const std::string& host, int port,
                            const std::string& certPath,
                            const std::string& keyPath,
                            const std::string& caCertPath) {
    host_ = host;
    port_ = port;
    certPath_ = certPath;
    keyPath_ = keyPath;
    caCertPath_ = caCertPath;

    LOG_INFO("Initializing TLS sender: host='{}', port={}", host_, port_);

#ifdef _WIN32
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        lastError_ = "WSAStartup failed: " + std::to_string(result);
        LOG_ERROR("{}", lastError_);
        return false;
    }
    wsaInitialized_ = true;
#endif

    if (!createSSLContext()) {
        LOG_ERROR("Failed to create SSL context");
        return false;
    }

    // Try initial connection
    if (!connectWithMutualTLS()) {
        LOG_WARN("Initial mTLS connection failed, will retry on first send");
        return true;
    }

    LOG_INFO("TLS sender initialized with mTLS");
    return true;
}

bool TlsSender::createSSLContext() {
    const SSL_METHOD* method = TLS_client_method();
    sslCtx_ = SSL_CTX_new(method);
    if (!sslCtx_) {
        lastError_ = "Failed to create SSL context";
        return false;
    }

    SSL_CTX_set_min_proto_version(sslCtx_, TLS1_2_VERSION);

    // Load client certificate (agent.crt)
    if (SSL_CTX_use_certificate_file(sslCtx_, certPath_.c_str(), SSL_FILETYPE_PEM) <= 0) {
        lastError_ = "Failed to load agent certificate: " + certPath_;
        LOG_ERROR("{}", lastError_);
        return false;
    }

    // Load client private key (agent.key) with passphrase
    SSL_CTX_set_default_passwd_cb_userdata(sslCtx_,
        const_cast<void*>(static_cast<const void*>("ResolutePulse2024")));
    SSL_CTX_set_default_passwd_cb(sslCtx_, [](char* buf, int size, int, void* userdata) -> int {
        const char* pass = static_cast<const char*>(userdata);
        int len = static_cast<int>(strlen(pass));
        if (len > size) len = size;
        memcpy(buf, pass, len);
        return len;
    });

    if (SSL_CTX_use_PrivateKey_file(sslCtx_, keyPath_.c_str(), SSL_FILETYPE_PEM) <= 0) {
        lastError_ = "Failed to load agent private key: " + keyPath_;
        LOG_ERROR("{}", lastError_);
        return false;
    }

    // Verify private key matches certificate
    if (!SSL_CTX_check_private_key(sslCtx_)) {
        lastError_ = "Agent private key does not match certificate";
        LOG_ERROR("{}", lastError_);
        return false;
    }

    // Load CA certificate for server verification
    if (SSL_CTX_load_verify_locations(sslCtx_, caCertPath_.c_str(), nullptr) <= 0) {
        lastError_ = "Failed to load CA certificate: " + caCertPath_;
        LOG_ERROR("{}", lastError_);
        return false;
    }

    // Enable server certificate verification
    SSL_CTX_set_verify(sslCtx_, SSL_VERIFY_PEER, nullptr);

    return true;
}

bool TlsSender::connectWithMutualTLS() {
    std::lock_guard<std::mutex> lock(mutex_);

    // Close existing connection
    if (ssl_) {
        SSL_shutdown(ssl_);
        SSL_free(ssl_);
        ssl_ = nullptr;
    }
    if (socket_ != INVALID_SOCKET) {
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
    }
    connected_ = false;

    // Create TCP socket
    socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket_ == INVALID_SOCKET) {
        lastError_ = "Failed to create socket";
        LOG_ERROR("{}", lastError_);
        return false;
    }

    LOG_INFO("Attempting mTLS connection to host='{}', port={}", host_, port_);

    // Set timeout
#ifdef _WIN32
    DWORD timeout = CONNECT_TIMEOUT_MS;
    setsockopt(socket_, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));
    setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
#endif

    // Resolve host
    struct addrinfo hints = {}, *result = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    std::string portStr = std::to_string(port_);
    if (getaddrinfo(host_.c_str(), portStr.c_str(), &hints, &result) != 0) {
        lastError_ = "Failed to resolve host: " + host_;
        LOG_ERROR("{}", lastError_);
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
        return false;
    }

    if (::connect(socket_, result->ai_addr, static_cast<int>(result->ai_addrlen)) != 0) {
        freeaddrinfo(result);
        lastError_ = "TCP connection failed to " + host_ + ":" + portStr;
        LOG_ERROR("{}", lastError_);
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
        return false;
    }
    freeaddrinfo(result);

    // TLS handshake with mTLS
    ssl_ = SSL_new(sslCtx_);
    SSL_set_fd(ssl_, static_cast<int>(socket_));

    if (SSL_connect(ssl_) <= 0) {
        lastError_ = "mTLS handshake failed";
        LOG_ERROR("{}", lastError_);
        
        BIO* errBio = BIO_new(BIO_s_mem());
        ERR_print_errors(errBio);
        char* errData = nullptr;
        long errLen = BIO_get_mem_data(errBio, &errData);
        if (errLen > 0) {
            LOG_ERROR("OpenSSL Errors: {}", std::string(errData, errLen));
        }
        BIO_free(errBio);

        SSL_free(ssl_);
        ssl_ = nullptr;
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
        return false;
    }

    connected_ = true;
    reconnections_++;
    LOG_INFO("mTLS connection established to {}:{}", host_, port_);
    return true;
}

void TlsSender::disconnect() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (ssl_) {
        SSL_shutdown(ssl_);
        SSL_free(ssl_);
        ssl_ = nullptr;
    }
    if (socket_ != INVALID_SOCKET) {
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
    }
    connected_ = false;
}

SendResult TlsSender::sendBatch(const std::vector<Event>& events) {
    if (events.empty()) return SendResult::Success;

    if (!sslCtx_) {
        lastError_ = "TLS sender not initialized (SSL context is null)";
        failedSends_ += events.size();
        return SendResult::NetworkError;
    }

    // Build NDJSON batch (same format as TcpSender for Fluent Bit compatibility)
    std::ostringstream batch;
    for (const auto& event : events) {
        nlohmann::json j;
        j["c"] = event.channel;
        j["e"] = event.eventId;
        j["t"] = event.timestamp;
        j["x"] = event.data;
        batch << j.dump() << "\n";
    }

    std::string batchData = batch.str();

    // Build protocol message
    MessageHeader header;
    header.type = static_cast<uint8_t>(MessageType::EVENT_BATCH);
    header.payloadLength = static_cast<uint32_t>(batchData.size());

    uint8_t headerBuf[MESSAGE_HEADER_SIZE];
    serializeHeader(header, headerBuf);

    // Try to send with reconnection
    int attempts = 0;
    while (attempts < MAX_RECONNECT_ATTEMPTS) {
        if (!connected_.load()) {
            LOG_DEBUG("Not connected, attempting mTLS connection (attempt {})", attempts + 1);
            if (!connectWithMutualTLS()) {
                attempts++;
                std::this_thread::sleep_for(std::chrono::milliseconds(1000 * attempts));
                continue;
            }
        }

        // Send header
        if (!sslSendRaw(headerBuf, MESSAGE_HEADER_SIZE)) {
            LOG_WARN("Failed to send header, will retry");
            disconnect();
            attempts++;
            continue;
        }

        // Send payload
        if (!sslSendRaw(batchData.c_str(), batchData.size())) {
            LOG_WARN("Failed to send payload, will retry");
            disconnect();
            attempts++;
            continue;
        }

        eventsSent_ += events.size();
        bytesSent_ += MESSAGE_HEADER_SIZE + batchData.size();
        batchesSent_++;
        return SendResult::Success;
    }

    failedSends_ += events.size();
    lastError_ = "Failed to send batch after " + std::to_string(MAX_RECONNECT_ATTEMPTS) + " attempts";
    LOG_ERROR("{}", lastError_);
    return SendResult::NetworkError;
}

bool TlsSender::sslSendRaw(const void* data, size_t length) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!ssl_ || !connected_.load()) {
        lastError_ = "Not connected";
        return false;
    }

    size_t totalSent = 0;
    const auto* buf = static_cast<const char*>(data);

    while (totalSent < length) {
        int sent = SSL_write(ssl_, buf + totalSent, static_cast<int>(length - totalSent));
        if (sent <= 0) {
            int err = SSL_get_error(ssl_, sent);
            lastError_ = "SSL_write error: " + std::to_string(err);
            LOG_ERROR("{}", lastError_);
            connected_ = false;
            return false;
        }
        totalSent += sent;
    }

    return true;
}

bool TlsSender::sslReadExact(void* buffer, size_t length) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!ssl_ || !connected_.load()) return false;

    size_t total = 0;
    auto* buf = static_cast<char*>(buffer);

    while (total < length) {
        int n = SSL_read(ssl_, buf + total, static_cast<int>(length - total));
        if (n <= 0) {
            connected_ = false;
            return false;
        }
        total += n;
    }
    return true;
}

bool TlsSender::readNextMessage(MessageType& outType, std::string& outPayload) {
    uint8_t headerBuf[MESSAGE_HEADER_SIZE];
    size_t total = 0;
    auto* buf = reinterpret_cast<char*>(headerBuf);

    // Read header (no lock — callers already hold mutex_ or use their own sync)
    while (total < MESSAGE_HEADER_SIZE) {
        int n = SSL_read(ssl_, buf + total, static_cast<int>(MESSAGE_HEADER_SIZE - total));
        if (n <= 0) { connected_ = false; return false; }
        total += n;
    }

    MessageHeader header;
    if (!deserializeHeader(headerBuf, header)) return false;
    if (header.payloadLength > MAX_PAYLOAD_SIZE) { connected_ = false; return false; }

    outPayload.resize(header.payloadLength);
    total = 0;
    while (total < header.payloadLength) {
        int n = SSL_read(ssl_, &outPayload[0] + total,
                         static_cast<int>(header.payloadLength - total));
        if (n <= 0) { connected_ = false; return false; }
        total += n;
    }

    outType = static_cast<MessageType>(header.type);
    return true;
}

bool TlsSender::readExpectedMessage(MessageType expected, std::string& outPayload) {
    // Keep reading messages while waiting for our ACK.
    // POLICY_UPDATE and MODULE_COMMAND may arrive asynchronously from the Manager
    // at any point — queue them for retrieval via tryReadInbound() rather than
    // discarding them.  Anything else is truly unexpected and gets a warning.
    for (int safety = 0; safety < 10; ++safety) {
        MessageType type;
        std::string payload;
        if (!readNextMessage(type, payload)) return false;

        if (type == expected) {
            outPayload = std::move(payload);
            return true;
        }

        // Queue inbound async messages for later dispatch by managementLoop
        if (type == MessageType::POLICY_UPDATE ||
            type == MessageType::MODULE_COMMAND) {

            auto j = nlohmann::json::parse(payload, nullptr, false);
            if (!j.is_discarded()) {
                // Annotate so tryReadInbound() can branch correctly
                j["_msgType"] = (type == MessageType::POLICY_UPDATE)
                                    ? "policy_update"
                                    : "module_command";
                pendingInbound_.push(std::move(j));
                LOG_DEBUG("Queued inbound {} while waiting for ACK (queue={})",
                          (type == MessageType::POLICY_UPDATE ? "POLICY_UPDATE" : "MODULE_COMMAND"),
                          pendingInbound_.size());
            }
        } else {
            LOG_WARN("Discarding unexpected message type 0x{:02X} while waiting for 0x{:02X}",
                     static_cast<uint8_t>(type), static_cast<uint8_t>(expected));
        }
    }

    LOG_ERROR("Too many unexpected messages while waiting for type 0x{:02X}",
              static_cast<uint8_t>(expected));
    return false;
}


SendResult TlsSender::sendHeartbeat(const std::string& agentId, uint64_t eventsCollected, uint64_t eventsSent) {
    if (!sslCtx_) {
        lastError_ = "TLS sender not initialized (SSL context is null)";
        return SendResult::NetworkError;
    }

    Heartbeat hb;
    hb.agentId = agentId;
    hb.eventsCollected = eventsCollected;
    hb.eventsSent = eventsSent;
    
    // Get current time
    time_t now = time(nullptr);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));
    hb.timestamp = buf;

    // TODO: Add real CPU/Mem info if available
    hb.cpuUsage = 0.0; 
    hb.memoryUsageMb = 0;

    std::string payload = nlohmann::json(hb).dump();

    MessageHeader header;
    header.type = static_cast<uint8_t>(MessageType::HEARTBEAT);
    header.payloadLength = static_cast<uint32_t>(payload.size());

    uint8_t headerBuf[MESSAGE_HEADER_SIZE];
    serializeHeader(header, headerBuf);

    int attempts = 0;
    while (attempts < MAX_RECONNECT_ATTEMPTS) {
        if (!connected_.load()) {
            if (!connectWithMutualTLS()) {
                attempts++;
                std::this_thread::sleep_for(std::chrono::milliseconds(1000 * attempts));
                continue;
            }
        }

        if (!sslSendRaw(headerBuf, MESSAGE_HEADER_SIZE)) {
            disconnect();
            attempts++;
            continue;
        }

        if (!sslSendRaw(payload.c_str(), payload.size())) {
            disconnect();
            attempts++;
            continue;
        }

        bytesSent_ += MESSAGE_HEADER_SIZE + payload.size();

        // --- Read response (tolerates interleaved POLICY_UPDATEs) ---
        std::string respPayload;
        if (!readExpectedMessage(MessageType::HEARTBEAT_ACK, respPayload)) {
            LOG_WARN("Failed to read HEARTBEAT_ACK");
            disconnect();
            attempts++;
            continue;
        }

        try {
            auto ack = nlohmann::json::parse(respPayload).get<HeartbeatAck>();
            if (!ack.licenseValid) {
                lastError_ = "License Error: " + ack.licenseMessage;
                return SendResult::AuthError;
            }
            return SendResult::Success;
        } catch (const std::exception& e) {
            lastError_ = "Failed to parse HeartbeatAck: " + std::string(e.what());
            LOG_ERROR("{}", lastError_);
            return SendResult::ServerError;
        }
    }

    lastError_ = "Failed to send heartbeat after " + std::to_string(MAX_RECONNECT_ATTEMPTS) + " attempts";
    return SendResult::NetworkError;
}

SendResult TlsSender::checkLicense(const std::string& agentId) {
    if (!sslCtx_) {
        lastError_ = "TLS sender not initialized (SSL context is null)";
        return SendResult::NetworkError;
    }

    LicenseCheck lc;
    lc.agentId = agentId;
    
    time_t now = time(nullptr);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));
    lc.timestamp = buf;

    std::string payload = nlohmann::json(lc).dump();

    MessageHeader header;
    header.type = static_cast<uint8_t>(MessageType::LICENSE_CHECK);
    header.payloadLength = static_cast<uint32_t>(payload.size());

    uint8_t headerBuf[MESSAGE_HEADER_SIZE];
    serializeHeader(header, headerBuf);

    int attempts = 0;
    while (attempts < MAX_RECONNECT_ATTEMPTS) {
        if (!connected_.load()) {
            if (!connectWithMutualTLS()) {
                attempts++;
                std::this_thread::sleep_for(std::chrono::milliseconds(1000 * attempts));
                continue;
            }
        }

        if (!sslSendRaw(headerBuf, MESSAGE_HEADER_SIZE)) {
            disconnect();
            attempts++;
            continue;
        }

        if (!sslSendRaw(payload.c_str(), payload.size())) {
            disconnect();
            attempts++;
            continue;
        }

        bytesSent_ += MESSAGE_HEADER_SIZE + payload.size();

        // --- Read response (tolerates interleaved POLICY_UPDATEs) ---
        std::string respPayload;
        if (!readExpectedMessage(MessageType::LICENSE_CHECK_RESULT, respPayload)) {
            LOG_WARN("Failed to read LICENSE_CHECK_RESULT");
            disconnect();
            attempts++;
            continue;
        }

        try {
            auto result = nlohmann::json::parse(respPayload).get<LicenseCheckResult>();
            lastLicenseType_ = result.licenseType;
            lastLicenseExpiry_ = result.licenseExpiry;
            if (!result.licenseValid) {
                lastError_ = "License Error: " + result.licenseMessage;
                return SendResult::AuthError;
            }
            return SendResult::Success;
        } catch (const std::exception& e) {
            lastError_ = "Failed to parse LicenseCheckResult: " + std::string(e.what());
            LOG_ERROR("{}", lastError_);
            return SendResult::ServerError;
        }
    }

    lastError_ = "Failed to send license check after " + std::to_string(MAX_RECONNECT_ATTEMPTS) + " attempts";
    return SendResult::NetworkError;
}

SendResult TlsSender::sendStatusReport(const std::string& agentId,
                                        const std::string& reportType,
                                        const nlohmann::json& reportData) {
    if (!sslCtx_) {
        lastError_ = "TLS sender not initialized";
        return SendResult::NetworkError;
    }

    StatusReport report;
    report.agentId = agentId;
    report.reportType = reportType;
    report.reportData = reportData.dump();

    time_t now = time(nullptr);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));
    report.timestamp = buf;

    std::string payload = nlohmann::json(report).dump();

    MessageHeader header;
    header.type = static_cast<uint8_t>(MessageType::STATUS_REPORT);
    header.payloadLength = static_cast<uint32_t>(payload.size());

    uint8_t headerBuf[MESSAGE_HEADER_SIZE];
    serializeHeader(header, headerBuf);

    if (!connected_.load()) {
        lastError_ = "Not connected";
        return SendResult::NetworkError;
    }

    if (!sslSendRaw(headerBuf, MESSAGE_HEADER_SIZE) ||
        !sslSendRaw(payload.c_str(), payload.size())) {
        disconnect();
        return SendResult::NetworkError;
    }

    bytesSent_ += MESSAGE_HEADER_SIZE + payload.size();

    // Read ACK (tolerates interleaved POLICY_UPDATEs)
    std::string respPayload;
    if (!readExpectedMessage(MessageType::STATUS_REPORT_ACK, respPayload)) {
        LOG_WARN("Failed to read STATUS_REPORT_ACK");
        disconnect();
        return SendResult::ServerError;
    }

    LOG_INFO("Status report sent: type={}", reportType);
    return SendResult::Success;
}

void TlsSender::enqueueStatusReport(const std::string& agentId,
                                    const std::string& reportType,
                                    const nlohmann::json& reportData) {
    std::lock_guard<std::mutex> lock(pendingReportsMutex_);
    pendingReports_.push({ agentId, reportType, reportData });
    LOG_DEBUG("Status report queued for sending: type={} (queue={})",
              reportType, pendingReports_.size());
}

int TlsSender::flushPendingStatusReports() {
    // Drain into a local snapshot so we release the lock before doing SSL I/O
    std::queue<PendingReport> local;
    {
        std::lock_guard<std::mutex> lock(pendingReportsMutex_);
        std::swap(local, pendingReports_);
    }

    int sent = 0;
    while (!local.empty()) {
        auto& r = local.front();
        auto result = sendStatusReport(r.agentId, r.reportType, r.data);
        if (result == SendResult::Success) {
            ++sent;
        } else {
            LOG_WARN("flushPendingStatusReports: failed to send type={}: {}",
                     r.reportType, lastError_);
            // Re-queue on failure so we retry next tick
            std::lock_guard<std::mutex> lock(pendingReportsMutex_);
            pendingReports_.push(std::move(r));
            break;  // stop on first failure (connection may be down)
        }
        local.pop();
    }
    return sent;
}

SendResult TlsSender::sendPolicyAck(const std::string& agentId,
                                     const std::string& policyType,
                                     bool applied,
                                     const std::string& message) {
    if (!sslCtx_) {
        lastError_ = "TLS sender not initialized";
        return SendResult::NetworkError;
    }

    PolicyUpdateAck ack;
    ack.agentId = agentId;
    ack.policyType = policyType;
    ack.applied = applied;
    ack.message = message;

    time_t now = time(nullptr);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));
    ack.timestamp = buf;

    std::string payload = nlohmann::json(ack).dump();

    MessageHeader header;
    header.type = static_cast<uint8_t>(MessageType::POLICY_UPDATE_ACK);
    header.payloadLength = static_cast<uint32_t>(payload.size());

    uint8_t headerBuf[MESSAGE_HEADER_SIZE];
    serializeHeader(header, headerBuf);

    if (!connected_.load()) {
        lastError_ = "Not connected";
        return SendResult::NetworkError;
    }

    // Fire-and-forget: no ACK expected back from Manager for this message
    if (!sslSendRaw(headerBuf, MESSAGE_HEADER_SIZE) ||
        !sslSendRaw(payload.c_str(), payload.size())) {
        disconnect();
        return SendResult::NetworkError;
    }

    bytesSent_ += MESSAGE_HEADER_SIZE + payload.size();
    LOG_INFO("Policy ACK sent: type={} applied={}", policyType, applied);
    return SendResult::Success;
}

SendResult TlsSender::sendModuleCommandResult(const std::string& agentId,
                                               const ModuleCommandResult& result) {
    if (!sslCtx_) {
        lastError_ = "TLS sender not initialized";
        return SendResult::NetworkError;
    }

    std::string payload = nlohmann::json(result).dump();

    MessageHeader header;
    header.type          = static_cast<uint8_t>(MessageType::MODULE_COMMAND_RESULT);
    header.payloadLength = static_cast<uint32_t>(payload.size());

    uint8_t headerBuf[MESSAGE_HEADER_SIZE];
    serializeHeader(header, headerBuf);

    if (!connected_.load()) {
        lastError_ = "Not connected";
        return SendResult::NetworkError;
    }

    // Fire-and-forget: no ACK expected back from Manager for this message
    if (!sslSendRaw(headerBuf, MESSAGE_HEADER_SIZE) ||
        !sslSendRaw(payload.c_str(), payload.size())) {
        disconnect();
        return SendResult::NetworkError;
    }

    bytesSent_ += MESSAGE_HEADER_SIZE + payload.size();
    LOG_INFO("MODULE_COMMAND_RESULT sent: verb={} status={} commandId={}",
             result.verb, result.status, result.commandId);
    return SendResult::Success;
}

bool TlsSender::tryReadInbound(nlohmann::json& out) {
    std::lock_guard<std::mutex> lock(mutex_);

    // 1) Drain any messages queued during sendHeartbeat/checkLicense first
    if (!pendingInbound_.empty()) {
        out = std::move(pendingInbound_.front());
        pendingInbound_.pop();
        return true;
    }

    // 2) Check for new data on the wire (non-blocking)
    if (!ssl_ || !connected_.load()) return false;
    if (SSL_pending(ssl_) <= 0) return false;

    // Data available — read one full message
    MessageType type;
    std::string payload;
    if (!readNextMessage(type, payload)) return false;

    // Surface POLICY_UPDATE and MODULE_COMMAND; annotate with _msgType; discard others
    if (type == MessageType::POLICY_UPDATE) {
        out = nlohmann::json::parse(payload, nullptr, false);
        if (out.is_discarded()) return false;
        out["_msgType"] = "policy_update";
        return true;
    }
    if (type == MessageType::MODULE_COMMAND) {
        out = nlohmann::json::parse(payload, nullptr, false);
        if (out.is_discarded()) return false;
        out["_msgType"] = "module_command";
        return true;
    }

    LOG_DEBUG("tryReadInbound: discarding unexpected type 0x{:02X}", static_cast<uint8_t>(type));
    return false;
}

} // namespace ResolutePulse

