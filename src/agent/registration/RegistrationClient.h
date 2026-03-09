#pragma once

#include "CertificateStore.h"
#include "common/Protocol.h"

#include <string>

namespace ResolutePulse {

class RegistrationClient {
public:
    RegistrationClient();
    ~RegistrationClient();

    // Generate ECC P-256 key pair
    // Saves encrypted private key to certsDir/agent.key
    // Returns public key PEM in memory
    bool generateKeyPair(const std::string& certsDir);

    // Register with the manager server
    // Uses one-way TLS (verifies manager cert, no client cert)
    // On success, saves agent.crt and ca.crt via CertificateStore
    bool registerWithManager(const std::string& host,
                             int port,
                             const std::string& agentId,
                             const std::string& hostname,
                             const std::string& osType,
                             const std::string& osVersion,
                             const std::string& agentVersion,
                             CertificateStore& certStore);

    // Get the generated public key PEM
    const std::string& getPublicKeyPem() const { return publicKeyPem_; }

    // Get last error
    const std::string& getLastError() const { return lastError_; }

private:
    std::string publicKeyPem_;
    std::string certsDir_;
    std::string lastError_;
};

} // namespace ResolutePulse
