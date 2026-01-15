#include "Base64.h"

namespace ResolutePulse {

const char* Base64::getCharTable() {
    static const char table[] = 
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";
    return table;
}

const int* Base64::getDecodeTable() {
    static int decodeTable[256] = {-1};
    static bool initialized = false;
    
    if (!initialized) {
        for (int i = 0; i < 256; ++i) decodeTable[i] = -1;
        const char* table = getCharTable();
        for (int i = 0; i < 64; ++i) {
            decodeTable[static_cast<unsigned char>(table[i])] = i;
        }
        initialized = true;
    }
    return decodeTable;
}

std::string Base64::encode(const std::vector<uint8_t>& data) {
    return encode(data.data(), data.size());
}

std::string Base64::encode(const uint8_t* data, size_t length) {
    const char* table = getCharTable();
    std::string result;
    result.reserve(((length + 2) / 3) * 4);
    
    size_t i = 0;
    while (i < length) {
        uint32_t octet_a = i < length ? data[i++] : 0;
        uint32_t octet_b = i < length ? data[i++] : 0;
        uint32_t octet_c = i < length ? data[i++] : 0;
        
        uint32_t triple = (octet_a << 16) + (octet_b << 8) + octet_c;
        
        result.push_back(table[(triple >> 18) & 0x3F]);
        result.push_back(table[(triple >> 12) & 0x3F]);
        result.push_back(table[(triple >> 6) & 0x3F]);
        result.push_back(table[triple & 0x3F]);
    }
    
    // Add padding
    size_t mod = length % 3;
    if (mod == 1) {
        result[result.size() - 1] = '=';
        result[result.size() - 2] = '=';
    } else if (mod == 2) {
        result[result.size() - 1] = '=';
    }
    
    return result;
}

std::vector<uint8_t> Base64::decode(const std::string& encoded) {
    const int* decodeTable = getDecodeTable();
    std::vector<uint8_t> result;
    
    if (encoded.empty()) return result;
    
    size_t length = encoded.length();
    size_t padding = 0;
    if (encoded[length - 1] == '=') padding++;
    if (length > 1 && encoded[length - 2] == '=') padding++;
    
    result.reserve((length / 4) * 3 - padding);
    
    uint32_t buffer = 0;
    int bitsCollected = 0;
    
    for (char c : encoded) {
        if (c == '=') break;
        
        int value = decodeTable[static_cast<unsigned char>(c)];
        if (value == -1) continue;  // Skip invalid characters
        
        buffer = (buffer << 6) | value;
        bitsCollected += 6;
        
        if (bitsCollected >= 8) {
            bitsCollected -= 8;
            result.push_back(static_cast<uint8_t>((buffer >> bitsCollected) & 0xFF));
        }
    }
    
    return result;
}

} // namespace ResolutePulse
