#include "WebBlocker.h"
#include "utils/Logger.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <filesystem>
#include <Windows.h>

namespace ResolutePulse {

WebBlocker::WebBlocker() = default;
WebBlocker::~WebBlocker() { stop(); }

std::string WebBlocker::getCurrentTimestamp() const {
    time_t now = time(nullptr);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));
    return std::string(buf);
}

std::string WebBlocker::cleanUrl(const std::string& url) const {
    std::string clean = url;
    // Strip protocol
    if (clean.find("http://") == 0) clean = clean.substr(7);
    if (clean.find("https://") == 0) clean = clean.substr(8);
    // Strip trailing slash
    while (!clean.empty() && clean.back() == '/') clean.pop_back();
    // Strip www. prefix for normalization (we add www. variant in hosts)
    // Trim whitespace
    while (!clean.empty() && (clean.front() == ' ' || clean.front() == '\t')) clean.erase(clean.begin());
    while (!clean.empty() && (clean.back() == ' ' || clean.back() == '\t')) clean.pop_back();
    return clean;
}

bool WebBlocker::initialize(const WebBlockConfig& config) {
    config_ = config;

    // Ensure config directory exists
    if (!config_.configPath.empty()) {
        std::filesystem::path p(config_.configPath);
        if (p.has_parent_path()) {
            std::filesystem::create_directories(p.parent_path());
        }
    }

    loadBlockedUrls();
    initialized_ = true;

    LOG_INFO("WebBlocker initialized ({} blocked URLs loaded)", blockedUrls_.size());
    return true;
}

void WebBlocker::start() {
    // Apply any persisted rules on startup
    if (initialized_ && !blockedUrls_.empty()) {
        updateHostsFile();
        LOG_INFO("WebBlocker: Applied {} blocking rules to hosts file", blockedUrls_.size());
    }
}

void WebBlocker::stop() {
    // Optionally clean up hosts file on stop - currently we leave rules in place
    // for persistence across restarts (same as Python reference behavior)
}

bool WebBlocker::blockUrl(const std::string& url) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::string clean = cleanUrl(url);
    if (clean.empty()) return false;

    // Check duplicates
    for (const auto& b : blockedUrls_) {
        if (b.url == clean) {
            LOG_DEBUG("WebBlocker: URL already blocked: {}", clean);
            return true;
        }
    }

    BlockedUrl entry;
    entry.url = clean;
    entry.blockedAt = getCurrentTimestamp();
    entry.status = "active";
    entry.source = "local";

    blockedUrls_.push_back(entry);
    saveBlockedUrls();

    LOG_INFO("WebBlocker: Blocked URL: {}", clean);
    return updateHostsFile();
}

bool WebBlocker::unblockUrl(const std::string& url) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::string clean = cleanUrl(url);
    auto it = std::remove_if(blockedUrls_.begin(), blockedUrls_.end(),
        [&clean](const BlockedUrl& b) { return b.url == clean; });

    if (it == blockedUrls_.end()) {
        LOG_DEBUG("WebBlocker: URL not in blocked list: {}", clean);
        return true;
    }

    blockedUrls_.erase(it, blockedUrls_.end());
    saveBlockedUrls();

    LOG_INFO("WebBlocker: Unblocked URL: {}", clean);
    return updateHostsFile();
}

std::vector<BlockedUrl> WebBlocker::getBlockedUrls() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return blockedUrls_;
}

bool WebBlocker::applyPolicy(const nlohmann::json& policy) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (policy.contains("action")) {
        std::string action = policy["action"].get<std::string>();

        if (action == "block" && policy.contains("url")) {
            std::string url = cleanUrl(policy["url"].get<std::string>());
            bool exists = false;
            for (const auto& b : blockedUrls_) {
                if (b.url == url) { exists = true; break; }
            }
            if (!exists) {
                BlockedUrl entry;
                entry.url = url;
                entry.blockedAt = getCurrentTimestamp();
                entry.status = "active";
                entry.source = "manager";
                blockedUrls_.push_back(entry);
            }
        } else if (action == "unblock" && policy.contains("url")) {
            std::string url = cleanUrl(policy["url"].get<std::string>());
            blockedUrls_.erase(
                std::remove_if(blockedUrls_.begin(), blockedUrls_.end(),
                    [&url](const BlockedUrl& b) { return b.url == url; }),
                blockedUrls_.end());
        } else if (action == "replace" && policy.contains("urls")) {
            // Full replacement of all blocked URLs
            blockedUrls_.clear();
            for (const auto& u : policy["urls"]) {
                BlockedUrl entry;
                entry.url = cleanUrl(u.value("url", ""));
                entry.blockedAt = u.value("blockedAt", getCurrentTimestamp());
                entry.status = u.value("status", "active");
                entry.source = "manager";
                if (!entry.url.empty()) {
                    blockedUrls_.push_back(entry);
                }
            }
        }
    }

    saveBlockedUrls();
    bool result = updateHostsFile();
    LOG_INFO("WebBlocker: Policy applied ({} URLs blocked)", blockedUrls_.size());
    return result;
}

nlohmann::json WebBlocker::getStatus() const {
    std::lock_guard<std::mutex> lock(mutex_);

    nlohmann::json status;
    status["enabled"] = config_.enabled;
    status["blockedUrlCount"] = blockedUrls_.size();

    nlohmann::json urls = nlohmann::json::array();
    for (const auto& b : blockedUrls_) {
        urls.push_back(b.toJson());
    }
    status["blockedUrls"] = urls;
    return status;
}

bool WebBlocker::updateHostsFile() {
    try {
        // Read existing hosts file
        std::string content;
        {
            std::ifstream in(config_.hostsFilePath, std::ios::in);
            if (in.is_open()) {
                std::ostringstream ss;
                ss << in.rdbuf();
                content = ss.str();
            }
        }

        // Remove our tagged lines
        std::istringstream stream(content);
        std::string line;
        std::vector<std::string> cleanedLines;

        while (std::getline(stream, line)) {
            // Remove \r if present
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.find("# ResolutePulse Block") == std::string::npos) {
                cleanedLines.push_back(line);
            }
        }

        // Remove our header if present
        auto headerIt = std::find_if(cleanedLines.begin(), cleanedLines.end(),
            [](const std::string& l) { return l.find("# ResolutePulse Security Agent") != std::string::npos; });
        if (headerIt != cleanedLines.end()) {
            cleanedLines.erase(headerIt);
        }

        // Remove trailing empty lines
        while (!cleanedLines.empty() && cleanedLines.back().empty()) {
            cleanedLines.pop_back();
        }

        // Add our section
        if (!blockedUrls_.empty()) {
            cleanedLines.push_back("");
            cleanedLines.push_back("# ResolutePulse Security Agent - Blocked URLs");

            for (const auto& entry : blockedUrls_) {
                if (entry.status == "active") {
                    cleanedLines.push_back("127.0.0.1 " + entry.url + " # ResolutePulse Block");
                    // Also block www. variant
                    if (entry.url.find("www.") != 0) {
                        cleanedLines.push_back("127.0.0.1 www." + entry.url + " # ResolutePulse Block");
                    }
                }
            }
        }

        // Write back
        std::string newContent;
        for (size_t i = 0; i < cleanedLines.size(); ++i) {
            newContent += cleanedLines[i];
            if (i + 1 < cleanedLines.size()) newContent += "\n";
        }

        {
            std::ofstream out(config_.hostsFilePath, std::ios::out | std::ios::trunc);
            if (!out.is_open()) {
                LOG_ERROR("WebBlocker: Failed to open hosts file for writing: {}",
                          config_.hostsFilePath);
                return false;
            }
            out << newContent;
        }

        // Flush DNS cache
        flushDnsCache();

        LOG_DEBUG("WebBlocker: Hosts file updated successfully");
        return true;

    } catch (const std::exception& e) {
        LOG_ERROR("WebBlocker: Error updating hosts file: {}", e.what());
        return false;
    }
}

bool WebBlocker::loadBlockedUrls() {
    try {
        if (config_.configPath.empty()) return true;
        if (!std::filesystem::exists(config_.configPath)) return true;

        std::ifstream in(config_.configPath);
        if (!in.is_open()) return false;

        nlohmann::json j = nlohmann::json::parse(in);
        blockedUrls_.clear();

        for (const auto& item : j) {
            BlockedUrl entry;
            entry.url = item.value("url", "");
            entry.blockedAt = item.value("blockedAt", "");
            entry.status = item.value("status", "active");
            entry.source = item.value("source", "local");
            if (!entry.url.empty()) {
                blockedUrls_.push_back(entry);
            }
        }
        return true;
    } catch (const std::exception& e) {
        LOG_WARN("WebBlocker: Failed to load blocked URLs: {}", e.what());
        return false;
    }
}

bool WebBlocker::saveBlockedUrls() {
    try {
        if (config_.configPath.empty()) return true;

        nlohmann::json j = nlohmann::json::array();
        for (const auto& entry : blockedUrls_) {
            j.push_back(entry.toJson());
        }

        std::ofstream out(config_.configPath);
        if (!out.is_open()) return false;
        out << j.dump(2);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("WebBlocker: Failed to save blocked URLs: {}", e.what());
        return false;
    }
}

void WebBlocker::flushDnsCache() {
    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};

    wchar_t cmd[] = L"ipconfig /flushdns";
    if (CreateProcessW(nullptr, cmd, nullptr, nullptr, FALSE,
                       CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 5000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        LOG_DEBUG("WebBlocker: DNS cache flushed");
    }
}

} // namespace ResolutePulse

