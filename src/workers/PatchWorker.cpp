// PatchWorker.cpp - rp-patch.exe entry point
// On-demand worker that runs Windows Update scans/installs via WUA COM APIs.
// Spawned by the core agent, streams results as JSON events over Named Pipe, then exits.

#include "patch/PatchManager.h"
#include "ipc/PipeChannel.h"
#include "utils/Logger.h"
#include "config/ConfigManager.h"

#include <nlohmann/json.hpp>
#include <filesystem>
#include <windows.h>

int main() {
    using namespace ResolutePulse;

    char selfPath[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, selfPath, MAX_PATH);
    std::filesystem::path agentDir   = std::filesystem::path(selfPath).parent_path();
    std::string           configPath = (agentDir / "config.json").string();

    // ConfigManager is a singleton
    auto& cfg = ConfigManager::instance();
    cfg.load(configPath);

    // Build PatchConfig from loaded config
    PatchConfig pmConfig;
    pmConfig.enabled           = cfg.getPatchConfig().enabled;
    pmConfig.autoScan          = cfg.getPatchConfig().auto_scan;
    pmConfig.scanIntervalHours = cfg.getPatchConfig().scan_interval_hours;
    pmConfig.autoInstall       = cfg.getPatchConfig().auto_install;
    pmConfig.excludeKBs        = cfg.getPatchConfig().exclude_kbs;

    PatchManager patcher;
    if (!patcher.initialize(pmConfig)) {
        LOG_ERROR("rp-patch: failed to initialize PatchManager");
        return 1;
    }

    // One-shot: open pipe, receive command, do work, stream results, exit
    PipeServer pipe;
    if (!pipe.listen("rp-patch")) {
        LOG_ERROR("rp-patch: pipe listen failed - {}", pipe.getLastError());
        return 1;
    }

    nlohmann::json cmd;
    if (!pipe.recvJson(cmd, 10000)) {
        LOG_ERROR("rp-patch: no command received within timeout");
        return 1;
    }

    std::string action = cmd.value("action", "scan");

    if (action == "scan") {
        // Synchronous scan - returns all available updates
        auto updates = patcher.scanForUpdates();
        for (const auto& u : updates) {
            pipe.sendJson({ {"type", "update"}, {"data", u.toJson()} });
        }
        pipe.sendJson({
            {"type",  "complete"},
            {"total", static_cast<int>(updates.size())}
        });

    } else if (action == "install") {
        // Install specific update IDs
        std::vector<std::string> ids;
        if (cmd.contains("update_ids") && cmd["update_ids"].is_array()) {
            for (auto& id : cmd["update_ids"]) {
                ids.push_back(id.get<std::string>());
            }
        }
        bool ok = patcher.installUpdates(ids);
        pipe.sendJson({
            {"type",    "complete"},
            {"success", ok},
            {"count",   static_cast<int>(ids.size())}
        });

    } else if (action == "get_installed") {
        auto installed = patcher.getInstalledUpdates();
        for (const auto& u : installed) {
            pipe.sendJson({ {"type", "installed"}, {"data", u.toJson()} });
        }
        pipe.sendJson({
            {"type",  "complete"},
            {"total", static_cast<int>(installed.size())}
        });
    }

    pipe.close();
    return 0;
}

