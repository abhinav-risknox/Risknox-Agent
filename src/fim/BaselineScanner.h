#pragma once

#include "FimDatabase.h"
#include <string>
#include <vector>
#include <functional>
#include <atomic>

namespace ResolutePulse {

struct FimConfig {
    std::vector<std::string> directories;
    std::vector<std::string> excludePatterns;
    uint64_t maxFileSizeMb = 100;
    bool hashFiles = true;
};

class BaselineScanner {
public:
    using ProgressCallback = std::function<void(size_t filesScanned, const std::string& currentFile)>;
    
    BaselineScanner();
    ~BaselineScanner();
    
    // Set configuration
    void setConfig(const FimConfig& config);
    
    // Set database for storing results
    void setDatabase(FimDatabase* db);
    
    // Set progress callback
    void setProgressCallback(ProgressCallback callback);
    
    // Perform full baseline scan of configured directories
    // Returns number of files scanned
    size_t scan();
    
    // Scan a single file and return its record
    std::optional<FileRecord> scanFile(const std::string& filePath);
    
    // Stop ongoing scan
    void stop();
    
    // Check if scanning
    bool isScanning() const { return scanning_.load(); }
    
private:
    void scanDirectory(const std::string& path, std::vector<FileRecord>& records);
    bool matchesExcludePattern(const std::string& path) const;
    std::string normalizePathSeparators(const std::string& path) const;
    
    FimConfig config_;
    FimDatabase* db_ = nullptr;
    ProgressCallback progressCallback_;
    std::atomic<bool> scanning_{false};
    std::atomic<bool> stopRequested_{false};
    size_t filesScanned_ = 0;
};

} // namespace ResolutePulse
