#pragma once
// MoTwWatcher.h
// Event-driven watcher for the Mark of the Web (Zone.Identifier ADS).
//
// When a browser finishes a download it renames the temp file (.crdownload → final)
// and *then* writes the :Zone.Identifier alternate data stream.  These are two
// separate kernel operations with a small timing gap.
//
// This class bridges that gap without sleeping or polling:
//  1. Agent.cpp calls addPending(path) right after the rename FIM event fires.
//  2. MoTwWatcher runs ReadDirectoryChangesW with FILE_NOTIFY_CHANGE_STREAM on
//     the Downloads folder.  The kernel fires this only when an ADS is written —
//     i.e., exactly when Zone.Identifier appears.
//  3. If the affected file is in the pending set the scan callback fires instantly.
//  4. Entries that never get a Zone.Identifier (local files, network shares) are
//     expired after 30 s without consuming any CPU in between.

#pragma once

#include <string>
#include <functional>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>
#include <vector>

#include <Windows.h>

namespace ResolutePulse {

class MoTwWatcher {
public:
    using ScanCallback = std::function<void(const std::string& filePath)>;

    MoTwWatcher() = default;
    ~MoTwWatcher() { stop(); }

    void setScanCallback(ScanCallback cb) { callback_ = std::move(cb); }

    /// Register a file path to scan once Zone.Identifier appears.
    /// Thread-safe — can be called from FIM callback thread.
    void addPending(const std::string& filePath);

    /// Start watching all of these directories.
    /// One watcher thread is created per directory.
    bool start(const std::vector<std::string>& watchDirs);

    void stop();

private:
    void watchLoop(const std::string& dir, HANDLE stopEvent);
    void expire();

    ScanCallback callback_;

    struct Entry {
        std::string lowerPath;   // pre-lowercased for fast lookup
        std::chrono::steady_clock::time_point added;
    };

    std::mutex           pendingMtx_;
    std::vector<Entry>   pending_;   // small — one entry per in-flight download

    std::vector<std::thread> threads_;
    HANDLE stopEvent_ = nullptr;
};

} // namespace ResolutePulse
