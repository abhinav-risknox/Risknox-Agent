#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace ResolutePulse {

class FileHasher {
public:
    // Compute SHA256 hash of a file, returns hex string
    // Returns empty string on error
    static std::string hashFile(const std::string& filePath);
    
    // Compute SHA256 hash of data buffer
    static std::string hashData(const uint8_t* data, size_t size);
    
    // Convert hash bytes to hex string
    static std::string bytesToHex(const std::vector<uint8_t>& bytes);
};

} // namespace ResolutePulse
