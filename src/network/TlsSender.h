#pragma once

#include "network/TlsConfig.h"
#include "network/BinaryProtocol.h"
#include <string>
#include <cstdint>

// Forward declarations for OpenSSL types
typedef struct ssl_st SSL;
typedef struct ssl_ctx_st SSL_CTX;

namespace ResolutePulse {

// Result codes for send operations
enum class SendResult {
    Success,
    NetworkError,
    TlsError,
    AuthenticationError,
    ServerError,
    Timeout
};

/**
 * TLS-encrypted TCP sender for secure binary message transmission
 * Uses OpenSSL for TLS 1.2/1.3 support
 */
class TlsSender {
public:
    TlsSender();
    ~TlsSender();
    
    /**
     * Initialize sender with host, port, and TLS configuration
     * @param host Server hostname/IP
     * @param port Server port
     * @param tlsConfig TLS settings (certificates, ciphers, etc.)
     * @return true if initialization successful
     */
    bool initialize(const std::string& host, int port, const TlsConfig& tlsConfig);
    
    /**
     * Establish TCP connection and perform TLS handshake
     * @return true if connected successfully
     */
    bool connect();
    
    /**
     * Close connection and cleanup
     */
    void disconnect();
    
    /**
     * Send a binary message over TLS
     * @param msg Binary message to send
     * @return Result code indicating success or failure type
     */
    SendResult sendBinary(const BinaryMessage& msg);
    
    /**
     * Send raw bytes over TLS
     * @param data Pointer to data
     * @param length Number of bytes to send
     * @return Result code
     */
    SendResult sendRaw(const uint8_t* data, size_t length);
    
    /**
     * Check if currently connected
     */
    bool isConnected() const;
    
    /**
     * Get last error message
     */
    std::string getLastError() const { return lastError_; }
    
    /**
     * Get statistics
     */
    uint64_t getMessagesSent() const { return messagesSent_; }
    uint64_t getBytesSent() const { return bytesSent_; }
    uint64_t getFailedMessages() const { return failedMessages_; }

private:
    // Initialization helpers
    bool initOpenSSL();
    bool createTlsContext();
    bool loadCertificates();
    bool configureCiphers();
    
    // Connection helpers
    bool createSocket();
    bool performTcpConnect();
    bool performTlsHandshake();
    
    // Cleanup
    void cleanupOpenSSL();
    void closeSocket();
    
    // Configuration
    std::string host_;
    int port_;
    TlsConfig tlsConfig_;
    
    // OpenSSL objects
    SSL_CTX* sslContext_;
    SSL* ssl_;
    
    // Socket
    int socketFd_;
    bool connected_;
    
    // Statistics
    uint64_t messagesSent_;
    uint64_t bytesSent_;
    uint64_t failedMessages_;
    
    // Error tracking
    std::string lastError_;
    
    // OpenSSL initialization tracking
    static bool opensslInitialized_;
};

} // namespace ResolutePulse
