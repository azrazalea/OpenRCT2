#pragma once

#include <nlohmann/json.hpp>

namespace rctctl::renderers {

void RenderStaffList(const nlohmann::json& result);
void RenderStaffDetail(const nlohmann::json& staff);

} // namespace rctctl::renderers
