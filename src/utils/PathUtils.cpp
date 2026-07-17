#include "PathUtils.h"
#include <windows.h>
#include <shlobj.h>
#include <stdexcept>

namespace ResolutePulse {

// ─────────────────────────────────────────────────────────────────────────────
std::filesystem::path PathUtils::getAgentDataDir()
{
    wchar_t* path = nullptr;
    HRESULT hr = SHGetKnownFolderPath(FOLDERID_ProgramData, 0, nullptr, &path);
    if (SUCCEEDED(hr)) {
        std::filesystem::path p(path);
        CoTaskMemFree(path);
        return p / "Risknox Pulse";
    }

    // Fallback: read the wide env var — getenv() returns narrow and can
    // silently truncate paths containing non-ASCII characters.
    wchar_t buf[MAX_PATH] = {};
    DWORD len = GetEnvironmentVariableW(L"ProgramData", buf, MAX_PATH);
    if (len > 0 && len < MAX_PATH)
        return std::filesystem::path(buf) / "Risknox Pulse";

    return std::filesystem::path(L"C:\\ProgramData") / "Risknox Pulse";
}

// ─────────────────────────────────────────────────────────────────────────────
std::filesystem::path PathUtils::getHostsFilePath()
{
    wchar_t path[MAX_PATH];
    if (GetSystemDirectoryW(path, MAX_PATH))
        return std::filesystem::path(path) / "drivers" / "etc" / "hosts";
    return L"C:\\Windows\\System32\\drivers\\etc\\hosts";
}

// ─────────────────────────────────────────────────────────────────────────────
std::filesystem::path PathUtils::getProgramFilesPath()
{
    wchar_t* path = nullptr;
    HRESULT hr = SHGetKnownFolderPath(FOLDERID_ProgramFiles, 0, nullptr, &path);
    if (SUCCEEDED(hr)) {
        std::filesystem::path p(path);
        CoTaskMemFree(path);
        return p;
    }

    wchar_t buf[MAX_PATH] = {};
    DWORD len = GetEnvironmentVariableW(L"ProgramFiles", buf, MAX_PATH);
    if (len > 0 && len < MAX_PATH)
        return std::filesystem::path(buf);

    return L"C:\\Program Files";
}

// ─────────────────────────────────────────────────────────────────────────────
std::filesystem::path PathUtils::getSystemDirectory()
{
    wchar_t path[MAX_PATH];
    if (GetSystemDirectoryW(path, MAX_PATH))
        return std::filesystem::path(path);

    wchar_t buf[MAX_PATH] = {};
    DWORD len = GetEnvironmentVariableW(L"SystemRoot", buf, MAX_PATH);
    if (len > 0 && len < MAX_PATH)
        return std::filesystem::path(buf) / "System32";

    return L"C:\\Windows\\System32";
}

// ─────────────────────────────────────────────────────────────────────────────
std::filesystem::path PathUtils::getExecutableDir()
{
    // Use a growable buffer to handle paths longer than MAX_PATH.
    DWORD size = MAX_PATH;
    std::wstring buf(size, L'\0');
    while (true) {
        DWORD ret = GetModuleFileNameW(nullptr, buf.data(), size);
        if (ret == 0)
            return {};  // hard failure — return empty path, caller checks
        if (ret < size) {
            buf.resize(ret);
            return std::filesystem::path(buf).parent_path();
        }
        // Buffer was too small — double and retry
        size *= 2;
        buf.resize(size);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
std::filesystem::path PathUtils::getUserDownloadsDir()
{
    // FOLDERID_Downloads resolves correctly even when the user has moved
    // their Downloads folder via Shell folder redirection.
    wchar_t* path = nullptr;
    HRESULT hr = SHGetKnownFolderPath(FOLDERID_Downloads, 0, nullptr, &path);
    if (SUCCEEDED(hr)) {
        std::filesystem::path p(path);
        CoTaskMemFree(path);
        return p;
    }
    return {};
}

// ─────────────────────────────────────────────────────────────────────────────
std::vector<std::filesystem::path> PathUtils::getAllUsersDownloadsDirs()
{
    // Read profile paths from the registry ProfileList rather than iterating
    // C:\Users\*, which misses profiles on custom partition or when the Users
    // dir itself has been relocated.
    std::vector<std::filesystem::path> result;

    constexpr wchar_t kProfileList[] =
        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\ProfileList";

    HKEY hList = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, kProfileList, 0,
                      KEY_READ, &hList) != ERROR_SUCCESS)
        return result;

    wchar_t sidBuf[128];
    for (DWORD idx = 0; ; ++idx) {
        DWORD sidLen = static_cast<DWORD>(std::size(sidBuf));
        LONG rc = RegEnumKeyExW(hList, idx, sidBuf, &sidLen,
                                nullptr, nullptr, nullptr, nullptr);
        if (rc == ERROR_NO_MORE_ITEMS) break;
        if (rc != ERROR_SUCCESS)       continue;

        // Skip machine/service SIDs — user SIDs start with S-1-5-21
        std::wstring sid(sidBuf);
        if (sid.find(L"S-1-5-21") == std::wstring::npos) continue;

        // Open the per-profile key to read ProfileImagePath
        HKEY hProf = nullptr;
        std::wstring profKey = std::wstring(kProfileList) + L"\\" + sid;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, profKey.c_str(), 0,
                          KEY_READ, &hProf) != ERROR_SUCCESS)
            continue;

        wchar_t pathBuf[MAX_PATH * 2] = {};
        DWORD pathLen = sizeof(pathBuf);
        DWORD type    = 0;
        rc = RegQueryValueExW(hProf, L"ProfileImagePath", nullptr,
                              &type, reinterpret_cast<LPBYTE>(pathBuf), &pathLen);
        RegCloseKey(hProf);

        if (rc != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ))
            continue;

        // Expand any %SystemDrive% etc. in the path
        wchar_t expanded[MAX_PATH * 2] = {};
        ExpandEnvironmentStringsW(pathBuf, expanded, static_cast<DWORD>(std::size(expanded)));

        std::filesystem::path dlDir = std::filesystem::path(expanded) / "Downloads";
        std::error_code ec;
        if (std::filesystem::is_directory(dlDir, ec))
            result.push_back(std::move(dlDir));
    }

    RegCloseKey(hList);
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
void PathUtils::ensureDataDirExists()
{
    std::filesystem::create_directories(getAgentDataDir());
}

} // namespace ResolutePulse
