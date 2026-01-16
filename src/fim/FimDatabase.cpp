#include "FimDatabase.h"
#include "utils/Logger.h"

namespace ResolutePulse {

FimDatabase::FimDatabase() = default;

FimDatabase::~FimDatabase() {
    close();
}

bool FimDatabase::initialize(const std::string& dbPath) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (db_ != nullptr) {
        LOG_WARN("FimDatabase already initialized");
        return true;
    }
    
    int rc = sqlite3_open(dbPath.c_str(), &db_);
    if (rc != SQLITE_OK) {
        LOG_ERROR("Failed to open FIM database '{}': {}", dbPath, sqlite3_errmsg(db_));
        sqlite3_close(db_);
        db_ = nullptr;
        return false;
    }
    
    // Enable WAL mode
    char* errMsg = nullptr;
    sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, &errMsg);
    if (errMsg) sqlite3_free(errMsg);
    
    sqlite3_busy_timeout(db_, 5000);
    
    if (!createSchema()) {
        sqlite3_close(db_);
        db_ = nullptr;
        return false;
    }
    
    if (!prepareStatements()) {
        sqlite3_close(db_);
        db_ = nullptr;
        return false;
    }
    
    LOG_INFO("FimDatabase initialized: {}", dbPath);
    return true;
}

bool FimDatabase::createSchema() {
    const char* schema = R"(
        CREATE TABLE IF NOT EXISTS files (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            path TEXT UNIQUE NOT NULL,
            hash TEXT NOT NULL,
            size INTEGER NOT NULL,
            mtime INTEGER NOT NULL,
            attributes INTEGER NOT NULL,
            updated_at INTEGER DEFAULT (strftime('%s', 'now'))
        );
        CREATE INDEX IF NOT EXISTS idx_files_path ON files(path);
    )";
    
    char* errMsg = nullptr;
    int rc = sqlite3_exec(db_, schema, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        LOG_ERROR("Failed to create FIM schema: {}", errMsg ? errMsg : "unknown");
        sqlite3_free(errMsg);
        return false;
    }
    
    return true;
}

bool FimDatabase::prepareStatements() {
    const char* upsertSql = 
        "INSERT INTO files (path, hash, size, mtime, attributes) VALUES (?, ?, ?, ?, ?) "
        "ON CONFLICT(path) DO UPDATE SET hash=excluded.hash, size=excluded.size, "
        "mtime=excluded.mtime, attributes=excluded.attributes, updated_at=strftime('%s','now');";
    
    const char* selectSql = 
        "SELECT id, path, hash, size, mtime, attributes FROM files WHERE path = ?;";
    
    const char* deleteSql = 
        "DELETE FROM files WHERE path = ?;";
    
    const char* selectAllSql = 
        "SELECT path FROM files;";
    
    const char* countSql = 
        "SELECT COUNT(*) FROM files;";
    
    int rc = sqlite3_prepare_v2(db_, upsertSql, -1, &stmtUpsert_, nullptr);
    if (rc != SQLITE_OK) {
        LOG_ERROR("Failed to prepare upsert: {}", sqlite3_errmsg(db_));
        return false;
    }
    
    rc = sqlite3_prepare_v2(db_, selectSql, -1, &stmtSelect_, nullptr);
    if (rc != SQLITE_OK) return false;
    
    rc = sqlite3_prepare_v2(db_, deleteSql, -1, &stmtDelete_, nullptr);
    if (rc != SQLITE_OK) return false;
    
    rc = sqlite3_prepare_v2(db_, selectAllSql, -1, &stmtSelectAll_, nullptr);
    if (rc != SQLITE_OK) return false;
    
    rc = sqlite3_prepare_v2(db_, countSql, -1, &stmtCount_, nullptr);
    if (rc != SQLITE_OK) return false;
    
    return true;
}

void FimDatabase::finalizeStatements() {
    if (stmtUpsert_) { sqlite3_finalize(stmtUpsert_); stmtUpsert_ = nullptr; }
    if (stmtSelect_) { sqlite3_finalize(stmtSelect_); stmtSelect_ = nullptr; }
    if (stmtDelete_) { sqlite3_finalize(stmtDelete_); stmtDelete_ = nullptr; }
    if (stmtSelectAll_) { sqlite3_finalize(stmtSelectAll_); stmtSelectAll_ = nullptr; }
    if (stmtCount_) { sqlite3_finalize(stmtCount_); stmtCount_ = nullptr; }
}

bool FimDatabase::upsertFile(const FileRecord& record) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!db_) return false;
    
    sqlite3_reset(stmtUpsert_);
    sqlite3_bind_text(stmtUpsert_, 1, record.path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmtUpsert_, 2, record.hash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmtUpsert_, 3, static_cast<int64_t>(record.size));
    sqlite3_bind_int64(stmtUpsert_, 4, static_cast<int64_t>(record.mtime));
    sqlite3_bind_int(stmtUpsert_, 5, static_cast<int>(record.attributes));
    
    int rc = sqlite3_step(stmtUpsert_);
    if (rc != SQLITE_DONE) {
        LOG_ERROR("Failed to upsert file '{}': {}", record.path, sqlite3_errmsg(db_));
        return false;
    }
    
    return true;
}

std::optional<FileRecord> FimDatabase::getFile(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!db_) return std::nullopt;
    
    sqlite3_reset(stmtSelect_);
    sqlite3_bind_text(stmtSelect_, 1, path.c_str(), -1, SQLITE_TRANSIENT);
    
    if (sqlite3_step(stmtSelect_) == SQLITE_ROW) {
        FileRecord record;
        record.id = sqlite3_column_int64(stmtSelect_, 0);
        record.path = reinterpret_cast<const char*>(sqlite3_column_text(stmtSelect_, 1));
        record.hash = reinterpret_cast<const char*>(sqlite3_column_text(stmtSelect_, 2));
        record.size = static_cast<uint64_t>(sqlite3_column_int64(stmtSelect_, 3));
        record.mtime = static_cast<uint64_t>(sqlite3_column_int64(stmtSelect_, 4));
        record.attributes = static_cast<uint32_t>(sqlite3_column_int(stmtSelect_, 5));
        return record;
    }
    
    return std::nullopt;
}

bool FimDatabase::deleteFile(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!db_) return false;
    
    sqlite3_reset(stmtDelete_);
    sqlite3_bind_text(stmtDelete_, 1, path.c_str(), -1, SQLITE_TRANSIENT);
    
    return sqlite3_step(stmtDelete_) == SQLITE_DONE;
}

std::vector<std::string> FimDatabase::getAllPaths() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> paths;
    
    if (!db_) return paths;
    
    sqlite3_reset(stmtSelectAll_);
    
    while (sqlite3_step(stmtSelectAll_) == SQLITE_ROW) {
        const char* path = reinterpret_cast<const char*>(sqlite3_column_text(stmtSelectAll_, 0));
        if (path) paths.push_back(path);
    }
    
    return paths;
}

size_t FimDatabase::getRecordCount() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!db_) return 0;
    
    sqlite3_reset(stmtCount_);
    
    if (sqlite3_step(stmtCount_) == SQLITE_ROW) {
        return static_cast<size_t>(sqlite3_column_int64(stmtCount_, 0));
    }
    
    return 0;
}

bool FimDatabase::deleteFilesNotIn(const std::vector<std::string>& paths) {
    // For now, simple implementation - can be optimized with temp table
    auto allPaths = getAllPaths();
    
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) return false;
    
    for (const auto& existing : allPaths) {
        bool found = false;
        for (const auto& keep : paths) {
            if (existing == keep) {
                found = true;
                break;
            }
        }
        if (!found) {
            sqlite3_reset(stmtDelete_);
            sqlite3_bind_text(stmtDelete_, 1, existing.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmtDelete_);
        }
    }
    
    return true;
}

void FimDatabase::close() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    finalizeStatements();
    
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
        LOG_INFO("FimDatabase closed");
    }
}

} // namespace ResolutePulse
