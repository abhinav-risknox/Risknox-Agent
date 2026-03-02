#pragma once

#include "manager/db/PostgresClient.h"

#include <string>
#include <unordered_map>
#include <mutex>

namespace ResolutePulse {

class AgentRegistry {
public:
    AgentRegistry(PostgresClient& db);
    ~AgentRegistry() = default;

    // Register a new agent (in-memory cache + DB)
    bool registerAgent(const AgentRecord& agent);

    // Get agent info (checks cache first, then DB)
    std::optional<AgentRecord> getAgent(const std::string& agentId);

    // Update agent status
    bool updateStatus(const std::string& agentId, const std::string& status);

    // Check if agent is registered
    bool isRegistered(const std::string& agentId);

    // Get count of active agents
    size_t getActiveAgentCount() const;

    // Refresh cache from DB
    void refreshCache();

private:
    PostgresClient& db_;
    std::unordered_map<std::string, AgentRecord> cache_;
    mutable std::mutex mutex_;
};

} // namespace ResolutePulse
