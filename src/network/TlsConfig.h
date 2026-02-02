#pragma once

#include <string>

namespace ResolutePulse {

/**
 * TLS/SSL configuration for secure network communication
 */
struct TlsConfig {
    // Enable/disable TLS
    bool enabled = true;
    
    // Certificate verification
    bool verify_peer = true;            // Verify server certificate
    bool verify_hostname = true;        // Verify hostname matches certificate
    
    // Certificate paths
    std::string ca_cert_path;           // CA certificate for verification
    std::string client_cert_path;       // Client certificate (for mTLS)
    std::string client_key_path;        // Client private key (for mTLS)
    
    // TLS version constraints
    std::string min_tls_version = "1.2"; // Minimum: TLS 1.2
    std::string max_tls_version = "1.3"; // Maximum: TLS 1.3
    
    // Cipher configuration
    std::string cipher_list = 
        "ECDHE-RSA-AES256-GCM-SHA384:"
        "ECDHE-RSA-AES128-GCM-SHA256:"
        "ECDHE-RSA-CHACHA20-POLY1305";
    
    // Timeouts
    int handshake_timeout_ms = 10000;   // 10 seconds for TLS handshake
    
    /**
     * Create a development/test configuration (self-signed, no verification)
     */
    static TlsConfig createDevConfig() {
        TlsConfig config;
        config.enabled = true;
        config.verify_peer = false;      // Accept self-signed certs
        config.verify_hostname = false;
        return config;
    }
    
    /**
     * Create a production configuration (full verification)
     */
    static TlsConfig createProductionConfig(
        const std::string& caCert,
        const std::string& clientCert = "",
        const std::string& clientKey = ""
    ) {
        TlsConfig config;
        config.enabled = true;
        config.verify_peer = true;
        config.verify_hostname = true;
        config.ca_cert_path = caCert;
        config.client_cert_path = clientCert;
        config.client_key_path = clientKey;
        return config;
    }
};

} // namespace ResolutePulse
