// SoftwareBlockerWorker.cpp - rp-softblock.exe entry point
// Persistent worker process that blocks/unblocks applications via the
// Windows registry (DisallowRun) and process monitoring.
// Communicates with the core agent via Named Pipe \\.\pipe\rp-softblock.

#include "appblock/SoftwareBlocker.h"
#include "ipc/PipeChannel.h"
#include "utils/Logger.h"
#include "utils/PathUtils.h"
#include "config/ConfigManager.h"

#include <nlohmann/json.hpp>
#include <filesystem>
#include <windows.h>

int main() {
    using namespace ResolutePulse;

    // Resolve config.json relative to this executable
    char selfPath[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, selfPath, MAX_PATH);
    std::filesystem::path agentDir  = std::filesystem::path(selfPath).parent_path();
    std::string           configPath = (agentDir / "config.json").string();

    // ConfigManager is a singleton
    auto& config = ConfigManager::instance();
    config.load(configPath);

    // Build the AppBlockConfig from the loaded config
    AppBlockConfig abConfig;
    abConfig.enabled           = config.getAppBlockConfig().enabled;
    abConfig.configPath        = (PathUtils::getAgentDataDir() / "blocked_apps.json").string();
    abConfig.monitorIntervalMs = config.getAppBlockConfig().monitor_interval_ms;

    SoftwareBlocker blocker;
    if (!blocker.initialize(abConfig)) {
        LOG_ERROR("rp-softblock: failed to initialize SoftwareBlocker");
        return 1;
    }

    LOG_INFO("rp-softblock: ready, listening on pipe rp-softblock");

    // Persistent event loop: accept one agent connection at a time
    while (true) {
        PipeServer pipe;
        if (!pipe.listen("rp-softblock")) {
            LOG_ERROR("rp-softblock: pipe listen failed - {}", pipe.getLastError());
            Sleep(2000);
            continue;
        }

        nlohmann::json cmd;
        while (pipe.recvJson(cmd, INFINITE)) {
            nlohmann::json result;
            try {
                std::string action = cmd.value("action", "");
                if (action == "get_status") {
                    result["ok"]     = true;
                    result["status"] = blocker.getStatus();
                } else {
                    bool ok = blocker.applyPolicy(cmd);
                    result["ok"] = ok;
                }
            } catch (const std::exception& e) {
                result["ok"]    = false;
                result["error"] = std::string(e.what());
            }
            pipe.sendJson(result);
        }

        // Agent disconnected - close and wait for next connection
        LOG_DEBUG("rp-softblock: agent disconnected, waiting for reconnect");
        pipe.close();
    }
}

