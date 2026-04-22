#pragma once

// PipeChannel.h - Windows Named Pipe IPC between the core agent and worker processes.
//
// Wire format: [4-byte LE uint32 length][UTF-8 JSON body]
//
// PipeServer: used inside worker executables (rp-softblock.exe, etc.)
// PipeClient: used inside the core agent to talk to workers

#include <string>
#include <nlohmann/json.hpp>

#ifdef _WIN32
#include <windows.h>
#endif

namespace ResolutePulse {

// ─────────────────────────────────────────────────────────────
// PipeServer - server side (inside each worker process)
// ─────────────────────────────────────────────────────────────
class PipeServer {
public:
    PipeServer() = default;
    ~PipeServer();

    // Open a named pipe and wait for a client to connect.
    // Blocks until client connects or an error occurs.
    bool listen(const std::string& pipeName);

    // Is the pipe connected to a client?
    bool isConnected() const;

    // Send a JSON message to the client
    bool sendJson(const nlohmann::json& msg);

    // Receive a JSON message from the client (blocks until data or error)
    bool recvJson(nlohmann::json& out, DWORD timeoutMs = INFINITE);

    // Disconnect and close pipe handle
    void close();

    const std::string& getLastError() const { return lastError_; }

private:
    bool writeAll(const void* data, DWORD len);
    bool readAll(void* data, DWORD len, DWORD timeoutMs);

    HANDLE      pipe_       = INVALID_HANDLE_VALUE;
    bool        connected_  = false;
    std::string lastError_;
};

// ─────────────────────────────────────────────────────────────
// PipeClient - client side (inside the core agent)
// ─────────────────────────────────────────────────────────────
class PipeClient {
public:
    PipeClient() = default;
    ~PipeClient();

    // Connect to a named pipe server. Retries for up to timeoutMs milliseconds.
    bool connect(const std::string& pipeName, DWORD timeoutMs = 3000);

    bool isConnected() const;

    // Send a JSON message to the worker
    bool sendJson(const nlohmann::json& msg);

    // Receive response JSON from the worker
    bool recvJson(nlohmann::json& out, DWORD timeoutMs = 30000);

    void close();

    const std::string& getLastError() const { return lastError_; }

private:
    bool writeAll(const void* data, DWORD len);
    bool readAll(void* data, DWORD len, DWORD timeoutMs);

    HANDLE      pipe_       = INVALID_HANDLE_VALUE;
    bool        connected_  = false;
    std::string lastError_;
};

} // namespace ResolutePulse

