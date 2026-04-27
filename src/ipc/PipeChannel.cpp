#include "PipeChannel.h"
#include "utils/Logger.h"

#include <cstring>

namespace ResolutePulse {

// ─────────────────────────────────────────────────────────────────────────────
// Wire helpers
// ─────────────────────────────────────────────────────────────────────────────

// The pipe prefix all Named Pipes require on Windows
static const std::string PIPE_PREFIX = "\\\\.\\pipe\\";

// ─────────────────────────────────────────────────────────────────────────────
// PipeServer
// ─────────────────────────────────────────────────────────────────────────────

PipeServer::~PipeServer() {
    close();
}

bool PipeServer::listen(const std::string& pipeName) {
    std::string fullName = PIPE_PREFIX + pipeName;

    pipe_ = CreateNamedPipeA(
        fullName.c_str(),
        PIPE_ACCESS_DUPLEX,                          // read + write
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
        1,          // only 1 instance (one agent talks to each worker)
        65536,      // outbound buffer
        65536,      // inbound buffer
        0,          // default timeout
        nullptr);   // no security attributes (local-only, process-level)

    if (pipe_ == INVALID_HANDLE_VALUE) {
        lastError_ = "CreateNamedPipe failed: " + std::to_string(GetLastError());
        LOG_ERROR("{}", lastError_);
        return false;
    }

    LOG_INFO("PipeServer waiting for connection on {}", fullName);

    // Block until the agent connects
    if (!ConnectNamedPipe(pipe_, nullptr)) {
        DWORD err = GetLastError();
        if (err != ERROR_PIPE_CONNECTED) {
            lastError_ = "ConnectNamedPipe failed: " + std::to_string(err);
            LOG_ERROR("{}", lastError_);
            CloseHandle(pipe_);
            pipe_ = INVALID_HANDLE_VALUE;
            return false;
        }
    }

    connected_ = true;
    LOG_INFO("PipeServer connected: {}", fullName);
    return true;
}

bool PipeServer::isConnected() const { return connected_; }

bool PipeServer::writeAll(const void* data, DWORD len) {
    const auto* ptr = static_cast<const char*>(data);
    DWORD written = 0;
    while (written < len) {
        DWORD n = 0;
        if (!WriteFile(pipe_, ptr + written, len - written, &n, nullptr) || n == 0) {
            lastError_ = "WriteFile failed: " + std::to_string(GetLastError());
            connected_ = false;
            return false;
        }
        written += n;
    }
    return true;
}

bool PipeServer::readAll(void* data, DWORD len, DWORD timeoutMs) {
    auto* ptr = static_cast<char*>(data);
    DWORD total = 0;
    while (total < len) {
        // Check if data is ready (peek with optional timeout)
        DWORD avail = 0;
        if (timeoutMs != INFINITE) {
            DWORD waited = 0;
            while (waited < timeoutMs) {
                PeekNamedPipe(pipe_, nullptr, 0, nullptr, &avail, nullptr);
                if (avail > 0) break;
                Sleep(10);
                waited += 10;
            }
            if (avail == 0) {
                lastError_ = "readAll timed out";
                return false;
            }
        }
        DWORD n = 0;
        if (!ReadFile(pipe_, ptr + total, len - total, &n, nullptr) || n == 0) {
            lastError_ = "ReadFile failed: " + std::to_string(GetLastError());
            connected_ = false;
            return false;
        }
        total += n;
    }
    return true;
}

bool PipeServer::sendJson(const nlohmann::json& msg) {
    std::string body = msg.dump();
    uint32_t len = static_cast<uint32_t>(body.size());
    // 4-byte LE length prefix
    uint8_t header[4] = {
        static_cast<uint8_t>(len & 0xFF),
        static_cast<uint8_t>((len >> 8) & 0xFF),
        static_cast<uint8_t>((len >> 16) & 0xFF),
        static_cast<uint8_t>((len >> 24) & 0xFF)
    };
    return writeAll(header, 4) && writeAll(body.data(), len);
}

bool PipeServer::recvJson(nlohmann::json& out, DWORD timeoutMs) {
    uint8_t header[4] = {};
    if (!readAll(header, 4, timeoutMs)) return false;

    uint32_t len = header[0]
                 | (static_cast<uint32_t>(header[1]) << 8)
                 | (static_cast<uint32_t>(header[2]) << 16)
                 | (static_cast<uint32_t>(header[3]) << 24);

    if (len == 0 || len > 1024 * 1024) {
        lastError_ = "Invalid message length: " + std::to_string(len);
        return false;
    }

    std::string body(len, '\0');
    if (!readAll(&body[0], len, timeoutMs)) return false;

    out = nlohmann::json::parse(body, nullptr, false);
    return !out.is_discarded();
}

void PipeServer::close() {
    if (pipe_ != INVALID_HANDLE_VALUE) {
        FlushFileBuffers(pipe_);
        DisconnectNamedPipe(pipe_);
        CloseHandle(pipe_);
        pipe_ = INVALID_HANDLE_VALUE;
        connected_ = false;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// PipeClient
// ─────────────────────────────────────────────────────────────────────────────

PipeClient::~PipeClient() {
    close();
}

bool PipeClient::connect(const std::string& pipeName, DWORD timeoutMs) {
    std::string fullName = PIPE_PREFIX + pipeName;

    DWORD waited = 0;
    while (waited <= timeoutMs) {
        // Try to open the pipe
        pipe_ = CreateFileA(
            fullName.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            0,          // no sharing
            nullptr,    // default security
            OPEN_EXISTING,
            0,
            nullptr);

        if (pipe_ != INVALID_HANDLE_VALUE) break;

        DWORD err = GetLastError();
        if (err != ERROR_PIPE_BUSY) {
            // Server hasn't created the pipe yet - wait a bit
            if (waited >= timeoutMs) {
                lastError_ = "PipeClient: failed to open pipe " + fullName
                            + " err=" + std::to_string(err);
                return false;
            }
            Sleep(100);
            waited += 100;
            continue;
        }

        // Pipe exists but is busy - use WaitNamedPipe
        if (!WaitNamedPipeA(fullName.c_str(), timeoutMs - waited)) {
            lastError_ = "WaitNamedPipe failed: " + std::to_string(GetLastError());
            return false;
        }
    }

    if (pipe_ == INVALID_HANDLE_VALUE) {
        lastError_ = "PipeClient: could not open " + fullName;
        return false;
    }

    // Switch to byte mode
    DWORD mode = PIPE_READMODE_BYTE;
    SetNamedPipeHandleState(pipe_, &mode, nullptr, nullptr);

    connected_ = true;
    LOG_INFO("PipeClient connected: {}", fullName);
    return true;
}

bool PipeClient::isConnected() const { return connected_; }

bool PipeClient::writeAll(const void* data, DWORD len) {
    const auto* ptr = static_cast<const char*>(data);
    DWORD written = 0;
    while (written < len) {
        DWORD n = 0;
        if (!WriteFile(pipe_, ptr + written, len - written, &n, nullptr) || n == 0) {
            lastError_ = "WriteFile failed: " + std::to_string(GetLastError());
            connected_ = false;
            return false;
        }
        written += n;
    }
    return true;
}

bool PipeClient::readAll(void* data, DWORD len, DWORD timeoutMs) {
    auto* ptr = static_cast<char*>(data);
    DWORD total = 0;
    while (total < len) {
        DWORD avail = 0;
        if (timeoutMs != INFINITE) {
            DWORD waited = 0;
            while (waited < timeoutMs) {
                PeekNamedPipe(pipe_, nullptr, 0, nullptr, &avail, nullptr);
                if (avail > 0) break;
                Sleep(10);
                waited += 10;
            }
            if (avail == 0) {
                lastError_ = "readAll timed out after " + std::to_string(timeoutMs) + "ms";
                return false;
            }
        }
        DWORD n = 0;
        if (!ReadFile(pipe_, ptr + total, len - total, &n, nullptr) || n == 0) {
            lastError_ = "ReadFile failed: " + std::to_string(GetLastError());
            connected_ = false;
            return false;
        }
        total += n;
    }
    return true;
}

bool PipeClient::sendJson(const nlohmann::json& msg) {
    std::string body = msg.dump();
    uint32_t len = static_cast<uint32_t>(body.size());
    uint8_t header[4] = {
        static_cast<uint8_t>(len & 0xFF),
        static_cast<uint8_t>((len >> 8) & 0xFF),
        static_cast<uint8_t>((len >> 16) & 0xFF),
        static_cast<uint8_t>((len >> 24) & 0xFF)
    };
    return writeAll(header, 4) && writeAll(body.data(), len);
}

bool PipeClient::recvJson(nlohmann::json& out, DWORD timeoutMs) {
    uint8_t header[4] = {};
    if (!readAll(header, 4, timeoutMs)) return false;

    uint32_t len = header[0]
                 | (static_cast<uint32_t>(header[1]) << 8)
                 | (static_cast<uint32_t>(header[2]) << 16)
                 | (static_cast<uint32_t>(header[3]) << 24);

    if (len == 0 || len > 1024 * 1024) {
        lastError_ = "Invalid message length: " + std::to_string(len);
        return false;
    }

    std::string body(len, '\0');
    if (!readAll(&body[0], len, timeoutMs)) return false;

    out = nlohmann::json::parse(body, nullptr, false);
    return !out.is_discarded();
}

void PipeClient::close() {
    if (pipe_ != INVALID_HANDLE_VALUE) {
        CloseHandle(pipe_);
        pipe_ = INVALID_HANDLE_VALUE;
        connected_ = false;
    }
}

} // namespace ResolutePulse

