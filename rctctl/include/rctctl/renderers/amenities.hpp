#pragma once

#include <nlohmann/json.hpp>

namespace rctctl::renderers {

void RenderAmenityAnalysis(const nlohmann::json& result);
void RenderAmenityEstimate(const nlohmann::json& result);
void RenderAmenityPlacement(const nlohmann::json& result);
void RenderAmenityRecommend(const nlohmann::json& result);
void RenderAmenityAuto(const nlohmann::json& result);

} // namespace rctctl::renderers
