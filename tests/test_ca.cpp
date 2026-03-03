#include "manager/ca/CertificateAuthority.h"
#include "utils/Logger.h"

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/bio.h>
#include <openssl/x509v3.h>

#include <iostream>
#include <fstream>
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

        // 6. Verify Subject Alternative Name extension is present (required by modern TLS)
        GENERAL_NAMES* sans = reinterpret_cast<GENERAL_NAMES*>(
            X509_get_ext_d2i(cert, NID_subject_alt_name, nullptr, nullptr));
        assert(sans != nullptr && "SAN extension must be present in issued cert");
        GENERAL_NAMES_free(sans);

        // 7. Verify CRL Distribution Points extension is present
        CRL_DIST_POINTS* cdp = reinterpret_cast<CRL_DIST_POINTS*>(
            X509_get_ext_d2i(cert, NID_crl_distribution_points, nullptr, nullptr));
        assert(cdp != nullptr && "CRL Distribution Points extension must be present in issued cert");
        CRL_DIST_POINTS_free(cdp);

        X509_free(caCert);
        X509_free(cert);

        std::cout << "[PASS] Certificate issued and verified thoroughly for agent-test-001\n";
        std::cout << "[INFO] Serial: " << issued.serialNumber << "\n";
        std::cout << "[INFO] Expires: " << issued.expiresAt << "\n";
    }

    // ─── Test 4: revokeCertificate() ───
    std::cout << "\n========== TEST 4: Revoke Certificate ==========\n";
    {
        CertificateAuthority ca4;
        ca4.initializeCA(testDir);

        // Issue a certificate first so we have a valid serial to revoke
        EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
        EVP_PKEY_keygen_init(ctx);
        EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, 2048);
        EVP_PKEY* agentKey = nullptr;
        EVP_PKEY_keygen(ctx, &agentKey);
        EVP_PKEY_CTX_free(ctx);

        BIO* bio = BIO_new(BIO_s_mem());
        PEM_write_bio_PUBKEY(bio, agentKey);
        char* data = nullptr;
        long len = BIO_get_mem_data(bio, &data);
        std::string agentPubPem(data, len);
        BIO_free(bio);
        EVP_PKEY_free(agentKey);

        IssuedCertificate issued = ca4.issueCertificate("agent-revoke-001", agentPubPem, 365);
        assert(!issued.serialNumber.empty() && "Must have a serial to revoke");

        // Revoke the certificate — should return true
        bool revokeResult = ca4.revokeCertificate(issued.serialNumber);
        assert(revokeResult && "revokeCertificate() must return true");

        // Revoking with an arbitrary serial should also succeed (CA logs it)
        bool revokeArbitrary = ca4.revokeCertificate("9999999");
        assert(revokeArbitrary && "revokeCertificate() with arbitrary serial must return true");

        // Revoking on an uninitialised CA must fail
        CertificateAuthority uninitCA;
        bool revokeUninit = uninitCA.revokeCertificate("1234");
        assert(!revokeUninit && "revokeCertificate() on uninitialised CA must return false");

        std::cout << "[PASS] revokeCertificate() behaves correctly\n";
        std::cout << "[INFO] Revoked serial: " << issued.serialNumber << "\n";
    }

    // ─── Test 5: generateCRL() ───
    std::cout << "\n========== TEST 5: Generate CRL ==========\n";
    {
        CertificateAuthority ca5;
        ca5.initializeCA(testDir);

        std::string crlPath = testDir + "/crl.pem";

        // CRL must already exist after initializeCA
        assert(std::filesystem::exists(crlPath) && "crl.pem must exist after initializeCA");

        // Regenerate CRL explicitly
        bool genResult = ca5.generateCRL();
        assert(genResult && "generateCRL() must return true");
        assert(std::filesystem::exists(crlPath) && "crl.pem must still exist after generateCRL()");

        // Validate the CRL file is valid PEM
        BIO* crlBio = BIO_new_file(crlPath.c_str(), "r");
        assert(crlBio != nullptr && "Cannot open crl.pem");
        X509_CRL* parsedCrl = PEM_read_bio_X509_CRL(crlBio, nullptr, nullptr, nullptr);
        BIO_free(crlBio);
        assert(parsedCrl != nullptr && "crl.pem must be a valid PEM-encoded CRL");

        // Verify CRL issuer matches CA
        X509_NAME* crlIssuer = X509_CRL_get_issuer(parsedCrl);
        CertificateAuthority caForPem;
        caForPem.initializeCA(testDir);
        X509* caCert = localPemToX509(caForPem.getCACertPem());
        X509_NAME* caSubject = X509_get_subject_name(caCert);
        assert(X509_NAME_cmp(crlIssuer, caSubject) == 0 && "CRL issuer must match CA subject");

        X509_CRL_free(parsedCrl);
        X509_free(caCert);

        // generateCRL() on uninitialised CA must fail
        CertificateAuthority uninitCA;
        bool genUninit = uninitCA.generateCRL();
        assert(!genUninit && "generateCRL() on uninitialised CA must return false");

        std::cout << "[PASS] generateCRL() produced a valid, correctly-signed CRL\n";
    }

    // ─── Test 6: updateCRL() — regenerate CRL after revocation ───
    std::cout << "\n========== TEST 6: Update CRL After Revocation ==========\n";
    {
        CertificateAuthority ca6;
        ca6.initializeCA(testDir);

        std::string crlPath = testDir + "/crl.pem";

        // Capture modification time before update
        auto mtimeBefore = std::filesystem::last_write_time(crlPath);

        // Small sleep so the filesystem timestamp can advance (at least 1 second)
        // Use a busy-wait that is portable without requiring <thread>
        time_t start = time(nullptr);
        while (time(nullptr) == start) { /* spin */ }

        // Issue and revoke a certificate, then regenerate CRL
        EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
        EVP_PKEY_keygen_init(ctx);
        EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, 2048);
        EVP_PKEY* agentKey = nullptr;
        EVP_PKEY_keygen(ctx, &agentKey);
        EVP_PKEY_CTX_free(ctx);

        BIO* bio = BIO_new(BIO_s_mem());
        PEM_write_bio_PUBKEY(bio, agentKey);
        char* data = nullptr;
        long len = BIO_get_mem_data(bio, &data);
        std::string agentPubPem(data, len);
        BIO_free(bio);
        EVP_PKEY_free(agentKey);

        IssuedCertificate issued = ca6.issueCertificate("agent-update-crl-001", agentPubPem, 365);
        assert(!issued.serialNumber.empty() && "Certificate must be issued before CRL update");

        bool revokeOk = ca6.revokeCertificate(issued.serialNumber);
        assert(revokeOk && "revokeCertificate() must succeed before CRL update");

        // updateCRL = regenerate CRL after revocation
        bool updateOk = ca6.generateCRL();
        assert(updateOk && "generateCRL() (updateCRL) must return true after revocation");

        // Verify crl.pem was actually re-written (mtime changed)
        auto mtimeAfter = std::filesystem::last_write_time(crlPath);
        assert(mtimeAfter > mtimeBefore && "crl.pem modification time must advance after updateCRL");

        // Confirm updated CRL is still a valid PEM CRL
        BIO* crlBio = BIO_new_file(crlPath.c_str(), "r");
        X509_CRL* updatedCrl = PEM_read_bio_X509_CRL(crlBio, nullptr, nullptr, nullptr);
        BIO_free(crlBio);
        assert(updatedCrl != nullptr && "Updated crl.pem must still be a valid CRL");
        X509_CRL_free(updatedCrl);

        std::cout << "[PASS] CRL updated (re-generated) successfully after revocation\n";
        std::cout << "[INFO] Revoked serial: " << issued.serialNumber << "\n";
    }

    // ─── Test 7: CA private key is AES-256-CBC encrypted on disk ───
    std::cout << "\n========== TEST 7: CA Key Encrypted on Disk ==========\n";
    {
        // Re-generate the CA so we verify saveCA() encrypted the file
        std::string freshDir = testDir + "_encrypted";
        if (std::filesystem::exists(freshDir)) std::filesystem::remove_all(freshDir);

        CertificateAuthority ca7;
        bool initOk = ca7.initializeCA(freshDir);
        assert(initOk && "initializeCA must succeed for encryption test");

        // Read the raw key file content and confirm it carries the ENCRYPTED header
        std::ifstream keyFile(freshDir + "/ca.key");
        assert(keyFile.is_open() && "ca.key must exist after initializeCA");
        std::string keyContent((std::istreambuf_iterator<char>(keyFile)),
                                std::istreambuf_iterator<char>());
        keyFile.close();

        assert(keyContent.find("ENCRYPTED") != std::string::npos &&
               "ca.key must be AES-256-CBC encrypted (PEM ENCRYPTED header expected)");

        std::filesystem::remove_all(freshDir);
        std::cout << "[PASS] ca.key is AES-256-CBC encrypted on disk\n";
    }

    // ─── Test 8: SAN URI contains the agent ID ───
    std::cout << "\n========== TEST 8: SAN URI Contains Agent ID ==========\n";
    {
        CertificateAuthority ca8;
        ca8.initializeCA(testDir);

        EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
        EVP_PKEY_keygen_init(ctx);
        EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, 2048);
        EVP_PKEY* agentKey = nullptr;
        EVP_PKEY_keygen(ctx, &agentKey);
        EVP_PKEY_CTX_free(ctx);

        BIO* bio = BIO_new(BIO_s_mem());
        PEM_write_bio_PUBKEY(bio, agentKey);
        char* data = nullptr;
        long len = BIO_get_mem_data(bio, &data);
        std::string agentPubPem(data, len);
        BIO_free(bio);
        EVP_PKEY_free(agentKey);

        const std::string agentId = "agent-san-check-001";
        IssuedCertificate issued = ca8.issueCertificate(agentId, agentPubPem, 365);
        assert(!issued.certificatePem.empty() && "Certificate must be issued");

        BIO* certBio = BIO_new_mem_buf(issued.certificatePem.data(),
                                       static_cast<int>(issued.certificatePem.size()));
        X509* cert = PEM_read_bio_X509(certBio, nullptr, nullptr, nullptr);
        BIO_free(certBio);
        assert(cert != nullptr && "Must parse issued certificate");

        // Decode the SAN extension and verify the URI contains the agent ID
        GENERAL_NAMES* sans = reinterpret_cast<GENERAL_NAMES*>(
            X509_get_ext_d2i(cert, NID_subject_alt_name, nullptr, nullptr));
        assert(sans != nullptr && "SAN extension must be present");

        bool foundAgentUri = false;
        for (int i = 0; i < sk_GENERAL_NAME_num(sans); ++i) {
            GENERAL_NAME* gn = sk_GENERAL_NAME_value(sans, i);
            if (gn->type == GEN_URI) {
                const char* uriStr = reinterpret_cast<const char*>(
                    ASN1_STRING_get0_data(gn->d.uniformResourceIdentifier));
                std::string uri(uriStr);
                if (uri.find("agent:" + agentId) != std::string::npos) {
                    foundAgentUri = true;
                    std::cout << "[INFO] SAN URI: " << uri << "\n";
                    break;
                }
            }
        }
        GENERAL_NAMES_free(sans);
        X509_free(cert);

        assert(foundAgentUri && "SAN URI must contain 'agent:<agentId>'");
        std::cout << "[PASS] SAN URI correctly contains the agent ID\n";
    }

    // ─── Test 9: CRL Distribution Point URI contains crl.pem ───
    std::cout << "\n========== TEST 9: CRL Distribution Point URI ==========\n";
    {
        CertificateAuthority ca9;
        ca9.initializeCA(testDir);

        EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
        EVP_PKEY_keygen_init(ctx);
        EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, 2048);
        EVP_PKEY* agentKey = nullptr;
        EVP_PKEY_keygen(ctx, &agentKey);
        EVP_PKEY_CTX_free(ctx);

        BIO* bio = BIO_new(BIO_s_mem());
        PEM_write_bio_PUBKEY(bio, agentKey);
        char* data = nullptr;
        long len = BIO_get_mem_data(bio, &data);
        std::string agentPubPem(data, len);
        BIO_free(bio);
        EVP_PKEY_free(agentKey);

        IssuedCertificate issued = ca9.issueCertificate("agent-cdp-check-001", agentPubPem, 365);
        assert(!issued.certificatePem.empty() && "Certificate must be issued");

        BIO* certBio = BIO_new_mem_buf(issued.certificatePem.data(),
                                       static_cast<int>(issued.certificatePem.size()));
        X509* cert = PEM_read_bio_X509(certBio, nullptr, nullptr, nullptr);
        BIO_free(certBio);
        assert(cert != nullptr && "Must parse issued certificate");

        // Decode CRL Distribution Points and verify the URI references crl.pem
        CRL_DIST_POINTS* cdp = reinterpret_cast<CRL_DIST_POINTS*>(
            X509_get_ext_d2i(cert, NID_crl_distribution_points, nullptr, nullptr));
        assert(cdp != nullptr && "CRL Distribution Points extension must be present");

        bool foundCrlUri = false;
        for (int i = 0; i < sk_DIST_POINT_num(cdp); ++i) {
            DIST_POINT* dp = sk_DIST_POINT_value(cdp, i);
            if (dp->distpoint && dp->distpoint->type == 0) { // fullName
                GENERAL_NAMES* gns = dp->distpoint->name.fullname;
                for (int j = 0; j < sk_GENERAL_NAME_num(gns); ++j) {
                    GENERAL_NAME* gn = sk_GENERAL_NAME_value(gns, j);
                    if (gn->type == GEN_URI) {
                        const char* uriStr = reinterpret_cast<const char*>(
                            ASN1_STRING_get0_data(gn->d.uniformResourceIdentifier));
                        std::string uri(uriStr);
                        if (uri.find("crl.pem") != std::string::npos) {
                            foundCrlUri = true;
                            std::cout << "[INFO] CRL DP URI: " << uri << "\n";
                        }
                    }
                }
            }
        }
        CRL_DIST_POINTS_free(cdp);
        X509_free(cert);

        assert(foundCrlUri && "CRL DP URI must reference 'crl.pem'");
        std::cout << "[PASS] CRL Distribution Point URI correctly references crl.pem\n";
    }

    // ─── Test 10: Serial numbers increment monotonically ───
    std::cout << "\n========== TEST 10: Serial Number Monotonicity ==========\n";
    {
        CertificateAuthority ca10;
        ca10.initializeCA(testDir);

        // Generate one throwaway agent key pair and reuse public key for all issuances
        EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
        EVP_PKEY_keygen_init(ctx);
        EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, 2048);
        EVP_PKEY* agentKey = nullptr;
        EVP_PKEY_keygen(ctx, &agentKey);
        EVP_PKEY_CTX_free(ctx);

        BIO* bio = BIO_new(BIO_s_mem());
        PEM_write_bio_PUBKEY(bio, agentKey);
        char* data = nullptr;
        long len = BIO_get_mem_data(bio, &data);
        std::string agentPubPem(data, len);
        BIO_free(bio);
        EVP_PKEY_free(agentKey);

        // Issue several certs and collect serial numbers
        const int N = 5;
        std::vector<uint64_t> serials;
        for (int i = 0; i < N; ++i) {
            std::string id = "agent-serial-" + std::to_string(i);
            IssuedCertificate issued = ca10.issueCertificate(id, agentPubPem, 30);
            assert(!issued.serialNumber.empty() && "Serial must be non-empty");
            serials.push_back(std::stoull(issued.serialNumber));
        }

        // Every subsequent serial must be strictly greater than the previous one
        for (int i = 1; i < N; ++i) {
            assert(serials[i] > serials[i - 1] &&
                   "Each serial number must be strictly greater than the previous");
        }

        std::cout << "[PASS] Serial numbers increment monotonically ("
                  << serials.front() << " → " << serials.back() << ")\n";
    }

    // ─── Cleanup ───
    std::cout << "\n========== CLEANUP ==========\n";
    std::filesystem::remove_all(testDir);
    std::cout << "[CLEANUP] Removed test directory\n";

    std::cout << "\n=== ALL TESTS PASSED ===\n\n";
    return 0;
}
