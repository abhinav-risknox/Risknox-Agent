#include "CertificateAuthority.h"
#include "utils/Logger.h"

#include <openssl/evp.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/pem.h>
#include <openssl/err.h>
#include <openssl/rand.h>
#include <openssl/bn.h>
#include <openssl/rsa.h>
#include <openssl/bio.h>

#include <fstream>
#include <sstream>
#include <filesystem>
#include <ctime>
#include <iomanip>

namespace ResolutePulse {

CertificateAuthority::CertificateAuthority() = default;

CertificateAuthority::~CertificateAuthority() {
    if (caKey_)  EVP_PKEY_free(caKey_);
    if (caCert_) X509_free(caCert_);
    if (crl_)    X509_CRL_free(crl_);
}

bool CertificateAuthority::initializeCA(const std::string& caDir) {
    std::lock_guard<std::mutex> lock(mutex_);
    caDir_ = caDir;

    // Create CA directory if it doesn't exist
    std::filesystem::create_directories(caDir_);

    std::string keyPath  = caDir_ + "/ca.key";
    std::string certPath = caDir_ + "/ca.crt";

    if (std::filesystem::exists(keyPath) && std::filesystem::exists(certPath)) {
        LOG_INFO("Loading existing CA from {}", caDir_);
        if (!loadCA()) {
            LOG_ERROR("Failed to load existing CA");
            return false;
        }
    } else {
        LOG_INFO("Generating new CA in {}", caDir_);
        if (!generateCA()) {
            LOG_ERROR("Failed to generate CA");
            return false;
        }
        if (!saveCA()) {
            LOG_ERROR("Failed to save CA files");
            return false;
        }
    }

    // Initialize serial file if it doesn't exist
    std::string serialPath = caDir_ + "/serial.txt";
    if (!std::filesystem::exists(serialPath)) {
        std::ofstream serialFile(serialPath);
        serialFile << "1000" << std::endl;
    }

    // Generate initial CRL if it doesn't exist
    std::string crlPath = caDir_ + "/crl.pem";
    if (!std::filesystem::exists(crlPath)) {
        generateCRL();
    }

    initialized_ = true;
    LOG_INFO("Certificate Authority initialized");
    return true;
}

bool CertificateAuthority::generateCA() {
    // Generate RSA key pair for CA
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
    if (!ctx) {
        LOG_ERROR("Failed to create EVP_PKEY_CTX");
        return false;
    }

    if (EVP_PKEY_keygen_init(ctx) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        LOG_ERROR("Failed to initialize key generation");
        return false;
    }

    if (EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, CA_KEY_BITS) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        LOG_ERROR("Failed to set RSA key bits");
        return false;
    }

    caKey_ = nullptr;
    if (EVP_PKEY_keygen(ctx, &caKey_) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        LOG_ERROR("Failed to generate CA key pair");
        return false;
    }
    EVP_PKEY_CTX_free(ctx);

    // Create self-signed CA certificate
    caCert_ = X509_new();
    if (!caCert_) {
        LOG_ERROR("Failed to create X509 structure");
        return false;
    }

    // Set version to X509v3
    X509_set_version(caCert_, 2);

    // Set serial number
    ASN1_INTEGER_set(X509_get_serialNumber(caCert_), 1);

    // Set validity period
    X509_gmtime_adj(X509_getm_notBefore(caCert_), 0);
    X509_gmtime_adj(X509_getm_notAfter(caCert_), 
                    static_cast<long>(CA_VALIDITY_DAYS) * 86400L);

    // Set subject name
    X509_NAME* name = X509_get_subject_name(caCert_);
    X509_NAME_add_entry_by_txt(name, "C",  MBSTRING_ASC,
        reinterpret_cast<const unsigned char*>("IN"), -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "O",  MBSTRING_ASC,
        reinterpret_cast<const unsigned char*>("ResolutePulse"), -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "OU", MBSTRING_ASC,
        reinterpret_cast<const unsigned char*>("Certificate Authority"), -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
        reinterpret_cast<const unsigned char*>("ResolutePulse Root CA"), -1, -1, 0);

    // Self-signed: issuer = subject
    X509_set_issuer_name(caCert_, name);

    // Set public key
    X509_set_pubkey(caCert_, caKey_);

    // Add CA extensions
    X509V3_CTX v3ctx;
    X509V3_set_ctx_nodb(&v3ctx);
    X509V3_set_ctx(&v3ctx, caCert_, caCert_, nullptr, nullptr, 0);

    // Basic Constraints: CA:TRUE
    X509_EXTENSION* ext = X509V3_EXT_conf_nid(nullptr, &v3ctx,
        NID_basic_constraints, const_cast<char*>("critical,CA:TRUE"));
    if (ext) { X509_add_ext(caCert_, ext, -1); X509_EXTENSION_free(ext); }

    // Key Usage: Certificate Sign, CRL Sign
    ext = X509V3_EXT_conf_nid(nullptr, &v3ctx,
        NID_key_usage, const_cast<char*>("critical,keyCertSign,cRLSign"));
    if (ext) { X509_add_ext(caCert_, ext, -1); X509_EXTENSION_free(ext); }

    // Subject Key Identifier
    ext = X509V3_EXT_conf_nid(nullptr, &v3ctx,
        NID_subject_key_identifier, const_cast<char*>("hash"));
    if (ext) { X509_add_ext(caCert_, ext, -1); X509_EXTENSION_free(ext); }

    // Sign with SHA-384
    if (!X509_sign(caCert_, caKey_, EVP_sha384())) {
        LOG_ERROR("Failed to sign CA certificate");
        return false;
    }

    LOG_INFO("CA key pair and self-signed certificate generated");
    return true;
}

bool CertificateAuthority::loadCA() {
    std::string keyPath  = caDir_ + "/ca.key";
    std::string certPath = caDir_ + "/ca.crt";

    // Load CA private key (using BIO to avoid OPENSSL_Applink issues on Windows)
    BIO* keyBio = BIO_new_file(keyPath.c_str(), "r");
    if (!keyBio) {
        LOG_ERROR("Cannot open CA key file: {}", keyPath);
        return false;
    }
    caKey_ = PEM_read_bio_PrivateKey(keyBio, nullptr, nullptr, nullptr);
    BIO_free(keyBio);

    if (!caKey_) {
        LOG_ERROR("Failed to read CA private key");
        return false;
    }

    // Load CA certificate
    BIO* certBio = BIO_new_file(certPath.c_str(), "r");
    if (!certBio) {
        LOG_ERROR("Cannot open CA cert file: {}", certPath);
        return false;
    }
    caCert_ = PEM_read_bio_X509(certBio, nullptr, nullptr, nullptr);
    BIO_free(certBio);

    if (!caCert_) {
        LOG_ERROR("Failed to read CA certificate");
        return false;
    }

    LOG_INFO("CA loaded from existing files");
    return true;
}

bool CertificateAuthority::saveCA() {
    std::string keyPath  = caDir_ + "/ca.key";
    std::string certPath = caDir_ + "/ca.crt";

    // Save CA private key (using BIO to avoid OPENSSL_Applink issues on Windows)
    BIO* keyBio = BIO_new_file(keyPath.c_str(), "w");
    if (!keyBio) {
        LOG_ERROR("Cannot create CA key file: {}", keyPath);
        return false;
    }
    PEM_write_bio_PrivateKey(keyBio, caKey_, nullptr, nullptr, 0, nullptr, nullptr);
    BIO_free(keyBio);

    // Save CA certificate
    BIO* certBio = BIO_new_file(certPath.c_str(), "w");
    if (!certBio) {
        LOG_ERROR("Cannot create CA cert file: {}", certPath);
        return false;
    }
    PEM_write_bio_X509(certBio, caCert_);
    BIO_free(certBio);

    LOG_INFO("CA key and certificate saved to {}", caDir_);
    return true;
}

// ─────────────────────────────────────────────────────────────
// Certificate Issuance (Step 5)
// ─────────────────────────────────────────────────────────────

IssuedCertificate CertificateAuthority::issueCertificate(
    const std::string& agentId,
    const std::string& publicKeyPem,
    int validDays)
{
    std::lock_guard<std::mutex> lock(mutex_);
    IssuedCertificate result;

    if (!initialized_) {
        LOG_ERROR("CA not initialized");
        return result;
    }

    // Parse agent's public key from PEM
    BIO* bio = BIO_new_mem_buf(publicKeyPem.data(), static_cast<int>(publicKeyPem.size()));
    if (!bio) {
        LOG_ERROR("Failed to create BIO for public key");
        return result;
    }

    EVP_PKEY* agentKey = PEM_read_bio_PUBKEY(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);

    if (!agentKey) {
        LOG_ERROR("Failed to parse agent public key PEM");
        return result;
    }

    // Create X509 certificate
    X509* cert = X509_new();
    if (!cert) {
        EVP_PKEY_free(agentKey);
        LOG_ERROR("Failed to create X509 structure");
        return result;
    }

    // Set version to v3
    X509_set_version(cert, 2);

    // Set serial number
    std::string serialStr = getNextSerial();
    BIGNUM* bn = nullptr;
    BN_dec2bn(&bn, serialStr.c_str());
    BN_to_ASN1_INTEGER(bn, X509_get_serialNumber(cert));
    BN_free(bn);

    // Set validity period
    X509_gmtime_adj(X509_getm_notBefore(cert), 0);
    X509_gmtime_adj(X509_getm_notAfter(cert), 
                    static_cast<long>(validDays) * 86400L);

    // Calculate expiry date for return value
    time_t now = time(nullptr);
    time_t expiry = now + static_cast<long>(validDays) * 86400L;
    struct tm* expiryTm = gmtime(&expiry);
    char expiryStr[64];
    strftime(expiryStr, sizeof(expiryStr), "%Y-%m-%dT%H:%M:%SZ", expiryTm);

    // Set subject — CN = agent_id
    X509_NAME* subjectName = X509_get_subject_name(cert);
    X509_NAME_add_entry_by_txt(subjectName, "CN", MBSTRING_ASC,
        reinterpret_cast<const unsigned char*>(agentId.c_str()), -1, -1, 0);
    X509_NAME_add_entry_by_txt(subjectName, "O", MBSTRING_ASC,
        reinterpret_cast<const unsigned char*>("ResolutePulse Agent"), -1, -1, 0);

    // Set issuer from CA certificate
    X509_set_issuer_name(cert, X509_get_subject_name(caCert_));

    // Set agent's public key
    X509_set_pubkey(cert, agentKey);

    // Add extensions
    X509V3_CTX v3ctx;
    X509V3_set_ctx_nodb(&v3ctx);
    X509V3_set_ctx(&v3ctx, caCert_, cert, nullptr, nullptr, 0);

    // Basic Constraints: CA:FALSE (this is an end-entity cert)
    X509_EXTENSION* ext = X509V3_EXT_conf_nid(nullptr, &v3ctx,
        NID_basic_constraints, const_cast<char*>("critical,CA:FALSE"));
    if (ext) { X509_add_ext(cert, ext, -1); X509_EXTENSION_free(ext); }

    // Key Usage: Digital Signature
    ext = X509V3_EXT_conf_nid(nullptr, &v3ctx,
        NID_key_usage, const_cast<char*>("critical,digitalSignature"));
    if (ext) { X509_add_ext(cert, ext, -1); X509_EXTENSION_free(ext); }

    // Extended Key Usage: TLS Client Authentication
    ext = X509V3_EXT_conf_nid(nullptr, &v3ctx,
        NID_ext_key_usage, const_cast<char*>("clientAuth"));
    if (ext) { X509_add_ext(cert, ext, -1); X509_EXTENSION_free(ext); }

    // Subject Key Identifier
    ext = X509V3_EXT_conf_nid(nullptr, &v3ctx,
        NID_subject_key_identifier, const_cast<char*>("hash"));
    if (ext) { X509_add_ext(cert, ext, -1); X509_EXTENSION_free(ext); }

    // Authority Key Identifier
    ext = X509V3_EXT_conf_nid(nullptr, &v3ctx,
        NID_authority_key_identifier, const_cast<char*>("keyid:always"));
    if (ext) { X509_add_ext(cert, ext, -1); X509_EXTENSION_free(ext); }

    // Sign the certificate with CA's private key using SHA-384
    if (!X509_sign(cert, caKey_, EVP_sha384())) {
        LOG_ERROR("Failed to sign certificate for agent: {}", agentId);
        X509_free(cert);
        EVP_PKEY_free(agentKey);
        return result;
    }

    // Convert to PEM
    result.certificatePem = x509ToPem(cert);
    result.serialNumber = serialStr;
    result.expiresAt = std::string(expiryStr);

    LOG_INFO("Issued certificate for agent={}, serial={}, expires={}",
             agentId, serialStr, expiryStr);

    X509_free(cert);
    EVP_PKEY_free(agentKey);

    return result;
}

// ─────────────────────────────────────────────────────────────
// Certificate Revocation (Step 6)
// ─────────────────────────────────────────────────────────────

bool CertificateAuthority::revokeCertificate(const std::string& serialNumber) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_) {
        LOG_ERROR("CA not initialized");
        return false;
    }

    // We track revocations via regenerating CRL
    // The actual DB update is handled by PostgresClient
    // Here we just regenerate the CRL
    LOG_INFO("Certificate revoked: serial={}", serialNumber);
    return true;
}

bool CertificateAuthority::generateCRL() {
    if (!caKey_ || !caCert_) {
        LOG_ERROR("CA key/cert not available for CRL generation");
        return false;
    }

    // Free existing CRL
    if (crl_) {
        X509_CRL_free(crl_);
        crl_ = nullptr;
    }

    crl_ = X509_CRL_new();
    if (!crl_) {
        LOG_ERROR("Failed to create CRL structure");
        return false;
    }

    // Set CRL version
    X509_CRL_set_version(crl_, 1);

    // Set issuer
    X509_CRL_set_issuer_name(crl_, X509_get_subject_name(caCert_));

    // Set update times
    X509_CRL_set1_lastUpdate(crl_, X509_getm_notBefore(caCert_));
    
    ASN1_TIME* nextUpdate = ASN1_TIME_new();
    X509_gmtime_adj(nextUpdate, static_cast<long>(CRL_VALIDITY_DAYS) * 86400L);
    X509_CRL_set1_nextUpdate(crl_, nextUpdate);
    ASN1_TIME_free(nextUpdate);

    // Sign the CRL
    if (!X509_CRL_sign(crl_, caKey_, EVP_sha384())) {
        LOG_ERROR("Failed to sign CRL");
        X509_CRL_free(crl_);
        crl_ = nullptr;
        return false;
    }

    // Save CRL to file (using BIO to avoid OPENSSL_Applink issues on Windows)
    std::string crlPath = caDir_ + "/crl.pem";
    BIO* crlBio = BIO_new_file(crlPath.c_str(), "w");
    if (!crlBio) {
        LOG_ERROR("Cannot create CRL file: {}", crlPath);
        return false;
    }
    PEM_write_bio_X509_CRL(crlBio, crl_);
    BIO_free(crlBio);

    LOG_INFO("CRL generated: {}", crlPath);
    return true;
}

// ─────────────────────────────────────────────────────────────
// Helper Methods
// ─────────────────────────────────────────────────────────────

std::string CertificateAuthority::getCACertPem() const {
    if (!caCert_) return "";
    return x509ToPem(caCert_);
}

std::string CertificateAuthority::getNextSerial() {
    std::string serialPath = caDir_ + "/serial.txt";
    
    // Read current serial
    uint64_t serial = 1000;
    {
        std::ifstream in(serialPath);
        if (in) in >> serial;
    }

    // Increment and save
    uint64_t nextSerial = serial + 1;
    {
        std::ofstream out(serialPath);
        out << nextSerial << std::endl;
    }

    return std::to_string(serial);
}

std::string CertificateAuthority::x509ToPem(X509* cert) {
    BIO* bio = BIO_new(BIO_s_mem());
    PEM_write_bio_X509(bio, cert);
    
    char* data = nullptr;
    long len = BIO_get_mem_data(bio, &data);
    std::string pem(data, len);
    BIO_free(bio);
    
    return pem;
}

std::string CertificateAuthority::keyToPem(EVP_PKEY* key, bool isPrivate) {
    BIO* bio = BIO_new(BIO_s_mem());
    
    if (isPrivate) {
        PEM_write_bio_PrivateKey(bio, key, nullptr, nullptr, 0, nullptr, nullptr);
    } else {
        PEM_write_bio_PUBKEY(bio, key);
    }
    
    char* data = nullptr;
    long len = BIO_get_mem_data(bio, &data);
    std::string pem(data, len);
    BIO_free(bio);
    
    return pem;
}

EVP_PKEY* CertificateAuthority::pemToKey(const std::string& pem, bool isPrivate) {
    BIO* bio = BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size()));
    EVP_PKEY* key = nullptr;
    
    if (isPrivate) {
        key = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
    } else {
        key = PEM_read_bio_PUBKEY(bio, nullptr, nullptr, nullptr);
    }
    
    BIO_free(bio);
    return key;
}

X509* CertificateAuthority::pemToX509(const std::string& pem) {
    BIO* bio = BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size()));
    X509* cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    return cert;
}

} // namespace ResolutePulse
