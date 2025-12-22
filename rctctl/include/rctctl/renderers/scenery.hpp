#pragma once

#include <nlohmann/json.hpp>

namespace rctctl::renderers {

void RenderSceneryCatalog(const nlohmann::json& result);
void RenderSceneryPlace(const nlohmann::json& result);
void RenderSceneryRemove(const nlohmann::json& result);

void RenderPathItemsCatalog(const nlohmann::json& result);
void RenderPathItemsPlace(const nlohmann::json& result);
void RenderPathItemsRemove(const nlohmann::json& result);

} // namespace rctctl::renderers
