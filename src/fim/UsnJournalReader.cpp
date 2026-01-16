#include "UsnJournalReader.h"
#include "utils/Logger.h"

#include <winioctl.h>
#include <vector>

namespace ResolutePulse {

// Define structures if not available in MinGW headers
#ifndef READ_USN_JOURNAL_DATA_V0
typedef struct {
    USN StartUsn;
    DWORD ReasonMask;
    DWORD ReturnOnlyOnClose;
    DWORDLONG Timeout;
    DWORDLONG BytesToWaitFor;
    DWORDLONG UsnJournalID;
} READ_USN_JOURNAL_DATA_V0, *PREAD_USN_JOURNAL_DATA_V0;
#endif

UsnJournalReader::UsnJournalReader() = default;

UsnJournalReader::~UsnJournalReader() {
    stop();
    if (volumeHandle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(volumeHandle_);
    }
}

std::string UsnJournalReader::wideToUtf8(const std::wstring& wide) {
    if (wide.empty()) return "";
    
    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), 
                                       static_cast<int>(wide.length()),
                                       nullptr, 0, nullptr, nullptr);
    if (utf8Len <= 0) return "";
    
    std::string utf8(utf8Len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), 
                        static_cast<int>(wide.length()),
                        &utf8[0], utf8Len, nullptr, nullptr);
    return utf8;
}

bool UsnJournalReader::initialize(const std::string& volumeLetter) {
    volumeLetter_ = volumeLetter;
    
    // Open volume handle
    volumePath_ = "\\\\.\\" + volumeLetter + ":";
    
    std::wstring wPath(volumePath_.begin(), volumePath_.end());
    volumeHandle_ = CreateFileW(
        wPath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
    
    if (volumeHandle_ == INVALID_HANDLE_VALUE) {
        LOG_ERROR("Cannot open volume {}: error {}", volumePath_, GetLastError());
        return false;
    }
    
    // Query USN journal
    USN_JOURNAL_DATA journalData;
    DWORD bytesReturned;
    
    if (!DeviceIoControl(
            volumeHandle_,
            FSCTL_QUERY_USN_JOURNAL,
            nullptr, 0,
            &journalData, sizeof(journalData),
            &bytesReturned,
            nullptr)) {
        
        DWORD error = GetLastError();
        if (error == ERROR_JOURNAL_NOT_ACTIVE) {
            LOG_WARN("USN Journal not active on volume {}. Creating...", volumeLetter);
            
            // Create journal
            CREATE_USN_JOURNAL_DATA createData = {0, 0}; // Use defaults
            if (!DeviceIoControl(
                    volumeHandle_,
                    FSCTL_CREATE_USN_JOURNAL,
                    &createData, sizeof(createData),
                    nullptr, 0,
                    &bytesReturned,
                    nullptr)) {
                LOG_ERROR("Cannot create USN Journal: error {}", GetLastError());
                return false;
            }
            
            // Query again
            if (!DeviceIoControl(
                    volumeHandle_,
                    FSCTL_QUERY_USN_JOURNAL,
                    nullptr, 0,
                    &journalData, sizeof(journalData),
                    &bytesReturned,
                    nullptr)) {
                LOG_ERROR("Cannot query USN Journal after creation: error {}", GetLastError());
                return false;
            }
        } else {
            LOG_ERROR("Cannot query USN Journal: error {}", error);
            return false;
        }
    }
    
    journalId_ = journalData.UsnJournalID;
    lastUsn_ = journalData.NextUsn;
    
    LOG_INFO("USN Journal initialized for volume {}: JournalID={}, NextUSN={}", 
             volumeLetter, journalId_, lastUsn_);
    
    return true;
}

void UsnJournalReader::setChangeCallback(ChangeCallback callback) {
    callback_ = std::move(callback);
}

bool UsnJournalReader::start() {
    if (volumeHandle_ == INVALID_HANDLE_VALUE) {
        LOG_ERROR("UsnJournalReader not initialized");
        return false;
    }
    
    if (running_) {
        LOG_WARN("UsnJournalReader already running");
        return true;
    }
    
    stopRequested_ = false;
    running_ = true;
    
    monitorThread_ = std::thread(&UsnJournalReader::monitorThread, this);
    
    LOG_INFO("USN Journal monitoring started for volume {}", volumeLetter_);
    return true;
}

void UsnJournalReader::stop() {
    if (!running_) return;
    
    stopRequested_ = true;
    
    if (monitorThread_.joinable()) {
        monitorThread_.join();
    }
    
    running_ = false;
    LOG_INFO("USN Journal monitoring stopped");
}

std::string UsnJournalReader::resolveFilePath(uint64_t fileReferenceNumber) {
    // Open file by reference number
    FILE_ID_DESCRIPTOR fileId;
    fileId.dwSize = sizeof(fileId);
    fileId.Type = FileIdType;
    fileId.FileId.QuadPart = static_cast<LONGLONG>(fileReferenceNumber);
    
    HANDLE hFile = OpenFileById(
        volumeHandle_,
        &fileId,
        0, // No access needed, just querying name
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        FILE_FLAG_BACKUP_SEMANTICS
    );
    
    if (hFile == INVALID_HANDLE_VALUE) {
        return "";
    }
    
    // Get file path
    wchar_t pathBuffer[MAX_PATH * 2];
    DWORD pathLen = GetFinalPathNameByHandleW(hFile, pathBuffer, MAX_PATH * 2, 
                                               FILE_NAME_NORMALIZED);
    CloseHandle(hFile);
    
    if (pathLen == 0 || pathLen >= MAX_PATH * 2) {
        return "";
    }
    
    std::wstring wPath(pathBuffer, pathLen);
    
    // Remove \\?\ prefix if present
    if (wPath.compare(0, 4, L"\\\\?\\") == 0) {
        wPath = wPath.substr(4);
    }
    
    return wideToUtf8(wPath);
}

void UsnJournalReader::monitorThread() {
    constexpr size_t BUFFER_SIZE = 64 * 1024;
    std::vector<uint8_t> buffer(BUFFER_SIZE);
    
    READ_USN_JOURNAL_DATA_V0 readData = {};
    readData.StartUsn = lastUsn_;
    readData.ReasonMask = 0xFFFFFFFF; // All reasons
    readData.ReturnOnlyOnClose = FALSE;
    readData.Timeout = 0;
    readData.BytesToWaitFor = 0;
    readData.UsnJournalID = journalId_;
    
    while (!stopRequested_) {
        DWORD bytesReturned = 0;
        
        readData.StartUsn = lastUsn_;
        
        BOOL success = DeviceIoControl(
            volumeHandle_,
            FSCTL_READ_USN_JOURNAL,
            &readData, sizeof(readData),
            buffer.data(), static_cast<DWORD>(buffer.size()),
            &bytesReturned,
            nullptr
        );
        
        if (!success) {
            DWORD error = GetLastError();
            if (error == ERROR_HANDLE_EOF || error == ERROR_NO_MORE_ITEMS) {
                // No new records, wait and retry
                Sleep(100);
                continue;
            }
            
            LOG_ERROR("USN read failed: error {}", error);
            break;
        }
        
        if (bytesReturned < sizeof(USN)) {
            Sleep(100);
            continue;
        }
        
        // First 8 bytes is the next USN
        USN nextUsn = *reinterpret_cast<USN*>(buffer.data());
        
        // Process records using the generic USN_RECORD structure
        PUSN_RECORD record = reinterpret_cast<PUSN_RECORD>(
            buffer.data() + sizeof(USN));
        
        while (reinterpret_cast<uint8_t*>(record) < buffer.data() + bytesReturned) {
            if (stopRequested_) break;
            
            // USN_RECORD (V2) structure works for both V2 and V3
            // Extract filename
            std::wstring wFilename(
                reinterpret_cast<wchar_t*>(
                    reinterpret_cast<uint8_t*>(record) + record->FileNameOffset),
                record->FileNameLength / sizeof(wchar_t)
            );
            
            UsnChange change;
            change.reason = record->Reason;
            change.usn = record->Usn;
            change.timestamp = record->TimeStamp.QuadPart;
            change.isDirectory = (record->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            
            // Try to resolve full path
            change.filePath = resolveFilePath(record->FileReferenceNumber);
            
            if (change.filePath.empty()) {
                // Fallback to just filename
                change.filePath = wideToUtf8(wFilename);
            }
            
            // Only report file changes (not pure Close events)
            if (change.reason != static_cast<uint32_t>(UsnReason::Close) && callback_) {
                callback_(change);
            }
            
            // Move to next record
            record = reinterpret_cast<PUSN_RECORD>(
                reinterpret_cast<uint8_t*>(record) + record->RecordLength);
        }
        
        lastUsn_ = nextUsn;
    }
    
    running_ = false;
}

} // namespace ResolutePulse
