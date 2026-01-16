#pragma once

#include <string>
#include <vector>
#include <optional>
#include <mutex>
#include <cstdint>
#include <sqlite3.h>

namespace ResolutePulse {

struct FileRecord {
    int64_t id = 0;
    std::string path;
    std::string hash;           // SHA256 hex string
    uint64_t size = 0;
    uint64_t mtime = 0;         // Last modification time (FILETIME as uint64)
    uint32_t attributes = 0;    // File attributes
    
    bool operator!=(const FileRecord& other) const {
        return hash != other.hash || size != other.size || 
               mtime != other.mtime || attributes != other.attributes;
    }
};

class FimDatabase {
public:
    FimDatabase();
    ~FimDatabase();
    
    // Initialize the database
    bool initialize(const std::string& dbPath);
    
    // Insert or update a file record
    bool upsertFile(const FileRecord& record);
    
    // Get a file record by path
    std::optional<FileRecord> getFile(const std::string& path);
    
    // Delete a file record by path
    bool deleteFile(const std::string& path);
    
    // Get all monitored file paths
    std::vector<std::string> getAllPaths();
    
    // Get count of records
    size_t getRecordCount();
    
    // Delete files not in the given list (cleanup after rescan)
    bool deleteFilesNotIn(const std::vector<std::string>& paths);
    
    // Close the database
    void close();
    
    bool isInitialized() const { return db_ != nullptr; }
    
private:
    bool createSchema();
    bool prepareStatements();
    void finalizeStatements();
    
    sqlite3* db_ = nullptr;
    std::mutex mutex_;
    
    sqlite3_stmt* stmtUpsert_ = nullptr;
    sqlite3_stmt* stmtSelect_ = nullptr;
    sqlite3_stmt* stmtDelete_ = nullptr;
    sqlite3_stmt* stmtSelectAll_ = nullptr;
    sqlite3_stmt* stmtCount_ = nullptr;
};

} // namespace ResolutePulse
