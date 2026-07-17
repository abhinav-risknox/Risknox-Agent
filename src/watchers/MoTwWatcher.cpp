#include "MoTwWatcher.h"
#include "utils/Logger.h"

#include <Windows.h>

// FILE_NOTIFY_CHANGE_STREAM fires when an alternate data stream is written.
// Defined in <winnt.h> only when _WIN32_WINNT >= 0x0600; guard for MinGW.
#ifndef FILE_NOTIFY_CHANGE_STREAM
#  define FILE_NOTIFY_CHANGE_STREAM 0x00000200
#endif

#include <algorithm>
#include <filesystem>

namespace ResolutePulse {

// ─────────────────────────────────────────────────────────────────────────────
void MoTwWatcher::addPending(const std::string& filePath)
{
    // Pre-lowercase so the stream-notification comparison is a single tolower call.
    std::string lower = filePath;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    std::lock_guard lock(pendingMtx_);
    // Deduplicate: browser may fire multiple events for the same file.
    for (auto& e : pending_) {
        if (e.lowerPath == lower) {
            e.added = std::chrono::steady_clock::now(); // refresh timestamp
            return;
        }
    }
    pending_.push_back({ std::move(lower), std::chrono::steady_clock::now() });
    LOG_DEBUG("MoTwWatcher: pending ({})", filePath);
}

// ─────────────────────────────────────────────────────────────────────────────
bool MoTwWatcher::start(const std::vector<std::string>& watchDirs)
{
    if (watchDirs.empty()) return false;

    stopEvent_ = CreateEventA(nullptr, /*manual reset*/TRUE, /*initial*/FALSE, nullptr);
    if (!stopEvent_) return false;

    for (const auto& dir : watchDirs) {
        threads_.emplace_back(&MoTwWatcher::watchLoop, this, dir, stopEvent_);
    }
    LOG_INFO("MoTwWatcher: watching {} Download director{}", watchDirs.size(),
             watchDirs.size() == 1 ? "y" : "ies");
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
void MoTwWatcher::stop()
{
    if (stopEvent_) SetEvent(stopEvent_);
    for (auto& t : threads_) if (t.joinable()) t.join();
    threads_.clear();
    if (stopEvent_) { CloseHandle(stopEvent_); stopEvent_ = nullptr; }
}

// ─────────────────────────────────────────────────────────────────────────────
// One thread per Downloads directory.
// Uses overlapped ReadDirectoryChangesW so we can also wake on stopEvent_.
void MoTwWatcher::watchLoop(const std::string& dir, HANDLE stopEvent)
{
    HANDLE hDir = CreateFileA(
        dir.c_str(),
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
        nullptr
    );
    if (hDir == INVALID_HANDLE_VALUE) {
        LOG_ERROR("MoTwWatcher: cannot open '{}': {}", dir, GetLastError());
        return;
    }

    alignas(DWORD) char buf[32 * 1024];
    OVERLAPPED ov   = {};
    ov.hEvent       = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    HANDLE handles[2] = { ov.hEvent, stopEvent };
    DWORD bytes = 0; // declared here so it's in scope for cleanup after the loop

    while (true) {
        ResetEvent(ov.hEvent);

        // FILE_NOTIFY_CHANGE_STREAM — fires only when an ADS is created/modified.
        // This is the kernel telling us "some alternate data stream changed on a
        // file in this directory", which is exactly when Zone.Identifier is written.
        BOOL queued = ReadDirectoryChangesW(
            hDir, buf, sizeof(buf),
            /*watchSubTree=*/FALSE,
            FILE_NOTIFY_CHANGE_STREAM,
            &bytes, &ov, nullptr
        );

        if (!queued && GetLastError() != ERROR_IO_PENDING) {
            LOG_ERROR("MoTwWatcher: ReadDirectoryChangesW error {}", GetLastError());
            break;
        }

        // Wait for notification OR stop signal OR 30 s expiry sweep
        DWORD wait = WaitForMultipleObjects(2, handles, FALSE, 30'000);

        if (wait == WAIT_OBJECT_0 + 1) break; // stopEvent — exit cleanly

        if (wait == WAIT_TIMEOUT) {
            CancelIo(hDir); // cancel the pending read so we can reissue it
            GetOverlappedResult(hDir, &ov, &bytes, TRUE);
            expire();
            continue;
        }

        // WAIT_OBJECT_0 — notification arrived
        if (!GetOverlappedResult(hDir, &ov, &bytes, FALSE) || bytes == 0)
            continue;

        // Walk the notification records
        const char* p = buf;
        while (p < buf + bytes) {
            const auto* info = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(p);

            // Convert wchar filename to UTF-8
            int wlen = static_cast<int>(info->FileNameLength / sizeof(WCHAR));
            int nbytes = WideCharToMultiByte(CP_UTF8, 0, info->FileName, wlen,
                                             nullptr, 0, nullptr, nullptr);
            std::string name(nbytes, '\0');
            WideCharToMultiByte(CP_UTF8, 0, info->FileName, wlen,
                                name.data(), nbytes, nullptr, nullptr);

            // name looks like:  "invoice.exe:Zone.Identifier:$DATA"
            // Only care about Zone.Identifier writes.
            std::string lower = name;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

            if (lower.find(":zone.identifier") != std::string::npos) {
                // Extract the base file name (before the first ':')
                auto colon = name.find(':');
                std::string baseName = (colon != std::string::npos)
                                       ? name.substr(0, colon)
                                       : name;

                // Build and lowercase the full path for lookup
                std::string fullLower = dir + "\\" + baseName;
                std::transform(fullLower.begin(), fullLower.end(),
                               fullLower.begin(), ::tolower);

                // Check pending set — O(n) but n is always tiny (open downloads)
                std::string original;
                {
                    std::lock_guard lock(pendingMtx_);
                    for (auto it = pending_.begin(); it != pending_.end(); ++it) {
                        if (it->lowerPath == fullLower) {
                            original = it->lowerPath; // reconstruct original below
                            pending_.erase(it);
                            break;
                        }
                    }
                }

                if (!original.empty()) {
                    // Reconstruct the original-case path from dir + baseName
                    std::string scanPath = dir + "\\" + baseName;
                    LOG_INFO("MoTwWatcher: Zone.Identifier written → scan: {}", scanPath);
                    if (callback_) callback_(scanPath);
                }
            }

            if (info->NextEntryOffset == 0) break;
            p += info->NextEntryOffset;
        }

        expire();
    }

    CancelIo(hDir);
    GetOverlappedResult(hDir, &ov, &bytes, TRUE);
    CloseHandle(ov.hEvent);
    CloseHandle(hDir);
}

// ─────────────────────────────────────────────────────────────────────────────
// Drop entries older than 30 s — these are files with no MOTW (local copies,
// network drive downloads, etc.).  Called from the watch thread, no extra thread.
void MoTwWatcher::expire()
{
    auto now = std::chrono::steady_clock::now();
    std::lock_guard lock(pendingMtx_);
    auto before = pending_.size();
    pending_.erase(
        std::remove_if(pending_.begin(), pending_.end(), [&](const Entry& e) {
            return std::chrono::duration_cast<std::chrono::seconds>(
                       now - e.added).count() > 30;
        }),
        pending_.end()
    );
    if (pending_.size() < before)
        LOG_DEBUG("MoTwWatcher: expired {} stale entr{}", before - pending_.size(),
                  (before - pending_.size()) == 1 ? "y" : "ies");
}

} // namespace ResolutePulse
