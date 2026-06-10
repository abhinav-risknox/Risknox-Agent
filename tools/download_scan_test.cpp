// Standalone test: watches USN Journal for Downloads changes and MotW
// Compile: cl /EHsc /std:c++17 download_scan_test.cpp /link ntdll.lib
// Run as Administrator for USN Journal access

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <winioctl.h>
#include <shlobj.h>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <thread>
#include <atomic>
#include <chrono>
#include <ctime>

// ---- helpers ----------------------------------------------------------------

static std::string wideToUtf8(const std::wstring& wide) {
    if (wide.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(n - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, &s[0], n, nullptr, nullptr);
    return s;
}

static std::string timestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    char buf[32];
    struct tm tm;
    localtime_s(&tm, &t);
    strftime(buf, sizeof(buf), "%H:%M:%S", &tm);
    return buf;
}

static std::string reasonFlags(DWORD r) {
    std::string s;
    auto add = [&](const char* name, DWORD bit) {
        if (r & bit) { if (!s.empty()) s += '|'; s += name; }
    };
    add("DataOverwrite",       0x00000001);
    add("DataExtend",          0x00000002);
    add("DataTruncation",      0x00000004);
    add("NamedDataOverwrite",  0x00000010);
    add("NamedDataExtend",     0x00000020);
    add("NamedDataTruncation", 0x00000040);
    add("FileCreate",          0x00000100);
    add("FileDelete",          0x00000200);
    add("EaChange",            0x00000400);
    add("SecurityChange",      0x00000800);
    add("RenameOldName",       0x00001000);
    add("RenameNewName",       0x00002000);
    add("IndexableChange",     0x00004000);
    add("BasicInfoChange",     0x00008000);
    add("HardLinkChange",      0x00010000);
    add("StreamChange",        0x00200000);
    add("Close",               0x80000000);
    if (s.empty()) s = "(none)";
    return s;
}

// ---- MotW check -------------------------------------------------------------

struct MotwInfo {
    bool hasMotw = false;
    int  zoneId  = -1;
};

static MotwInfo checkMotw(const std::string& filePath) {
    MotwInfo info;
    std::string adsPath = filePath + ":Zone.Identifier";
    HANDLE h = CreateFileA(adsPath.c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return info;

    char buf[512] = {};
    DWORD read = 0;
    if (ReadFile(h, buf, sizeof(buf) - 1, &read, nullptr) && read > 0) {
        std::string content(buf, read);
        auto pos = content.find("ZoneId=");
        if (pos != std::string::npos) {
            info.hasMotw = true;
            info.zoneId  = std::stoi(content.substr(pos + 7));
        }
    }
    CloseHandle(h);
    return info;
}

// ---- USN Journal watch ------------------------------------------------------

static std::unordered_map<uint64_t, std::string> pathCache;

static std::string resolvePath(HANDLE vol, uint64_t fileRef) {
    auto it = pathCache.find(fileRef);
    if (it != pathCache.end()) return it->second;

    FILE_ID_DESCRIPTOR fid;
    fid.dwSize = sizeof(fid);
    fid.Type   = FileIdType;
    fid.FileId.QuadPart = static_cast<LONGLONG>(fileRef);

    HANDLE hf = OpenFileById(vol, &fid, 0,
                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                             nullptr, FILE_FLAG_BACKUP_SEMANTICS);
    if (hf == INVALID_HANDLE_VALUE) return "";

    wchar_t buf[MAX_PATH * 2];
    DWORD len = GetFinalPathNameByHandleW(hf, buf, MAX_PATH * 2, FILE_NAME_NORMALIZED);
    CloseHandle(hf);
    if (!len || len >= MAX_PATH * 2) return "";

    std::wstring wp(buf, len);
    if (wp.compare(0, 4, L"\\\\?\\") == 0) wp = wp.substr(4);
    std::string result = wideToUtf8(wp);

    if (pathCache.size() >= 10000) pathCache.clear();
    pathCache[fileRef] = result;
    return result;
}

// Extensions to watch (mirrors config)
static const std::vector<std::string> WATCH_EXT = {
    ".exe", ".dll", ".msi", ".bat", ".cmd", ".ps1",
    ".vbs", ".js",  ".jar", ".zip", ".rar", ".7z"
};

static bool extensionMatches(const std::string& lpath) {
    for (const auto& ext : WATCH_EXT) {
        if (lpath.size() >= ext.size() &&
            lpath.compare(lpath.size() - ext.size(), ext.size(), ext) == 0)
            return true;
    }
    return false;
}

// ---- main -------------------------------------------------------------------

int main() {
    std::cout << "=== Download Scan Test Tool ===\n";
    std::cout << "Watching USN Journal on C: for Downloads changes.\n";
    std::cout << "Download a file via your browser and watch for events.\n";
    std::cout << "Press Ctrl+C to stop.\n\n";

    // Discover all Downloads folders dynamically
    std::vector<std::string> downloadsDirs;
    std::filesystem::path usersRoot = "C:\\Users";
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(usersRoot, ec)) {
        if (!entry.is_directory(ec)) continue;
        std::string name = entry.path().filename().string();
        if (name == "Public" || name == "Default" ||
            name == "Default User" || name == "All Users") continue;
        std::filesystem::path dl = entry.path() / "Downloads";
        if (std::filesystem::exists(dl, ec)) {
            downloadsDirs.push_back(dl.string());
            std::cout << "[INFO] Watching: " << dl << "\n";
        }
    }
    std::cout << "\n";

    // Open volume
    HANDLE vol = CreateFileW(L"\\\\.\\C:",
                             GENERIC_READ,
                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                             nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (vol == INVALID_HANDLE_VALUE) {
        std::cerr << "[ERROR] Cannot open C: volume. Run as Administrator.\n";
        return 1;
    }

    // Query USN Journal
    USN_JOURNAL_DATA jd;
    DWORD br;
    if (!DeviceIoControl(vol, FSCTL_QUERY_USN_JOURNAL, nullptr, 0,
                         &jd, sizeof(jd), &br, nullptr)) {
        std::cerr << "[ERROR] FSCTL_QUERY_USN_JOURNAL failed: " << GetLastError() << "\n";
        CloseHandle(vol);
        return 1;
    }

    uint64_t lastUsn = jd.NextUsn;
    uint64_t journalId = jd.UsnJournalID;
    std::cout << "[INFO] USN Journal ready. NextUSN=" << lastUsn << "\n\n";

    constexpr size_t BUF = 65536;
    std::vector<uint8_t> buf(BUF);

    // ALL reasons so we see everything coming from Downloads
    struct READ_USN_JOURNAL_DATA_V0 {
        USN     StartUsn;
        DWORD   ReasonMask;
        DWORD   ReturnOnlyOnClose;
        DWORDLONG Timeout;
        DWORDLONG BytesToWaitFor;
        DWORDLONG UsnJournalID;
    } rd = {};
    rd.ReasonMask      = 0xFFFFFFFF; // capture everything
    rd.UsnJournalID    = journalId;

    while (true) {
        rd.StartUsn = lastUsn;
        DWORD returned = 0;
        BOOL ok = DeviceIoControl(vol, FSCTL_READ_USN_JOURNAL,
                                  &rd, sizeof(rd),
                                  buf.data(), (DWORD)BUF,
                                  &returned, nullptr);
        if (!ok) {
            DWORD err = GetLastError();
            if (err == ERROR_HANDLE_EOF || err == ERROR_NO_MORE_ITEMS) {
                Sleep(500);
                continue;
            }
            std::cerr << "[ERROR] FSCTL_READ_USN_JOURNAL: " << err << "\n";
            break;
        }
        if (returned < sizeof(USN)) { Sleep(500); continue; }

        USN nextUsn = *reinterpret_cast<USN*>(buf.data());
        PUSN_RECORD rec = reinterpret_cast<PUSN_RECORD>(buf.data() + sizeof(USN));

        while (reinterpret_cast<uint8_t*>(rec) < buf.data() + returned) {
            bool isDir = (rec->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

            // Resolve full path
            std::string path = resolvePath(vol, rec->FileReferenceNumber);

            // Check if in any Downloads folder
            std::string lpath = path;
            std::transform(lpath.begin(), lpath.end(), lpath.begin(), ::tolower);

            bool inDownloads = false;
            for (const auto& dl : downloadsDirs) {
                std::string ldl = dl;
                std::transform(ldl.begin(), ldl.end(), ldl.begin(), ::tolower);
                if (lpath.compare(0, ldl.size(), ldl) == 0) {
                    inDownloads = true;
                    break;
                }
            }

            // Also catch by substring for safety
            if (!inDownloads && lpath.find("\\downloads\\") != std::string::npos)
                inDownloads = true;

            if (inDownloads && !isDir) {
                DWORD reason = rec->Reason;
                std::cout << "[" << timestamp() << "] USN EVENT\n";
                std::cout << "  Path    : " << (path.empty() ? "(unresolved)" : path) << "\n";
                std::cout << "  Reason  : 0x" << std::hex << reason << std::dec
                          << "  " << reasonFlags(reason) << "\n";
                std::cout << "  isCreate: " << ((reason & 0x100) ? "YES" : "no")
                          << "  isModify: " << ((reason & 0x7F) ? "YES" : "no")
                          << "  isRename: " << ((reason & 0x3000) ? "YES" : "no")
                          << "  isDelete: " << ((reason & 0x200) ? "YES" : "no") << "\n";

                // Extension check
                bool extOk = extensionMatches(lpath);
                std::cout << "  ExtMatch: " << (extOk ? "YES" : "no (skipped by agent)") << "\n";

                // FimMonitor branch simulation
                bool wouldfireCreated  = (reason & 0x100);               // FileCreate
                bool wouldfireModified = (reason & 0x7F) || (reason & 0x800); // isModify/Security
                bool wouldfireRename   = (reason & 0x3000);              // UNHANDLED in FimMonitor!
                std::cout << "  FimMonitor: ";
                if (wouldfireCreated)       std::cout << "-> Created callback\n";
                else if (wouldfireModified) std::cout << "-> Modified callback\n";
                else if (wouldfireRename)   std::cout << "-> *** RENAME - NOT HANDLED (callback never fires!) ***\n";
                else                        std::cout << "-> (no callback - delete or unhandled)\n";

                // MotW check
                if (extOk) {
                    auto motw = checkMotw(path);
                    if (motw.hasMotw)
                        std::cout << "  MotW    : YES (ZoneId=" << motw.zoneId << ")\n";
                    else
                        std::cout << "  MotW    : no Zone.Identifier found yet\n";
                }
                std::cout << "\n";
            }

            rec = reinterpret_cast<PUSN_RECORD>(
                reinterpret_cast<uint8_t*>(rec) + rec->RecordLength);
        }

        lastUsn = nextUsn;
        Sleep(200);
    }

    CloseHandle(vol);
    return 0;
}
