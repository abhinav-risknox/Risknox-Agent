#include "manager/ca/CertificateAuthority.h"
#include "utils/Logger.h"

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/bio.h>
#include <openssl/x509v3.h>

#include <iostream>
#include <filesystem>
#include <cassert>

using namespace ResolutePulse;

X509* localPemToX509(const std::string& pem) {
    BIO* bio = BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size()));
    X509* cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    return cert;
}

int main() {
    Logger::initialize("debug");

    std::string testDir = "test_ca_output";

    // Clean up any previous test run
    if (std::filesystem::exists(testDir)) {
        std::filesystem::remove_all(testDir);
        std::cout << "[CLEANUP] Removed existing test directory\n";
    }

    // ─── Test 1: Fresh CA generation ───
    std::cout << "\n========== TEST 1: Fresh CA Generation ==========\n";
    {
        CertificateAuthority ca;

        assert(!ca.isInitialized() && "CA should NOT be initialized yet");

        bool result = ca.initializeCA(testDir);
        assert(result && "initializeCA must return true");
        assert(ca.isInitialized() && "CA should be initialized");
        assert(ca.getCADir() == testDir && "CA dir mismatch");

        // Verify files were created
        assert(std::filesystem::exists(testDir + "/ca.key") && "ca.key missing");
        assert(std::filesystem::exists(testDir + "/ca.crt") && "ca.crt missing");
        assert(std::filesystem::exists(testDir + "/serial.txt") && "serial.txt missing");
        assert(std::filesystem::exists(testDir + "/crl.pem") && "crl.pem missing");

        // Check CA cert PEM is non-empty
        std::string caPem = ca.getCACertPem();
        assert(!caPem.empty() && "CA cert PEM should not be empty");
        assert(caPem.find("BEGIN CERTIFICATE") != std::string::npos && "PEM must contain CERTIFICATE header");

        std::cout << "[PASS] Fresh CA generated successfully\n";
        std::cout << "[INFO] CA cert PEM length: " << caPem.size() << " bytes\n";
    }

    // ─── Test 2: Load existing CA ───
    std::cout << "\n========== TEST 2: Load Existing CA ==========\n";
    {
        CertificateAuthority ca2;
        bool result = ca2.initializeCA(testDir);
        assert(result && "initializeCA (load) must return true");
        assert(ca2.isInitialized() && "CA should be initialized after load");

        std::string caPem = ca2.getCACertPem();
        assert(!caPem.empty() && "Loaded CA cert PEM should not be empty");

        std::cout << "[PASS] Existing CA loaded successfully\n";
    }

    // ─── Test 3: issueCertificate smoke test ───
    std::cout << "\n========== TEST 3: Issue Agent Certificate ==========\n";
    {
        CertificateAuthority ca3;
        ca3.initializeCA(testDir);

        // Generate a throwaway agent RSA key pair for testing
        EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
        EVP_PKEY_keygen_init(ctx);
        EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, 2048);
        EVP_PKEY* agentKey = nullptr;
        EVP_PKEY_keygen(ctx, &agentKey);
        EVP_PKEY_CTX_free(ctx);

        // Export public key to PEM
        BIO* bio = BIO_new(BIO_s_mem());
        PEM_write_bio_PUBKEY(bio, agentKey);
        char* data = nullptr;
        long len = BIO_get_mem_data(bio, &data);
        std::string agentPubPem(data, len);
        BIO_free(bio);
        EVP_PKEY_free(agentKey);

        IssuedCertificate issued = ca3.issueCertificate("agent-test-001", agentPubPem, 365);
        assert(!issued.certificatePem.empty() && "Issued cert PEM should not be empty");
        assert(!issued.serialNumber.empty() && "Serial should not be empty");
        assert(!issued.expiresAt.empty() && "Expiry should not be empty");

        // --- Thorough Verification ---
        BIO* certBio = BIO_new_mem_buf(issued.certificatePem.data(), static_cast<int>(issued.certificatePem.size()));
        X509* cert = PEM_read_bio_X509(certBio, nullptr, nullptr, nullptr);
        BIO_free(certBio);
        assert(cert != nullptr && "Failed to parse issued certificate PEM");

        // 1. Verify Common Name (CN)
        X509_NAME* name = X509_get_subject_name(cert);
        char cn[256];
        X509_NAME_get_text_by_NID(name, NID_commonName, cn, sizeof(cn));
        assert(std::string(cn) == "agent-test-001" && "CN mismatch");

        // 2. Verify Organization (O)
        char o[256];
        X509_NAME_get_text_by_NID(name, NID_organizationName, o, sizeof(o));
        assert(std::string(o) == "ResolutePulse Agent" && "Organization mismatch");

        // 3. Verify Basic Constraints (CA:FALSE)
        BASIC_CONSTRAINTS* bc = (BASIC_CONSTRAINTS*)X509_get_ext_d2i(cert, NID_basic_constraints, nullptr, nullptr);
        assert(bc != nullptr && "Basic constraints missing");
        assert(!bc->ca && "Certificate should NOT be a CA");
        BASIC_CONSTRAINTS_free(bc);

        // 4. Verify Extended Key Usage (clientAuth)
        EXTENDED_KEY_USAGE* eku = (EXTENDED_KEY_USAGE*)X509_get_ext_d2i(cert, NID_ext_key_usage, nullptr, nullptr);
        assert(eku != nullptr && "Extended key usage missing");
        bool hasClientAuth = false;
        for (int i = 0; i < sk_ASN1_OBJECT_num(eku); i++) {
            if (OBJ_obj2nid(sk_ASN1_OBJECT_value(eku, i)) == NID_client_auth) {
                hasClientAuth = true;
                break;
            }
        }
        assert(hasClientAuth && "clientAuth extension missing");
        EXTENDED_KEY_USAGE_free(eku);

        // 5. Verify Issuer matches CA
        X509_NAME* issuerName = X509_get_issuer_name(cert);
        X509* caCert = localPemToX509(ca3.getCACertPem());
        X509_NAME* caName = X509_get_subject_name(caCert);
        assert(X509_NAME_cmp(issuerName, caName) == 0 && "Issuer mismatch");

        X509_free(caCert);
        X509_free(cert);

        std::cout << "[PASS] Certificate issued and verified thoroughly for agent-test-001\n";
        std::cout << "[INFO] Serial: " << issued.serialNumber << "\n";
        std::cout << "[INFO] Expires: " << issued.expiresAt << "\n";
    }

    // ─── Cleanup ───
    std::cout << "\n========== CLEANUP ==========\n";
    std::filesystem::remove_all(testDir);
    std::cout << "[CLEANUP] Removed test directory\n";

    std::cout << "\n=== ALL TESTS PASSED ===\n\n";
    return 0;
}
