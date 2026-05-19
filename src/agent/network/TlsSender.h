#pragma once

#include "collector/Event.h"

#include <string>
#include <vector>
#include <atomic>
#include <mutex>

#include <nlohmann/json.hpp>
#include <queue>
#include "common/Protocol.h"

// Forward declare OpenSSL types
typedef struct ssl_ctx_st SSL_CTX;
typedef struct ssl_st SSL;

#ifdef _WIN32
#include <winsock2.h>
#endif

#include "network/SenderInterface.h"

namespace ResolutePulse {

class TlsSender : public SenderInterface {
public:
    TlsSender();
    ~TlsSender();

    // Initialize the mTLS sender
    // @param host      - Manager hostname/IP
    // @param port      - Manager port (default 1514)
    // @param certPath  - Path to agent.crt
    // @param keyPath   - Path to agent.key
    // @param caCertPath - Path to ca.crt
    bool initialize(const std::string& host, int port,
                    const std::string& certPath,
                    const std::string& keyPath,
                    const std::string& caCertPath);

    // Send a batch of events over mTLS
    SendResult sendBatch(const std::vector<Event>& events) override;

    // Send a heartbeat to the manager
    SendResult sendHeartbeat(const std::string& agentId, uint64_t eventsCollected, uint64_t eventsSent) override;

    // Send a license check to the manager
    SendResult checkLicense(const std::string& agentId);

    // Send a status report back to the manager (called from the management thread only)
    SendResult sendStatusReport(const std::string& agentId,
                                const std::string& reportType,
                                const nlohmann::json& reportData);

    // Thread-safe: enqueue a status report to be sent by the management loop thread.
    // Safe to call from any background thread (avScanLoop, sysInfoLoop, etc.).
    void enqueueStatusReport(const std::string& agentId,
                             const std::string& reportType,
                             const nlohmann::json& reportData);

    // Drain the pending status report queue. Called by managementLoop on each tick.
    // Returns the number of reports sent.
    int flushPendingStatusReports();

    // Send a policy update acknowledgement back to the manager.
    // commandId is echoed from the received PolicyUpdate for correlation.
    SendResult sendPolicyAck(const std::string& agentId,
                             const std::string& commandId,
                             const std::string& policyType,
                             bool applied,
                             const std::string& message);

    // Send the result of a MODULE_COMMAND back to the manager
    SendResult sendModuleCommandResult(const std::string& agentId,
                                       const ModuleCommandResult& result);

    // Check if connected via mTLS
    bool isConnected() const override { return connected_.load(); }

    // Disconnect
    void disconnect();

    // Try to read an inbound POLICY_UPDATE or MODULE_COMMAND from the Manager
    // (non-blocking). Returns true and populates `out` if a message was available.
    // The caller must check out["_msgType"] to distinguish the two.
    // Returns false immediately if no data is pending.
    // Uses select() to check both SSL buffer AND underlying TCP socket.
    bool tryReadInbound(nlohmann::json& out);

    // Block until data is available on the mTLS socket OR timeout expires.
    // Uses select() — zero CPU while waiting, instant wake when Manager pushes.
    // Intended to replace sleep_for() in the management loop.
    // Returns true if data is ready, false on timeout/error.
    bool waitForDataOrTimeout(int timeoutMs);

    // Statistics
    uint64_t getEventsSent() const { return eventsSent_.load(); }
    uint64_t getBytesSent() const { return bytesSent_.load(); }
    uint64_t getBatchesSent() const { return batchesSent_.load(); }
    uint64_t getFailedSends() const { return failedSends_.load(); }
    uint64_t getReconnections() const { return reconnections_.load(); }

    const std::string& getLastError() const override { return lastError_; }

    // License details from last checkLicense call
    const std::string& getLastLicenseType() const { return lastLicenseType_; }
    const std::string& getLastLicenseExpiry() const { return lastLicenseExpiry_; }

private:
    // Create SSL context with mutual TLS
    bool createSSLContext();

    // Connect with mTLS
    bool connectWithMutualTLS();

    // Send raw data over SSL
    bool sslSendRaw(const void* data, size_t length);

    // Read exact number of bytes over SSL
    bool sslReadExact(void* buffer, size_t length);

    // Read one complete protocol message from the socket (header + payload).
    // Returns false on I/O error. Populates outType and outPayload.
    bool readNextMessage(MessageType& outType, std::string& outPayload);

    // Read messages until we get the expected type, queuing anything else.
    bool readExpectedMessage(MessageType expected, std::string& outPayload);

    std::string host_;
    int         port_ = 1514;
    std::string certPath_;
    std::string keyPath_;
    std::string caCertPath_;

    SSL_CTX*    sslCtx_ = nullptr;
    SSL*        ssl_    = nullptr;

#ifdef _WIN32
    SOCKET      socket_ = INVALID_SOCKET;
    bool        wsaInitialized_ = false;
#else
    int         socket_ = -1;
#endif

    std::atomic<bool> connected_{false};
    std::mutex        mutex_;
    std::string       lastError_;
    std::string       lastLicenseType_;
    std::string       lastLicenseExpiry_;

    std::atomic<uint64_t> eventsSent_{0};
    std::atomic<uint64_t> bytesSent_{0};
    std::atomic<uint64_t> batchesSent_{0};
    std::atomic<uint64_t> failedSends_{0};
    std::atomic<uint64_t> reconnections_{0};

    // Queue for POLICY_UPDATE / MODULE_COMMAND messages arriving while waiting for an ACK
    std::queue<nlohmann::json> pendingInbound_;

    // Thread-safe outbound queue: background threads post here, management loop drains it
    struct PendingReport {
        std::string agentId;
        std::string reportType;
        nlohmann::json data;
    };
    std::queue<PendingReport> pendingReports_;
    std::mutex                pendingReportsMutex_;

    static constexpr int MAX_RECONNECT_ATTEMPTS = 3;
    static constexpr int CONNECT_TIMEOUT_MS = 5000;
};

} // namespace ResolutePulse
