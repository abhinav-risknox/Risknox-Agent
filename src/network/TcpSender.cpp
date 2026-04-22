#include "TcpSender.h"
#include "utils/Logger.h"

#include <nlohmann/json.hpp>
#include <sstream>
#include <fstream>

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/pem.h>

#ifdef _WIN32
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#endif

namespace ResolutePulse {

TcpSender::TcpSender() = default;

TcpSender::~TcpSender() {
    disconnect();
    
    if (sslCtx_) {
        SSL_CTX_free(sslCtx_);
        sslCtx_ = nullptr;
    }

#ifdef _WIN32
    if (wsaInitialized_) {
        WSACleanup();
    }
#endif
}

bool TcpSender::initialize(const std::string& host, int port,
                            bool tlsEnabled,
                            const std::string& caCertPath) {
    host_ = host;
    port_ = port;
    tlsEnabled_ = tlsEnabled;
    caCertPath_ = caCertPath;
    
    LOG_INFO("Initializing TCP sender: {}:{} (TLS={})", host_, port_, tlsEnabled_ ? "on" : "off");
    
#ifdef _WIN32
    // Initialize Winsock
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        lastError_ = "WSAStartup failed: " + std::to_string(result);
        LOG_ERROR(lastError_);
        return false;
    }
    wsaInitialized_ = true;
#endif

    // Create SSL context if TLS is enabled
    if (tlsEnabled_) {
        if (!createSSLContext()) {
            LOG_ERROR("Failed to create SSL context for Fluent Bit TLS");
            return false;
        }
    }
    
    // Try to connect
    if (!connect()) {
        LOG_WARN("Initial connection failed, will retry on first send");
        return true; // Still return true - we'll retry later
    }
    
    LOG_INFO("TCP sender initialized successfully");
    return true;
}

bool TcpSender::createSSLContext() {
    const SSL_METHOD* method = TLS_client_method();
    sslCtx_ = SSL_CTX_new(method);
    if (!sslCtx_) {
        lastError_ = "Failed to create SSL context";
        LOG_ERROR("{}", lastError_);
        return false;
    }

    SSL_CTX_set_min_proto_version(sslCtx_, TLS1_2_VERSION);

    // Load CA certificate for verifying Fluent Bit's server cert
    if (!caCertPath_.empty()) {
        // Use memory-based loading to avoid OPENSSL_Applink issues on Windows
        std::ifstream ifs(caCertPath_, std::ios::binary);
        if (!ifs) {
            lastError_ = "Cannot open CA certificate: " + caCertPath_;
            LOG_ERROR("{}", lastError_);
            SSL_CTX_free(sslCtx_);
            sslCtx_ = nullptr;
            return false;
        }
        std::string caPem((std::istreambuf_iterator<char>(ifs)),
                           std::istreambuf_iterator<char>());
        
        BIO* bio = BIO_new_mem_buf(caPem.data(), static_cast<int>(caPem.size()));
        X509* caCert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
        BIO_free(bio);

        if (!caCert) {
            lastError_ = "Failed to parse CA certificate: " + caCertPath_;
            LOG_ERROR("{}", lastError_);
            SSL_CTX_free(sslCtx_);
            sslCtx_ = nullptr;
            return false;
        }

        X509_STORE* store = SSL_CTX_get_cert_store(sslCtx_);
        X509_STORE_add_cert(store, caCert);
        X509_free(caCert);

        // Enable server certificate verification
        SSL_CTX_set_verify(sslCtx_, SSL_VERIFY_PEER, nullptr);
        LOG_INFO("TLS: CA certificate loaded from {}", caCertPath_);
    } else {
        // No CA cert provided - accept any server cert (self-signed localhost)
        SSL_CTX_set_verify(sslCtx_, SSL_VERIFY_NONE, nullptr);
        LOG_WARN("TLS: No CA certificate provided, server verification disabled");
    }

    return true;
}

bool TcpSender::connectWithTimeout(SOCKET sock, const struct sockaddr* addr, int addrlen, int timeoutMs) {
    // Put socket in non-blocking mode
    u_long mode = 1;
    ioctlsocket(sock, FIONBIO, &mode);
    
    int ret = ::connect(sock, addr, addrlen);
    if (ret == 0) {
        // Connected immediately
        mode = 0;
        ioctlsocket(sock, FIONBIO, &mode);
        return true;
    }
    
#ifdef _WIN32
    int err = WSAGetLastError();
    if (err != WSAEWOULDBLOCK) {
        lastError_ = "Connection failed: " + std::to_string(err);
        LOG_ERROR(lastError_);
        return false;
    }
#endif
    
    // Wait for connection with select(), checking stopRequested_ periodically
    int elapsed = 0;
    constexpr int POLL_INTERVAL_MS = 250;  // Check stop flag every 250ms
    
    while (elapsed < timeoutMs && !stopRequested_.load()) {
        int remaining = std::min(POLL_INTERVAL_MS, timeoutMs - elapsed);
        
        fd_set writefds, errfds;
        FD_ZERO(&writefds);
        FD_ZERO(&errfds);
        FD_SET(sock, &writefds);
        FD_SET(sock, &errfds);
        
        struct timeval tv;
        tv.tv_sec = remaining / 1000;
        tv.tv_usec = (remaining % 1000) * 1000;
        
        ret = select(0, nullptr, &writefds, &errfds, &tv);
        
        if (ret > 0) {
            if (FD_ISSET(sock, &errfds)) {
                // Connection failed
                int optErr = 0;
                int optLen = sizeof(optErr);
                getsockopt(sock, SOL_SOCKET, SO_ERROR, (char*)&optErr, &optLen);
                lastError_ = "Connection failed: " + std::to_string(optErr);
                LOG_ERROR(lastError_);
                return false;
            }
            if (FD_ISSET(sock, &writefds)) {
                // Connected! Restore blocking mode
                mode = 0;
                ioctlsocket(sock, FIONBIO, &mode);
                return true;
            }
        } else if (ret < 0) {
            lastError_ = "select() failed: " + std::to_string(WSAGetLastError());
            LOG_ERROR(lastError_);
            return false;
        }
        
        elapsed += remaining;
    }
    
    if (stopRequested_.load()) {
        lastError_ = "Connection aborted: shutdown requested";
        LOG_INFO(lastError_);
    } else {
        lastError_ = "Connection timed out after " + std::to_string(timeoutMs) + "ms";
        LOG_ERROR(lastError_);
    }
    return false;
}

bool TcpSender::connect() {
    std::lock_guard<std::mutex> lock(socketMutex_);
    
    // Abort early if shutdown was requested
    if (stopRequested_.load()) {
        lastError_ = "Connection skipped: shutdown requested";
        return false;
    }
    
    // Clean up existing SSL session
    if (ssl_) {
        SSL_shutdown(ssl_);
        SSL_free(ssl_);
        ssl_ = nullptr;
    }

    // Clean up existing socket if any
    if (socket_ != INVALID_SOCKET) {
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
    }
    
    // Create socket
    socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket_ == INVALID_SOCKET) {
#ifdef _WIN32
        lastError_ = "Socket creation failed: " + std::to_string(WSAGetLastError());
#else
        lastError_ = "Socket creation failed";
#endif
        LOG_ERROR(lastError_);
        return false;
    }
    
    // Set send/recv timeouts (these apply after connection is established)
#ifdef _WIN32
    DWORD timeout = SEND_TIMEOUT_MS;
    setsockopt(socket_, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));
    setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
#endif
    
    // Resolve hostname
    struct addrinfo hints = {0}, *result = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    
    std::string portStr = std::to_string(port_);
    int ret = getaddrinfo(host_.c_str(), portStr.c_str(), &hints, &result);
    if (ret != 0) {
        lastError_ = "Failed to resolve host: " + host_;
        LOG_ERROR(lastError_);
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
        return false;
    }
    
    // Connect with timeout (non-blocking + select)
    bool connectOk = connectWithTimeout(socket_, result->ai_addr, (int)result->ai_addrlen, CONNECT_TIMEOUT_MS);
    freeaddrinfo(result);
    
    if (!connectOk) {
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
        return false;
    }
    
    // TLS handshake if enabled
    if (tlsEnabled_ && sslCtx_) {
        ssl_ = SSL_new(sslCtx_);
        SSL_set_fd(ssl_, static_cast<int>(socket_));

        if (SSL_connect(ssl_) <= 0) {
            lastError_ = "TLS handshake with Fluent Bit failed";
            LOG_ERROR("{}", lastError_);

            // Log OpenSSL errors
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

        LOG_INFO("TLS connection established with Fluent Bit at {}:{}", host_, port_);
    } else {
        LOG_INFO("Connected to Fluent Bit at {}:{}", host_, port_);
    }
    
    connected_ = true;
    reconnections_++;
    
    return true;
}

void TcpSender::disconnect() {
    std::lock_guard<std::mutex> lock(socketMutex_);
    
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
    LOG_DEBUG("Disconnected from Fluent Bit");
}

void TcpSender::cleanup() {
    disconnect();
}

SendResult TcpSender::sendBatch(const std::vector<Event>& events) {
    if (events.empty()) {
        return SendResult::Success;
    }
    
    // Build NDJSON batch (one JSON object per line) with compact field names
    std::ostringstream batch;
    for (const auto& event : events) {
        nlohmann::json j;
        j["c"] = event.channel;      // c = channel
        j["e"] = event.eventId;       // e = event_id
        j["t"] = event.timestamp;     // t = timestamp
        j["x"] = event.data;          // x = data (generic payload)
        
        batch << j.dump() << "\n";
    }
    
    std::string batchData = batch.str();
    
    // Try to send, reconnect if needed
    int attempts = 0;
    while (attempts < MAX_RECONNECT_ATTEMPTS && !stopRequested_.load()) {
        if (!connected_.load()) {
            LOG_DEBUG("Not connected, attempting to connect (attempt {})", attempts + 1);
            if (!connect()) {
                attempts++;
                // Interruptible sleep: check stopRequested_ every 250ms
                for (int ms = 0; ms < 1000 * attempts && !stopRequested_.load(); ms += 250) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(250));
                }
                continue;
            }
        }
        
        if (sendRaw(batchData.c_str(), batchData.length())) {
            eventsSent_ += events.size();
            bytesSent_ += batchData.length();
            batchesSent_++;
            return SendResult::Success;
        }
        
        // Send failed, disconnect and retry
        LOG_WARN("Send failed, will retry");
        disconnect();
        attempts++;
        // Interruptible sleep
        for (int ms = 0; ms < 1000 * attempts && !stopRequested_.load(); ms += 250) {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
    }
    
    failedSends_ += events.size();
    lastError_ = "Failed to send batch after " + std::to_string(MAX_RECONNECT_ATTEMPTS) + " attempts";
    LOG_ERROR(lastError_);
    
    return SendResult::NetworkError;
}

bool TcpSender::sendRaw(const char* data, size_t length) {
    std::lock_guard<std::mutex> lock(socketMutex_);
    
    if (socket_ == INVALID_SOCKET || !connected_.load()) {
        lastError_ = "Not connected";
        return false;
    }
    
    size_t totalSent = 0;
    while (totalSent < length) {
        int sent;

        if (tlsEnabled_ && ssl_) {
            // TLS path: use SSL_write
            sent = SSL_write(ssl_, data + totalSent, static_cast<int>(length - totalSent));
            if (sent <= 0) {
                int sslErr = SSL_get_error(ssl_, sent);
                lastError_ = "SSL_write error: " + std::to_string(sslErr);
                LOG_ERROR("{}", lastError_);
                connected_ = false;
                return false;
            }
        } else {
            // Plain TCP path: use send()
            sent = send(socket_, data + totalSent, (int)(length - totalSent), 0);
            if (sent == SOCKET_ERROR) {
#ifdef _WIN32
                int error = WSAGetLastError();
                lastError_ = "Send error: " + std::to_string(error);
#else
                lastError_ = "Send error";
#endif
                LOG_ERROR(lastError_);
                connected_ = false;
                return false;
            }

            if (sent == 0) {
                lastError_ = "Connection closed by remote";
                LOG_ERROR(lastError_);
                connected_ = false;
                return false;
            }
        }
        
        totalSent += sent;
    }
    
    return true;
}

} // namespace ResolutePulse

