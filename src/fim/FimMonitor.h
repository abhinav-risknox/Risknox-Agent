#pragma once

#include "FimDatabase.h"
#include "BaselineScanner.h"
#include "UsnJournalReader.h"
#include "FimEvent.h"
#include "queue/EventQueue.h"
#include <memory>
#include <functional>
#include <thread>
#include <atomic>
#include <set>

namespace ResolutePulse {

class FimMonitor {
public:
    using FimEventCallback = std::function<void(const FimEvent&)>;
    
    FimMonitor();
    ~FimMonitor();
    
    // Initialize with configuration
    bool initialize(const FimConfig& config, const std::string& dbPath);
    
    // Set callback for FIM events
    void setEventCallback(FimEventCallback callback);
    
    // Start monitoring
    bool start();
    
    // Stop monitoring
    void stop();
    
    // Force a baseline rescan
    void rescan();
    
    bool isRunning() const { return running_.load(); }
    
private:
    void onUsnChange(const UsnChange& change);
    void processChange(const std::string& filePath, const UsnChange& change);
    std::string getCurrentTimestamp();
    bool isPathMonitored(const std::string& path) const;
    
    FimConfig config_;
    std::unique_ptr<FimDatabase> database_;
    std::unique_ptr<BaselineScanner> scanner_;
    std::unique_ptr<UsnJournalReader> usnReader_;
    
    FimEventCallback eventCallback_;
    std::atomic<bool> running_{false};
    
    // Set of monitored directory prefixes (lowercase for comparison)
    std::vector<std::string> monitoredPrefixes_;
};

} // namespace ResolutePulse
