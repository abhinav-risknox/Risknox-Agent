#include "network/TlsSender.h"
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/bio.h>
#include <cstring>
#include <stdexcept>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    typedef int socklen_t;
    #define close closesocket
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <unistd.h>
#endif

namespace ResolutePulse {

bool TlsSender::opensslInitialized_ = false;

TlsSender::TlsSender()
    : port_(0)
    , sslContext_(nullptr)
    , ssl_(nullptr)
    , socketFd_(-1)
    , connected_(false)
    , messagesSent_(0)
    , bytesSent_(0)
    , failedMessages_(0)
{
}

TlsSender::~TlsSender() {
    disconnect();
    cleanupOpenSSL();
}

bool TlsSender::initialize(const std::string& host, int port, const TlsConfig& tlsConfig) {
    host_ = host;
    port_ = port;
    tlsConfig_ = tlsConfig;
    
    if (!initOpenSSL()) {
        return false;
    }
    
    if (tlsConfig_.enabled && !createTlsContext()) {
        return false;
    }
    
    return true;
}

bool TlsSender::initOpenSSL() {
    if (!opensslInitialized_) {
#if OPENSSL_VERSION_NUMBER < 0x10100000L
        SSL_library_init();
        SSL_load_error_strings();
        OpenSSL_add_all_algorithms();
#else
        // OpenSSL 1.1.0+ handles initialization automatically
#endif
        opensslInitialized_ = true;
    }
    return true;
}

bool TlsSender::createTlsContext() {
    // Create TLS context (TLS 1.2 and 1.3)
    const SSL_METHOD* method = TLS_client_method();
    sslContext_ = SSL_CTX_new(method);
    
    if (!sslContext_) {
        lastError_ = "Failed to create SSL context";
        ERR_print_errors_fp(stderr);
        return false;
    }
    
    // Set minimum TLS version
    int minVersion = (tlsConfig_.min_tls_version == "1.3") ? TLS1_3_VERSION : TLS1_2_VERSION;
    SSL_CTX_set_min_proto_version(sslContext_, minVersion);
    
    // Set maximum TLS version
    int maxVersion = (tlsConfig_.max_tls_version == "1.3") ? TLS1_3_VERSION : TLS1_2_VERSION;
    SSL_CTX_set_max_proto_version(sslContext_, maxVersion);
    
    // Load certificates if provided
    if (!loadCertificates()) {
        return false;
    }
    
    // Configure ciphers
    if (!configureCiphers()) {
        return false;
    }
    
    // Set verification mode
    if (tlsConfig_.verify_peer) {
        SSL_CTX_set_verify(sslContext_, SSL_VERIFY_PEER, nullptr);
    } else {
        SSL_CTX_set_verify(sslContext_, SSL_VERIFY_NONE, nullptr);
    }
    
    return true;
}

bool TlsSender::loadCertificates() {
    // Load CA certificate for server verification
    if (!tlsConfig_.ca_cert_path.empty()) {
        if (SSL_CTX_load_verify_locations(sslContext_, tlsConfig_.ca_cert_path.c_str(), nullptr) != 1) {
            lastError_ = "Failed to load CA certificate: " + tlsConfig_.ca_cert_path;
            ERR_print_errors_fp(stderr);
            return false;
        }
    }
    
    // Load client certificate (for mTLS)
    if (!tlsConfig_.client_cert_path.empty()) {
        if (SSL_CTX_use_certificate_file(sslContext_, tlsConfig_.client_cert_path.c_str(), SSL_FILETYPE_PEM) != 1) {
            lastError_ = "Failed to load client certificate: " + tlsConfig_.client_cert_path;
            ERR_print_errors_fp(stderr);
            return false;
        }
    }
    
    // Load client private key (for mTLS)
    if (!tlsConfig_.client_key_path.empty()) {
        if (SSL_CTX_use_PrivateKey_file(sslContext_, tlsConfig_.client_key_path.c_str(), SSL_FILETYPE_PEM) != 1) {
            lastError_ = "Failed to load client private key: " + tlsConfig_.client_key_path;
            ERR_print_errors_fp(stderr);
            return false;
        }
        
        // Verify private key matches certificate
        if (SSL_CTX_check_private_key(sslContext_) != 1) {
            lastError_ = "Client private key does not match certificate";
            return false;
        }
    }
    
    return true;
}

bool TlsSender::configureCiphers() {
    if (!tlsConfig_.cipher_list.empty()) {
        if (SSL_CTX_set_cipher_list(sslContext_, tlsConfig_.cipher_list.c_str()) != 1) {
            lastError_ = "Failed to set cipher list";
            ERR_print_errors_fp(stderr);
            return false;
        }
    }
    return true;
}

bool TlsSender::connect() {
    if (connected_) {
        return true; // Already connected
    }
    
    // Create socket
    if (!createSocket()) {
        return false;
    }
    
    // TCP connect
    if (!performTcpConnect()) {
        closeSocket();
        return false;
    }
    
    // TLS handshake (if enabled)
    if (tlsConfig_.enabled && !performTlsHandshake()) {
        closeSocket();
        return false;
    }
    
    connected_ = true;
    return true;
}

bool TlsSender::createSocket() {
    socketFd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (socketFd_ < 0) {
        lastError_ = "Failed to create socket";
        return false;
    }
    return true;
}

bool TlsSender::performTcpConnect() {
    struct sockaddr_in serverAddr;
    std::memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port_);
    
    // Resolve hostname
    if (inet_pton(AF_INET, host_.c_str(), &serverAddr.sin_addr) <= 0) {
        // Not an IP address, try hostname resolution
        struct hostent* he = gethostbyname(host_.c_str());
        if (!he) {
            lastError_ = "Failed to resolve hostname: " + host_;
            return false;
        }
        std::memcpy(&serverAddr.sin_addr, he->h_addr_list[0], he->h_length);
    }
    
    // Connect
    if (::connect(socketFd_, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        lastError_ = "Failed to connect to " + host_ + ":" + std::to_string(port_);
        return false;
    }
    
    return true;
}

bool TlsSender::performTlsHandshake() {
    // Create SSL object
    ssl_ = SSL_new(sslContext_);
    if (!ssl_) {
        lastError_ = "Failed to create SSL object";
        ERR_print_errors_fp(stderr);
        return false;
    }
    
    // Attach socket to SSL
    SSL_set_fd(ssl_, socketFd_);
    
    // Set hostname for SNI (Server Name Indication)
    if (tlsConfig_.verify_hostname) {
        SSL_set_tlsext_host_name(ssl_, host_.c_str());
    }
    
    // Perform TLS handshake
    int ret = SSL_connect(ssl_);
    if (ret != 1) {
        int err = SSL_get_error(ssl_, ret);
        char errBuf[256];
        ERR_error_string_n(err, errBuf, sizeof(errBuf));
        lastError_ = "TLS handshake failed: " + std::string(errBuf);
        ERR_print_errors_fp(stderr);
        SSL_free(ssl_);
        ssl_ = nullptr;
        return false;
    }
    
    // Verify server certificate (if required)
    if (tlsConfig_.verify_peer) {
        X509* cert = SSL_get_peer_certificate(ssl_);
        if (!cert) {
            lastError_ = "Server did not present a certificate";
            SSL_free(ssl_);
            ssl_ = nullptr;
            return false;
        }
        X509_free(cert);
        
        long verifyResult = SSL_get_verify_result(ssl_);
        if (verifyResult != X509_V_OK) {
            lastError_ = "Certificate verification failed: " + 
                        std::string(X509_verify_cert_error_string(verifyResult));
            SSL_free(ssl_);
            ssl_ = nullptr;
            return false;
        }
    }
    
    return true;
}

void TlsSender::disconnect() {
    if (ssl_) {
        SSL_shutdown(ssl_);
        SSL_free(ssl_);
        ssl_ = nullptr;
    }
    
    closeSocket();
    connected_ = false;
}

void TlsSender::closeSocket() {
    if (socketFd_ >= 0) {
        close(socketFd_);
        socketFd_ = -1;
    }
}

void TlsSender::cleanupOpenSSL() {
    if (sslContext_) {
        SSL_CTX_free(sslContext_);
        sslContext_ = nullptr;
    }
}

bool TlsSender::isConnected() const {
    return connected_;
}

SendResult TlsSender::sendBinary(const BinaryMessage& msg) {
    // Serialize message
    std::vector<uint8_t> data = msg.serialize();
    
    // Send over network
    return sendRaw(data.data(), data.size());
}

SendResult TlsSender::sendRaw(const uint8_t* data, size_t length) {
    if (!connected_) {
        if (!connect()) {
            failedMessages_++;
            return SendResult::NetworkError;
        }
    }
    
    size_t totalSent = 0;
    
    while (totalSent < length) {
        int sent;
        
        if (tlsConfig_.enabled && ssl_) {
            // Send over TLS
            sent = SSL_write(ssl_, data + totalSent, static_cast<int>(length - totalSent));
            
            if (sent <= 0) {
                int err = SSL_get_error(ssl_, sent);
                if (err == SSL_ERROR_WANT_WRITE || err == SSL_ERROR_WANT_READ) {
                    continue; // Retry
                }
                
                lastError_ = "TLS write failed";
                disconnect(); // Force reconnect
                failedMessages_++;
                return SendResult::TlsError;
            }
        } else {
            // Send over plain TCP
            sent = send(socketFd_, reinterpret_cast<const char*>(data + totalSent), 
                       static_cast<int>(length - totalSent), 0);
            
            if (sent < 0) {
                lastError_ = "TCP send failed";
                disconnect();
                failedMessages_++;
                return SendResult::NetworkError;
            }
        }
        
        totalSent += sent;
    }
    
    messagesSent_++;
    bytesSent_ += length;
    return SendResult::Success;
}

} // namespace ResolutePulse
