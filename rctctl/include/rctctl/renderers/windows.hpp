#pragma once

#include <nlohmann/json.hpp>

namespace rctctl::renderers {

void RenderWindowList(const nlohmann::json& result);
void RenderWindowClose(const nlohmann::json& result);

} // namespace rctctl::renderers
