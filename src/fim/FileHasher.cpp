#include "FileHasher.h"
#include "utils/Logger.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <bcrypt.h>
#include <fstream>
#include <sstream>
#include <iomanip>

#pragma comment(lib, "bcrypt.lib")

namespace ResolutePulse {

std::string FileHasher::hashFile(const std::string& filePath) {
    // Open file
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        LOG_DEBUG("Cannot open file for hashing: {}", filePath);
        return "";
    }
    
    // Get file size
    file.seekg(0, std::ios::end);
    auto fileSize = file.tellg();
    file.seekg(0, std::ios::beg);
    
    // Skip very large files (configurable)
    constexpr size_t MAX_HASH_SIZE = 100 * 1024 * 1024; // 100 MB
    if (fileSize > MAX_HASH_SIZE) {
        LOG_DEBUG("File too large to hash: {} ({} bytes)", filePath, static_cast<size_t>(fileSize));
        return "";
    }
    
    // Initialize BCrypt
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_HASH_HANDLE hHash = nullptr;
    NTSTATUS status;
    
    status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    if (!BCRYPT_SUCCESS(status)) {
        LOG_ERROR("BCryptOpenAlgorithmProvider failed: {}", status);
        return "";
    }
    
    // Get hash object size
    DWORD hashObjectSize = 0;
    DWORD dataSize = 0;
    status = BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, 
                                reinterpret_cast<PUCHAR>(&hashObjectSize),
                                sizeof(DWORD), &dataSize, 0);
    if (!BCRYPT_SUCCESS(status)) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return "";
    }
    
    // Get hash length
    DWORD hashLength = 0;
    status = BCryptGetProperty(hAlg, BCRYPT_HASH_LENGTH,
                                reinterpret_cast<PUCHAR>(&hashLength),
                                sizeof(DWORD), &dataSize, 0);
    if (!BCRYPT_SUCCESS(status)) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return "";
    }
    
    // Allocate hash object
    std::vector<uint8_t> hashObject(hashObjectSize);
    std::vector<uint8_t> hashValue(hashLength);
    
    status = BCryptCreateHash(hAlg, &hHash, hashObject.data(), hashObjectSize, 
                               nullptr, 0, 0);
    if (!BCRYPT_SUCCESS(status)) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return "";
    }
    
    // Read and hash file in chunks
    constexpr size_t BUFFER_SIZE = 64 * 1024; // 64 KB chunks
    std::vector<char> buffer(BUFFER_SIZE);
    
    while (file.good()) {
        file.read(buffer.data(), BUFFER_SIZE);
        auto bytesRead = file.gcount();
        if (bytesRead > 0) {
            status = BCryptHashData(hHash, reinterpret_cast<PUCHAR>(buffer.data()),
                                    static_cast<ULONG>(bytesRead), 0);
            if (!BCRYPT_SUCCESS(status)) {
                BCryptDestroyHash(hHash);
                BCryptCloseAlgorithmProvider(hAlg, 0);
                return "";
            }
        }
    }
    
    // Finalize hash
    status = BCryptFinishHash(hHash, hashValue.data(), hashLength, 0);
    
    BCryptDestroyHash(hHash);
    BCryptCloseAlgorithmProvider(hAlg, 0);
    
    if (!BCRYPT_SUCCESS(status)) {
        return "";
    }
    
    return bytesToHex(hashValue);
}

std::string FileHasher::hashData(const uint8_t* data, size_t size) {
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_HASH_HANDLE hHash = nullptr;
    
    NTSTATUS status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    if (!BCRYPT_SUCCESS(status)) return "";
    
    DWORD hashObjectSize = 0;
    DWORD hashLength = 0;
    DWORD dataSize = 0;
    
    BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&hashObjectSize),
                      sizeof(DWORD), &dataSize, 0);
    BCryptGetProperty(hAlg, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hashLength),
                      sizeof(DWORD), &dataSize, 0);
    
    std::vector<uint8_t> hashObject(hashObjectSize);
    std::vector<uint8_t> hashValue(hashLength);
    
    status = BCryptCreateHash(hAlg, &hHash, hashObject.data(), hashObjectSize, nullptr, 0, 0);
    if (!BCRYPT_SUCCESS(status)) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return "";
    }
    
    BCryptHashData(hHash, const_cast<PUCHAR>(data), static_cast<ULONG>(size), 0);
    BCryptFinishHash(hHash, hashValue.data(), hashLength, 0);
    
    BCryptDestroyHash(hHash);
    BCryptCloseAlgorithmProvider(hAlg, 0);
    
    return bytesToHex(hashValue);
}

std::string FileHasher::bytesToHex(const std::vector<uint8_t>& bytes) {
    std::ostringstream ss;
    ss << std::hex << std::setfill('0');
    for (uint8_t byte : bytes) {
        ss << std::setw(2) << static_cast<int>(byte);
    }
    return ss.str();
}

} // namespace ResolutePulse
