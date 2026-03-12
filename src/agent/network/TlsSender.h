#pragma once

#include "collector/Event.h"

#include <string>
#include <vector>
#include <atomic>
#include <mutex>

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

    // Check if connected via mTLS
    bool isConnected() const override { return connected_.load(); }

    // Disconnect
    void disconnect();

    // Statistics
    uint64_t getEventsSent() const { return eventsSent_.load(); }
    uint64_t getBytesSent() const { return bytesSent_.load(); }
    uint64_t getBatchesSent() const { return batchesSent_.load(); }
    uint64_t getFailedSends() const { return failedSends_.load(); }
    uint64_t getReconnections() const { return reconnections_.load(); }

    const std::string& getLastError() const override { return lastError_; }

private:
    // Create SSL context with mutual TLS
    bool createSSLContext();

    // Connect with mTLS
    bool connectWithMutualTLS();

    // Send raw data over SSL
    bool sslSendRaw(const void* data, size_t length);

    // Read exact number of bytes over SSL
    bool sslReadExact(void* buffer, size_t length);

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

    std::atomic<uint64_t> eventsSent_{0};
    std::atomic<uint64_t> bytesSent_{0};
    std::atomic<uint64_t> batchesSent_{0};
    std::atomic<uint64_t> failedSends_{0};
    std::atomic<uint64_t> reconnections_{0};

    static constexpr int MAX_RECONNECT_ATTEMPTS = 3;
    static constexpr int CONNECT_TIMEOUT_MS = 5000;
};

} // namespace ResolutePulse
