#include "EventBuffer.h"
#include "utils/Logger.h"

namespace ResolutePulse {

EventBuffer::EventBuffer() = default;

EventBuffer::~EventBuffer() {
    close();
}

bool EventBuffer::initialize(const std::string& dbPath) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (db_ != nullptr) {
        LOG_WARN("EventBuffer already initialized");
        return true;
    }
    
    int rc = sqlite3_open(dbPath.c_str(), &db_);
    if (rc != SQLITE_OK) {
        LOG_ERROR("Failed to open database '{}': {}", dbPath, sqlite3_errmsg(db_));
        sqlite3_close(db_);
        db_ = nullptr;
        return false;
    }
    
    // Enable WAL mode for better concurrent performance
    char* errMsg = nullptr;
    rc = sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        LOG_WARN("Failed to enable WAL mode: {}", errMsg ? errMsg : "unknown");
        sqlite3_free(errMsg);
    }
    
    // Set busy timeout
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
    
    LOG_INFO("EventBuffer initialized: {}", dbPath);
    
    // Log current buffer size
    size_t count = getEventCount();
    if (count > 0) {
        LOG_INFO("Existing buffered events: {}", count);
    }
    
    return true;
}

bool EventBuffer::createSchema() {
    const char* schema = R"(
        CREATE TABLE IF NOT EXISTS events (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            channel TEXT NOT NULL,
            event_id INTEGER NOT NULL,
            timestamp TEXT NOT NULL,
            xml TEXT NOT NULL,
            created_at INTEGER DEFAULT (strftime('%s', 'now'))
        );
        CREATE INDEX IF NOT EXISTS idx_events_created ON events(created_at);
    )";
    
    char* errMsg = nullptr;
    int rc = sqlite3_exec(db_, schema, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        LOG_ERROR("Failed to create schema: {}", errMsg ? errMsg : "unknown");
        sqlite3_free(errMsg);
        return false;
    }
    
    return true;
}

bool EventBuffer::prepareStatements() {
    const char* insertSql = 
        "INSERT INTO events (channel, event_id, timestamp, xml) VALUES (?, ?, ?, ?);";
    
    const char* selectSql = 
        "SELECT id, channel, event_id, timestamp, xml FROM events "
        "ORDER BY created_at ASC LIMIT ?";
    
    const char* deleteSql = 
        "DELETE FROM events WHERE id = ?;";
    
    const char* countSql = 
        "SELECT COUNT(*) FROM events;";
    
    int rc = sqlite3_prepare_v2(db_, insertSql, -1, &stmtInsert_, nullptr);
    if (rc != SQLITE_OK) {
        LOG_ERROR("Failed to prepare insert statement: {}", sqlite3_errmsg(db_));
        return false;
    }
    
    rc = sqlite3_prepare_v2(db_, selectSql, -1, &stmtSelect_, nullptr);
    if (rc != SQLITE_OK) {
        LOG_ERROR("Failed to prepare select statement: {}", sqlite3_errmsg(db_));
        return false;
    }
    
    rc = sqlite3_prepare_v2(db_, deleteSql, -1, &stmtDelete_, nullptr);
    if (rc != SQLITE_OK) {
        LOG_ERROR("Failed to prepare delete statement: {}", sqlite3_errmsg(db_));
        return false;
    }
    
    rc = sqlite3_prepare_v2(db_, countSql, -1, &stmtCount_, nullptr);
    if (rc != SQLITE_OK) {
        LOG_ERROR("Failed to prepare count statement: {}", sqlite3_errmsg(db_));
        return false;
    }
    
    return true;
}

void EventBuffer::finalizeStatements() {
    if (stmtInsert_) { sqlite3_finalize(stmtInsert_); stmtInsert_ = nullptr; }
    if (stmtSelect_) { sqlite3_finalize(stmtSelect_); stmtSelect_ = nullptr; }
    if (stmtDelete_) { sqlite3_finalize(stmtDelete_); stmtDelete_ = nullptr; }
    if (stmtCount_) { sqlite3_finalize(stmtCount_); stmtCount_ = nullptr; }
}

bool EventBuffer::addEvent(const Event& event) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!db_) return false;
    
    sqlite3_reset(stmtInsert_);
    sqlite3_bind_text(stmtInsert_, 1, event.channel.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmtInsert_, 2, static_cast<int>(event.eventId));
    sqlite3_bind_text(stmtInsert_, 3, event.timestamp.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmtInsert_, 4, event.xml.c_str(), -1, SQLITE_TRANSIENT);
    
    int rc = sqlite3_step(stmtInsert_);
    if (rc != SQLITE_DONE) {
        LOG_ERROR("Failed to insert event: {}", sqlite3_errmsg(db_));
        return false;
    }
    
    return true;
}

bool EventBuffer::addEvents(const std::vector<Event>& events) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!db_ || events.empty()) return false;
    
    char* errMsg = nullptr;
    sqlite3_exec(db_, "BEGIN TRANSACTION;", nullptr, nullptr, &errMsg);
    
    bool success = true;
    for (const auto& event : events) {
        sqlite3_reset(stmtInsert_);
        sqlite3_bind_text(stmtInsert_, 1, event.channel.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmtInsert_, 2, static_cast<int>(event.eventId));
        sqlite3_bind_text(stmtInsert_, 3, event.timestamp.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmtInsert_, 4, event.xml.c_str(), -1, SQLITE_TRANSIENT);
        
        int rc = sqlite3_step(stmtInsert_);
        if (rc != SQLITE_DONE) {
            LOG_ERROR("Failed to insert event: {}", sqlite3_errmsg(db_));
            success = false;
            break;
        }
    }
    
    if (success) {
        sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, &errMsg);
    } else {
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, &errMsg);
    }
    
    if (errMsg) sqlite3_free(errMsg);
    
    return success;
}

std::vector<Event> EventBuffer::getEvents(size_t count) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Event> events;
    
    if (!db_) return events;
    
    sqlite3_reset(stmtSelect_);
    sqlite3_bind_int(stmtSelect_, 1, static_cast<int>(count));
    
    while (sqlite3_step(stmtSelect_) == SQLITE_ROW) {
        Event event;
        event.id = sqlite3_column_int64(stmtSelect_, 0);
        event.channel = reinterpret_cast<const char*>(sqlite3_column_text(stmtSelect_, 1));
        event.eventId = static_cast<uint32_t>(sqlite3_column_int(stmtSelect_, 2));
        event.timestamp = reinterpret_cast<const char*>(sqlite3_column_text(stmtSelect_, 3));
        
        const char* xmlText = reinterpret_cast<const char*>(sqlite3_column_text(stmtSelect_, 4));
        if (xmlText) {
            event.xml = xmlText;
        }
        
        events.push_back(std::move(event));
    }
    
    return events;
}

bool EventBuffer::removeEvents(const std::vector<int64_t>& ids) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!db_ || ids.empty()) return false;
    
    char* errMsg = nullptr;
    sqlite3_exec(db_, "BEGIN TRANSACTION;", nullptr, nullptr, &errMsg);
    
    bool success = true;
    for (int64_t id : ids) {
        sqlite3_reset(stmtDelete_);
        sqlite3_bind_int64(stmtDelete_, 1, id);
        
        int rc = sqlite3_step(stmtDelete_);
        if (rc != SQLITE_DONE) {
            LOG_ERROR("Failed to delete event {}: {}", id, sqlite3_errmsg(db_));
            success = false;
            break;
        }
    }
    
    if (success) {
        sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, &errMsg);
    } else {
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, &errMsg);
    }
    
    if (errMsg) sqlite3_free(errMsg);
    
    return success;
}

size_t EventBuffer::getEventCount() {
    // Note: mutex should already be held when called from initialize()
    // For external calls, we need to acquire it
    std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
    
    if (!db_) return 0;
    
    sqlite3_reset(stmtCount_);
    
    if (sqlite3_step(stmtCount_) == SQLITE_ROW) {
        return static_cast<size_t>(sqlite3_column_int64(stmtCount_, 0));
    }
    
    return 0;
}

void EventBuffer::close() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    finalizeStatements();
    
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
        LOG_INFO("EventBuffer closed");
    }
}

} // namespace ResolutePulse
