#include "AgentRegistry.h"
#include "utils/Logger.h"

namespace ResolutePulse {

AgentRegistry::AgentRegistry(PostgresClient& db) : db_(db) {}

bool AgentRegistry::registerAgent(const AgentRecord& agent) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!db_.insertAgent(agent)) {
        return false;
    }

    cache_[agent.agentId] = agent;
    LOG_INFO("Agent registered in registry: {}", agent.agentId);
    return true;
}

std::optional<AgentRecord> AgentRegistry::getAgent(const std::string& agentId) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Check cache first
    auto it = cache_.find(agentId);
    if (it != cache_.end()) {
        return it->second;
    }

    // Query DB
    auto agent = db_.getAgent(agentId);
    if (agent.has_value()) {
        cache_[agentId] = agent.value();
    }
    return agent;
}

bool AgentRegistry::updateStatus(const std::string& agentId, const std::string& status) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!db_.updateAgentStatus(agentId, status)) {
        return false;
    }

    auto it = cache_.find(agentId);
    if (it != cache_.end()) {
        it->second.status = status;
    }
    return true;
}

bool AgentRegistry::isRegistered(const std::string& agentId) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (cache_.count(agentId) > 0) {
        return true;
    }

    return db_.agentExists(agentId);
}

size_t AgentRegistry::getActiveAgentCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t count = 0;
    for (const auto& [id, agent] : cache_) {
        if (agent.status == "ACTIVE") count++;
    }
    return count;
}

void AgentRegistry::refreshCache() {
    std::lock_guard<std::mutex> lock(mutex_);
    // Clear and let entries be re-cached on demand
    cache_.clear();
    LOG_DEBUG("Agent registry cache cleared");
}

} // namespace ResolutePulse
