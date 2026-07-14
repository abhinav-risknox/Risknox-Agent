#include "RegistrationClient.h"
#include "utils/Logger.h"

#include <openssl/evp.h>
#include <openssl/ec.h>
#include <openssl/pem.h>
#include <openssl/bio.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>

namespace ResolutePulse {

RegistrationClient::RegistrationClient() = default;

RegistrationClient::~RegistrationClient() = default;

bool RegistrationClient::generateKeyPair(const std::string& certsDir) {
    certsDir_ = certsDir;
    std::filesystem::create_directories(certsDir_);

    // Generate ECC P-256 key pair using EVP API
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, nullptr);
    if (!ctx) {
        lastError_ = "Failed to create EVP_PKEY_CTX for EC";
        LOG_ERROR("{}", lastError_);
        return false;
    }

    if (EVP_PKEY_keygen_init(ctx) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        lastError_ = "Failed to init EC key generation";
        LOG_ERROR("{}", lastError_);
        return false;
    }

    if (EVP_PKEY_CTX_set_ec_paramgen_curve_nid(ctx, NID_X9_62_prime256v1) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        lastError_ = "Failed to set P-256 curve";
        LOG_ERROR("{}", lastError_);
        return false;
    }

    EVP_PKEY* key = nullptr;
    if (EVP_PKEY_keygen(ctx, &key) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        lastError_ = "Failed to generate ECC key pair";
        LOG_ERROR("{}", lastError_);
        return false;
    }
    EVP_PKEY_CTX_free(ctx);

    // Save private key encrypted with AES-256-CBC
    std::string keyPath = certsDir_ + "/agent.key";
    // Use memory BIO and std::ofstream to avoid OPENSSL_Applink issues on Windows
    BIO* keyBio = BIO_new(BIO_s_mem());
    const char* passphrase = "ResolutePulse2024";
    if (PEM_write_bio_PrivateKey(keyBio, key, EVP_aes_256_cbc(),
                                 reinterpret_cast<const unsigned char*>(passphrase),
                                 static_cast<int>(strlen(passphrase)),
                                 nullptr, nullptr) > 0) {
        char* keyData = nullptr;
        long keyLen = BIO_get_mem_data(keyBio, &keyData);
        std::ofstream keyFile(keyPath, std::ios::binary);
        keyFile.write(keyData, keyLen);
    }
    BIO_free(keyBio);

    // Extract public key PEM
    BIO* bio = BIO_new(BIO_s_mem());
    PEM_write_bio_PUBKEY(bio, key);

    char* pubData = nullptr;
    long pubLen = BIO_get_mem_data(bio, &pubData);
    publicKeyPem_ = std::string(pubData, pubLen);
    BIO_free(bio);

    EVP_PKEY_free(key);

    LOG_INFO("ECC P-256 key pair generated, private key saved to {}", keyPath);
    return true;
}

bool RegistrationClient::registerWithManager(
    const std::string& host,
    int port,
    const std::string& agentId,
    const std::string& hostname,
    const std::string& osType,
    const std::string& osVersion,
    const std::string& agentVersion,
    const std::string& macAddress,
    const std::string& ipAddress,
    CertificateStore& certStore)
{
    LOG_INFO("Registering with manager at host='{}', port={}...", host, port);

#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

    // Create SSL context for one-way TLS (no client cert)
    const SSL_METHOD* method = TLS_client_method();
    SSL_CTX* sslCtx = SSL_CTX_new(method);
    if (!sslCtx) {
        lastError_ = "Failed to create SSL context";
        LOG_ERROR("{}", lastError_);
        return false;
    }

    SSL_CTX_set_min_proto_version(sslCtx, TLS1_2_VERSION);

    // For initial registration, we may not have the CA cert yet
    // Accept the manager's cert without verification for the first connection
    // The manager will provide the CA cert in the response
    SSL_CTX_set_verify(sslCtx, SSL_VERIFY_NONE, nullptr);

    // Create TCP socket
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        SSL_CTX_free(sslCtx);
        lastError_ = "Failed to create socket";
        LOG_ERROR("{}", lastError_);
        return false;
    }

    // Resolve and connect
    struct addrinfo hints = {}, *result = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    std::string portStr = std::to_string(port);
    if (getaddrinfo(host.c_str(), portStr.c_str(), &hints, &result) != 0) {
        closesocket(sock);
        SSL_CTX_free(sslCtx);
        lastError_ = "Failed to resolve host: " + host;
        LOG_ERROR("{}", lastError_);
        return false;
    }

    if (::connect(sock, result->ai_addr, static_cast<int>(result->ai_addrlen)) != 0) {
        freeaddrinfo(result);
        closesocket(sock);
        SSL_CTX_free(sslCtx);
        lastError_ = "Failed to connect to " + host + ":" + portStr;
        LOG_ERROR("{}", lastError_);
        return false;
    }
    freeaddrinfo(result);

    LOG_INFO("TCP connected to {}:{}", host, port);

    // TLS handshake
    SSL* ssl = SSL_new(sslCtx);
    SSL_set_fd(ssl, static_cast<int>(sock));

    if (SSL_connect(ssl) <= 0) {
        lastError_ = "TLS handshake failed";
        LOG_ERROR("{}", lastError_);

        BIO* errBio = BIO_new(BIO_s_mem());
        ERR_print_errors(errBio);
        char* errData = nullptr;
        long errLen = BIO_get_mem_data(errBio, &errData);
        if (errLen > 0) {
            LOG_ERROR("OpenSSL Errors: {}", std::string(errData, errLen));
        }
        BIO_free(errBio);

        SSL_free(ssl);
        closesocket(sock);
        SSL_CTX_free(sslCtx);
        return false;
    }

    LOG_INFO("TLS connection established with manager");

    // Build registration request
    RegisterRequest request;
    request.agentId      = agentId;
    request.hostname     = hostname;
    request.osType       = osType;
    request.osVersion    = osVersion;
    request.agentVersion = agentVersion;
    request.publicKeyPem = publicKeyPem_;
    request.macAddress   = macAddress;
    request.ipAddress    = ipAddress;

    std::string message = buildMessage(MessageType::REGISTER_REQUEST, request);

    // Send registration request
    int sent = SSL_write(ssl, message.c_str(), static_cast<int>(message.size()));
    if (sent <= 0) {
        lastError_ = "Failed to send registration request";
        LOG_ERROR("{}", lastError_);
        SSL_shutdown(ssl);
        SSL_free(ssl);
        closesocket(sock);
        SSL_CTX_free(sslCtx);
        return false;
    }

    LOG_INFO("Registration request sent");

    // Read response header
    uint8_t headerBuf[MESSAGE_HEADER_SIZE];
    size_t totalRead = 0;
    while (totalRead < MESSAGE_HEADER_SIZE) {
        int n = SSL_read(ssl, headerBuf + totalRead,
                         static_cast<int>(MESSAGE_HEADER_SIZE - totalRead));
        if (n <= 0) {
            lastError_ = "Failed to read response header";
            LOG_ERROR("{}", lastError_);
            SSL_shutdown(ssl);
            SSL_free(ssl);
            closesocket(sock);
            SSL_CTX_free(sslCtx);
            return false;
        }
        totalRead += n;
    }

    MessageHeader respHeader;
    if (!deserializeHeader(headerBuf, respHeader)) {
        lastError_ = "Invalid response header";
        LOG_ERROR("{}", lastError_);
        SSL_shutdown(ssl);
        SSL_free(ssl);
        closesocket(sock);
        SSL_CTX_free(sslCtx);
        return false;
    }

    // Read response payload
    std::string respPayload(respHeader.payloadLength, '\0');
    totalRead = 0;
    while (totalRead < respHeader.payloadLength) {
        int n = SSL_read(ssl, &respPayload[totalRead],
                         static_cast<int>(respHeader.payloadLength - totalRead));
        if (n <= 0) break;
        totalRead += n;
    }

    // Close TLS connection
    SSL_shutdown(ssl);
    SSL_free(ssl);
    closesocket(sock);
    SSL_CTX_free(sslCtx);

#ifdef _WIN32
    WSACleanup();
#endif

    auto respType = static_cast<MessageType>(respHeader.type);

    if (respType == MessageType::REGISTER_REJECT) {
        try {
            auto j = nlohmann::json::parse(respPayload);
            RegisterReject reject = j.get<RegisterReject>();
            lastError_ = "Registration rejected: " + reject.reason;
            LOG_ERROR("{}", lastError_);
        } catch (...) {
            lastError_ = "Registration rejected (unknown reason)";
            LOG_ERROR("{}", lastError_);
        }
        return false;
    }

    if (respType != MessageType::REGISTER_ACCEPT) {
        lastError_ = "Unexpected response type: 0x" + std::to_string(respHeader.type);
        LOG_ERROR("{}", lastError_);
        return false;
    }

    // Parse accept response
    RegisterAccept accept;
    try {
        auto j = nlohmann::json::parse(respPayload);
        accept = j.get<RegisterAccept>();
    } catch (const std::exception& e) {
        lastError_ = "Failed to parse registration accept: " + std::string(e.what());
        LOG_ERROR("{}", lastError_);
        return false;
    }

    // Save certificates via CertificateStore
    if (!certStore.save(accept.certificatePem, accept.caCertPem)) {
        lastError_ = "Failed to save certificates";
        LOG_ERROR("{}", lastError_);
        return false;
    }

    LOG_INFO("Registration successful! agent_id={}, trial={}, expires={}",
             accept.agentId, accept.trial, accept.expiresAt);

    return true;
}

} // namespace ResolutePulse
