#pragma once

#include <string>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <optional>
#include <nlohmann/json.hpp>

namespace ResolutePulse {

/**
 * Thread-safe command queue for policy updates received from the Manager.
 * The management thread enqueues commands, and the Agent main or a dedicated
 * policy thread dequeues and processes them.
 */
struct PolicyCommand {
    std::string policyType;     // "patch", "web_blocking", "software_blocking"
    nlohmann::json policyData;  // The policy payload
    std::string commandId;      // For ack back to Manager
    std::string timestamp;
};

class CommandQueue {
public:
    CommandQueue() = default;
    ~CommandQueue() = default;

    void push(PolicyCommand cmd) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.push(std::move(cmd));
        }
        cv_.notify_one();
    }

    std::optional<PolicyCommand> tryPop() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) return std::nullopt;
        auto cmd = std::move(queue_.front());
        queue_.pop();
        return cmd;
    }

    std::optional<PolicyCommand> waitPop(std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (cv_.wait_for(lock, timeout, [this]{ return !queue_.empty(); })) {
            auto cmd = std::move(queue_.front());
            queue_.pop();
            return cmd;
        }
        return std::nullopt;
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<PolicyCommand> queue_;
};

} // namespace ResolutePulse
