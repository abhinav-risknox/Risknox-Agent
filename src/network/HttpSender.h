#pragma once

#include "collector/Event.h"
#include "config/ConfigManager.h"
#include <string>
#include <vector>
#include <memory>

// Forward declarations
namespace httplib {
    class Client;
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
    class SSLClient;
#endif
}

namespace ResolutePulse {

enum class SendResult {
    Success,        // Events sent successfully
    NetworkError,   // Network/connection error (should retry)
    ServerError,    // Server returned 5xx (should retry)
    ClientError,    // Server returned 4xx (don't retry, log error)
    AuthError       // Authentication failed (401/403)
};

class HttpSender {
public:
    HttpSender();
    ~HttpSender();
    
    // Initialize the sender
    // @param url - HTTP/HTTPS endpoint URL
    // @param authToken - Bearer token for authentication
    // @param tls - TLS configuration (for mTLS support)
    bool initialize(const std::string& url,
                   const std::string& authToken,
                   const TlsConfig& tls);
    
    // Send a batch of events
    // @param events - Events to send
    // @param agentId - Agent identifier
    // @return SendResult indicating success or type of failure
    SendResult sendBatch(const std::vector<Event>& events,
                        const std::string& agentId);
    
    // Get last error message
    const std::string& getLastError() const { return lastError_; }
    
    // Get statistics
    uint64_t getBatchesSent() const { return batchesSent_; }
    uint64_t getEventsSent() const { return eventsSent_; }
    uint64_t getFailedBatches() const { return failedBatches_; }
    
private:
    bool parseUrl(const std::string& url);
    
    std::unique_ptr<httplib::Client> httpClient_;
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
    std::unique_ptr<httplib::SSLClient> httpsClient_;
#endif
    bool useHttps_ = false;
    
    std::string host_;
    int port_ = 80;
    std::string path_;
    std::string authToken_;
    
    std::string lastError_;
    
    uint64_t batchesSent_ = 0;
    uint64_t eventsSent_ = 0;
    uint64_t failedBatches_ = 0;
};

} // namespace ResolutePulse
