#pragma once

#include <nlohmann/json.hpp>

namespace rctctl::renderers {

void RenderRideList(const nlohmann::json& result);
void RenderRideAvailability(const nlohmann::json& result);
void RenderRideStatus(const nlohmann::json& result);
void RenderRideFinancials(const nlohmann::json& result);
void RenderRidePerception(const nlohmann::json& result);
void RenderRideOperations(const nlohmann::json& result);
void RenderRidePrice(const nlohmann::json& result, bool announceChange);
void RenderRideStatusChange(const nlohmann::json& result);
void RenderRideRename(const nlohmann::json& result);
void RenderRideConfigure(const nlohmann::json& result);
void RenderRidePlacement(const nlohmann::json& result);
void RenderRideEntrancePlacement(const nlohmann::json& result);
void RenderRideDemolish(const nlohmann::json& result);
void RenderRideBreakdowns(const nlohmann::json& result);
void RenderRideThroughput(const nlohmann::json& result);
void RenderRideFeedback(const nlohmann::json& result);

// Pre-built Coaster renderers
void RenderRideCoastersCategories(const nlohmann::json& result);
void RenderRideCoastersTypes(const nlohmann::json& result);
void RenderRideCoastersList(const nlohmann::json& result);
void RenderRideCoastersPreview(const nlohmann::json& result);
void RenderRideCoastersPlace(const nlohmann::json& result);

// Theme renderers
void RenderRideTheme(const nlohmann::json& result);
void RenderRideThemeChange(const nlohmann::json& result);
void RenderEntranceStyleList(const nlohmann::json& result);
void RenderColorList(const nlohmann::json& result);

} // namespace rctctl::renderers
