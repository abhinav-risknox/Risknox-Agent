// =============================================================================
// test_manager_server.cpp — Integration tests for ManagerServer + AgentHandler
//
// Uses:
//   • Real CertificateAuthority (temp dir)
//   • Mock libpq (extern "C" stubs — no database needed)
//   • Real TCP + TLS client to exercise the full registration path
// =============================================================================

#include "manager/server/ManagerServer.h"
#include "manager/server/AgentHandler.h"
#include "manager/ca/CertificateAuthority.h"
#include "manager/db/PostgresClient.h"
#include "common/Protocol.h"
#include "utils/Logger.h"

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/bio.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include <iostream>
#include <cassert>
#include <cstring>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <filesystem>
#include <set>

using namespace ResolutePulse;

// =============================================================================
// Mock libpq implementation (same pattern as test_postgres_client.cpp)
// =============================================================================

struct pg_conn {
    std::string errorMessage;
    int status;
};

struct pg_result {
    int status;
    std::vector<std::vector<std::string>> rows;
};

// Global mock state
static pg_conn* g_mockConn = nullptr;
static int g_mockConnStatus = 0; // 0 = CONNECTION_OK
static std::string g_mockErrorMessage = "";

static pg_result* g_mockResult = nullptr;
static std::string g_lastQuery = "";
static std::vector<std::string> g_lastParams = {};

// Track which agents have been "inserted" so agentExists can return correctly
static std::set<std::string> g_registeredAgents;

extern "C" {
    typedef enum {
        CONNECTION_OK,
        CONNECTION_BAD,
        CONNECTION_STARTED,
        CONNECTION_MADE,
        CONNECTION_AWAITING_RESPONSE,
        CONNECTION_AUTH_OK,
        CONNECTION_SETENV,
        CONNECTION_SSL_STARTUP,
        CONNECTION_NEEDED,
        CONNECTION_CHECK_WRITABLE,
        CONNECTION_CONSUME,
        CONNECTION_GSS_STARTUP
    } ConnStatusType;

    typedef enum {
        PGRES_EMPTY_QUERY = 0,
        PGRES_COMMAND_OK,
        PGRES_TUPLES_OK,
        PGRES_COPY_OUT,
        PGRES_COPY_IN,
        PGRES_BAD_RESPONSE,
        PGRES_NONFATAL_ERROR,
        PGRES_FATAL_ERROR,
        PGRES_COPY_BOTH,
        PGRES_SINGLE_TUPLE,
        PGRES_PIPELINE_SYNC,
        PGRES_PIPELINE_ABORTED
    } ExecStatusType;

    typedef struct pg_conn PGconn;
    typedef struct pg_result PGresult;

    PGconn* PQconnectdb(const char* conninfo) {
        g_mockConn = new PGconn();
        g_mockConn->status = g_mockConnStatus;
        g_mockConn->errorMessage = g_mockErrorMessage;
        return g_mockConn;
    }

    void PQfinish(PGconn* conn) {
        if (conn == g_mockConn) {
            delete g_mockConn;
            g_mockConn = nullptr;
        }
    }

    ConnStatusType PQstatus(const PGconn* conn) {
        if (!conn) return CONNECTION_BAD;
        return (ConnStatusType)conn->status;
    }

    char* PQerrorMessage(const PGconn* conn) {
        if (!conn) return (char*)"No connection";
        return (char*)conn->errorMessage.c_str();
    }

    PGresult* PQexec(PGconn* conn, const char* query) {
        g_lastQuery = query;
        // Return a success result for any exec
        static pg_result defaultResult;
        defaultResult.status = 1; // PGRES_COMMAND_OK
        defaultResult.rows.clear();
        return &defaultResult;
    }

    PGresult* PQexecParams(PGconn* conn, const char* command, int nParams,
                          const void* const * paramTypes, const char* const * paramValues,
                          const int* paramLengths, const int* paramFormats, int resultFormat) {
        g_lastQuery = command;
        g_lastParams.clear();
        for (int i = 0; i < nParams; ++i) {
            g_lastParams.push_back(paramValues[i] ? paramValues[i] : "NULL");
        }

        // Smart mock: check query type to return appropriate results
        std::string query(command);

        // agentExists: "SELECT 1 FROM agents WHERE agent_id = $1"
        if (query.find("SELECT 1 FROM agents") != std::string::npos) {
            static pg_result existsResult;
            if (nParams > 0 && g_registeredAgents.count(paramValues[0])) {
                existsResult.status = 2; // PGRES_TUPLES_OK
                existsResult.rows = {{"1"}};
            } else {
                existsResult.status = 2; // PGRES_TUPLES_OK
                existsResult.rows.clear(); // no rows = doesn't exist
            }
            return &existsResult;
        }

        // INSERT INTO agents — track the agent ID
        if (query.find("INSERT INTO agents") != std::string::npos) {
            if (nParams > 0) {
                g_registeredAgents.insert(paramValues[0]);
            }
            static pg_result insertResult;
            insertResult.status = 1; // PGRES_COMMAND_OK
            insertResult.rows.clear();
            return &insertResult;
        }

        // INSERT INTO certificates
        if (query.find("INSERT INTO certificates") != std::string::npos) {
            static pg_result certResult;
            certResult.status = 1; // PGRES_COMMAND_OK
            certResult.rows.clear();
            return &certResult;
        }

        // UPDATE operations
        if (query.find("UPDATE") != std::string::npos) {
            static pg_result updateResult;
            updateResult.status = 1; // PGRES_COMMAND_OK
            updateResult.rows.clear();
            return &updateResult;
        }

        // getLicense: return no rows (trial license)
        if (query.find("licenses") != std::string::npos) {
            static pg_result licenseResult;
            licenseResult.status = 2; // PGRES_TUPLES_OK
            licenseResult.rows.clear(); // no license = trial
            return &licenseResult;
        }

        // Default: COMMAND_OK
        static pg_result defaultResult;
        defaultResult.status = 1;
        defaultResult.rows.clear();
        return &defaultResult;
    }

    ExecStatusType PQresultStatus(const PGresult* res) {
        if (!res) return PGRES_FATAL_ERROR;
        return (ExecStatusType)res->status;
    }

    void PQclear(PGresult* res) {
        // Static results — no cleanup needed
    }

    int PQntuples(const PGresult* res) {
        if (!res) return 0;
        return (int)res->rows.size();
    }

    char* PQgetvalue(const PGresult* res, int tup_num, int field_num) {
        if (!res || tup_num >= (int)res->rows.size() || field_num >= (int)res->rows[tup_num].size()) {
            return (char*)"";
        }
        return (char*)res->rows[tup_num][field_num].c_str();
    }

    int PQgetisnull(const PGresult* res, int tup_num, int field_num) {
        if (!res || tup_num >= (int)res->rows.size() || field_num >= (int)res->rows[tup_num].size()) {
            return 1;
        }
        return 0;
    }
}

// =============================================================================
// Helper: Generate an RSA key pair and return public key PEM
// =============================================================================
static std::string generateAgentKeyPair(EVP_PKEY** outKey) {
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
    EVP_PKEY_keygen_init(ctx);
    EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, 2048);
    EVP_PKEY* key = nullptr;
    EVP_PKEY_keygen(ctx, &key);
    EVP_PKEY_CTX_free(ctx);

    BIO* bio = BIO_new(BIO_s_mem());
    PEM_write_bio_PUBKEY(bio, key);
    char* data = nullptr;
    long len = BIO_get_mem_data(bio, &data);
    std::string pubPem(data, len);
    BIO_free(bio);

    if (outKey) {
        *outKey = key;
    } else {
        EVP_PKEY_free(key);
    }
    return pubPem;
}

// =============================================================================
// Helper: Create a TLS client connection to the server, perform handshake,
//         send a registration request, and read the response.
// Returns the raw response payload (JSON string) and the response message type.
// =============================================================================
struct TlsResponse {
    MessageType type;
    std::string payload;
    bool success;
};

static TlsResponse sendRegistration(int port, const std::string& caCertPath,
                                     const RegisterRequest& request) {
    TlsResponse resp;
    resp.success = false;

    // -- Create TCP socket --
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        std::cerr << "[ERROR] Cannot create socket\n";
        return resp;
    }

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    if (connect(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        std::cerr << "[ERROR] Cannot connect to server on port " << port << "\n";
        closesocket(sock);
        return resp;
    }

    // -- Create TLS client context --
    SSL_CTX* clientCtx = SSL_CTX_new(TLS_client_method());
    SSL_CTX_set_min_proto_version(clientCtx, TLS1_2_VERSION);

    // Load CA certificate so the client trusts the server
    if (SSL_CTX_load_verify_locations(clientCtx, caCertPath.c_str(), nullptr) <= 0) {
        std::cerr << "[ERROR] Client cannot load CA cert\n";
        ERR_print_errors_fp(stderr);
        SSL_CTX_free(clientCtx);
        closesocket(sock);
        return resp;
    }

    // Don't verify server cert strictly in tests (self-signed chain)
    SSL_CTX_set_verify(clientCtx, SSL_VERIFY_NONE, nullptr);

    SSL* ssl = SSL_new(clientCtx);
    SSL_set_fd(ssl, static_cast<int>(sock));

    if (SSL_connect(ssl) <= 0) {
        std::cerr << "[ERROR] TLS handshake failed\n";
        ERR_print_errors_fp(stderr);
        SSL_free(ssl);
        SSL_CTX_free(clientCtx);
        closesocket(sock);
        return resp;
    }

    // -- Build and send registration message --
    std::string msg = buildMessage(MessageType::REGISTER_REQUEST, request);

    int written = SSL_write(ssl, msg.c_str(), static_cast<int>(msg.size()));
    if (written <= 0) {
        std::cerr << "[ERROR] SSL_write failed\n";
        SSL_shutdown(ssl);
        SSL_free(ssl);
        SSL_CTX_free(clientCtx);
        closesocket(sock);
        return resp;
    }

    // -- Read response header --
    uint8_t headerBuf[MESSAGE_HEADER_SIZE];
    int totalRead = 0;
    while (totalRead < MESSAGE_HEADER_SIZE) {
        int n = SSL_read(ssl, headerBuf + totalRead, MESSAGE_HEADER_SIZE - totalRead);
        if (n <= 0) {
            std::cerr << "[ERROR] Failed to read response header\n";
            SSL_shutdown(ssl);
            SSL_free(ssl);
            SSL_CTX_free(clientCtx);
            closesocket(sock);
            return resp;
        }
        totalRead += n;
    }

    MessageHeader respHeader;
    if (!deserializeHeader(headerBuf, respHeader)) {
        std::cerr << "[ERROR] Invalid response header\n";
        SSL_shutdown(ssl);
        SSL_free(ssl);
        SSL_CTX_free(clientCtx);
        closesocket(sock);
        return resp;
    }

    resp.type = static_cast<MessageType>(respHeader.type);

    // -- Read response payload --
    std::string payloadBuf(respHeader.payloadLength, '\0');
    totalRead = 0;
    while (totalRead < (int)respHeader.payloadLength) {
        int n = SSL_read(ssl, &payloadBuf[totalRead],
                         static_cast<int>(respHeader.payloadLength) - totalRead);
        if (n <= 0) {
            std::cerr << "[ERROR] Failed to read response payload\n";
            SSL_shutdown(ssl);
            SSL_free(ssl);
            SSL_CTX_free(clientCtx);
            closesocket(sock);
            return resp;
        }
        totalRead += n;
    }

    resp.payload = payloadBuf;
    resp.success = true;

    SSL_shutdown(ssl);
    SSL_free(ssl);
    SSL_CTX_free(clientCtx);
    closesocket(sock);
    return resp;
}

// =============================================================================
// TESTS
// =============================================================================

static const std::string TEST_CA_DIR = "test_manager_server_ca";
static const int TEST_PORT = 15140; // high port to avoid conflicts

int main() {
    Logger::initialize("debug");

    // Clean up from any previous run
    if (std::filesystem::exists(TEST_CA_DIR)) {
        std::filesystem::remove_all(TEST_CA_DIR);
    }

    // Initialize shared CA — used by all tests
    CertificateAuthority ca;
    bool caOk = ca.initializeCA(TEST_CA_DIR);
    assert(caOk && "CA must initialize for tests to proceed");

    // Initialize mock DB
    PostgresClient db;
    g_mockConnStatus = 0; // CONNECTION_OK
    db.connect("host=localhost dbname=test_mock");

    // ═════════════════════════════════════════════════════════════
    // Test 1: Server Construction — default state
    // ═════════════════════════════════════════════════════════════
    std::cout << "\n========== TEST 1: Server Construction ==========\n";
    {
        ManagerServer server;
        assert(!server.isRunning() && "Freshly constructed server must NOT be running");
        std::cout << "[PASS] Default server is not running\n";
    }

    // ═════════════════════════════════════════════════════════════
    // Test 2: Initialize with Real CA
    // ═════════════════════════════════════════════════════════════
    std::cout << "\n========== TEST 2: Initialize with Real CA ==========\n";
    {
        ManagerServer server;
        bool initOk = server.initialize(TEST_PORT, ca, db);
        assert(initOk && "initialize() must return true with valid CA and DB");

        // Verify the manager cert/key were generated
        assert(std::filesystem::exists(TEST_CA_DIR + "/manager.crt") &&
               "Manager certificate must exist after initialize()");
        assert(std::filesystem::exists(TEST_CA_DIR + "/manager.key") &&
               "Manager private key must exist after initialize()");

        std::cout << "[PASS] Server initialized successfully with real CA\n";
    }

    // ═════════════════════════════════════════════════════════════
    // Test 3: Start / Stop Lifecycle
    // ═════════════════════════════════════════════════════════════
    std::cout << "\n========== TEST 3: Start / Stop Lifecycle ==========\n";
    {
        ManagerServer server;
        bool initOk = server.initialize(TEST_PORT + 1, ca, db);
        assert(initOk && "initialize() must succeed");

        bool startOk = server.start();
        assert(startOk && "start() must return true");
        assert(server.isRunning() && "Server must be running after start()");

        // Small delay to let the accept loop spin up
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        server.stop();
        assert(!server.isRunning() && "Server must NOT be running after stop()");

        std::cout << "[PASS] Start/stop lifecycle works correctly\n";
    }

    // ═════════════════════════════════════════════════════════════
    // Test 4: Agent Registration — End to End
    // ═════════════════════════════════════════════════════════════
    std::cout << "\n========== TEST 4: Agent Registration E2E ==========\n";
    {
        g_registeredAgents.clear(); // reset mock state

        ManagerServer server;
        bool initOk = server.initialize(TEST_PORT + 2, ca, db);
        assert(initOk && "initialize() must succeed");

        bool startOk = server.start();
        assert(startOk && "start() must succeed");

        std::this_thread::sleep_for(std::chrono::milliseconds(300));

        // Generate an agent key pair
        EVP_PKEY* agentKey = nullptr;
        std::string agentPubPem = generateAgentKeyPair(&agentKey);
        EVP_PKEY_free(agentKey);

        // Build registration request
        RegisterRequest request;
        request.agentId = "test-agent-e2e-001";
        request.hostname = "test-host";
        request.osType = "Windows";
        request.osVersion = "10.0";
        request.agentVersion = "1.0.0";
        request.publicKeyPem = agentPubPem;

        std::string caCertPath = TEST_CA_DIR + "/ca.crt";
        TlsResponse resp = sendRegistration(TEST_PORT + 2, caCertPath, request);

        assert(resp.success && "TLS registration request must succeed");
        assert(resp.type == MessageType::REGISTER_ACCEPT &&
               "Server must respond with REGISTER_ACCEPT");

        // Parse the response
        auto j = nlohmann::json::parse(resp.payload);
        RegisterAccept accept = j.get<RegisterAccept>();

        assert(accept.status == "authorized" && "Status must be 'authorized'");
        assert(accept.agentId == "test-agent-e2e-001" && "Agent ID must match");
        assert(!accept.certificatePem.empty() && "Certificate PEM must not be empty");
        assert(!accept.caCertPem.empty() && "CA cert PEM must not be empty");
        assert(!accept.expiresAt.empty() && "Expiry must not be empty");
        assert(accept.trial == true && "Must be trial (no license in mock)");

        // Verify the issued certificate is valid PEM
        BIO* certBio = BIO_new_mem_buf(accept.certificatePem.data(),
                                        static_cast<int>(accept.certificatePem.size()));
        X509* cert = PEM_read_bio_X509(certBio, nullptr, nullptr, nullptr);
        BIO_free(certBio);
        assert(cert != nullptr && "Issued certificate must be valid PEM");

        // Verify CN matches agent ID
        X509_NAME* subject = X509_get_subject_name(cert);
        char cn[256] = {};
        X509_NAME_get_text_by_NID(subject, NID_commonName, cn, sizeof(cn));
        assert(std::string(cn) == "test-agent-e2e-001" && "Certificate CN must match agent ID");

        X509_free(cert);

        server.stop();
        std::cout << "[PASS] Agent registration E2E succeeded\n";
        std::cout << "[INFO] Agent ID: " << accept.agentId << "\n";
        std::cout << "[INFO] Trial: " << (accept.trial ? "yes" : "no") << "\n";
        std::cout << "[INFO] Expires: " << accept.expiresAt << "\n";
    }

    // ═════════════════════════════════════════════════════════════
    // Test 5: Duplicate Registration Rejection
    // ═════════════════════════════════════════════════════════════
    std::cout << "\n========== TEST 5: Duplicate Registration Rejection ==========\n";
    {
        // Don't clear g_registeredAgents — "test-agent-e2e-001" is already registered

        ManagerServer server;
        bool initOk = server.initialize(TEST_PORT + 3, ca, db);
        assert(initOk && "initialize() must succeed");
        bool startOk = server.start();
        assert(startOk && "start() must succeed");

        std::this_thread::sleep_for(std::chrono::milliseconds(300));

        EVP_PKEY* agentKey = nullptr;
        std::string agentPubPem = generateAgentKeyPair(&agentKey);
        EVP_PKEY_free(agentKey);

        RegisterRequest request;
        request.agentId = "test-agent-e2e-001"; // same agent ID
        request.hostname = "test-host-2";
        request.osType = "Linux";
        request.osVersion = "5.15";
        request.agentVersion = "1.0.0";
        request.publicKeyPem = agentPubPem;

        std::string caCertPath = TEST_CA_DIR + "/ca.crt";
        TlsResponse resp = sendRegistration(TEST_PORT + 3, caCertPath, request);

        assert(resp.success && "TLS request must succeed");
        assert(resp.type == MessageType::REGISTER_REJECT &&
               "Server must respond with REGISTER_REJECT for duplicate");

        auto j = nlohmann::json::parse(resp.payload);
        RegisterReject reject = j.get<RegisterReject>();

        assert(reject.status == "rejected" && "Status must be 'rejected'");
        assert(reject.errorCode == 409 && "Error code must be 409 (conflict)");
        assert(reject.reason.find("already registered") != std::string::npos &&
               "Reason must mention 'already registered'");

        server.stop();
        std::cout << "[PASS] Duplicate registration correctly rejected (409)\n";
        std::cout << "[INFO] Reason: " << reject.reason << "\n";
    }

    // ═════════════════════════════════════════════════════════════
    // Test 6: Invalid Public Key Rejection
    // ═════════════════════════════════════════════════════════════
    std::cout << "\n========== TEST 6: Invalid Public Key Rejection ==========\n";
    {
        ManagerServer server;
        bool initOk = server.initialize(TEST_PORT + 4, ca, db);
        assert(initOk && "initialize() must succeed");
        bool startOk = server.start();
        assert(startOk && "start() must succeed");

        std::this_thread::sleep_for(std::chrono::milliseconds(300));

        RegisterRequest request;
        request.agentId = "test-agent-bad-key-001";
        request.hostname = "test-host-bad";
        request.osType = "Windows";
        request.osVersion = "11.0";
        request.agentVersion = "1.0.0";
        request.publicKeyPem = "THIS-IS-NOT-A-VALID-PEM-KEY";

        std::string caCertPath = TEST_CA_DIR + "/ca.crt";
        TlsResponse resp = sendRegistration(TEST_PORT + 4, caCertPath, request);

        assert(resp.success && "TLS request must succeed");
        assert(resp.type == MessageType::REGISTER_REJECT &&
               "Server must respond with REGISTER_REJECT for invalid key");

        auto j = nlohmann::json::parse(resp.payload);
        RegisterReject reject = j.get<RegisterReject>();

        assert(reject.status == "rejected" && "Status must be 'rejected'");
        assert(reject.errorCode == 401 && "Error code must be 401");
        assert(reject.reason.find("public key") != std::string::npos &&
               "Reason must mention 'public key'");

        server.stop();
        std::cout << "[PASS] Invalid public key correctly rejected (401)\n";
        std::cout << "[INFO] Reason: " << reject.reason << "\n";
    }

    // ═════════════════════════════════════════════════════════════
    // Cleanup
    // ═════════════════════════════════════════════════════════════
    std::cout << "\n========== CLEANUP ==========\n";
    db.disconnect();
    std::filesystem::remove_all(TEST_CA_DIR);
    std::cout << "[CLEANUP] Removed test CA directory\n";

    if (g_mockConn) { delete g_mockConn; g_mockConn = nullptr; }

    std::cout << "\n=== ALL MANAGER SERVER TESTS PASSED ===\n\n";
    return 0;
}
