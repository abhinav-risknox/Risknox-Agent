#include "FimMonitor.h"
#include "utils/Logger.h"
#include "FileHasher.h"
#include <chrono>
#include <iomanip>
#include <sstream>
#include <algorithm>

namespace ResolutePulse {

FimMonitor::FimMonitor() = default;

FimMonitor::~FimMonitor() {
    stop();
}

bool FimMonitor::initialize(const FimConfig& config, const std::string& dbPath) {
    config_ = config;
    
    // Build monitored prefixes for quick path matching
    for (const auto& dir : config_.directories) {
        std::string prefix = dir;
        std::replace(prefix.begin(), prefix.end(), '/', '\\');
        // Convert to lowercase for case-insensitive matching
        std::transform(prefix.begin(), prefix.end(), prefix.begin(), ::tolower);
        if (!prefix.empty() && prefix.back() != '\\') {
            prefix += '\\';
        }
        monitoredPrefixes_.push_back(prefix);
    }
    
    // Initialize database
    database_ = std::make_unique<FimDatabase>();
    if (!database_->initialize(dbPath)) {
        LOG_ERROR("Failed to initialize FIM database");
        return false;
    }
    
    // Initialize scanner
    scanner_ = std::make_unique<BaselineScanner>();
    scanner_->setConfig(config_);
    scanner_->setDatabase(database_.get());
    scanner_->setProgressCallback([](size_t count, const std::string& file) {
        if (count % 1000 == 0) {
            LOG_INFO("FIM baseline scan progress: {} files", count);
        }
    });
    
    // Initialize USN reader
    // For now, monitor C: drive. In future, detect drives from config paths
    usnReader_ = std::make_unique<UsnJournalReader>();
    if (!usnReader_->initialize("C")) {
        LOG_WARN("USN Journal not available - FIM will use polling only");
        usnReader_.reset();
    } else {
        usnReader_->setChangeCallback([this](const UsnChange& change) {
            onUsnChange(change);
        });
    }
    
    LOG_INFO("FIM Monitor initialized with {} directories", config_.directories.size());
    return true;
}

void FimMonitor::setEventCallback(FimEventCallback callback) {
    eventCallback_ = std::move(callback);
}

bool FimMonitor::start() {
    if (running_) return true;
    
    running_ = true;
    
    // Perform initial baseline scan if database is empty
    if (database_->getRecordCount() == 0) {
        LOG_INFO("Performing initial FIM baseline scan...");
        size_t count = scanner_->scan();
        LOG_INFO("Baseline scan complete: {} files indexed", count);
    }
    
    // Start USN monitoring
    if (usnReader_) {
        usnReader_->start();
    }
    
    LOG_INFO("FIM monitoring started");
    return true;
}

void FimMonitor::stop() {
    if (!running_) return;
    
    running_ = false;
    
    if (usnReader_) {
        usnReader_->stop();
    }
    
    if (scanner_) {
        scanner_->stop();
    }
    
    LOG_INFO("FIM monitoring stopped");
}

void FimMonitor::rescan() {
    if (scanner_) {
        LOG_INFO("Starting FIM rescan...");
        scanner_->scan();
    }
}

bool FimMonitor::isPathMonitored(const std::string& path) const {
    std::string lowerPath = path;
    std::transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(), ::tolower);
    std::replace(lowerPath.begin(), lowerPath.end(), '/', '\\');
    
    for (const auto& prefix : monitoredPrefixes_) {
        if (lowerPath.compare(0, prefix.size(), prefix) == 0) {
            return true;
        }
    }
    return false;
}

std::string FimMonitor::getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::tm tm;
    gmtime_s(&tm, &time);
    
    std::ostringstream ss;
    ss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");
    ss << '.' << std::setfill('0') << std::setw(3) << ms.count() << 'Z';
    
    return ss.str();
}

void FimMonitor::onUsnChange(const UsnChange& change) {
    // Skip directories
    if (change.isDirectory) return;

    // Skip if not in monitored paths
    if (!isPathMonitored(change.filePath)) return;

    // Process the change
    processChange(change.filePath, change);
}

void FimMonitor::processChange(const std::string& filePath, const UsnChange& change) {
    // Get existing record from database
    auto existingRecord = database_->getFile(filePath);
    
    FimEvent event;
    event.path = filePath;
    event.timestamp = getCurrentTimestamp();
    
    if (change.isDelete()) {
        // File deleted
        if (existingRecord) {
            event.changeType = FimChangeType::Deleted;
            event.oldHash = existingRecord->hash;
            event.oldSize = existingRecord->size;
            event.oldMtime = existingRecord->mtime;
            
            // Remove from database
            database_->deleteFile(filePath);
            
            LOG_DEBUG("FIM: File deleted: {}", filePath);
            
            if (eventCallback_) {
                eventCallback_(event);
            }
        }
    } else if (change.isCreate()) {
        // New file created
        auto newRecord = scanner_->scanFile(filePath);
        if (newRecord) {
            event.changeType = FimChangeType::Created;
            event.newHash = newRecord->hash;
            event.newSize = newRecord->size;
            event.newMtime = newRecord->mtime;
            
            // Add to database
            database_->upsertFile(*newRecord);
            
            LOG_DEBUG("FIM: File created: {}", filePath);
            
            if (eventCallback_) {
                eventCallback_(event);
            }
        }
    } else if (change.isModify() || change.isSecurityChange()) {
        // File modified
        auto newRecord = scanner_->scanFile(filePath);
        if (newRecord) {
            if (existingRecord && *existingRecord != *newRecord) {
                // File changed
                event.changeType = change.isSecurityChange() ?
                    FimChangeType::PermissionChanged : FimChangeType::Modified;
                event.oldHash = existingRecord->hash;
                event.oldSize = existingRecord->size;
                event.oldMtime = existingRecord->mtime;
                event.newHash = newRecord->hash;
                event.newSize = newRecord->size;
                event.newMtime = newRecord->mtime;

                // Update database
                database_->upsertFile(*newRecord);

                LOG_DEBUG("FIM: File modified: {}", filePath);

                if (eventCallback_) {
                    eventCallback_(event);
                }
            } else if (!existingRecord) {
                // New file (not in baseline yet)
                event.changeType = FimChangeType::Created;
                event.newHash = newRecord->hash;
                event.newSize = newRecord->size;

                database_->upsertFile(*newRecord);

                if (eventCallback_) {
                    eventCallback_(event);
                }
            }
        }
    } else if (change.isRename()) {
        // Browser downloads rename .crdownload -> final file; USN reports RenameNewName.
        // Treat the new name as a creation so download scan triggers correctly.
        if (change.reason & static_cast<uint32_t>(UsnReason::RenameNewName)) {
            event.changeType = FimChangeType::Created;
            // Try to hash for FIM integrity; fire callback regardless (path is what matters for AV scan)
            auto newRecord = scanner_->scanFile(filePath);
            if (newRecord) {
                event.newHash = newRecord->hash;
                event.newSize = newRecord->size;
                event.newMtime = newRecord->mtime;
                database_->upsertFile(*newRecord);
            } else {
                LOG_INFO("FIM: scanFile could not read {} (locked/missing) — callback still fires", filePath);
            }
            LOG_INFO("FIM: File renamed to: {}", filePath);
            if (eventCallback_) {
                eventCallback_(event);
            }
        } else if (change.reason & static_cast<uint32_t>(UsnReason::RenameOldName)) {
            if (existingRecord) {
                event.changeType = FimChangeType::Deleted;
                event.oldHash = existingRecord->hash;
                event.oldSize = existingRecord->size;
                event.oldMtime = existingRecord->mtime;
                database_->deleteFile(filePath);
                LOG_DEBUG("FIM: File renamed from: {}", filePath);
                if (eventCallback_) {
                    eventCallback_(event);
                }
            }
        }
    }
}

} // namespace ResolutePulse
