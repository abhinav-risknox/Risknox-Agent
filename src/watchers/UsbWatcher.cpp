#include "utils/Logger.h"

#include <windows.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

static fs::path getLogPath() {
    const char* pd = std::getenv("ProgramData");
    fs::path base = pd ? fs::path(pd) : fs::path("C:\\ProgramData");
    return base / "YourAgent" / "Logs" / "usbwatcher.log";
}

static void ensureDir(const fs::path& p) {
    std::error_code ec;
    fs::create_directories(p, ec);
}

static void writeLog(const std::string& message, const std::string& level = "INFO") {
    auto logPath = getLogPath();
    ensureDir(logPath.parent_path());
    std::ofstream ofs(logPath, std::ios::app);
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    char ts[32] = {};
    std::tm tm{};
    gmtime_s(&tm, &now);
    std::strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm);
    std::string entry = "[" + std::string(ts) + "][" + level + "] " + message;
    if (ofs) ofs << entry << "\n";
    std::cout << entry << std::endl;
    LOG_INFO("{}", entry);
}

struct UsbDriveInfo {
    std::wstring driveLetter;
    std::wstring volumeName;
    std::wstring fileSystem;
    std::wstring interfaceType;
    double sizeGB = 0.0;
    double freeSpaceGB = 0.0;
};

static std::map<std::wstring, UsbDriveInfo> enumerateUsbDrives() {
    std::map<std::wstring, UsbDriveInfo> drives;
    DWORD mask = GetLogicalDrives();
    for (int i = 0; i < 26; ++i) {
        if (!(mask & (1u << i))) continue;
        wchar_t root[] = { wchar_t(L'A' + i), L':', L'\\', L'\0' };
        if (GetDriveTypeW(root) != DRIVE_REMOVABLE) continue;

        wchar_t volName[MAX_PATH] = {};
        wchar_t fsName[MAX_PATH] = {};
        DWORD serial = 0, maxComp = 0, flags = 0;
        ULARGE_INTEGER freeBytes{}, totalBytes{}, totalFree{};
        GetVolumeInformationW(root, volName, MAX_PATH, &serial, &maxComp, &flags, fsName, MAX_PATH);
        GetDiskFreeSpaceExW(root, &freeBytes, &totalBytes, &totalFree);

        UsbDriveInfo info;
        info.driveLetter = std::wstring(root).substr(0, 2);
        info.volumeName = volName;
        info.fileSystem = fsName;
        info.interfaceType = L"USB";
        info.sizeGB = totalBytes.QuadPart / 1024.0 / 1024.0 / 1024.0;
        info.freeSpaceGB = totalFree.QuadPart / 1024.0 / 1024.0 / 1024.0;
        drives[info.driveLetter] = info;
    }
    return drives;
}

static void logDrive(const UsbDriveInfo& d) {
    std::wstring msg = L"USB Connected -> ";
    msg += L"{\"DriveLetter\":\"" + d.driveLetter + L"\",";
    msg += L"\"VolumeName\":\"" + d.volumeName + L"\",";
    msg += L"\"FileSystem\":\"" + d.fileSystem + L"\",";
    msg += L"\"SizeGB\":" + std::to_wstring(d.sizeGB) + L",";
    msg += L"\"FreeSpaceGB\":" + std::to_wstring(d.freeSpaceGB) + L",";
    msg += L"\"InterfaceType\":\"" + d.interfaceType + L"\",";
    msg += L"\"PNPDeviceID\":\"\"}";
    writeLog(std::string(msg.begin(), msg.end()));
}

static void scanFilesOnDrive(const fs::path& root) {
    for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied);
         it != fs::recursive_directory_iterator(); ++it) {
        if (!it->is_regular_file()) continue;
        try {
            auto sz = fs::file_size(it->path());
            auto ext = it->path().extension().wstring();
            std::wstring msg = L"USB File -> {\"FileName\":\"" + it->path().filename().wstring() + L"\",";
            msg += L"\"FullPath\":\"" + it->path().wstring() + L"\",";
            msg += L"\"Extension\":\"" + ext + L"\",";
            msg += L"\"SizeKB\":" + std::to_wstring((double)sz / 1024.0) + L"}";
            writeLog(std::string(msg.begin(), msg.end()));
        } catch (...) {
            writeLog("Failed processing file: " + it->path().string(), "WARN");
        }
    }
}

int main() {
    writeLog("USB Watcher Started");

    auto knownDrives = enumerateUsbDrives();
    for (;;) {
        try {
            auto current = enumerateUsbDrives();

            for (const auto& [driveLetter, info] : current) {
                if (knownDrives.find(driveLetter) == knownDrives.end()) {
                    knownDrives[driveLetter] = info;
                    writeLog("New USB Detected: " + std::string(driveLetter.begin(), driveLetter.end()));

                    std::this_thread::sleep_for(std::chrono::seconds(2));

                    logDrive(info);

                    scanFilesOnDrive(fs::path(driveLetter + L"\\"));
                    writeLog("USB Scan Completed -> " + std::string(driveLetter.begin(), driveLetter.end()));
                }
            }

            for (auto it = knownDrives.begin(); it != knownDrives.end();) {
                if (current.find(it->first) == current.end()) {
                    writeLog("USB Removed: " + std::string(it->first.begin(), it->first.end()));
                    it = knownDrives.erase(it);
                } else {
                    ++it;
                }
            }
        } catch (const std::exception& e) {
            writeLog(std::string("Main loop error: ") + e.what(), "WARN");
        } catch (...) {
            writeLog("Main loop error: unknown exception", "WARN");
        }

        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
}
