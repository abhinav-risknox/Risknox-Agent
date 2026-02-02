#include "TcpSender.h"
#include "utils/Logger.h"

#include <nlohmann/json.hpp>
#include <sstream>

#ifdef _WIN32
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#endif

namespace ResolutePulse {

TcpSender::TcpSender() = default;

TcpSender::~TcpSender() {
    disconnect();
    
#ifdef _WIN32
    if (wsaInitialized_) {
        WSACleanup();
    }
#endif
}

bool TcpSender::initialize(const std::string& host, int port) {
    host_ = host;
    port_ = port;
    
    LOG_INFO("Initializing TCP sender: {}:{}", host_, port_);
    
#ifdef _WIN32
    // Initialize Winsock
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        lastError_ = "WSAStartup failed: " + std::to_string(result);
        LOG_ERROR(lastError_);
        return false;
    }
    wsaInitialized_ = true;
#endif
    
    // Try to connect
    if (!connect()) {
        LOG_WARN("Initial connection failed, will retry on first send");
        return true; // Still return true - we'll retry later
    }
    
    LOG_INFO("TCP sender initialized successfully");
    return true;
}

bool TcpSender::connect() {
    std::lock_guard<std::mutex> lock(socketMutex_);
    
    // Clean up existing socket if any
    if (socket_ != INVALID_SOCKET) {
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
    }
    
    // Create socket
    socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket_ == INVALID_SOCKET) {
#ifdef _WIN32
        lastError_ = "Socket creation failed: " + std::to_string(WSAGetLastError());
#else
        lastError_ = "Socket creation failed";
#endif
        LOG_ERROR(lastError_);
        return false;
    }
    
    // Set timeouts
#ifdef _WIN32
    DWORD timeout = SEND_TIMEOUT_MS;
    setsockopt(socket_, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));
    setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
#endif
    
    // Resolve hostname
    struct addrinfo hints = {0}, *result = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    
    std::string portStr = std::to_string(port_);
    int ret = getaddrinfo(host_.c_str(), portStr.c_str(), &hints, &result);
    if (ret != 0) {
        lastError_ = "Failed to resolve host: " + host_;
        LOG_ERROR(lastError_);
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
        return false;
    }
    
    // Connect to server
    ret = ::connect(socket_, result->ai_addr, (int)result->ai_addrlen);
    freeaddrinfo(result);
    
    if (ret == SOCKET_ERROR) {
#ifdef _WIN32
        lastError_ = "Connection failed: " + std::to_string(WSAGetLastError());
#else
        lastError_ = "Connection failed";
#endif
        LOG_ERROR(lastError_);
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
        return false;
    }
    
    connected_ = true;
    reconnections_++;
    LOG_INFO("Connected to Fluent Bit at {}:{}", host_, port_);
    
    return true;
}

void TcpSender::disconnect() {
    std::lock_guard<std::mutex> lock(socketMutex_);
    
    if (socket_ != INVALID_SOCKET) {
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
    }
    
    connected_ = false;
    LOG_DEBUG("Disconnected from Fluent Bit");
}

void TcpSender::cleanup() {
    disconnect();
}

SendResult TcpSender::sendBatch(const std::vector<Event>& events) {
    if (events.empty()) {
        return SendResult::Success;
    }
    
    // Build NDJSON batch (one JSON object per line) with compact field names
    std::ostringstream batch;
    for (const auto& event : events) {
        nlohmann::json j;
        j["c"] = event.channel;      // c = channel
        j["e"] = event.eventId;       // e = event_id
        j["t"] = event.timestamp;     // t = timestamp
        j["x"] = event.xml;           // x = xml (raw, no parsing)
        
        batch << j.dump() << "\n";
    }
    
    std::string batchData = batch.str();
    
    // Try to send, reconnect if needed
    int attempts = 0;
    while (attempts < MAX_RECONNECT_ATTEMPTS) {
        if (!connected_.load()) {
            LOG_DEBUG("Not connected, attempting to connect (attempt {})", attempts + 1);
            if (!connect()) {
                attempts++;
                std::this_thread::sleep_for(std::chrono::milliseconds(1000 * attempts));
                continue;
            }
        }
        
        if (sendRaw(batchData.c_str(), batchData.length())) {
            eventsSent_ += events.size();
            bytesSent_ += batchData.length();
            batchesSent_++;
            return SendResult::Success;
        }
        
        // Send failed, disconnect and retry
        LOG_WARN("Send failed, will retry");
        disconnect();
        attempts++;
        std::this_thread::sleep_for(std::chrono::milliseconds(1000 * attempts));
    }
    
    failedSends_ += events.size();
    lastError_ = "Failed to send batch after " + std::to_string(MAX_RECONNECT_ATTEMPTS) + " attempts";
    LOG_ERROR(lastError_);
    
    return SendResult::NetworkError;
}

bool TcpSender::sendRaw(const char* data, size_t length) {
    std::lock_guard<std::mutex> lock(socketMutex_);
    
    if (socket_ == INVALID_SOCKET || !connected_.load()) {
        lastError_ = "Not connected";
        return false;
    }
    
    size_t totalSent = 0;
    while (totalSent < length) {
        int sent = send(socket_, data + totalSent, (int)(length - totalSent), 0);
        
        if (sent == SOCKET_ERROR) {
#ifdef _WIN32
            int error = WSAGetLastError();
            lastError_ = "Send error: " + std::to_string(error);
#else
            lastError_ = "Send error";
#endif
            LOG_ERROR(lastError_);
            connected_ = false;
            return false;
        }
        
        if (sent == 0) {
            lastError_ = "Connection closed by remote";
            LOG_ERROR(lastError_);
            connected_ = false;
            return false;
        }
        
        totalSent += sent;
    }
    
    return true;
}

} // namespace ResolutePulse
