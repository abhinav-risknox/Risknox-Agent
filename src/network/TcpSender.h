#pragma once

#include "collector/Event.h"
#include <string>
#include <vector>
#include <atomic>
#include <mutex>

// Forward declare Windows socket type
#ifdef _WIN32
#include <winsock2.h>
#endif

// Forward declare OpenSSL types
typedef struct ssl_st SSL;
typedef struct ssl_ctx_st SSL_CTX;

#include "SenderInterface.h"

namespace ResolutePulse {

class TcpSender : public SenderInterface {
public:
    TcpSender();
    ~TcpSender();
    
    // Initialize the sender and connect to Fluent Bit
    // @param host - Fluent Bit hostname/IP
    // @param port - Fluent Bit TCP port
    // @param agentId - The agent ID to include in telemetry
    // @param tlsEnabled - Whether to use TLS (default: false for backward compatibility)
    // @param caCertPath - Path to CA certificate for verifying Fluent Bit's server cert
    bool initialize(const std::string& host, int port,
                    const std::string& agentId,
                    bool tlsEnabled = false,
                    const std::string& caCertPath = "");
    
    // Send a batch of events as NDJSON lines (no agentId parameter)
    // @param events - Events to send
    // @return SendResult indicating success or type of failure
    SendResult sendBatch(const std::vector<Event>& events) override;
    
    // Check if connected to Fluent Bit
    bool isConnected() const override { return connected_.load(); }
    
    // Disconnect from Fluent Bit
    void disconnect();
    
    // Signal the sender to abort any pending connection attempts immediately
    void requestStop() { stopRequested_ = true; }
    
    // Get last error message
    const std::string& getLastError() const override { return lastError_; }
    
    // Get statistics
    uint64_t getEventsSent() const { return eventsSent_.load(); }
    uint64_t getBytesSent() const { return bytesSent_.load(); }
    uint64_t getReconnections() const { return reconnections_.load(); }
    uint64_t getFailedSends() const { return failedSends_.load(); }
    uint64_t getBatchesSent() const { return batchesSent_.load(); }
    
private:
    // Connect to the server (TCP + optional TLS handshake)
    bool connect();
    
    // Non-blocking connect with timeout (avoids 21s default TCP timeout on Windows)
    bool connectWithTimeout(SOCKET sock, const struct sockaddr* addr, int addrlen, int timeoutMs);
    
    // Create OpenSSL context for TLS
    bool createSSLContext();
    
    // Send raw data over socket (plain TCP or TLS)
    bool sendRaw(const char* data, size_t length);
    
    // Cleanup socket resources
    void cleanup();
    
    std::string host_;
    int port_ = 0;
    std::string agentId_;
    
    // TLS configuration
    bool tlsEnabled_ = false;
    std::string caCertPath_;
    SSL_CTX* sslCtx_ = nullptr;
    SSL* ssl_ = nullptr;
    
#ifdef _WIN32
    SOCKET socket_ = INVALID_SOCKET;
    bool wsaInitialized_ = false;
#else
    int socket_ = -1;
#endif
    
    std::atomic<bool> connected_{false};
    std::atomic<bool> stopRequested_{false};
    std::mutex socketMutex_;
    
    std::string lastError_;
    
    // Statistics
    std::atomic<uint64_t> eventsSent_{0};
    std::atomic<uint64_t> bytesSent_{0};
    std::atomic<uint64_t> reconnections_{0};
    std::atomic<uint64_t> failedSends_{0};
    std::atomic<uint64_t> batchesSent_{0};
    
    // Configuration
    static constexpr int CONNECT_TIMEOUT_MS = 5000;
    static constexpr int SEND_TIMEOUT_MS = 3000;
    static constexpr int MAX_RECONNECT_ATTEMPTS = 3;
};

} // namespace ResolutePulse
