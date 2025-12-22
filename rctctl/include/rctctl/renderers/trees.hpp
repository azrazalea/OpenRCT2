#pragma once

#include <nlohmann/json.hpp>

namespace rctctl::renderers {

void RenderTreeCatalog(const nlohmann::json& result);
void RenderTreePlant(const nlohmann::json& result);
void RenderTreeRemove(const nlohmann::json& result);

} // namespace rctctl::renderers
