#pragma once

#include <nlohmann/json.hpp>

namespace rctctl::renderers {

void RenderParkStatus(const nlohmann::json& result);
void RenderParkGuests(const nlohmann::json& result);
void RenderParkPrice(const nlohmann::json& result, bool announceChange);
void RenderParkGateState(const nlohmann::json& result);
void RenderParkRatingHistory(const nlohmann::json& result);
void RenderParkRewards(const nlohmann::json& result);
void RenderSandboxStatus(const nlohmann::json& result);
void RenderParkWarnings(const nlohmann::json& result);

} // namespace rctctl::renderers
