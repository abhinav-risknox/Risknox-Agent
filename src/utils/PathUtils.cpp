#include "PathUtils.h"
#include <windows.h>
#include <shlobj.h>
#include <stdexcept>

namespace ResolutePulse {

std::filesystem::path PathUtils::getAgentDataDir() {
    wchar_t* path = nullptr;
    HRESULT hr = SHGetKnownFolderPath(FOLDERID_ProgramData, 0, nullptr, &path);
    if (SUCCEEDED(hr)) {
        std::filesystem::path p(path);
        CoTaskMemFree(path);
        return p / "Risknox Pulse";
    }
    
    // Fallback if API fails
    const char* programData = std::getenv("ProgramData");
    return std::filesystem::path(programData ? programData : "C:\\ProgramData") / "Risknox Pulse";
}

std::filesystem::path PathUtils::getHostsFilePath() {
    wchar_t path[MAX_PATH];
    if (GetSystemDirectoryW(path, MAX_PATH)) {
        return std::filesystem::path(path) / "drivers" / "etc" / "hosts";
    }
    return "C:\\Windows\\System32\\drivers\\etc\\hosts";
}

std::filesystem::path PathUtils::getProgramFilesPath() {
    wchar_t* path = nullptr;
    HRESULT hr = SHGetKnownFolderPath(FOLDERID_ProgramFiles, 0, nullptr, &path);
    if (SUCCEEDED(hr)) {
        std::filesystem::path p(path);
        CoTaskMemFree(path);
        return p;
    }
    
    const char* pf = std::getenv("ProgramFiles");
    return std::filesystem::path(pf ? pf : "C:\\Program Files");
}

std::filesystem::path PathUtils::getExecutableDir() {
    wchar_t buffer[MAX_PATH];
    GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    return std::filesystem::path(buffer).parent_path();
}

void PathUtils::ensureDataDirExists() {
    std::filesystem::create_directories(getAgentDataDir());
}

} // namespace ResolutePulse
