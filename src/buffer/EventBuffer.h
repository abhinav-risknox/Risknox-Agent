#pragma once

#include "collector/Event.h"
#include <string>
#include <vector>
#include <mutex>
#include <sqlite3.h>

namespace ResolutePulse {

class EventBuffer {
public:
    EventBuffer();
    ~EventBuffer();
    
    // Initialize the database
    // @param dbPath - Path to SQLite database file
    // @return true on success
    bool initialize(const std::string& dbPath);
    
    // Add a single event to the buffer
    bool addEvent(const Event& event);
    
    // Add multiple events to the buffer (uses transaction)
    bool addEvents(const std::vector<Event>& events);
    
    // Get events from buffer (oldest first)
    // @param count - Maximum number of events to retrieve
    // @return Vector of events with their database IDs set
    std::vector<Event> getEvents(size_t count);
    
    // Remove events by their database IDs
    bool removeEvents(const std::vector<int64_t>& ids);
    
    // Get total number of buffered events
    size_t getEventCount();
    
    // Close the database
    void close();
    
    // Check if initialized
    bool isInitialized() const { return db_ != nullptr; }
    
private:
    bool createSchema();
    bool prepareStatements();
    void finalizeStatements();
    
    sqlite3* db_ = nullptr;
    std::mutex mutex_;
    
    // Prepared statements for performance
    sqlite3_stmt* stmtInsert_ = nullptr;
    sqlite3_stmt* stmtSelect_ = nullptr;
    sqlite3_stmt* stmtDelete_ = nullptr;
    sqlite3_stmt* stmtCount_ = nullptr;
};

} // namespace ResolutePulse
