#pragma once

#include <string>
#include <cstdint>
#include <nlohmann/json.hpp>

namespace ResolutePulse {

enum class FimChangeType {
    Created,
    Modified,
    Deleted,
    Renamed,
    PermissionChanged
};

inline std::string changeTypeToString(FimChangeType type) {
    switch (type) {
        case FimChangeType::Created: return "created";
        case FimChangeType::Modified: return "modified";
        case FimChangeType::Deleted: return "deleted";
        case FimChangeType::Renamed: return "renamed";
        case FimChangeType::PermissionChanged: return "permission_changed";
        default: return "unknown";
    }
}

struct FimEvent {
    std::string path;
    FimChangeType changeType;
    std::string oldHash;
    std::string newHash;
    uint64_t oldSize = 0;
    uint64_t newSize = 0;
    uint64_t oldMtime = 0;
    uint64_t newMtime = 0;
    std::string timestamp;
    
    nlohmann::json toJson() const {
        nlohmann::json j;
        j["type"] = "fim";
        j["path"] = path;
        j["change_type"] = changeTypeToString(changeType);
        j["timestamp"] = timestamp;
        
        if (changeType != FimChangeType::Created) {
            j["old_hash"] = oldHash;
            j["old_size"] = oldSize;
        }
        
        if (changeType != FimChangeType::Deleted) {
            j["new_hash"] = newHash;
            j["new_size"] = newSize;
        }
        
        return j;
    }
};

} // namespace ResolutePulse
