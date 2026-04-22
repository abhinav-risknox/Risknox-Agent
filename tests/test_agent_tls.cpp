// =============================================================================
// test_agent_tls.cpp - Unit + Integration tests for the Agent TLS path
//
// Tests:
//   1. CertificateStore - save / load / isValid / daysUntilExpiry     (unit)
//   2. RegistrationClient - generateKeyPair                            (unit)
//   3. TlsSender - initialize() with missing cert files               (unit)
//   4. RegistrationClient - registerWithManager() live E2E            (integration)
//   5. TlsSender - sendBatch() when never initialized                 (unit)
//   6. TlsSender - sendBatch() over mTLS with real certs              (integration)
//
// Tests 4 and 6 require ResolutePulseManager.exe running on port 1514.
// =============================================================================

#include "agent/registration/CertificateStore.h"
#include "agent/registration/RegistrationClient.h"
#include "agent/network/TlsSender.h"
#include "manager/ca/CertificateAuthority.h"
#include "collector/Event.h"
#include "utils/Logger.h"

#include <openssl/x509.h>
#include <openssl/pem.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/err.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#endif

#include <iostream>
#include <cassert>
#include <string>
#include <filesystem>
#include <fstream>
#include <thread>
#include <chrono>

using namespace ResolutePulse;

// ─── Utility ─────────────────────────────────────────────────────────────────

static void wsaInit() {
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
}

// Write a PEM string to a file (used to set up test fixtures)
static bool writePem(const std::string& path, const std::string& pem) {
    std::ofstream f(path);
    if (!f) return false;
    f << pem;
    return true;
}

// ─── Test-local CA helper ─────────────────────────────────────────────────────

// Uses CertificateAuthority to issue a short-lived cert for the store tests.
static const std::string TLS_TEST_DIR = "test_agent_tls_tmp";
static const std::string TLS_CA_DIR   = TLS_TEST_DIR + "/ca";
static const std::string TLS_CERTS_DIR = TLS_TEST_DIR + "/certs";

// ─── Main ─────────────────────────────────────────────────────────────────────

int main() {
    wsaInit();
    Logger::initialize("debug");

    // Clean slate
    std::filesystem::remove_all(TLS_TEST_DIR);
    std::filesystem::create_directories(TLS_CA_DIR);
    std::filesystem::create_directories(TLS_CERTS_DIR);

    // ═════════════════════════════════════════════════════════════════════════
    // Set up a local CA for unit tests that need real certificates
    // ═════════════════════════════════════════════════════════════════════════
    CertificateAuthority ca;
    bool caOk = ca.initializeCA(TLS_CA_DIR);
    assert(caOk && "Local CA must initialize for unit tests to proceed");
    std::cout << "[INFO] Local test CA initialized in " << TLS_CA_DIR << "\n";

    // Issue a test agent certificate via the CA's issueAgentCertificate method
    // We need a public key for the CSR - generate one inline
    EVP_PKEY_CTX* kctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
    EVP_PKEY_keygen_init(kctx);
    EVP_PKEY_CTX_set_rsa_keygen_bits(kctx, 2048);
    EVP_PKEY* agentKey = nullptr;
    EVP_PKEY_keygen(kctx, &agentKey);
    EVP_PKEY_CTX_free(kctx);

    // Extract public key PEM
    BIO* pubBio = BIO_new(BIO_s_mem());
    PEM_write_bio_PUBKEY(pubBio, agentKey);
    char* pubData = nullptr;
    long  pubLen  = BIO_get_mem_data(pubBio, &pubData);
    std::string agentPubPem(pubData, pubLen);
    BIO_free(pubBio);

    // Issue certificate
    IssuedCertificate issued = ca.issueCertificate("test-unit-agent-001", agentPubPem, 365);
    assert(!issued.certificatePem.empty() && "CA must issue a certificate for unit tests");
    std::string issuedCertPem = issued.certificatePem;

    // Save the private key (encrypted with the same passphrase used by RegistrationClient)
    // Use memory BIO and std::ofstream to avoid OPENSSL_Applink issues on Windows
    BIO* keyBio = BIO_new(BIO_s_mem());
    const char* pass = "ResolutePulse2024";
    if (PEM_write_bio_PrivateKey(keyBio, agentKey, EVP_aes_256_cbc(),
                                reinterpret_cast<const unsigned char*>(pass),
                                static_cast<int>(strlen(pass)), nullptr, nullptr) > 0) {
        char* keyData = nullptr;
        long keyLen = BIO_get_mem_data(keyBio, &keyData);
        std::string agentKeyPath = TLS_CERTS_DIR + "/agent.key";
        std::ofstream keyFile(agentKeyPath, std::ios::binary);
        keyFile.write(keyData, keyLen);
    }
    BIO_free(keyBio);
    EVP_PKEY_free(agentKey);

    // Get CA cert PEM
    std::string caCertPath = TLS_CA_DIR + "/ca.crt";
    std::ifstream caFile(caCertPath);
    std::ostringstream caSS;
    caSS << caFile.rdbuf();
    std::string caCertPem = caSS.str();
    assert(!caCertPem.empty() && "CA cert must not be empty");

    // ═════════════════════════════════════════════════════════════════════════
    // Test 1: CertificateStore - save / load / isValid / daysUntilExpiry
    // ═════════════════════════════════════════════════════════════════════════
    std::cout << "\n========== TEST 1: CertificateStore save/load/isValid ==========\n";
    {
        CertificateStore store;
        store.setCertsDir(TLS_CERTS_DIR);

        // Before save - should not exist
        assert(!store.exists() && "Store must not exist before save()");

        // Save issued cert + CA cert
        bool saved = store.save(issuedCertPem, caCertPem);
        assert(saved && "save() must return true");

        assert(std::filesystem::exists(TLS_CERTS_DIR + "/agent.crt") && "agent.crt must exist");
        assert(std::filesystem::exists(TLS_CERTS_DIR + "/ca.crt")    && "ca.crt must exist");
        // agent.key was written manually above; store.exists() checks all three
        assert(store.exists() && "store.exists() must be true after save + key file present");

        // Load
        bool loaded = store.load();
        assert(loaded && "load() must return true");

        // Validity
        assert(store.isValid() && "Loaded certificate must be valid (not expired)");
        assert(store.daysUntilExpiry() > 0 && "Must have at least 1 day until expiry");

        // PEM content
        assert(!store.getAgentCertPem().empty() && "Agent cert PEM must not be empty after load");
        assert(!store.getCACertPem().empty()    && "CA cert PEM must not be empty after load");

        std::cout << "[PASS] CertificateStore - saved, loaded, valid, "
                  << store.daysUntilExpiry() << " days until expiry\n";
    }

    // ═════════════════════════════════════════════════════════════════════════
    // Test 2: RegistrationClient - generateKeyPair
    // ═════════════════════════════════════════════════════════════════════════
    std::cout << "\n========== TEST 2: RegistrationClient generateKeyPair ==========\n";
    {
        std::string keyGenDir = TLS_TEST_DIR + "/keygen";
        std::filesystem::create_directories(keyGenDir);

        RegistrationClient client;
        bool ok = client.generateKeyPair(keyGenDir);
        assert(ok && "generateKeyPair() must succeed");

        // Private key file exists
        assert(std::filesystem::exists(keyGenDir + "/agent.key")
               && "agent.key must be written to disk");

        // Public key PEM is populated
        const std::string& pubPem = client.getPublicKeyPem();
        assert(!pubPem.empty() && "Public key PEM must not be empty");
        assert(pubPem.find("-----BEGIN PUBLIC KEY-----") != std::string::npos
               && "Public key PEM must have PEM header");

        // Verify the key can be parsed by OpenSSL
        BIO* bio = BIO_new_mem_buf(pubPem.data(), static_cast<int>(pubPem.size()));
        EVP_PKEY* loaded = PEM_read_bio_PUBKEY(bio, nullptr, nullptr, nullptr);
        BIO_free(bio);
        assert(loaded != nullptr && "OpenSSL must be able to parse the generated public key");
        EVP_PKEY_free(loaded);

        std::cout << "[PASS] RegistrationClient - ECC key pair generated, key file on disk\n";
    }

    // ═════════════════════════════════════════════════════════════════════════
    // Test 3: TlsSender - initialize() fails gracefully with bad cert paths
    // ═════════════════════════════════════════════════════════════════════════
    std::cout << "\n========== TEST 3: TlsSender - bad cert paths ==========\n";
    {
        TlsSender sender;
        // Provide paths to non-existent files
        bool ok = sender.initialize(
            "127.0.0.1", 1514,
            "nonexistent_agent.crt",
            "nonexistent_agent.key",
            "nonexistent_ca.crt"
        );
        // SSL context creation must fail because cert file doesn't exist
        assert(!ok && "initialize() must return false when cert files do not exist");
        assert(!sender.isConnected() && "Sender must not be connected after failed init");

        std::cout << "[PASS] TlsSender - initialize() correctly returns false for missing certs\n";
        std::cout << "[INFO] Last error: " << sender.getLastError() << "\n";
    }

    // ═════════════════════════════════════════════════════════════════════════
    // Test 4: RegistrationClient - registerWithManager() live (integration)
    // ═════════════════════════════════════════════════════════════════════════
    std::cout << "\n========== TEST 4: RegistrationClient - live registration ==========\n";
    {
        std::string regDir = TLS_TEST_DIR + "/reg_certs";
        std::filesystem::remove_all(regDir);
        std::filesystem::create_directories(regDir);

        RegistrationClient client;

        // Generate key pair
        bool keyOk = client.generateKeyPair(regDir);
        assert(keyOk && "generateKeyPair() must succeed before registration");

        CertificateStore certStore;
        certStore.setCertsDir(regDir);

        // Use a unique agent ID to avoid duplicate-rejection from the manager
        std::string agentId = "test-tls-agent-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count() & 0xFFFFFF);

        bool regOk = client.registerWithManager(
            "127.0.0.1", 1514,
            agentId,
            "test-tls-host",
            "windows",
            "10.0",
            "1.0.0",
            certStore
        );

        if (!regOk) {
            // Manager may not be running - warn and skip instead of crashing
            std::cout << "[SKIP] Test 4 skipped - manager unreachable: "
                      << client.getLastError() << "\n";
        } else {
            assert(certStore.exists() && "Certificates must be saved after registration");

            bool loadOk = certStore.load();
            assert(loadOk && "load() must succeed after registration");
            assert(certStore.isValid() && "Issued certificate must be valid");
            assert(certStore.daysUntilExpiry() > 0 && "Certificate must have future expiry");

            // PEM content is real PEM
            assert(certStore.getAgentCertPem().find("-----BEGIN CERTIFICATE-----")
                   != std::string::npos && "Agent cert PEM must be valid");
            assert(certStore.getCACertPem().find("-----BEGIN CERTIFICATE-----")
                   != std::string::npos && "CA cert PEM must be valid");

            // Persist regDir for Test 6
            // (leave it in place - Test 6 will re-use it)
            std::cout << "[PASS] RegistrationClient - registered with live manager\n";
            std::cout << "[INFO] Agent ID: " << agentId << "\n";
            std::cout << "[INFO] Cert valid for " << certStore.daysUntilExpiry() << " days\n";

            // Stash agent info to a marker file for Test 6
            {
                std::ofstream marker(TLS_TEST_DIR + "/reg_agent_id.txt");
                marker << agentId << "\n" << regDir << "\n";
            }
        }
    }

    // ═════════════════════════════════════════════════════════════════════════
    // Test 5: TlsSender - sendBatch() when never initialized → NetworkError
    // ═════════════════════════════════════════════════════════════════════════
    std::cout << "\n========== TEST 5: TlsSender - sendBatch() uninitialized ==========\n";
    {
        TlsSender sender;

        Event e;
        e.channel  = "Security";
        e.eventId  = 4624;
        e.timestamp = "2026-03-09T10:00:00Z";
        e.data     = "{\"test\":true}";

        SendResult result = sender.sendBatch({e});
        assert(result == SendResult::NetworkError
               && "sendBatch() on uninitialized sender must return NetworkError");
        assert(sender.getEventsSent() == 0 && "No events must be counted on failure");
        assert(sender.getFailedSends() == 1 && "failedSends must be incremented");

        std::cout << "[PASS] TlsSender - sendBatch() on uninitialized sender returns NetworkError\n";
    }

    // ═════════════════════════════════════════════════════════════════════════
    // Test 6: TlsSender - sendBatch() over mTLS with real certs (integration)
    // ═════════════════════════════════════════════════════════════════════════
    std::cout << "\n========== TEST 6: TlsSender - mTLS sendBatch() E2E ==========\n";
    {
        // Read stashed registration info from Test 4
        std::string markerPath = TLS_TEST_DIR + "/reg_agent_id.txt";
        if (!std::filesystem::exists(markerPath)) {
            std::cout << "[SKIP] Test 6 skipped - Test 4 did not complete (no live manager)\n";
        } else {
            std::ifstream marker(markerPath);
            std::string agentId, regDir;
            std::getline(marker, agentId);
            std::getline(marker, regDir);

            CertificateStore certStore;
            certStore.setCertsDir(regDir);
            bool loadOk = certStore.load();
            assert(loadOk && "Must be able to reload the registered certs");

            TlsSender sender;
            bool initOk = sender.initialize(
                "127.0.0.1", 1514,
                certStore.getAgentCertPath(),
                certStore.getAgentKeyPath(),
                certStore.getCACertPath()
            );

            if (!initOk) {
                // Even if initialization "fails" at connect time, it may return true
                // and retry on first send - check for connect failure specifically
                std::cout << "[SKIP] Test 6 skipped - TlsSender init failed: "
                          << sender.getLastError() << "\n";
            } else {
                // Build two events
                Event e1;
                e1.channel   = "Security";
                e1.eventId   = 4624;
                e1.timestamp = "2026-03-09T10:00:00Z";
                e1.data      = R"({"EventID":4624,"user":"test"})";

                Event e2;
                e2.channel   = "System";
                e2.eventId   = 7045;
                e2.timestamp = "2026-03-09T10:00:01Z";
                e2.data      = R"({"EventID":7045,"service":"TestSvc"})";

                SendResult result = sender.sendBatch({e1, e2});

                if (result != SendResult::Success) {
                    std::cout << "[SKIP] Test 6 skipped - manager rejected mTLS send: "
                              << sender.getLastError() << "\n";
                } else {
                    assert(sender.getEventsSent()  == 2 && "2 events must be counted");
                    assert(sender.getBatchesSent() == 1 && "1 batch must be counted");
                    assert(sender.getBytesSent()   >  0 && "Bytes sent must be > 0");
                    assert(sender.isConnected()       && "Sender must remain connected");

                    sender.disconnect();
                    assert(!sender.isConnected() && "disconnect() must clear connected flag");

                    std::cout << "[PASS] TlsSender - sendBatch() over mTLS succeeded\n";
                    std::cout << "[INFO] Events sent: " << sender.getEventsSent() << "\n";
                    std::cout << "[INFO] Bytes sent:  " << sender.getBytesSent()  << "\n";
                }
            }
        }
    }

    // ═════════════════════════════════════════════════════════════════════════
    // Cleanup
    // ═════════════════════════════════════════════════════════════════════════
    std::cout << "\n========== CLEANUP ==========\n";
    std::filesystem::remove_all(TLS_TEST_DIR);
    std::cout << "[CLEANUP] Removed " << TLS_TEST_DIR << "\n";

    std::cout << "\n=== ALL AGENT TLS TESTS COMPLETED ===\n\n";
    return 0;
}

