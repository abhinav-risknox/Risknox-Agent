#pragma once

#include <string>
#include <functional>
#include <atomic>
#include <thread>
#include <cstdint>
#include <unordered_map>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

namespace ResolutePulse {

// USN change reasons (subset of important ones)
enum class UsnReason : uint32_t {
    DataOverwrite       = 0x00000001,
    DataExtend          = 0x00000002,
    DataTruncation      = 0x00000004,
    NamedDataOverwrite  = 0x00000010,
    NamedDataExtend     = 0x00000020,
    NamedDataTruncation = 0x00000040,
    FileCreate          = 0x00000100,
    FileDelete          = 0x00000200,
    EaChange            = 0x00000400,
    SecurityChange      = 0x00000800,
    RenameOldName       = 0x00001000,
    RenameNewName       = 0x00002000,
    IndexableChange     = 0x00004000,
    BasicInfoChange     = 0x00008000,
    HardLinkChange      = 0x00010000,
    CompressionChange   = 0x00020000,
    EncryptionChange    = 0x00040000,
    ObjectIdChange      = 0x00080000,
    ReparsePointChange  = 0x00100000,
    StreamChange        = 0x00200000,
    Close               = 0x80000000
};

struct UsnChange {
    std::string filePath;
    uint32_t reason;
    uint64_t usn;
    uint64_t timestamp; // FILETIME
    bool isDirectory;
    
    bool isCreate() const { return reason & static_cast<uint32_t>(UsnReason::FileCreate); }
    bool isDelete() const { return reason & static_cast<uint32_t>(UsnReason::FileDelete); }
    bool isModify() const { return reason & 0x0000007F; } // Data changes
    bool isRename() const { return reason & 0x00003000; }
    bool isSecurityChange() const { return reason & static_cast<uint32_t>(UsnReason::SecurityChange); }
};

class UsnJournalReader {
public:
    using ChangeCallback = std::function<void(const UsnChange&)>;
    
    UsnJournalReader();
    ~UsnJournalReader();
    
    // Initialize for a specific volume (e.g., "C:")
    bool initialize(const std::string& volumeLetter);
    
    // Set callback for changes
    void setChangeCallback(ChangeCallback callback);
    
    // Start monitoring in background thread
    bool start();
    
    // Stop monitoring
    void stop();
    
    bool isRunning() const { return running_.load(); }
    
    // Get last USN (for persistence)
    uint64_t getLastUsn() const { return lastUsn_; }
    
    // Set starting USN (for resume after restart)
    void setStartingUsn(uint64_t usn) { lastUsn_ = usn; }
    
private:
    void monitorThread();
    std::string resolveFilePath(uint64_t fileReferenceNumber);
    std::string wideToUtf8(const std::wstring& wide);
    
    HANDLE volumeHandle_ = INVALID_HANDLE_VALUE;
    std::string volumeLetter_;
    std::string volumePath_;
    
    ChangeCallback callback_;
    std::thread monitorThread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stopRequested_{false};
    
    uint64_t journalId_ = 0;
    uint64_t lastUsn_ = 0;

    // Path resolution cache to avoid repeated OpenFileById calls
    std::unordered_map<uint64_t, std::string> pathCache_;
    static constexpr size_t MAX_PATH_CACHE_SIZE = 10000;

    // Polling constants
    static constexpr DWORD IDLE_POLL_MS = 1000;       // Sleep when no records
    static constexpr DWORD BATCH_YIELD_MS = 10;       // Yield after processing a batch

    // Only capture meaningful FIM changes (not close, metadata-only, etc.)
    static constexpr uint32_t FIM_REASON_MASK =
        0x00000001 |  // DataOverwrite
        0x00000002 |  // DataExtend
        0x00000004 |  // DataTruncation
        0x00000010 |  // NamedDataOverwrite
        0x00000020 |  // NamedDataExtend
        0x00000040 |  // NamedDataTruncation
        0x00000100 |  // FileCreate
        0x00000200 |  // FileDelete
        0x00000800 |  // SecurityChange
        0x00001000 |  // RenameOldName
        0x00002000;   // RenameNewName
};

} // namespace ResolutePulse
