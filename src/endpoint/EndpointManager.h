#pragma once

#include <string>
#include <nlohmann/json.hpp>

namespace ResolutePulse {
namespace Endpoint {

class EndpointManager {
public:
    static bool handleCommand(const std::string& verb, const nlohmann::json& params, nlohmann::json& result, std::string& errorMsg);
};

} // namespace Endpoint
} // namespace ResolutePulse
