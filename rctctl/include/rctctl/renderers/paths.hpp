#pragma once

#include <nlohmann/json.hpp>

namespace rctctl::renderers {

void RenderPathPlace(const nlohmann::json& result);
void RenderPathCatalog(const nlohmann::json& result);
void RenderPathRemove(const nlohmann::json& result);
void RenderPathsList(const nlohmann::json& result);

} // namespace rctctl::renderers

