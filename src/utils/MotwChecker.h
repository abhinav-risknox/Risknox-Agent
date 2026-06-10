#pragma once

#include <string>
#include <Windows.h>

namespace ResolutePulse {

struct MotwInfo {
    bool        hasMotw   = false;
    int         zoneId    = -1;   // 3=Internet, 4=Restricted
    std::string hostUrl;          // Where the file came from
    std::string referrerUrl;
};

// Reads the Zone.Identifier NTFS alternate data stream from a file.
// Returns hasMotw=true if ZoneId is 3 (Internet) or 4 (Restricted).
inline MotwInfo checkMotw(const std::string& filePath) {
    MotwInfo result;

    std::string streamPath = filePath + ":Zone.Identifier";

    HANDLE hFile = CreateFileA(
        streamPath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (hFile == INVALID_HANDLE_VALUE) {
        return result; // No Zone.Identifier stream — local file
    }

    // Read the stream content (max 4KB, zone data is tiny)
    char buf[4096] = {};
    DWORD bytesRead = 0;
    ReadFile(hFile, buf, sizeof(buf) - 1, &bytesRead, nullptr);
    CloseHandle(hFile);

    if (bytesRead == 0) return result;

    std::string content(buf, bytesRead);

    // Parse ZoneId=
    auto zonePos = content.find("ZoneId=");
    if (zonePos != std::string::npos) {
        try {
            result.zoneId = std::stoi(content.substr(zonePos + 7));
        } catch (...) {}
    }

    // Parse HostUrl=
    auto hostPos = content.find("HostUrl=");
    if (hostPos != std::string::npos) {
        size_t end = content.find('\n', hostPos);
        std::string url = content.substr(hostPos + 8,
                          end == std::string::npos ? std::string::npos : end - hostPos - 8);
        // Strip \r
        if (!url.empty() && url.back() == '\r') url.pop_back();
        result.hostUrl = url;
    }

    // Parse ReferrerUrl=
    auto refPos = content.find("ReferrerUrl=");
    if (refPos != std::string::npos) {
        size_t end = content.find('\n', refPos);
        std::string url = content.substr(refPos + 12,
                          end == std::string::npos ? std::string::npos : end - refPos - 12);
        if (!url.empty() && url.back() == '\r') url.pop_back();
        result.referrerUrl = url;
    }

    // Zone 3 = Internet, Zone 4 = Restricted Sites
    result.hasMotw = (result.zoneId == 3 || result.zoneId == 4);
    return result;
}

} // namespace ResolutePulse
