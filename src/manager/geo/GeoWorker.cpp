#include "GeoWorker.h"

// httplib is already linked by RestApi — use plain HTTP (ip-api.com free tier)
#define CPPHTTPLIB_OPENSSL_SUPPORT
#include <httplib/httplib.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <thread>
#include <sstream>

namespace ResolutePulse {

GeoWorker::GeoWorker(PostgresClient& db) : db_(db) {}

GeoWorker::~GeoWorker() {
    stop();
}

void GeoWorker::start() {
    if (running_.load()) return;
    running_ = true;
    thread_ = std::thread(&GeoWorker::run, this);
    LOG_INFO("GeoWorker started");
}

void GeoWorker::stop() {
    if (!running_.load()) return;
    running_ = false;
    cv_.notify_all();
    if (thread_.joinable())
        thread_.join();
    LOG_INFO("GeoWorker stopped");
}

void GeoWorker::triggerCycle() {
    std::unique_lock<std::mutex> lock(cvMutex_);
    triggerNow_.store(true);
    cv_.notify_all();
}

// ─────────────────────────────────────────────────────────────
// Thread entry point
// ─────────────────────────────────────────────────────────────

void GeoWorker::run() {
    LOG_INFO("GeoWorker thread running (cycle every {} hours)", CYCLE_HOURS);

    while (running_.load()) {
        // Run a geo lookup cycle immediately, then sleep until next cycle
        try {
            runCycle();
        } catch (const std::exception& e) {
            LOG_ERROR("GeoWorker cycle exception: {}", e.what());
        }

        // Sleep for CYCLE_HOURS, waking early if stop() or triggerCycle() is called
        std::unique_lock<std::mutex> lock(cvMutex_);
        cv_.wait_for(lock,
                     std::chrono::hours(CYCLE_HOURS),
                     [this] { return !running_.load() || triggerNow_.load(); });
        triggerNow_.store(false);
    }

    LOG_INFO("GeoWorker thread exiting");
}

// ─────────────────────────────────────────────────────────────
// One full scan cycle
// ─────────────────────────────────────────────────────────────

void GeoWorker::runCycle() {
    auto pending = db_.getAgentsNeedingGeo(CYCLE_HOURS);

    if (pending.empty()) {
        LOG_DEBUG("GeoWorker: all agents geo up-to-date");
        return;
    }

    LOG_INFO("GeoWorker: {} agent(s) need geo lookup", pending.size());
    int success = 0, failed = 0;

    for (const auto& [agentId, publicIp] : pending) {
        if (!running_.load()) break;  // stop() was called mid-cycle

        LOG_DEBUG("GeoWorker: lookup {} → {}", agentId.substr(0, 8), publicIp);

        auto geo = lookupIp(publicIp);
        if (geo) {
            if (db_.upsertGeoRecord(*geo)) {
                ++success;
                LOG_INFO("GeoWorker: {} → {}, {} ({})",
                         publicIp, geo->city, geo->country, geo->isp);
            } else {
                ++failed;
                LOG_WARN("GeoWorker: DB upsert failed for {}", publicIp);
            }
        } else {
            ++failed;
            LOG_WARN("GeoWorker: lookup failed for {} (agent {})",
                     publicIp, agentId.substr(0, 8));
        }

        // Respect ip-api.com rate limit between requests
        if (running_.load()) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(REQUEST_DELAY_MS));
        }
    }

    LOG_INFO("GeoWorker cycle done: {} resolved, {} failed", success, failed);
}

// ─────────────────────────────────────────────────────────────
// ip-api.com HTTP lookup
// ─────────────────────────────────────────────────────────────

std::optional<GeoRecord> GeoWorker::lookupIp(const std::string& ip) {
    // ip-api.com free tier is HTTP only
    httplib::Client client(API_HOST, 80);
    client.set_connection_timeout(10);
    client.set_read_timeout(10);

    // Request only the fields we store
    std::string path = "/json/" + ip +
        "?fields=status,message,country,countryCode,regionName,"
        "city,lat,lon,timezone,isp,org,hosting,proxy,query";

    auto res = client.Get(path.c_str());

    if (!res) {
        LOG_WARN("GeoWorker: HTTP request failed for {} (no response)", ip);
        return std::nullopt;
    }

    if (res->status != 200) {
        LOG_WARN("GeoWorker: HTTP {} for {}", res->status, ip);
        return std::nullopt;
    }

    nlohmann::json j;
    try {
        j = nlohmann::json::parse(res->body);
    } catch (const std::exception& e) {
        LOG_WARN("GeoWorker: JSON parse error for {}: {}", ip, e.what());
        return std::nullopt;
    }

    // ip-api.com returns {"status":"fail","message":"private range"} for private IPs
    std::string status = j.value("status", "fail");
    if (status != "success") {
        std::string msg = j.value("message", "unknown");
        LOG_WARN("GeoWorker: ip-api.com status='{}' message='{}' for {}", status, msg, ip);
        return std::nullopt;
    }

    GeoRecord geo;
    geo.ipAddress   = j.value("query",       ip);
    geo.country     = j.value("country",     "");
    geo.countryCode = j.value("countryCode", "");
    geo.regionName  = j.value("regionName",  "");
    geo.city        = j.value("city",        "");
    geo.lat         = j.value("lat",         0.0);
    geo.lon         = j.value("lon",         0.0);
    geo.timezone    = j.value("timezone",    "");
    geo.isp         = j.value("isp",         "");
    geo.org         = j.value("org",         "");
    geo.hosting     = j.value("hosting",     false);
    geo.proxy       = j.value("proxy",       false);
    geo.queryStatus = status;

    return geo;
}

} // namespace ResolutePulse
