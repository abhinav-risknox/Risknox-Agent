#include "ManagerServer.h"
#include "AgentHandler.h"
#include "utils/Logger.h"

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/pem.h>

#include <nlohmann/json.hpp>

#ifdef _WIN32
#include <ws2tcpip.h>
#endif

#include <filesystem>
#include <fstream>
#include <sstream>
#include <atomic>

namespace ResolutePulse {

ManagerServer::ManagerServer() = default;

ManagerServer::~ManagerServer() {
    stop();
    if (sslCtx_) SSL_CTX_free(sslCtx_);
#ifdef _WIN32
    if (listenSocket_ != INVALID_SOCKET) closesocket(listenSocket_);
    if (commandSocket_ != INVALID_SOCKET) closesocket(commandSocket_);
    if (wsaInitialized_) WSACleanup();
#endif
}

bool ManagerServer::initialize(int port,
                                CertificateAuthority& ca,
                                PostgresClient& db)
{
    port_ = port;
    ca_ = &ca;
    db_ = &db;

    LOG_INFO("Initializing Manager Server on port {}", port_);

#ifdef _WIN32
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        LOG_ERROR("WSAStartup failed: {}", result);
        return false;
    }
    wsaInitialized_ = true;
#endif

    // Issue a server certificate for the Manager itself
    serverCertPath_ = ca.getCADir() + "/manager.crt";
    serverKeyPath_  = ca.getCADir() + "/manager.key";

    if (!std::filesystem::exists(serverCertPath_) ||
        !std::filesystem::exists(serverKeyPath_)) {
        LOG_INFO("Generating Manager server certificate...");

        // Generate a key pair for the manager
        EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
        EVP_PKEY_keygen_init(ctx);
        EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, 2048);
        EVP_PKEY* serverKey = nullptr;
        EVP_PKEY_keygen(ctx, &serverKey);
        EVP_PKEY_CTX_free(ctx);

        // Extract public key PEM
        BIO* bio = BIO_new(BIO_s_mem());
        PEM_write_bio_PUBKEY(bio, serverKey);
        char* pubData = nullptr;
        long pubLen = BIO_get_mem_data(bio, &pubData);
        std::string pubKeyPem(pubData, pubLen);
        BIO_free(bio);

        // Issue cert through CA - must use CertType::Server for serverAuth EKU
        auto issued = ca.issueCertificate("ResolutePulse-Manager", pubKeyPem, 365,
                                           CertType::Server);
        if (issued.certificatePem.empty()) {
            LOG_ERROR("Failed to issue server certificate");
            EVP_PKEY_free(serverKey);
            return false;
        }

        // Save server cert using C++ fstream (avoids OPENSSL_Applink)
        {
            std::ofstream ofs(serverCertPath_, std::ios::binary);
            if (ofs) ofs.write(issued.certificatePem.c_str(), issued.certificatePem.size());
        }

        // Save server private key using memory BIO + fstream
        {
            BIO* keyBio = BIO_new(BIO_s_mem());
            PEM_write_bio_PrivateKey(keyBio, serverKey, nullptr, nullptr, 0, nullptr, nullptr);
            char* keyData = nullptr;
            long keyLen = BIO_get_mem_data(keyBio, &keyData);
            std::ofstream ofs(serverKeyPath_, std::ios::binary);
            if (ofs) ofs.write(keyData, keyLen);
            BIO_free(keyBio);
        }

        EVP_PKEY_free(serverKey);
        LOG_INFO("Manager server certificate generated");
    }

    if (!createSSLContext()) {
        LOG_ERROR("Failed to create SSL context");
        return false;
    }

    if (!loadCertificates()) {
        LOG_ERROR("Failed to load certificates");
        return false;
    }

    LOG_INFO("Manager Server initialized");
    return true;
}

bool ManagerServer::createSSLContext() {
    const SSL_METHOD* method = TLS_server_method();
    sslCtx_ = SSL_CTX_new(method);
    if (!sslCtx_) {
        LOG_ERROR("Unable to create SSL context");
        return false;
    }

    // Set minimum TLS version
    SSL_CTX_set_min_proto_version(sslCtx_, TLS1_2_VERSION);

    // Request client certificate but don't require it (for registration)
    // During registration, agents don't have certificates yet
    SSL_CTX_set_verify(sslCtx_, SSL_VERIFY_PEER, nullptr);

    // Load CRL for revocation checking (memory BIO avoids OPENSSL_Applink)
    std::string crlPath = ca_->getCADir() + "/crl.pem";
    if (std::filesystem::exists(crlPath)) {
        X509_STORE* store = SSL_CTX_get_cert_store(sslCtx_);
        X509_STORE_set_flags(store, X509_V_FLAG_CRL_CHECK);

        std::ifstream ifs(crlPath, std::ios::binary);
        if (ifs) {
            std::string crlPem((std::istreambuf_iterator<char>(ifs)),
                                std::istreambuf_iterator<char>());
            BIO* crlBio = BIO_new_mem_buf(crlPem.data(), static_cast<int>(crlPem.size()));
            X509_CRL* crl = PEM_read_bio_X509_CRL(crlBio, nullptr, nullptr, nullptr);
            BIO_free(crlBio);
            if (crl) {
                X509_STORE_add_crl(store, crl);
                X509_CRL_free(crl);
            }
        }
    }

    return true;
}

bool ManagerServer::loadCertificates() {
    // Load manager certificate from file into memory, then into SSL context
    {
        std::ifstream ifs(serverCertPath_, std::ios::binary);
        if (!ifs) {
            LOG_ERROR("Cannot open server certificate: {}", serverCertPath_);
            return false;
        }
        std::string certPem((std::istreambuf_iterator<char>(ifs)),
                             std::istreambuf_iterator<char>());
        BIO* bio = BIO_new_mem_buf(certPem.data(), static_cast<int>(certPem.size()));
        X509* cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
        BIO_free(bio);
        if (!cert || SSL_CTX_use_certificate(sslCtx_, cert) <= 0) {
            LOG_ERROR("Failed to load server certificate: {}", serverCertPath_);
            if (cert) X509_free(cert);
            return false;
        }
        X509_free(cert);
    }

    // Load manager private key from file into memory, then into SSL context
    {
        std::ifstream ifs(serverKeyPath_, std::ios::binary);
        if (!ifs) {
            LOG_ERROR("Cannot open server private key: {}", serverKeyPath_);
            return false;
        }
        std::string keyPem((std::istreambuf_iterator<char>(ifs)),
                            std::istreambuf_iterator<char>());
        BIO* bio = BIO_new_mem_buf(keyPem.data(), static_cast<int>(keyPem.size()));
        EVP_PKEY* key = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
        BIO_free(bio);
        if (!key || SSL_CTX_use_PrivateKey(sslCtx_, key) <= 0) {
            LOG_ERROR("Failed to load server private key: {}", serverKeyPath_);
            if (key) EVP_PKEY_free(key);
            return false;
        }
        EVP_PKEY_free(key);
    }

    // Load CA certificate for client verification (memory-based)
    {
        std::string caCertPath = ca_->getCADir() + "/ca.crt";
        std::ifstream ifs(caCertPath, std::ios::binary);
        if (!ifs) {
            LOG_ERROR("Cannot open CA certificate: {}", caCertPath);
            return false;
        }
        std::string caPem((std::istreambuf_iterator<char>(ifs)),
                           std::istreambuf_iterator<char>());
        BIO* bio = BIO_new_mem_buf(caPem.data(), static_cast<int>(caPem.size()));
        X509* caCert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
        BIO_free(bio);
        if (!caCert) {
            LOG_ERROR("Failed to parse CA certificate");
            return false;
        }
        X509_STORE* store = SSL_CTX_get_cert_store(sslCtx_);
        X509_STORE_add_cert(store, caCert);
        X509_free(caCert);
    }

    return true;
}

bool ManagerServer::startListening() {
    listenSocket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket_ == INVALID_SOCKET) {
        LOG_ERROR("Failed to create listen socket");
        return false;
    }

    // Allow reuse of address
    int opt = 1;
    setsockopt(listenSocket_, SOL_SOCKET, SO_REUSEADDR, 
               reinterpret_cast<const char*>(&opt), sizeof(opt));

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<uint16_t>(port_));

    if (bind(listenSocket_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        LOG_ERROR("Failed to bind to port {}", port_);
        return false;
    }

    if (listen(listenSocket_, 10) == SOCKET_ERROR) {
        LOG_ERROR("Failed to listen on port {}", port_);
        return false;
    }

    LOG_INFO("Manager listening on port {}", port_);
    return true;
}

bool ManagerServer::start() {
    if (!startListening()) {
        return false;
    }

    running_ = true;
    acceptThread_        = std::thread(&ManagerServer::acceptLoop, this);
    commandIngestThread_ = std::thread(&ManagerServer::commandIngestLoop, this);
    return true;
}

void ManagerServer::stop() {
    running_ = false;

#ifdef _WIN32
    if (listenSocket_ != INVALID_SOCKET) {
        closesocket(listenSocket_);
        listenSocket_ = INVALID_SOCKET;
    }
    if (commandSocket_ != INVALID_SOCKET) {
        closesocket(commandSocket_);
        commandSocket_ = INVALID_SOCKET;
    }
#endif

    if (acceptThread_.joinable())        acceptThread_.join();
    if (commandIngestThread_.joinable()) commandIngestThread_.join();

    // Wait for all client threads
    std::lock_guard<std::mutex> lock(threadsMutex_);
    for (auto& t : clientThreads_) {
        if (t.joinable()) t.join();
    }
    clientThreads_.clear();

    LOG_INFO("Manager Server stopped");
}

void ManagerServer::acceptLoop() {
    LOG_INFO("Accept loop started");

    while (running_.load()) {
        struct sockaddr_in clientAddr = {};
        socklen_t addrLen = sizeof(clientAddr);

        SOCKET clientSocket = accept(listenSocket_,
            reinterpret_cast<struct sockaddr*>(&clientAddr), &addrLen);

        if (clientSocket == INVALID_SOCKET) {
            if (running_.load()) {
                LOG_WARN("Accept failed");
            }
            continue;
        }

        // Get client IP
        char ipStr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientAddr.sin_addr, ipStr, sizeof(ipStr));
        std::string clientIp(ipStr);

        LOG_INFO("New connection from {}", clientIp);

        // Spawn a thread to handle this client
        std::lock_guard<std::mutex> lock(threadsMutex_);
        clientThreads_.emplace_back(&ManagerServer::handleClient, this, clientSocket, clientIp);
    }
}

void ManagerServer::handleClient(SOCKET clientSocket, const std::string& clientAddr) {
    // Create SSL connection
    SSL* ssl = SSL_new(sslCtx_);
    SSL_set_fd(ssl, static_cast<int>(clientSocket));

    if (SSL_accept(ssl) <= 0) {
        LOG_WARN("TLS handshake failed from {}", clientAddr);
        
        // Log OpenSSL errors without using _fp (to avoid OPENSSL_Applink issues)
        BIO* errBio = BIO_new(BIO_s_mem());
        ERR_print_errors(errBio);
        char* errData = nullptr;
        long errLen = BIO_get_mem_data(errBio, &errData);
        if (errLen > 0) {
            LOG_ERROR("OpenSSL Errors: {}", std::string(errData, errLen));
        }
        BIO_free(errBio);

        SSL_free(ssl);
        closesocket(clientSocket);
        return;
    }

    LOG_INFO("TLS connection established with {}", clientAddr);

    // Check if client presented a certificate (mTLS)
    X509* clientCert = SSL_get_peer_certificate(ssl);
    bool hasClientCert = (clientCert != nullptr);
    if (clientCert) X509_free(clientCert);

    // Create handler and process with persistent connection (Keep-Alive)
    AgentHandler handler(*ca_, *db_, this);
    std::string agentId;

    while (running_.load()) {
        if (!handler.handleConnection(ssl, clientAddr, hasClientCert)) {
            break; 
        }
        // After first successful authenticated message, retrieve the agentId
        // and register in the session map
        if (agentId.empty() && hasClientCert) {
            agentId = handler.getLastAgentId();
            if (!agentId.empty()) {
                registerSession(agentId, ssl);
            }
        }
    }

    // Cleanup - unregister session
    if (!agentId.empty()) {
        unregisterSession(agentId);
    }

    SSL_shutdown(ssl);
    SSL_free(ssl);
    closesocket(clientSocket);
    LOG_DEBUG("Connection closed: {}", clientAddr);
}

// ─────────────────────────────────────────────────────────────
// Session registry
// ─────────────────────────────────────────────────────────────

void ManagerServer::registerSession(const std::string& agentId, SSL* ssl) {
    std::lock_guard<std::mutex> lk(sessionsMutex_);
    activeSessions_[agentId] = ssl;
    LOG_INFO("Session registered: agent={}", agentId);
}

void ManagerServer::unregisterSession(const std::string& agentId) {
    std::lock_guard<std::mutex> lk(sessionsMutex_);
    activeSessions_.erase(agentId);
    LOG_INFO("Session unregistered: agent={}", agentId);
}

// ─────────────────────────────────────────────────────────────
// Command ingest socket (127.0.0.1:1515)
// ─────────────────────────────────────────────────────────────

void ManagerServer::commandIngestLoop() {
    commandSocket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (commandSocket_ == INVALID_SOCKET) {
        LOG_ERROR("Failed to create command ingest socket");
        return;
    }

    int opt = 1;
    setsockopt(commandSocket_, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&opt), sizeof(opt));

    struct sockaddr_in addr = {};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");  // loopback ONLY
    addr.sin_port        = htons(1515);

    if (bind(commandSocket_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        LOG_ERROR("Failed to bind command ingest socket on 127.0.0.1:1515");
        closesocket(commandSocket_);
        commandSocket_ = INVALID_SOCKET;
        return;
    }

    listen(commandSocket_, 8);
    LOG_INFO("Command ingest socket listening on 127.0.0.1:1515");

    while (running_.load()) {
        SOCKET client = accept(commandSocket_, nullptr, nullptr);
        if (client == INVALID_SOCKET) {
            if (running_.load()) LOG_WARN("Command ingest accept failed");
            continue;
        }

        // Read the full JSON payload - commands are small, one recv is enough
        char buf[65536];
        int n = recv(client, buf, static_cast<int>(sizeof(buf) - 1), 0);
        closesocket(client);

        if (n <= 0) continue;
        buf[n] = '\0';

        try {
            auto cmd        = nlohmann::json::parse(buf);
            std::string aid = cmd.at("agent_id").get<std::string>();

            // command_type distinguishes module control commands from policy updates.
            // Defaults to "policy" for full backward compatibility with existing callers.
            std::string cmdClass = cmd.value("command_type", "policy");

            if (cmdClass == "module") {
                // MODULE_COMMAND: verb + optional params
                std::string verb      = cmd.at("verb").get<std::string>();
                std::string commandId = cmd.value("command_id", "");
                if (commandId.empty()) {
                    // Generate a simple UUID-like ID if caller didn't provide one
                    commandId = aid + "-" + verb + "-" + std::to_string(time(nullptr));
                }
                auto params = cmd.value("params", nlohmann::json::object());
                LOG_INFO("Command ingest [module]: agent={} verb={} commandId={}", aid, verb, commandId);
                dispatchModuleCommand(aid, commandId, verb, params);
            } else {
                // POLICY_UPDATE (legacy / default path)
                std::string pt  = cmd.at("policy_type").get<std::string>();
                auto pd         = cmd.at("policy_data");
                LOG_INFO("Command ingest [policy]: agent={} type={}", aid, pt);
                dispatchCommand(aid, pt, pd);
            }
        } catch (const std::exception& e) {
            LOG_WARN("Command ingest: malformed JSON - {}", e.what());
        }
    }

    LOG_INFO("Command ingest loop stopped");
}

void ManagerServer::dispatchCommand(const std::string& agentId,
                                     const std::string& policyType,
                                     const nlohmann::json& policyData) {
    // Collision-safe commandId: timestamp + monotonic counter
    static std::atomic<uint64_t> seq{0};
    std::string commandId = agentId + "-" + policyType + "-" +
                            std::to_string(time(nullptr)) + "-" +
                            std::to_string(seq.fetch_add(1));

    // Step 1: write-ahead — always insert as 'pending' so an audit row exists
    // regardless of whether the agent is currently connected.
    db_->recordPolicyCommand(agentId, commandId, policyType, policyData.dump(), /*pending=*/true);

    // Step 2: attempt immediate online delivery
    SSL* ssl = nullptr;
    {
        std::lock_guard<std::mutex> lk(sessionsMutex_);
        auto it = activeSessions_.find(agentId);
        if (it != activeSessions_.end()) ssl = it->second;
    }

    if (!ssl) {
        LOG_INFO("dispatchCommand: agent {} offline — command queued for heartbeat drain", agentId);
        return;  // row stays 'pending'; heartbeat drain retries on reconnect
    }

    AgentHandler handler(*ca_, *db_, this);
    bool ok = handler.pushPolicyUpdate(ssl, agentId, commandId, policyType, policyData);
    if (ok) {
        LOG_INFO("Policy '{}' dispatched to agent {}", policyType, agentId);
        db_->markPolicyCommandDispatched(commandId);  // pending → sent
    } else {
        LOG_ERROR("Policy '{}' push failed for agent {} — will retry on next heartbeat",
                  policyType, agentId);
        // row stays 'pending'; heartbeat drain retries automatically
    }
}

void ManagerServer::dispatchModuleCommand(const std::string& agentId,
                                           const std::string& commandId,
                                           const std::string& verb,
                                           const nlohmann::json& params) {
    // Always insert the command into the DB first (online and offline paths both need an audit row)
    db_->recordModuleCommand(agentId, commandId, verb, params.dump(), /*pending=*/true);

    SSL* ssl = nullptr;
    {
        std::lock_guard<std::mutex> lk(sessionsMutex_);
        auto it = activeSessions_.find(agentId);
        if (it != activeSessions_.end()) ssl = it->second;
    }

    if (!ssl) {
        LOG_INFO("dispatchModuleCommand: agent {} not connected - leaving queued (verb={})",
                 agentId, verb);
        return;
    }

    AgentHandler handler(*ca_, *db_, this);
    bool ok = handler.pushModuleCommand(ssl, agentId, commandId, verb, params);
    if (ok) {
        LOG_INFO("MODULE_COMMAND '{}' dispatched to agent {} (commandId={})",
                 verb, agentId, commandId);
        // Update status to 'sent' and set dispatched_at
        db_->markModuleCommandDispatched(commandId);
    } else {
        LOG_ERROR("Failed to dispatch MODULE_COMMAND '{}' to agent {} - left as pending for offline delivery", verb, agentId);
    }
}

} // namespace ResolutePulse

