#pragma once

#include <string>
#include <functional>
#include <thread>
#include <atomic>

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

/// Event-driven USB monitor using RegisterDeviceNotification.
/// Zero CPU while idle — fires callbacks instantly on insertion/removal.
class UsbMonitor {
public:
    UsbMonitor() = default;
    ~UsbMonitor() { stop(); }

    void setArrivalCallback(UsbArrivalCallback cb) { onArrival_ = std::move(cb); }
    void setRemovalCallback(UsbRemovalCallback cb) { onRemoval_ = std::move(cb); }
    void setScanDelaySeconds(int s)  { scanDelaySeconds_ = s; }

    bool start();
    void stop();

    // Called from WndProc — public so the message handler can reach them
    void handleArrival(unsigned long unitMask);
    void handleRemoval(unsigned long unitMask);

private:
    void messageLoop();

    static char driveLetterFromMask(unsigned long unitMask);
    UsbDriveInfo getDriveInfo(const std::string& driveLetter) const;

    UsbArrivalCallback onArrival_;
    UsbRemovalCallback onRemoval_;

    int               scanDelaySeconds_ = 2;
    unsigned long     threadId_         = 0;
    std::atomic<bool> running_{false};
    std::atomic<bool> stopRequested_{false};
    std::thread       monitorThread_;
};

} // namespace ResolutePulse
