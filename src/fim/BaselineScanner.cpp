#include "BaselineScanner.h"
#include "FileHasher.h"
#include "utils/Logger.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <algorithm>

namespace ResolutePulse {

BaselineScanner::BaselineScanner() = default;
BaselineScanner::~BaselineScanner() = default;

void BaselineScanner::setConfig(const FimConfig& config) {
    config_ = config;
}

void BaselineScanner::setDatabase(FimDatabase* db) {
    db_ = db;
}

void BaselineScanner::setProgressCallback(ProgressCallback callback) {
    progressCallback_ = std::move(callback);
}

void BaselineScanner::stop() {
    stopRequested_ = true;
}

std::string BaselineScanner::normalizePathSeparators(const std::string& path) const {
    std::string normalized = path;
    std::replace(normalized.begin(), normalized.end(), '/', '\\');
    return normalized;
}

bool BaselineScanner::matchesExcludePattern(const std::string& path) const {
    // Simple pattern matching (supports * wildcard)
    std::string filename = path;
    auto lastSlash = path.rfind('\\');
    if (lastSlash != std::string::npos) {
        filename = path.substr(lastSlash + 1);
    }
    
    for (const auto& pattern : config_.excludePatterns) {
        // Simple wildcard matching
        if (pattern.front() == '*') {
            // *.ext pattern
            std::string ext = pattern.substr(1);
            if (filename.size() >= ext.size() &&
                filename.compare(filename.size() - ext.size(), ext.size(), ext) == 0) {
                return true;
            }
        } else if (pattern.back() == '*') {
            // prefix* pattern
            std::string prefix = pattern.substr(0, pattern.size() - 1);
            if (filename.size() >= prefix.size() &&
                filename.compare(0, prefix.size(), prefix) == 0) {
                return true;
            }
        } else if (filename == pattern) {
            return true;
        }
    }
    
    return false;
}

std::optional<FileRecord> BaselineScanner::scanFile(const std::string& filePath) {
    std::string normalizedPath = normalizePathSeparators(filePath);
    
    // Get file attributes
    WIN32_FILE_ATTRIBUTE_DATA fileInfo;
    std::wstring widePath(normalizedPath.begin(), normalizedPath.end());
    
    if (!GetFileAttributesExW(widePath.c_str(), GetFileExInfoStandard, &fileInfo)) {
        return std::nullopt;
    }
    
    // Skip directories
    if (fileInfo.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        return std::nullopt;
    }
    
    // Get file size
    uint64_t fileSize = (static_cast<uint64_t>(fileInfo.nFileSizeHigh) << 32) | 
                         fileInfo.nFileSizeLow;
    
    // Skip files too large
    if (config_.maxFileSizeMb > 0 && fileSize > config_.maxFileSizeMb * 1024 * 1024) {
        LOG_DEBUG("Skipping large file: {} ({} MB)", normalizedPath, fileSize / (1024*1024));
        return std::nullopt;
    }
    
    // Build file record
    FileRecord record;
    record.path = normalizedPath;
    record.size = fileSize;
    record.mtime = (static_cast<uint64_t>(fileInfo.ftLastWriteTime.dwHighDateTime) << 32) |
                    fileInfo.ftLastWriteTime.dwLowDateTime;
    record.attributes = fileInfo.dwFileAttributes;
    
    // Compute hash
    if (config_.hashFiles) {
        record.hash = FileHasher::hashFile(normalizedPath);
        if (record.hash.empty()) {
            // Could not hash - might be locked or permission denied
            LOG_DEBUG("Could not hash file: {}", normalizedPath);
            return std::nullopt;
        }
    }
    
    return record;
}

void BaselineScanner::scanDirectory(const std::string& path, std::vector<FileRecord>& records) {
    if (stopRequested_) return;
    
    std::string searchPath = path + "\\*";
    
    WIN32_FIND_DATAW findData;
    HANDLE hFind = FindFirstFileW(
        std::wstring(searchPath.begin(), searchPath.end()).c_str(),
        &findData
    );
    
    if (hFind == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        if (error != ERROR_ACCESS_DENIED) {
            LOG_WARN("Cannot access directory: {} (error {})", path, error);
        }
        return;
    }
    
    do {
        if (stopRequested_) break;
        
        // Convert filename to UTF-8
        std::wstring wname(findData.cFileName);
        std::string name(wname.begin(), wname.end());
        
        // Skip . and ..
        if (name == "." || name == "..") continue;
        
        std::string fullPath = path + "\\" + name;
        
        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            // Recurse into subdirectory
            scanDirectory(fullPath, records);
        } else {
            // Check exclude patterns
            if (matchesExcludePattern(fullPath)) {
                continue;
            }
            
            // Scan file
            auto record = scanFile(fullPath);
            if (record) {
                records.push_back(*record);
                filesScanned_++;
                
                if (progressCallback_) {
                    progressCallback_(filesScanned_, fullPath);
                }
            }
        }
    } while (FindNextFileW(hFind, &findData));
    
    FindClose(hFind);
}

size_t BaselineScanner::scan() {
    if (!db_) {
        LOG_ERROR("BaselineScanner: No database configured");
        return 0;
    }
    
    scanning_ = true;
    stopRequested_ = false;
    filesScanned_ = 0;
    
    LOG_INFO("Starting baseline scan of {} directories", config_.directories.size());
    
    std::vector<FileRecord> allRecords;
    
    for (const auto& dir : config_.directories) {
        if (stopRequested_) break;
        
        std::string normalizedDir = normalizePathSeparators(dir);
        LOG_INFO("Scanning directory: {}", normalizedDir);
        
        scanDirectory(normalizedDir, allRecords);
    }
    
    // Store all records in database inside a single transaction.
    // Without a transaction every upsertFile() is its own implicit write,
    // creating thousands of lock-acquire/release cycles that race with the
    // USN journal callback thread → "database is locked".
    LOG_INFO("Storing {} file records in database", allRecords.size());

    bool txOk = db_->beginTransaction();
    if (!txOk) {
        LOG_WARN("FIM: could not begin transaction, falling back to auto-commit");
    }

    bool anyError = false;
    for (const auto& record : allRecords) {
        if (stopRequested_) break;
        if (!db_->upsertFile(record)) {
            anyError = true;
            break;
        }
    }

    if (txOk) {
        if (anyError || stopRequested_) {
            db_->rollbackTransaction();
            LOG_WARN("FIM baseline scan transaction rolled back");
        } else {
            db_->commitTransaction();
        }
    }

    scanning_ = false;
    LOG_INFO("Baseline scan complete: {} files", filesScanned_);

    return filesScanned_;
}

} // namespace ResolutePulse
