#pragma once

#include <cstdint>
#include <string>
#include <nlohmann/json.hpp>

namespace ResolutePulse {

// ─────────────────────────────────────────────────────────────
// Message Types
// ─────────────────────────────────────────────────────────────
enum class MessageType : uint8_t {
    REGISTER_REQUEST   = 0x01,
    REGISTER_ACCEPT    = 0x02,
    REGISTER_REJECT    = 0x03,
    HEARTBEAT          = 0x10,
    HEARTBEAT_ACK      = 0x11,
    EVENT_BATCH        = 0x20,
    COMMAND            = 0x30,
    COMMAND_RESULT     = 0x31
};

// ─────────────────────────────────────────────────────────────
// Message Header — prepended to every message on the wire
// ─────────────────────────────────────────────────────────────
struct MessageHeader {
    uint8_t  magic[4] = {'R', 'P', 'L', 'S'};  // "RPLS" magic bytes
    uint8_t  version  = 1;
    uint8_t  type     = 0;                       // MessageType cast to uint8_t
    uint16_t reserved = 0;
    uint32_t payloadLength = 0;                  // Length of JSON payload following this header
};

static constexpr size_t MESSAGE_HEADER_SIZE = 12;  // 4 + 1 + 1 + 2 + 4

// ─────────────────────────────────────────────────────────────
// Registration Messages
// ─────────────────────────────────────────────────────────────

struct RegisterRequest {
    std::string agentId;
    std::string hostname;
    std::string osType;
    std::string osVersion;
    std::string agentVersion;
    std::string publicKeyPem;   // Agent's ECC public key in PEM format

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(RegisterRequest,
        agentId, hostname, osType, osVersion, agentVersion, publicKeyPem)
};

struct RegisterAccept {
    std::string status;          // "authorized"
    std::string agentId;
    std::string certificatePem;  // Signed agent certificate
    std::string caCertPem;       // CA certificate for verification
    std::string expiresAt;       // ISO 8601 date string
    bool        trial = false;   // True if on trial (no license)

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(RegisterAccept,
        status, agentId, certificatePem, caCertPem, expiresAt, trial)
};

struct RegisterReject {
    std::string status;          // "rejected"
    std::string agentId;
    std::string reason;          // Human-readable rejection reason
    int         errorCode = 0;   // Machine-readable error code

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(RegisterReject,
        status, agentId, reason, errorCode)
};

// ─────────────────────────────────────────────────────────────
// Heartbeat Messages
// ─────────────────────────────────────────────────────────────

struct Heartbeat {
    std::string agentId;
    std::string timestamp;       // ISO 8601
    uint64_t    eventsCollected = 0;
    uint64_t    eventsSent = 0;
    double      cpuUsage = 0.0;
    uint64_t    memoryUsageMb = 0;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(Heartbeat,
        agentId, timestamp, eventsCollected, eventsSent, cpuUsage, memoryUsageMb)
};

struct HeartbeatAck {
    std::string agentId;
    std::string timestamp;
    bool        configChanged = false;  // If true, agent should re-fetch config

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(HeartbeatAck,
        agentId, timestamp, configChanged)
};

// ─────────────────────────────────────────────────────────────
// Command Messages
// ─────────────────────────────────────────────────────────────

struct Command {
    std::string commandId;       // Unique command identifier
    std::string agentId;         // Target agent
    std::string type;            // Command type: "restart", "update_config", etc.
    std::string payload;         // JSON payload specific to command type
    std::string timestamp;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(Command,
        commandId, agentId, type, payload, timestamp)
};

struct CommandResult {
    std::string commandId;
    std::string agentId;
    std::string status;          // "success", "failed", "in_progress"
    std::string output;          // Command output or error message
    std::string timestamp;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(CommandResult,
        commandId, agentId, status, output, timestamp)
};

// ─────────────────────────────────────────────────────────────
// Helper: Serialize / Deserialize MessageHeader
// ─────────────────────────────────────────────────────────────

inline void serializeHeader(const MessageHeader& header, uint8_t* buffer) {
    buffer[0] = header.magic[0];
    buffer[1] = header.magic[1];
    buffer[2] = header.magic[2];
    buffer[3] = header.magic[3];
    buffer[4] = header.version;
    buffer[5] = header.type;
    // reserved in network byte order (big-endian)
    buffer[6] = static_cast<uint8_t>((header.reserved >> 8) & 0xFF);
    buffer[7] = static_cast<uint8_t>(header.reserved & 0xFF);
    // payload length in network byte order
    buffer[8]  = static_cast<uint8_t>((header.payloadLength >> 24) & 0xFF);
    buffer[9]  = static_cast<uint8_t>((header.payloadLength >> 16) & 0xFF);
    buffer[10] = static_cast<uint8_t>((header.payloadLength >> 8) & 0xFF);
    buffer[11] = static_cast<uint8_t>(header.payloadLength & 0xFF);
}

inline bool deserializeHeader(const uint8_t* buffer, MessageHeader& header) {
    // Validate magic
    if (buffer[0] != 'R' || buffer[1] != 'P' || buffer[2] != 'L' || buffer[3] != 'S') {
        return false;
    }
    header.magic[0] = buffer[0];
    header.magic[1] = buffer[1];
    header.magic[2] = buffer[2];
    header.magic[3] = buffer[3];
    header.version = buffer[4];
    header.type    = buffer[5];
    header.reserved = static_cast<uint16_t>((buffer[6] << 8) | buffer[7]);
    header.payloadLength = static_cast<uint32_t>(
        (buffer[8] << 24) | (buffer[9] << 16) | (buffer[10] << 8) | buffer[11]);
    return true;
}

// ─────────────────────────────────────────────────────────────
// Helper: Build a complete message (header + JSON payload)
// ─────────────────────────────────────────────────────────────

template<typename T>
inline std::string buildMessage(MessageType type, const T& payload) {
    nlohmann::json j = payload;
    std::string jsonStr = j.dump();

    MessageHeader header;
    header.type = static_cast<uint8_t>(type);
    header.payloadLength = static_cast<uint32_t>(jsonStr.size());

    std::string message(MESSAGE_HEADER_SIZE + jsonStr.size(), '\0');
    serializeHeader(header, reinterpret_cast<uint8_t*>(&message[0]));
    std::copy(jsonStr.begin(), jsonStr.end(), message.begin() + MESSAGE_HEADER_SIZE);

    return message;
}

} // namespace ResolutePulse
