#pragma once

#include <nlohmann/json.hpp>

namespace rctctl::renderers {

void RenderGuestList(const nlohmann::json& result);
void RenderGuestDetail(const nlohmann::json& guest);
void RenderGuestThoughtSummary(const nlohmann::json& payload);
void RenderGuestMoodSummary(const nlohmann::json& payload);

} // namespace rctctl::renderers
