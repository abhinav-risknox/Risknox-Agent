#pragma once

#include <nlohmann/json.hpp>

namespace ResolutePulse {
namespace Endpoint {

class InventoryCollector {
public:
    static nlohmann::json collectFullInventory();
};

} // namespace Endpoint
} // namespace ResolutePulse
