#pragma once

#include <nlohmann/json.hpp>

namespace rctctl::renderers {

void RenderMapStatus(const nlohmann::json& result);
void RenderMapTile(const nlohmann::json& result);
void RenderMapArea(const nlohmann::json& result);
void RenderMapHeatmap(const nlohmann::json& result);
void RenderMapOwnership(const nlohmann::json& result);
void RenderEntrances(const nlohmann::json& result);
void RenderScan(const nlohmann::json& result);
void RenderSceneryClear(const nlohmann::json& result);

} // namespace rctctl::renderers
