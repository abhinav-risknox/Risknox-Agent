#pragma once

#include <string>

namespace ResolutePulse {

class CertificateStore {
public:
    CertificateStore();
    ~CertificateStore() = default;

    // Set the certificates directory
    void setCertsDir(const std::string& certsDir) { certsDir_ = certsDir; }

    // Check if agent certificate exists on disk
    bool exists() const;

    // Save certificates received during registration
    bool save(const std::string& agentCertPem,
              const std::string& caCertPem);

    // Load agent certificate and key info
    bool load();

    // Check if the loaded certificate is still valid (not expired)
    bool isValid() const;

    // Days until certificate expiration
    int daysUntilExpiry() const;

    // Get file paths
    std::string getAgentCertPath() const { return certsDir_ + "/agent.crt"; }
    std::string getAgentKeyPath()  const { return certsDir_ + "/agent.key"; }
    std::string getCACertPath()    const { return certsDir_ + "/ca.crt"; }

    // Getters for loaded data
    const std::string& getAgentCertPem() const { return agentCertPem_; }
    const std::string& getCACertPem()    const { return caCertPem_; }

private:
    std::string certsDir_ = "certs";
    std::string agentCertPem_;
    std::string caCertPem_;
    
    // Parsed expiry from loaded cert
    long expiryTime_ = 0;  // Unix timestamp
};

} // namespace ResolutePulse
