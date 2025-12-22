#pragma once

#include <nlohmann/json.hpp>

namespace rctctl::renderers {

void RenderWeatherStatus(const nlohmann::json& result);
void RenderNewsList(const nlohmann::json& result);
void RenderNewsHistory(const nlohmann::json& result);
void RenderAwardsHistory(const nlohmann::json& result);
void RenderAwardsList(const nlohmann::json& result);

} // namespace rctctl::renderers
