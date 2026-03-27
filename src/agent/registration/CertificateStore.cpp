#include "CertificateStore.h"
#include "utils/Logger.h"

#include <openssl/x509.h>
#include <openssl/pem.h>
#include <openssl/bio.h>

#include <fstream>
#include <sstream>
#include <filesystem>
#include <ctime>

namespace ResolutePulse {

CertificateStore::CertificateStore() = default;

bool CertificateStore::exists() const {
    return std::filesystem::exists(getAgentCertPath()) &&
           std::filesystem::exists(getAgentKeyPath()) &&
           std::filesystem::exists(getCACertPath());
}

bool CertificateStore::save(const std::string& agentCertPem,
                             const std::string& caCertPem) {
    // Create certs directory
    std::filesystem::create_directories(certsDir_);

    // Save agent certificate
    {
        std::ofstream f(getAgentCertPath());
        if (!f) {
            LOG_ERROR("Failed to write agent certificate: {}", getAgentCertPath());
            return false;
        }
        f << agentCertPem;
    }

    // Save CA certificate
    {
        std::ofstream f(getCACertPath());
        if (!f) {
            LOG_ERROR("Failed to write CA certificate: {}", getCACertPath());
            return false;
        }
        f << caCertPem;
    }

    agentCertPem_ = agentCertPem;
    caCertPem_ = caCertPem;

    LOG_INFO("Certificates saved to {}", certsDir_);
    return true;
}

bool CertificateStore::load() {
    // Load agent certificate PEM
    {
        std::ifstream f(getAgentCertPath());
        if (!f) {
            LOG_ERROR("Cannot read agent certificate: {}", getAgentCertPath());
            return false;
        }
        std::ostringstream ss;
        ss << f.rdbuf();
        agentCertPem_ = ss.str();
    }

    // Load CA certificate PEM
    {
        std::ifstream f(getCACertPath());
        if (!f) {
            LOG_ERROR("Cannot read CA certificate: {}", getCACertPath());
            return false;
        }
        std::ostringstream ss;
        ss << f.rdbuf();
        caCertPem_ = ss.str();
    }

    // Parse the agent cert to get expiry time
    BIO* bio = BIO_new_mem_buf(agentCertPem_.data(), static_cast<int>(agentCertPem_.size()));
    X509* cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);

    if (!cert) {
        LOG_ERROR("Failed to parse agent certificate");
        return false;
    }

    // Get notAfter and convert to time_t
    // ASN1_TIME_to_tm() returns UTC time, so use _mkgmtime (not mktime which assumes local time)
    const ASN1_TIME* notAfter = X509_get0_notAfter(cert);
    struct tm tm = {};
    ASN1_TIME_to_tm(notAfter, &tm);
#ifdef _WIN32
    expiryTime_ = static_cast<long>(_mkgmtime(&tm));
#else
    expiryTime_ = static_cast<long>(timegm(&tm));
#endif

    X509_free(cert);

    LOG_INFO("Certificates loaded from {}", certsDir_);
    return true;
}

bool CertificateStore::isValid() const {
    if (agentCertPem_.empty()) return false;
    return daysUntilExpiry() > 0;
}

int CertificateStore::daysUntilExpiry() const {
    if (expiryTime_ == 0) return 0;

    time_t now = time(nullptr);
    long diff = expiryTime_ - now;
    if (diff <= 0) return 0;

    return static_cast<int>(diff / 86400);
}

} // namespace ResolutePulse
