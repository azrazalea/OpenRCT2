#pragma once

#include <nlohmann/json.hpp>

namespace rctctl::renderers {

void RenderResearchStatus(const nlohmann::json& result);
void RenderMarketingStatus(const nlohmann::json& result);

} // namespace rctctl::renderers
