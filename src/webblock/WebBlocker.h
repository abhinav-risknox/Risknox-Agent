#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <nlohmann/json.hpp>

namespace ResolutePulse {

struct BlockedUrl {
    std::string url;
    std::string blockedAt;
    std::string status;   // "active" or "inactive"
    std::string source;   // "manager" or "local"

    nlohmann::json toJson() const {
        return {
            {"url", url},
            {"blockedAt", blockedAt},
            {"status", status},
            {"source", source}
        };
    }
};

struct WebBlockConfig {
    bool enabled = false;
    std::string configPath;     // Path to blocked_urls.json
    std::string hostsFilePath = "C:\\Windows\\System32\\drivers\\etc\\hosts";
};

class WebBlocker {
public:
    WebBlocker();
    ~WebBlocker();

    bool initialize(const WebBlockConfig& config);
    void start();
    void stop();

    // Operations
    bool blockUrl(const std::string& url);
    bool unblockUrl(const std::string& url);
    std::vector<BlockedUrl> getBlockedUrls() const;

    // Apply policy from Manager (additive or full replace)
    bool applyPolicy(const nlohmann::json& policy);

    // Status for reporting
    nlohmann::json getStatus() const;

private:
    bool updateHostsFile();
    bool loadBlockedUrls();
    bool saveBlockedUrls();
    void flushDnsCache();
    std::string cleanUrl(const std::string& url) const;
    std::string getCurrentTimestamp() const;

    WebBlockConfig config_;
    std::vector<BlockedUrl> blockedUrls_;
    mutable std::mutex mutex_;
    bool initialized_ = false;
};

} // namespace ResolutePulse
