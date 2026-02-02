#include "network/BinaryProtocol.h"
#include <cstring>
#include <stdexcept>

#ifdef _WIN32
    #include <winsock2.h>
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <arpa/inet.h>
#endif

namespace ResolutePulse {

std::vector<uint8_t> BinaryMessage::serialize() const {
    std::vector<uint8_t> buffer;
    
    // Calculate total length (agent ID + XML)
    uint32_t payloadLength = 32 + static_cast<uint32_t>(xmlData.size());
    
    // Reserve space for entire message
    buffer.reserve(4 + payloadLength);
    
    // 1. Write length prefix (4 bytes, big-endian/network byte order)
    uint32_t netLength = htonl(payloadLength);
    const uint8_t* lengthBytes = reinterpret_cast<const uint8_t*>(&netLength);
    buffer.insert(buffer.end(), lengthBytes, lengthBytes + 4);
    
    // 2. Write agent ID (32 bytes, null-padded)
    uint8_t agentIdBytes[32] = {0}; // Initialize to zeros
    size_t copyLen = (agentId.size() < 32) ? agentId.size() : 32;
    std::memcpy(agentIdBytes, agentId.c_str(), copyLen);
    buffer.insert(buffer.end(), agentIdBytes, agentIdBytes + 32);
    
    // 3. Write XML data
    const uint8_t* xmlBytes = reinterpret_cast<const uint8_t*>(xmlData.c_str());
    buffer.insert(buffer.end(), xmlBytes, xmlBytes + xmlData.size());
    
    return buffer;
}

BinaryMessage BinaryMessage::deserialize(const uint8_t* data, size_t length) {
    // Minimum valid message: 4 (length) + 32 (agent ID) = 36 bytes
    if (length < 36) {
        throw std::runtime_error("Invalid binary message: too short (minimum 36 bytes)");
    }
    
    BinaryMessage msg;
    
    // 1. Read length prefix (4 bytes, big-endian)
    uint32_t netLength;
    std::memcpy(&netLength, data, 4);
    uint32_t payloadLength = ntohl(netLength);
    
    // Validate length matches actual data
    if (length != 4 + payloadLength) {
        throw std::runtime_error("Invalid binary message: length mismatch");
    }
    
    // 2. Read agent ID (32 bytes, extract until null terminator)
    char agentIdBuffer[33] = {0}; // +1 for null terminator
    std::memcpy(agentIdBuffer, data + 4, 32);
    msg.agentId = std::string(agentIdBuffer); // Stops at first null
    
    // 3. Read XML data (remaining bytes)
    size_t xmlLength = payloadLength - 32;
    if (xmlLength > 0) {
        const char* xmlStart = reinterpret_cast<const char*>(data + 36);
        msg.xmlData = std::string(xmlStart, xmlLength);
    }
    
    return msg;
}

} // namespace ResolutePulse
