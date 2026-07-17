#include "UsbMonitor.h"
#include "utils/Logger.h"

#include <Windows.h>
#include <Dbt.h>
#include <string>
#include <thread>
#include <chrono>

namespace ResolutePulse {

// ─────────────────────────────────────────────────────────────────────────────
// Hidden-window WndProc — forwards WM_DEVICECHANGE to the UsbMonitor instance
// ─────────────────────────────────────────────────────────────────────────────

static LRESULT CALLBACK UsbWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_DEVICECHANGE) {
        auto* self = reinterpret_cast<UsbMonitor*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));
        if (!self) return DefWindowProcA(hwnd, msg, wParam, lParam);

        PDEV_BROADCAST_HDR hdr = reinterpret_cast<PDEV_BROADCAST_HDR>(lParam);

        if (wParam == DBT_DEVICEARRIVAL && hdr && hdr->dbch_devicetype == DBT_DEVTYP_VOLUME) {
            auto* vol = reinterpret_cast<PDEV_BROADCAST_VOLUME>(lParam);
            self->handleArrival(vol->dbcv_unitmask);
        }
        else if (wParam == DBT_DEVICEREMOVECOMPLETE && hdr && hdr->dbch_devicetype == DBT_DEVTYP_VOLUME) {
            auto* vol = reinterpret_cast<PDEV_BROADCAST_VOLUME>(lParam);
            self->handleRemoval(vol->dbcv_unitmask);
        }
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

// ─────────────────────────────────────────────────────────────────────────────
// start / stop
// ─────────────────────────────────────────────────────────────────────────────

bool UsbMonitor::start() {
    if (running_.load()) return true;

    stopRequested_ = false;
    running_       = true;

    monitorThread_ = std::thread(&UsbMonitor::messageLoop, this);
    LOG_INFO("UsbMonitor: started (event-driven, scan delay={}s)", scanDelaySeconds_);
    return true;
}

void UsbMonitor::stop() {
    if (!running_.load()) return;

    stopRequested_ = true;

    // Post WM_QUIT to break the message loop
    if (threadId_ != 0) {
        PostThreadMessageA(threadId_, WM_QUIT, 0, 0);
    }

    if (monitorThread_.joinable()) {
        monitorThread_.join();
    }
    running_ = false;
    LOG_INFO("UsbMonitor: stopped");
}

// ─────────────────────────────────────────────────────────────────────────────
// messageLoop — runs in background thread, zero CPU while idle
// ─────────────────────────────────────────────────────────────────────────────

void UsbMonitor::messageLoop() {
    threadId_ = GetCurrentThreadId();

    // Register window class
    const char* className = "RisknoxUsbMonitor";
    WNDCLASSEXA wc = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = UsbWndProc;
    wc.hInstance      = GetModuleHandleA(nullptr);
    wc.lpszClassName = className;
    RegisterClassExA(&wc);

    // Create a hidden window to receive WM_DEVICECHANGE broadcasts.
    // Must NOT be a message-only window (HWND_MESSAGE) — those don't get broadcasts.
    HWND hwnd = CreateWindowExA(
        0, className, "RisknoxUsbWatcher",
        0,  // no style — invisible
        0, 0, 0, 0,
        nullptr,         // NOT HWND_MESSAGE — regular hidden window
        nullptr,
        GetModuleHandleA(nullptr),
        nullptr
    );

    if (!hwnd) {
        LOG_ERROR("UsbMonitor: Failed to create message window: {}", GetLastError());
        running_ = false;
        return;
    }

    // Store 'this' in the window for WndProc to find
    SetWindowLongPtrA(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    LOG_DEBUG("UsbMonitor: message window created, entering event loop");

    // Message pump — blocks on GetMessage (zero CPU), wakes on device events
    MSG msg;
    while (GetMessageA(&msg, nullptr, 0, 0) > 0) {
        if (stopRequested_.load()) break;
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    DestroyWindow(hwnd);
    UnregisterClassA(className, GetModuleHandleA(nullptr));
    running_ = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// handleArrival — called from WndProc on DBT_DEVICEARRIVAL
// ─────────────────────────────────────────────────────────────────────────────

void UsbMonitor::handleArrival(unsigned long unitMask) {
    char letter = driveLetterFromMask(unitMask);
    if (letter == 0) return;

    std::string driveLetter = std::string(1, letter) + ":";
    std::string root = driveLetter + "\\";

    // Only care about removable drives (USB sticks, external HDDs)
    UINT driveType = GetDriveTypeA(root.c_str());
    if (driveType != DRIVE_REMOVABLE && driveType != DRIVE_FIXED) {
        // DRIVE_FIXED catches USB external HDDs which report as fixed
        LOG_DEBUG("UsbMonitor: Ignoring non-removable drive {} (type={})", driveLetter, driveType);
        return;
    }

    LOG_INFO("UsbMonitor: USB drive inserted: {}", driveLetter);

    // Fire the connected callback immediately (before the scan delay) so the
    // UI can show a "Scanning USB..." popup right away.
    if (onConnected_) {
        onConnected_(driveLetter);
    }

    // Detach to a worker thread so we don't block the message pump
    // during the scan delay + callback
    std::thread([this, driveLetter]() {
        // Wait for the drive to be fully mounted
        std::this_thread::sleep_for(std::chrono::seconds(scanDelaySeconds_));

        UsbDriveInfo info = getDriveInfo(driveLetter);
        LOG_INFO("UsbMonitor: Drive info - volume='{}' fs='{}' size={}MB",
                 info.volumeName, info.fileSystem,
                 info.totalBytes / (1024 * 1024));

        if (onArrival_) {
            onArrival_(info);
        }
    }).detach();
}

// ─────────────────────────────────────────────────────────────────────────────
// handleRemoval — called from WndProc on DBT_DEVICEREMOVECOMPLETE
// ─────────────────────────────────────────────────────────────────────────────

void UsbMonitor::handleRemoval(unsigned long unitMask) {
    char letter = driveLetterFromMask(unitMask);
    if (letter == 0) return;

    std::string driveLetter = std::string(1, letter) + ":";
    LOG_INFO("UsbMonitor: USB drive removed: {}", driveLetter);

    if (onRemoval_) {
        onRemoval_(driveLetter);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// driveLetterFromMask — convert DEV_BROADCAST_VOLUME.dbcv_unitmask to letter
// Bit 0 = A:, Bit 1 = B:, Bit 2 = C:, ... Bit 25 = Z:
// ─────────────────────────────────────────────────────────────────────────────

char UsbMonitor::driveLetterFromMask(unsigned long unitMask) {
    for (int i = 0; i < 26; ++i) {
        if (unitMask & (1 << i)) {
            return 'A' + i;
        }
    }
    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// getDriveInfo — volume name, filesystem, capacity
// ─────────────────────────────────────────────────────────────────────────────

UsbDriveInfo UsbMonitor::getDriveInfo(const std::string& driveLetter) const {
    UsbDriveInfo info;
    info.driveLetter = driveLetter;

    std::string root = driveLetter + "\\";

    char volName[256] = {};
    char fsName[64]   = {};
    GetVolumeInformationA(root.c_str(), volName, sizeof(volName),
                          nullptr, nullptr, nullptr, fsName, sizeof(fsName));
    info.volumeName = volName;
    info.fileSystem = fsName;

    ULARGE_INTEGER freeBytesAvail, totalBytes, totalFreeBytes;
    if (GetDiskFreeSpaceExA(root.c_str(), &freeBytesAvail, &totalBytes, &totalFreeBytes)) {
        info.totalBytes = totalBytes.QuadPart;
        info.freeBytes  = totalFreeBytes.QuadPart;
    }

    return info;
}

} // namespace ResolutePulse
