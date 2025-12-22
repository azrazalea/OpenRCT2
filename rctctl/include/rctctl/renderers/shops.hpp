#pragma once

#include <nlohmann/json.hpp>

namespace rctctl::renderers {

void RenderShopCatalog(const nlohmann::json& result);
void RenderShopList(const nlohmann::json& result);
void RenderShopPlacement(const nlohmann::json& result);
void RenderShopRemoval(const nlohmann::json& result);
void RenderShopPrice(const nlohmann::json& result, bool announceChange);
void RenderShopFinances(const nlohmann::json& result);
void RenderShopPerformance(const nlohmann::json& result);

// Facilities (Kiosks, Toilets, ATMs, First Aid)
void RenderFacilitiesList(const nlohmann::json& result);
void RenderFacilityFinances(const nlohmann::json& result);
void RenderFacilityPerformance(const nlohmann::json& result);

} // namespace rctctl::renderers
