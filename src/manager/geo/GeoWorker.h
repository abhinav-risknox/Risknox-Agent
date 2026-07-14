#pragma once

// GeoWorker.h
// Background thread that runs inside the Manager.
// Every 24 hours (and immediately on startup) it:
//   1. Queries agents table for public_ip values not yet geo-resolved (or stale).
//   2. Calls ip-api.com/json/{ip} for each via httplib (already linked).
//   3. Upserts results into ip_geolocation table via PostgresClient.
//
// Private IPs (10.x, 192.168.x, 172.16-31.x, 127.x) are skipped by the DB query.
// Rate: 1 request per 1.5 seconds (ip-api.com free tier: 45 req/min).

#include "manager/db/PostgresClient.h"
#include "utils/Logger.h"

#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <string>

namespace ResolutePulse {

class GeoWorker {
public:
    explicit GeoWorker(PostgresClient& db);
    ~GeoWorker();

    // Start the background thread (returns immediately)
    void start();

    // Signal thread to stop and wait for it to exit
    void stop();

    bool isRunning() const { return running_.load(); }

    // Wake up the worker thread to run a cycle immediately
    void triggerCycle();

private:
    void run();          // Thread entry point
    void runCycle();     // One full scan: find stale IPs → lookup → upsert

    // Call ip-api.com for a single public IP.
    // Returns populated GeoRecord on success, nullopt on any failure.
    std::optional<GeoRecord> lookupIp(const std::string& ip);

    PostgresClient&         db_;
    std::thread             thread_;
    std::atomic<bool>       running_{false};
    std::mutex              cvMutex_;
    std::condition_variable cv_;
    std::atomic<bool>       triggerNow_{false};

    // How many hours before a geo record is considered stale and re-fetched
    static constexpr int CYCLE_HOURS     = 24;
    // Delay between HTTP requests in ms (45 req/min free tier → 1333ms min)
    static constexpr int REQUEST_DELAY_MS = 1500;
    // ip-api.com HTTP endpoint (free tier, no HTTPS)
    static constexpr const char* API_HOST = "ip-api.com";
};

} // namespace ResolutePulse
