#pragma once

#include <vector>
#include <string>
#include <cstdint>

namespace ResolutePulse {

/**
 * Binary message format for efficient network transmission
 * 
 * Wire format:
 * ┌─────────────────┬──────────────────┬──────────────┐
 * │ Message Length  │  Agent ID        │  Event XML   │
 * │ (4 bytes)       │  (32 bytes)      │  (variable)  │
 * └─────────────────┴──────────────────┴──────────────┘
 * 
 * - Length: 4-byte unsigned int (big-endian), includes agent ID + XML
 * - Agent ID: 32-byte UTF-8 string (null-padded if shorter)
 * - XML: Raw Windows Event XML data
 */
struct BinaryMessage {
    std::string agentId;
    std::string xmlData;
    
    /**
     * Serialize message to binary wire format
     * @return Byte vector ready for network transmission
     */
    std::vector<uint8_t> serialize() const;
    
    /**
     * Deserialize binary data into message
     * @param data Pointer to binary data
     * @param length Length of data in bytes
     * @return Deserialized message
     * @throws std::runtime_error if data is invalid
     */
    static BinaryMessage deserialize(const uint8_t* data, size_t length);
    
    /**
     * Get total serialized size
     * @return Size in bytes (4 + 32 + xmlData.size())
     */
    size_t getSerializedSize() const {
        return 4 + 32 + xmlData.size();
    }
};

} // namespace ResolutePulse
