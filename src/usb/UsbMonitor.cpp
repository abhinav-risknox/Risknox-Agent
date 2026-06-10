#include "UsbMonitor.h"
#include "utils/Logger.h"

#include <Windows.h>
#include <string>
#include <thread>
#include <chrono>

namespace ResolutePulse {

bool UsbMonitor::start() {
    if (running_.load()) return true;

    stopRequested_ = false;
    running_       = true;

    monitorThread_ = std::thread(&UsbMonitor::monitorLoop, this);
    LOG_INFO("UsbMonitor: started (poll interval={}ms)", pollIntervalMs_);
    return true;
}

void UsbMonitor::stop() {
    if (!running_.load()) return;

    stopRequested_ = true;
    if (monitorThread_.joinable()) {
        monitorThread_.join();
    }
    running_ = false;
    LOG_INFO("UsbMonitor: stopped");
}

std::set<std::string> UsbMonitor::getRemovableDrives() const {
    std::set<std::string> drives;

    DWORD driveMask = GetLogicalDrives();
    for (int i = 0; i < 26; ++i) {
        if (!(driveMask & (1 << i))) continue;

        char letter = 'A' + i;
        std::string root = std::string(1, letter) + ":\\";

        if (GetDriveTypeA(root.c_str()) == DRIVE_REMOVABLE) {
            drives.insert(std::string(1, letter) + ":");
        }
    }
    return drives;
}

UsbDriveInfo UsbMonitor::getDriveInfo(const std::string& driveLetter) const {
    UsbDriveInfo info;
    info.driveLetter = driveLetter;

    std::string root = driveLetter + "\\";

    // Volume name and filesystem
    char volName[256]  = {};
    char fsName[64]    = {};
    GetVolumeInformationA(root.c_str(), volName, sizeof(volName),
                          nullptr, nullptr, nullptr, fsName, sizeof(fsName));
    info.volumeName = volName;
    info.fileSystem = fsName;

    // Size info
    ULARGE_INTEGER freeBytesAvail, totalBytes, totalFreeBytes;
    if (GetDiskFreeSpaceExA(root.c_str(), &freeBytesAvail, &totalBytes, &totalFreeBytes)) {
        info.totalBytes = totalBytes.QuadPart;
        info.freeBytes  = totalFreeBytes.QuadPart;
    }

    return info;
}

void UsbMonitor::monitorLoop() {
    // Seed known drives on startup so we don't fire arrival for already-connected drives
    std::set<std::string> knownDrives = getRemovableDrives();
    LOG_DEBUG("UsbMonitor: seeded {} removable drive(s)", knownDrives.size());

    while (!stopRequested_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(pollIntervalMs_));

        if (stopRequested_.load()) break;

        std::set<std::string> currentDrives = getRemovableDrives();

        // Detect new arrivals
        for (const auto& drive : currentDrives) {
            if (knownDrives.find(drive) == knownDrives.end()) {
                LOG_INFO("UsbMonitor: USB drive inserted: {}", drive);

                // Small delay so the drive is fully mounted before we scan
                std::this_thread::sleep_for(std::chrono::seconds(2));

                UsbDriveInfo info = getDriveInfo(drive);
                LOG_INFO("UsbMonitor: Drive info - volume='{}' fs='{}' size={}MB",
                         info.volumeName, info.fileSystem,
                         info.totalBytes / (1024 * 1024));

                if (onArrival_) {
                    onArrival_(info);
                }
            }
        }

        // Detect removals
        for (const auto& drive : knownDrives) {
            if (currentDrives.find(drive) == currentDrives.end()) {
                LOG_INFO("UsbMonitor: USB drive removed: {}", drive);
                if (onRemoval_) {
                    onRemoval_(drive);
                }
            }
        }

        knownDrives = currentDrives;
    }

    running_ = false;
}

} // namespace ResolutePulse
