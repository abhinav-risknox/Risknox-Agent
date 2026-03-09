#pragma once

#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <cstring>

// Forward declarations for OpenSSL types
typedef struct evp_pkey_st EVP_PKEY;
typedef struct x509_st X509;
typedef struct X509_crl_st X509_CRL;

namespace ResolutePulse {

struct IssuedCertificate {
    std::string certificatePem;
    std::string serialNumber;
    std::string expiresAt;       // ISO 8601
};

// Controls the Extended Key Usage on issued certificates
enum class CertType {
    AgentClient,  // id-kp-clientAuth  — for agent leaf certs
    Server        // id-kp-serverAuth  — for the manager's own TLS cert
};

class CertificateAuthority {
public:
    CertificateAuthority();
    ~CertificateAuthority();

    // Initialize or load existing CA
    // @param caDir - Directory for CA files (ca.key, ca.crt, crl.pem, serial.txt)
    bool initializeCA(const std::string& caDir);

    // Issue a certificate for an agent
    // @param agentId     - Agent identifier (used as CN)
    // @param publicKeyPem - Agent's public key in PEM format
    // @param validDays    - Certificate validity in days
    // @return IssuedCertificate with PEM, serial, and expiry
    IssuedCertificate issueCertificate(const std::string& agentId,
                                        const std::string& publicKeyPem,
                                        int validDays,
                                        CertType type = CertType::AgentClient);

    // Revoke a certificate by serial number
    bool revokeCertificate(const std::string& serialNumber);

    // Regenerate the CRL file
    bool generateCRL();

    // Get the CA certificate in PEM format
    std::string getCACertPem() const;

    // Get the CA directory path
    const std::string& getCADir() const { return caDir_; }

    // Check if CA is initialized
    bool isInitialized() const { return initialized_; }

private:
    // Generate a new CA key pair and self-signed certificate
    bool generateCA();

    // Load existing CA from files
    bool loadCA();

    // Save CA key and certificate to files
    bool saveCA();

    // Read the next serial number and increment
    std::string getNextSerial();

    // Helper: convert X509 to PEM string
    static std::string x509ToPem(X509* cert);

    // Helper: convert EVP_PKEY to PEM string
    static std::string keyToPem(EVP_PKEY* key, bool isPrivate);

    // Helper: load PEM into EVP_PKEY
    static EVP_PKEY* pemToKey(const std::string& pem, bool isPrivate);

    // Helper: load PEM into X509
    static X509* pemToX509(const std::string& pem);

    std::string caDir_;
    EVP_PKEY*   caKey_  = nullptr;
    X509*       caCert_ = nullptr;
    X509_CRL*   crl_    = nullptr;
    bool        initialized_ = false;
    std::mutex  mutex_;

    static constexpr int  CA_KEY_BITS       = 4096;      // RSA key size for CA
    static constexpr int  CA_VALIDITY_DAYS  = 3650;      // 10 years
    static constexpr int  CRL_VALIDITY_DAYS = 30;
    // Passphrase used to encrypt/decrypt the CA private key on disk.
    // TODO: replace with env-var / HSM in production.
    static constexpr const char* CA_KEY_PASSPHRASE = "RPCAKey2025";
};

} // namespace ResolutePulse
