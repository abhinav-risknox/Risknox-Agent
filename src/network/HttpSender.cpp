// Only enable OpenSSL if the macro is defined by CMake
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
// OpenSSL support enabled
#endif

#include <httplib/httplib.h>

#include "HttpSender.h"
#include "utils/Logger.h"
#include <regex>

namespace ResolutePulse {

HttpSender::HttpSender() = default;

HttpSender::~HttpSender() = default;

bool HttpSender::parseUrl(const std::string& url) {
    // Simple URL parsing: [http|https]://host[:port]/path
    std::regex urlRegex(R"(^(https?)://([^:/]+)(?::(\d+))?(.*)$)");
    std::smatch matches;
    
    if (!std::regex_match(url, matches, urlRegex)) {
        lastError_ = "Invalid URL format: " + url;
        return false;
    }
    
    std::string scheme = matches[1].str();
    host_ = matches[2].str();
    
    if (matches[3].matched) {
        port_ = std::stoi(matches[3].str());
    } else {
        port_ = (scheme == "https") ? 443 : 80;
    }
    
    path_ = matches[4].str();
    if (path_.empty()) {
        path_ = "/";
    }
    
    useHttps_ = (scheme == "https");
    
    return true;
}

bool HttpSender::initialize(const std::string& url,
                           const std::string& authToken,
                           const TlsConfig& tls) {
    if (!parseUrl(url)) {
        LOG_ERROR("Failed to parse URL: {}", lastError_);
        return false;
    }
    
    authToken_ = authToken;
    
    LOG_INFO("Initializing HTTP sender: {}:{}{} (HTTPS: {})", 
             host_, port_, path_, useHttps_);
    
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
    if (useHttps_) {
        // Create HTTPS client
        httpsClient_ = std::make_unique<httplib::SSLClient>(host_, port_);
        
        // Configure TLS
        if (!tls.ca_cert_path.empty()) {
            httpsClient_->set_ca_cert_path(tls.ca_cert_path.c_str());
            LOG_DEBUG("Using CA cert: {}", tls.ca_cert_path);
        }
        
        // Configure mTLS (client certificate)
        if (!tls.client_cert_path.empty() && !tls.client_key_path.empty()) {
            httpsClient_->set_client_cert_path(
                tls.client_cert_path.c_str(),
                tls.client_key_path.c_str()
            );
            LOG_INFO("mTLS enabled with client certificate");
        }
        
        httpsClient_->enable_server_certificate_verification(tls.verify_peer);
        httpsClient_->set_connection_timeout(10, 0);  // 10 seconds
        httpsClient_->set_read_timeout(30, 0);        // 30 seconds
        httpsClient_->set_write_timeout(30, 0);       // 30 seconds
        
        LOG_INFO("HTTP sender initialized successfully (HTTPS mode)");
        return true;
    }
#else
    if (useHttps_) {
        LOG_ERROR("HTTPS requested but OpenSSL support not compiled in. Use HTTP URL.");
        lastError_ = "HTTPS not supported - OpenSSL not available";
        return false;
    }
    (void)tls;  // Suppress unused parameter warning
#endif

    // Create HTTP client
    httpClient_ = std::make_unique<httplib::Client>(host_, port_);
    httpClient_->set_connection_timeout(10, 0);
    httpClient_->set_read_timeout(30, 0);
    httpClient_->set_write_timeout(30, 0);
    
    LOG_INFO("HTTP sender initialized successfully (HTTP mode)");
    return true;
}

SendResult HttpSender::sendBatch(const std::vector<Event>& events,
                                 const std::string& agentId) {
    if (events.empty()) {
        return SendResult::Success;
    }
    
    // Create JSON payload
    nlohmann::json payload = Event::createBatchPayload(agentId, events);
    std::string body = payload.dump();
    
    // Set headers
    httplib::Headers headers = {
        {"Authorization", "Bearer " + authToken_},
        {"Content-Type", "application/json"},
        {"X-Agent-ID", agentId}
    };
    
    LOG_DEBUG("Sending batch of {} events ({} bytes)", events.size(), body.size());
    
    httplib::Result result;
    
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
    if (useHttps_ && httpsClient_) {
        result = httpsClient_->Post(path_, headers, body, "application/json");
    } else 
#endif
    if (httpClient_) {
        result = httpClient_->Post(path_, headers, body, "application/json");
    } else {
        lastError_ = "HTTP client not initialized";
        failedBatches_++;
        return SendResult::NetworkError;
    }
    
    if (!result) {
        auto err = result.error();
        lastError_ = "HTTP request failed: " + httplib::to_string(err);
        LOG_ERROR("{}", lastError_);
        failedBatches_++;
        return SendResult::NetworkError;
    }
    
    int status = result->status;
    
    if (status >= 200 && status < 300) {
        LOG_DEBUG("Batch sent successfully (status {})", status);
        batchesSent_++;
        eventsSent_ += events.size();
        return SendResult::Success;
    }
    
    if (status == 401 || status == 403) {
        lastError_ = "Authentication failed: " + std::to_string(status);
        LOG_ERROR("{}", lastError_);
        failedBatches_++;
        return SendResult::AuthError;
    }
    
    if (status >= 400 && status < 500) {
        lastError_ = "Client error: " + std::to_string(status) + " - " + result->body;
        LOG_ERROR("{}", lastError_);
        failedBatches_++;
        return SendResult::ClientError;
    }
    
    if (status >= 500) {
        lastError_ = "Server error: " + std::to_string(status);
        LOG_WARN("{}", lastError_);
        failedBatches_++;
        return SendResult::ServerError;
    }
    
    lastError_ = "Unexpected status: " + std::to_string(status);
    LOG_WARN("{}", lastError_);
    failedBatches_++;
    return SendResult::NetworkError;
}

} // namespace ResolutePulse
