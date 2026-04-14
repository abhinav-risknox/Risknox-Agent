#pragma once

#include "manager/ca/CertificateAuthority.h"
#include "manager/db/PostgresClient.h"

#include <string>
#include <atomic>
#include <thread>
#include <vector>
#include <functional>
#include <mutex>

// Forward declare OpenSSL types
typedef struct ssl_ctx_st SSL_CTX;
typedef struct ssl_st SSL;

#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
typedef int SOCKET;
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR   (-1)
#define closesocket(s) ::close(s)
#endif

namespace ResolutePulse {

class AgentHandler;

class ManagerServer {
public:
    ManagerServer();
    ~ManagerServer();

    // Initialize the server
    // @param port        - Port to listen on (default 1514)
    // @param ca          - Certificate Authority for issuing certs
    // @param db          - PostgreSQL client for persistence
    bool initialize(int port,
                    CertificateAuthority& ca,
                    PostgresClient& db);

    // Start the server (blocks in accept loop)
    bool start();

    // Stop the server
    void stop();

    // Check if running
    bool isRunning() const { return running_.load(); }

private:
    // Create SSL context for TLS server
    bool createSSLContext();

    // Load server certificates into SSL context
    bool loadCertificates();

    // Bind and start listening
    bool startListening();

    // Main accept loop
    void acceptLoop();

    // Handle a single client connection in a thread
    void handleClient(SOCKET clientSocket, const std::string& clientAddr);

    int               port_ = 1514;
    CertificateAuthority* ca_ = nullptr;
    PostgresClient*       db_ = nullptr;
    SSL_CTX*          sslCtx_ = nullptr;

#ifdef _WIN32
    SOCKET            listenSocket_ = INVALID_SOCKET;
    bool              wsaInitialized_ = false;
#else
    int               listenSocket_ = -1;
#endif

    std::atomic<bool> running_{false};
    std::thread       acceptThread_;
    std::vector<std::thread> clientThreads_;
    std::mutex        threadsMutex_;

    // Server certificate paths (issued by the CA for the manager itself)
    std::string       serverCertPath_;
    std::string       serverKeyPath_;
};

} // namespace ResolutePulse
