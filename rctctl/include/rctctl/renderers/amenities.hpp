#pragma once

#include <nlohmann/json.hpp>

namespace rctctl::renderers {

void RenderAmenityAnalysis(const nlohmann::json& result);
void RenderAmenityAuto(const nlohmann::json& result);

} // namespace rctctl::renderers
