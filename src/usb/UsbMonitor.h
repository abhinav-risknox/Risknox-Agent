#pragma once

#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <set>

namespace ResolutePulse {

struct UsbDriveInfo {
    std::string driveLetter;   // e.g. "E:"
    std::string volumeName;
    std::string fileSystem;
    uint64_t    totalBytes = 0;
    uint64_t    freeBytes  = 0;
};

using UsbArrivalCallback = std::function<void(const UsbDriveInfo&)>;
using UsbRemovalCallback = std::function<void(const std::string& driveLetter)>;

class UsbMonitor {
public:
    UsbMonitor() = default;
    ~UsbMonitor() { stop(); }

    void setArrivalCallback(UsbArrivalCallback cb) { onArrival_ = std::move(cb); }
    void setRemovalCallback(UsbRemovalCallback cb) { onRemoval_ = std::move(cb); }
    void setPollIntervalMs(int ms) { pollIntervalMs_ = ms; }

    bool start();
    void stop();

private:
    void monitorLoop();
    std::set<std::string> getRemovableDrives() const;
    UsbDriveInfo getDriveInfo(const std::string& driveLetter) const;

    UsbArrivalCallback onArrival_;
    UsbRemovalCallback onRemoval_;

    int               pollIntervalMs_ = 2000;
    std::atomic<bool> running_{false};
    std::atomic<bool> stopRequested_{false};
    std::thread       monitorThread_;
};

} // namespace ResolutePulse
