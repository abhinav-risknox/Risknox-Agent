// WebBlockerWorker.cpp - rp-webblock.exe entry point
// Persistent worker that manages DNS sinkholing via the Windows hosts file.
// Communicates with the core agent via Named Pipe \\.\pipe\rp-webblock.

#include "webblock/WebBlocker.h"
#include "ipc/PipeChannel.h"
#include "utils/Logger.h"
#include "utils/PathUtils.h"
#include "config/ConfigManager.h"

#include <nlohmann/json.hpp>
#include <filesystem>
#include <windows.h>

int main() {
    using namespace ResolutePulse;

    // Config must live in ProgramData (writable, service-safe, sees config_push updates)
    std::string configPath = (PathUtils::getAgentDataDir() / "config.json").string();

    // ConfigManager is a singleton
    auto& config = ConfigManager::instance();
    config.load(configPath);

    // Build WebBlockConfig from loaded config
    WebBlockConfig wbConfig;
    wbConfig.enabled    = config.getWebBlockConfig().enabled;
    wbConfig.configPath = (PathUtils::getAgentDataDir() / "blocked_urls.json").string();
    // hostsFilePath keeps its default (Windows hosts file)

    WebBlocker blocker;
    if (!blocker.initialize(wbConfig)) {
        LOG_ERROR("rp-webblock: failed to initialize WebBlocker");
        return 1;
    }

    LOG_INFO("rp-webblock: ready, listening on pipe rp-webblock");

    while (true) {
        PipeServer pipe;
        if (!pipe.listen("rp-webblock")) {
            LOG_ERROR("rp-webblock: pipe listen failed - {}", pipe.getLastError());
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

        LOG_DEBUG("rp-webblock: agent disconnected, waiting for reconnect");
        pipe.close();
    }
}

