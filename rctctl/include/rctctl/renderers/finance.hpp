#pragma once

#include <nlohmann/json.hpp>

namespace rctctl::renderers {

void RenderFinanceSummary(const nlohmann::json& result);
void RenderFinanceHistory(const nlohmann::json& result);
void RenderLoanStatus(const nlohmann::json& result);

} // namespace rctctl::renderers
