#pragma once

#include <nlohmann/json.hpp>

namespace rctctl::renderers {

void RenderWallRemove(const nlohmann::json& result);

} // namespace rctctl::renderers
