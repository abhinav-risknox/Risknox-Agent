#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace ResolutePulse {

class Base64 {
public:
    static std::string encode(const std::vector<uint8_t>& data);
    static std::string encode(const uint8_t* data, size_t length);
    static std::vector<uint8_t> decode(const std::string& encoded);
    
private:
    static const char* getCharTable();
    static const int* getDecodeTable();
};

} // namespace ResolutePulse
